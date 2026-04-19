#include <WiFi.h>
#include <PubSubClient.h>
#include "DHTesp.h"
#include <time.h>
#include <math.h>

const char* WIFI_SSID = "Wokwi-GUEST";
const char* WIFI_PASS = "";

const char* MQTT_HOST = "test.mosquitto.org";
const int   MQTT_PORT = 1883;

const char* TOPIC = "naven/iot/sensors";
const char* DEVICE_ID = "esp32-wokwi-001";

static const int DHT_PIN = 15;
static const int VIB_PIN = 34;

// NEW: controller switch (use a spare digital pin in Wokwi with a switch/button)
// When ON -> normal machinery behavior
// When OFF -> abnormal / fault behavior to trigger anomaly detection
static const int CTRL_PIN = 27;   // change if you prefer another GPIO

WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);
DHTesp dht;

uint64_t eventTimeMsUTC() {
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  return (uint64_t)tv.tv_sec * 1000ULL + (uint64_t)(tv.tv_usec / 1000ULL);
}

void syncTime() {
  configTime(0, 0, "pool.ntp.org");
  time_t now = 0;
  int retries = 0;
  while (now < 1577836800 && retries < 30) { // > 2020-01-01
    delay(500);
    time(&now);
    retries++;
  }
}

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) delay(250);
}

void connectMQTT() {
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setKeepAlive(30);

  while (!mqtt.connected()) {
    String clientId = "wokwi-esp32-" + String((uint32_t)esp_random(), HEX);
    mqtt.connect(clientId.c_str());
    if (!mqtt.connected()) delay(500);
  }
}

// ----------------- Heavy machinery simulation -----------------
float g_warmup = 0.0f;          // 0..1 warmup factor after startup
float g_health = 0.0f;          // 0..1 gradual wear (slowly increasing)
int   g_faultTicks = 0;         // countdown for temporary fault burst

float clampf(float x, float lo, float hi) {
  if (x < lo) return lo;
  if (x > hi) return hi;
  return x;
}

float randUniform(float lo, float hi) {
  return lo + (hi - lo) * (random(0, 10001) / 10000.0f);
}

float heavyMachineTempC(uint64_t ts_ms, bool controllerOn) {
  // Load cycle: ~4 minutes period
  float t = (float)(ts_ms % (4UL * 60UL * 1000UL)) / 1000.0f;
  float load = 0.5f + 0.5f * sinf(2.0f * (float)M_PI * t / (4.0f * 60.0f)); // 0..1

  // Warmup to operating range over ~2 minutes
  g_warmup = clampf(g_warmup + 0.008f, 0.0f, 1.0f);

  // Slow wear over time
  g_health = clampf(g_health + 0.00005f, 0.0f, 1.0f);

  // Normal operation range (heavy machinery): roughly 45..75C under load
  float base = 45.0f + 30.0f * load;
  float warm = (base - 25.0f) * g_warmup;
  float wear = 3.0f * g_health;
  float noise = randUniform(-0.6f, 0.6f);

  float temp = 25.0f + warm + wear + noise;

  // Rare small overheat spike even in normal mode
  if (random(0, 1000) < 3) temp += randUniform(8.0f, 18.0f);

  if (!controllerOn) {
    // Abnormal mode: temperature becomes erratic / clearly wrong
    //  - sometimes unrealistically low or high
    //  - higher variance + frequent spikes
    float chaos = randUniform(-12.0f, 22.0f);
    temp += chaos;

    if (random(0, 100) < 25) { // frequent spikes
      temp += randUniform(15.0f, 35.0f);
    }

    // occasional sensor-glitch style drop
    if (random(0, 100) < 10) {
      temp -= randUniform(10.0f, 25.0f);
    }
  }

  return clampf(temp, 0.0f, 130.0f);
}

int heavyMachineVib(uint64_t ts_ms, bool controllerOn) {
  float t = (float)(ts_ms % (4UL * 60UL * 1000UL)) / 1000.0f;
  float load = 0.5f + 0.5f * sinf(2.0f * (float)M_PI * t / (4.0f * 60.0f)); // 0..1

  float base = 12.0f + 18.0f * load + 12.0f * g_health; // 12..40ish
  float noise = randUniform(-5.0f, 5.0f);

  float vib = base + noise;

  // Occasional impact spikes (normal)
  if (random(0, 100) < 4) vib += randUniform(15.0f, 35.0f);

  // Rare fault burst (normal)
  if (g_faultTicks == 0 && random(0, 2000) < 2) g_faultTicks = random(10, 26);
  if (g_faultTicks > 0) {
    g_faultTicks--;
    vib += randUniform(10.0f, 25.0f);
  }

  // Pot still acts as a subtle severity dial (optional)
  int raw = analogRead(VIB_PIN);
  float dial = (float)raw / 4095.0f;     // 0..1
  vib += (dial - 0.5f) * 10.0f;          // -5..+5

  if (!controllerOn) {
    // Abnormal mode: clearly elevated / unstable vibration
    vib += randUniform(20.0f, 45.0f);    // shift upward a lot

    // frequent sharp spikes
    if (random(0, 100) < 30) vib += randUniform(20.0f, 50.0f);

    // sometimes "sensor glitch" -> near zero then spike next time (creates anomalies)
    if (random(0, 100) < 8) vib = randUniform(0.0f, 5.0f);
  }

  vib = clampf(vib, 0.0f, 100.0f);
  return (int)lroundf(vib);
}
// ---------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  randomSeed(esp_random());

  dht.setup(DHT_PIN, DHTesp::DHT22);

  // Controller pin: using internal pull-up.
  // Wire a switch in Wokwi from GPIO27 to GND.
  pinMode(CTRL_PIN, INPUT_PULLUP);

  connectWiFi();
  syncTime();
  connectMQTT();

  Serial.println("MQTT publishing started");
  Serial.println("Controller: ON when GPIO27 is HIGH (switch open), OFF when LOW (switch to GND)");
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) connectWiFi();
  if (!mqtt.connected()) connectMQTT();
  mqtt.loop();

  TempAndHumidity th = dht.getTempAndHumidity();

  // Guard against NaN reads
  if (isnan(th.temperature) || isnan(th.humidity)) {
    Serial.println("DHT read failed (NaN), skipping publish");
    delay(1000);
    return;
  }

  uint64_t ts = eventTimeMsUTC();

  // Controller logic:
  // With INPUT_PULLUP, HIGH = switch open => controller ON (normal)
  // LOW = switch closed to GND => controller OFF (abnormal demo mode)
  bool controllerOn = (digitalRead(CTRL_PIN) == HIGH);

  // Keep output JSON field names EXACTLY the same:
  float tempC = heavyMachineTempC(ts, controllerOn);
  int vib = heavyMachineVib(ts, controllerOn);

  char payload[256];
  int n = snprintf(payload, sizeof(payload),
    "{\"device_id\":\"%s\",\"event_time_ms\":%llu,\"temp_c\":%.2f,\"vib\":%d}",
    DEVICE_ID, (unsigned long long)ts, tempC, vib
  );

  if (n > 0 && n < (int)sizeof(payload)) {
    bool ok = mqtt.publish(TOPIC, payload);
    if (!ok) Serial.println("MQTT publish failed");

    // Extra debug line (does NOT change JSON payload)
    Serial.print(controllerOn ? "[CTRL ON] " : "[CTRL OFF] ");
    Serial.println(payload);
  } else {
    Serial.println("Payload formatting overflow");
  }

  delay(1000); // 1 msg/sec
}