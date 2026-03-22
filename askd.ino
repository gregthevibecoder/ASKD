// ================================================
// ASKD — Arduino Sensor Kit Dashboard
// Arduino Uno R4 WiFi + Seeed Sensor Kit Base
// Streams JSON over Serial at 115200 baud, 500ms
// Button D4 single press: WiFi scan
//   - Single sweep tone then scan
//   - Double beep on completion
// ================================================

#include <ArduinoJson.h>
#include <Adafruit_AHTX0.h>
#include <SPL07-003.h>
#include <Adafruit_LIS3DH.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_SSD1306.h>
#include <WiFiS3.h>
#include <Wire.h>

// ── Pins ─────────────────────────────────────────────────────
#define PIN_LIGHT   A3
#define PIN_SOUND   A2
#define PIN_ROTARY  A0
#define PIN_LED     6
#define PIN_BUTTON  4
#define PIN_BUZZER  5

// ── OLED ─────────────────────────────────────────────────────
#define OLED_WIDTH   128
#define OLED_HEIGHT   64
#define OLED_RESET    -1
#define OLED_ADDRESS 0x3C

// ── Timing ───────────────────────────────────────────────────
#define POLL_MS   500
#define SOUNDS     50

// ── Sensor objects ───────────────────────────────────────────
Adafruit_AHTX0   aht;
SPL07_003        spl;
Adafruit_LIS3DH  lis;
Adafruit_SSD1306 oled(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET);

// ── State ────────────────────────────────────────────────────
unsigned long lastSend = 0;
uint32_t      idx      = 0;

bool ahtOk   = false;
bool splOk   = false;
bool lisOk   = false;
bool oledOk  = false;
bool lastBtn = false;

// Rolling sound buffer
int soundBuf[SOUNDS];
int soundIdx = 0;

// Seismic baseline
float baseline = 9.81f;

// Last sensor values for OLED
float lastTemp  = 0;
float lastHumid = 0;
float lastPres  = 0;
float lastSeis  = 0;


// ============================================================
void setup()
{
    Serial.begin(115200);
    while (!Serial) { ; }

    Wire.begin();
    pinMode(PIN_LED,    OUTPUT);
    pinMode(PIN_BUTTON, INPUT);
    pinMode(PIN_BUZZER, OUTPUT);

    if (aht.begin()) ahtOk = true;
    if (spl.begin(0x77, &Wire)) splOk = true;

    if (lis.begin(0x19))
    {
        lisOk = true;
        lis.setRange(LIS3DH_RANGE_2_G);
        lis.setDataRate(LIS3DH_DATARATE_100_HZ);
        float sum = 0;
        for (int i = 0; i < 20; i++)
        {
            sensors_event_t e; lis.getEvent(&e);
            sum += sqrt(e.acceleration.x * e.acceleration.x +
                        e.acceleration.y * e.acceleration.y +
                        e.acceleration.z * e.acceleration.z);
            delay(10);
        }
        baseline = sum / 20.0f;
    }

    if (oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS))
    {
        oledOk = true;
        oled.setRotation(2);
        oled.clearDisplay();
        oled.display();
    }

    memset(soundBuf, 0, sizeof(soundBuf));

    if (oledOk) bootScreen();

    for (int i = 0; i < 3; i++)
    {
        digitalWrite(PIN_LED, HIGH); delay(150);
        digitalWrite(PIN_LED, LOW);  delay(150);
    }

    Serial.println("{\"status\":\"ASKD_ONLINE\"}");
}


// ============================================================
void loop()
{
    soundBuf[soundIdx] = analogRead(PIN_SOUND);
    soundIdx = (soundIdx + 1) % SOUNDS;

    bool btn = (digitalRead(PIN_BUTTON) == HIGH);

    if (btn && !lastBtn)
    {
        doWifiScan();
        lastSend = millis();
    }

    lastBtn = btn;

    if (millis() - lastSend >= POLL_MS)
    {
        lastSend = millis();
        sendJson();
    }

    digitalWrite(PIN_LED, idx % 2 == 0 ? HIGH : LOW);
}


// ============================================================
void doWifiScan()
{
    if (oledOk) oledScanning();

    // Single sweep 200Hz → 2000Hz over ~1.5 seconds
    for (int freq = 200; freq <= 2000; freq += 20)
    {
        tone(PIN_BUZZER, freq);
        delay(16);
    }
    noTone(PIN_BUZZER);

    // Run the scan
    int count = WiFi.scanNetworks();

    // Completion double beep
    delay(80);
    tone(PIN_BUZZER, 880,  120); delay(180);
    tone(PIN_BUZZER, 1320, 200); delay(250);
    noTone(PIN_BUZZER);

    if (oledOk)
    {
        oledScanDone(min(count, 8));
        delay(1500);
        oled.clearDisplay();
        oled.display();
    }

    int sendCount = min(count, 8);

    DynamicJsonDocument doc(1024);
    doc["type"]  = "ssids";
    doc["count"] = count;

    JsonArray arr = doc.createNestedArray("ssids");
    for (int i = 0; i < sendCount; i++)
    {
        JsonObject net = arr.createNestedObject();
        net["ssid"] = WiFi.SSID(i);
        net["rssi"] = WiFi.RSSI(i);
    }

    serializeJson(doc, Serial);
    Serial.println();
}


// ============================================================
void oledScanning()
{
    oled.clearDisplay();
    oled.setTextColor(SSD1306_WHITE);
    oled.fillRect(0, 0, OLED_WIDTH, 14, SSD1306_WHITE);
    oled.setTextColor(SSD1306_BLACK);
    oled.setTextSize(1);
    oled.setCursor(16, 3);
    oled.print("[ WIFI SCANNER ]");
    oled.setTextColor(SSD1306_WHITE);
    oled.setCursor(20, 22); oled.print("Scanning for");
    oled.setCursor(16, 34); oled.print("networks...");
    oled.fillCircle(42, 48, 3, SSD1306_WHITE);
    oled.fillCircle(56, 48, 3, SSD1306_WHITE);
    oled.fillCircle(70, 48, 3, SSD1306_WHITE);
    oled.display();
}


// ============================================================
void oledScanDone(int found)
{
    oled.clearDisplay();
    oled.setTextColor(SSD1306_WHITE);
    oled.fillRect(0, 0, OLED_WIDTH, 14, SSD1306_WHITE);
    oled.setTextColor(SSD1306_BLACK);
    oled.setTextSize(1);
    oled.setCursor(16, 3);
    oled.print("[ WIFI SCANNER ]");
    oled.setTextColor(SSD1306_WHITE);
    oled.setCursor(28, 22); oled.print("Scan complete!");
    oled.setCursor(16, 36); oled.print("Found: ");
    oled.print(found);
    oled.print(" networks");
    oled.fillRect(0, 50, OLED_WIDTH, 14, SSD1306_WHITE);
    oled.setTextColor(SSD1306_BLACK);
    oled.setCursor(16, 53); oled.print("Results sent to PC");
    oled.display();
}


// ============================================================
void bootScreen()
{
    oled.clearDisplay();
    oled.setTextColor(SSD1306_WHITE);
    oled.drawRect(0, 0, OLED_WIDTH, OLED_HEIGHT, SSD1306_WHITE);
    oled.setTextSize(2); oled.setCursor(20, 8);
    oled.print("ASKD v1");
    oled.drawLine(4, 28, 123, 28, SSD1306_WHITE);
    oled.setTextSize(1);
    oled.setCursor(8,  33); oled.print("Sensor Kit Dashboard");
    oled.setCursor(14, 44); oled.print("Uno R4 WiFi");
    oled.setCursor(22, 55); oled.print("Starting...");
    oled.display(); delay(2000);
    for (int x = 0; x <= OLED_WIDTH; x += 4)
    {
        oled.fillRect(0, 0, x, OLED_HEIGHT, SSD1306_BLACK);
        oled.display(); delay(8);
    }
}


// ============================================================
void sendJson()
{
    float temp = -1, humid = -1;
    if (ahtOk)
    {
        sensors_event_t h, t;
        aht.getEvent(&h, &t);
        temp = t.temperature; humid = h.relative_humidity;
        lastTemp = temp; lastHumid = humid;
    }

    int   lr   = analogRead(PIN_LIGHT);
    float lpct = (lr / 1023.0f) * 100.0f;

    long ss = 0;
    for (int i = 0; i < SOUNDS; i++) ss += soundBuf[i];
    float spct = ((ss / SOUNDS) / 1023.0f) * 100.0f;

    int   rr   = analogRead(PIN_ROTARY);
    float rpct = (rr / 1023.0f) * 100.0f;

    float pres = -1, alt = -1;
    if (splOk)
    {
        pres = spl.readPressure() / 100.0f;
        alt  = 44330.0f * (1.0f - pow(pres / 1013.25f, 0.1903f));
        lastPres = pres;
    }

    float seis = 0, ax = 0, ay = 0, az = 0;
    if (lisOk)
    {
        sensors_event_t e; lis.getEvent(&e);
        ax = e.acceleration.x; ay = e.acceleration.y; az = e.acceleration.z;
        float m = sqrt(ax*ax + ay*ay + az*az);
        seis = abs(m - baseline);
        if (seis < 0.05f) seis = 0.0f;
        lastSeis = seis;
    }

    digitalWrite(PIN_LED, idx % 2 == 0 ? HIGH : LOW);

    StaticJsonDocument<384> doc;
    doc["idx"]      = idx++;
    doc["ms"]       = millis();
    doc["temp"]     = serialized(String(temp,  1));
    doc["humid"]    = serialized(String(humid, 1));
    doc["light"]    = serialized(String(lpct,  1));
    doc["sound"]    = serialized(String(spct,  1));
    doc["rotary"]   = serialized(String(rpct,  1));
    doc["pressure"] = serialized(String(pres,  1));
    doc["altitude"] = serialized(String(alt,   1));
    doc["seismic"]  = serialized(String(seis,  2));
    doc["ax"]       = serialized(String(ax,    2));
    doc["ay"]       = serialized(String(ay,    2));
    doc["az"]       = serialized(String(az,    2));
    doc["ahtOk"]    = ahtOk ? 1 : 0;
    doc["splOk"]    = splOk ? 1 : 0;
    doc["lisOk"]    = lisOk ? 1 : 0;

    serializeJson(doc, Serial);
    Serial.println();
}