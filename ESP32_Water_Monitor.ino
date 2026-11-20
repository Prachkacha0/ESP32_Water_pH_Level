/* ESP32 Water Monitor with LCD, Wi-Fi setup and Telegram alerts.
   S1 is the lower XKC-Y25 and S2 is the upper XKC-Y25.
   Add a push button between GPIO25 and GND; hold 3 seconds for configuration.
*/
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <WiFiManager.h>
#include <UniversalTelegramBot.h>
#include <Preferences.h>
#include <time.h>

// Supplied only at compile time. The real key is never committed to Git.
#ifndef CLOUD_DEVICE_KEY
#define CLOUD_DEVICE_KEY ""
#endif

#define PH_SENSOR_PIN       34
#define DS18B20_PIN          4
#define LEVEL_SENSOR_1_PIN  16
#define LEVEL_SENSOR_2_PIN  17
#define CONFIG_BUTTON_PIN   25
#define I2C_SDA_PIN         21
#define I2C_SCL_PIN         22
#define LCD_ADDRESS       0x27
#define LCD_COLUMNS         20
#define LCD_ROWS             4

LiquidCrystal_I2C lcd(LCD_ADDRESS, LCD_COLUMNS, LCD_ROWS);
OneWire oneWire(DS18B20_PIN);
DallasTemperature temperatureSensors(&oneWire);
WiFiManager wifiManager;
Preferences preferences;
WiFiClientSecure telegramClient;
UniversalTelegramBot *telegramBot = nullptr;
WebServer dashboardServer(80);
bool dashboardStarted = false;
bool dashboardRoutesConfigured = false;

constexpr char CONFIG_AP_NAME[] = "WaterMonitor-Setup";
constexpr char CONFIG_AP_PASSWORD[] = "water1234";
constexpr char CLOUD_API_URL[] = "https://esp32-water-ph.vercel.app/api/telemetry";
constexpr unsigned long CONFIG_BUTTON_HOLD_MS = 3000;
constexpr unsigned long TEMPERATURE_CONVERSION_MS = 750;
constexpr uint8_t LEVEL_DETECTED_STATE = HIGH; // Change to LOW if sensor logic is reversed.
constexpr unsigned long LEVEL_DEBOUNCE_MS = 100;

// Calibrate the pH constants with pH 7.00 and pH 4.00 buffer solutions.
constexpr float PH_NEUTRAL_VALUE = 7.00f;
constexpr float PH_NEUTRAL_VOLTAGE = 2.50f;
constexpr float PH_SLOPE = -5.714f;
constexpr uint8_t PH_SAMPLE_COUNT = 15;

float temperatureMinimum = 24.0f;
float temperatureMaximum = 29.0f;
float phMinimum = 6.0f;
float phMaximum = 7.5f;

constexpr float TEMPERATURE_HYSTERESIS = 0.2f;
constexpr float PH_HYSTERESIS = 0.05f;
constexpr unsigned long DISPLAY_INTERVAL_MS = 1000;
constexpr unsigned long SERIAL_INTERVAL_MS = 2000;
constexpr unsigned long TELEGRAM_RETRY_MS = 60000;
constexpr unsigned long TELEGRAM_REMINDER_MS = 1800000;
constexpr unsigned long CLOUD_UPLOAD_INTERVAL_MS = 15000;
constexpr unsigned long CLOUD_HTTP_TIMEOUT_MS = 8000;

char telegramBotToken[64] = "";
char telegramChatId[32] = "";
char cloudDeviceKey[96] = CLOUD_DEVICE_KEY;

WiFiManagerParameter telegramTokenParameter(
    "tg_token", "Telegram Bot Token", "", 63, "type='password'");
WiFiManagerParameter telegramChatParameter("tg_chat", "Telegram Chat ID", "", 31);
WiFiManagerParameter cloudKeyParameter(
    "cloud_key", "Cloud Device API Key", "", 95, "type='password'");
WiFiManagerParameter temperatureMinimumParameter(
    "temp_min", "Temperature minimum (C)", "24.0", 8, "type='number' step='0.1'");
WiFiManagerParameter temperatureMaximumParameter(
    "temp_max", "Temperature maximum (C)", "29.0", 8, "type='number' step='0.1'");
WiFiManagerParameter phMinimumParameter(
    "ph_min", "pH minimum", "6.0", 8, "type='number' step='0.1'");
WiFiManagerParameter phMaximumParameter(
    "ph_max", "pH maximum", "7.5", 8, "type='number' step='0.1'");

float phVoltage = NAN;
float phValue = NAN;
float waterTemperatureC = NAN;
bool level1Detected = false;
bool level2Detected = false;
bool level1RawPrevious = false;
bool level2RawPrevious = false;
unsigned long level1ChangedAt = 0;
unsigned long level2ChangedAt = 0;
unsigned long temperatureRequestAt = 0;
unsigned long lastDisplayUpdate = 0;
unsigned long lastSerialUpdate = 0;
unsigned long configButtonPressedAt = 0;
bool configButtonWasPressed = false;
bool configButtonHandled = false;
bool ntpStarted = false;
unsigned long settingsSavedAt = 0;
unsigned long lastCloudUploadAttempt = 0;
unsigned long lastCloudUploadSuccess = 0;
bool cloudLastUploadOk = false;

// Vercel currently chains to GTS Root R1. Source: https://pki.goog/roots.pem
const char CLOUD_ROOT_CA[] PROGMEM = R"CERT(-----BEGIN CERTIFICATE-----
MIIFVzCCAz+gAwIBAgINAgPlk28xsBNJiGuiFzANBgkqhkiG9w0BAQwFADBHMQsw
CQYDVQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2VzIExMQzEU
MBIGA1UEAxMLR1RTIFJvb3QgUjEwHhcNMTYwNjIyMDAwMDAwWhcNMzYwNjIyMDAw
MDAwWjBHMQswCQYDVQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZp
Y2VzIExMQzEUMBIGA1UEAxMLR1RTIFJvb3QgUjEwggIiMA0GCSqGSIb3DQEBAQUA
A4ICDwAwggIKAoICAQC2EQKLHuOhd5s73L+UPreVp0A8of2C+X0yBoJx9vaMf/vo
27xqLpeXo4xL+Sv2sfnOhB2x+cWX3u+58qPpvBKJXqeqUqv4IyfLpLGcY9vXmX7w
Cl7raKb0xlpHDU0QM+NOsROjyBhsS+z8CZDfnWQpJSMHobTSPS5g4M/SCYe7zUjw
TcLCeoiKu7rPWRnWr4+wB7CeMfGCwcDfLqZtbBkOtdh+JhpFAz2weaSUKK0Pfybl
qAj+lug8aJRT7oM6iCsVlgmy4HqMLnXWnOunVmSPlk9orj2XwoSPwLxAwAtcvfaH
szVsrBhQf4TgTM2S0yDpM7xSma8ytSmzJSq0SPly4cpk9+aCEI3oncKKiPo4Zor8
Y/kB+Xj9e1x3+naH+uzfsQ55lVe0vSbv1gHR6xYKu44LtcXFilWr06zqkUspzBmk
MiVOKvFlRNACzqrOSbTqn3yDsEB750Orp2yjj32JgfpMpf/VjsPOS+C12LOORc92
wO1AK/1TD7Cn1TsNsYqiA94xrcx36m97PtbfkSIS5r762DL8EGMUUXLeXdYWk70p
aDPvOmbsB4om3xPXV2V4J95eSRQAogB/mqghtqmxlbCluQ0WEdrHbEg8QOB+DVrN
VjzRlwW5y0vtOUucxD/SVRNuJLDWcfr0wbrM7Rv1/oFB2ACYPTrIrnqYNxgFlQID
AQABo0IwQDAOBgNVHQ8BAf8EBAMCAYYwDwYDVR0TAQH/BAUwAwEB/zAdBgNVHQ4E
FgQU5K8rJnEaK0gnhS9SZizv8IkTcT4wDQYJKoZIhvcNAQEMBQADggIBAJ+qQibb
C5u+/x6Wki4+omVKapi6Ist9wTrYggoGxval3sBOh2Z5ofmmWJyq+bXmYOfg6LEe
QkEzCzc9zolwFcq1JKjPa7XSQCGYzyI0zzvFIoTgxQ6KfF2I5DUkzps+GlQebtuy
h6f88/qBVRRiClmpIgUxPoLW7ttXNLwzldMXG+gnoot7TiYaelpkttGsN/H9oPM4
7HLwEXWdyzRSjeZ2axfG34arJ45JK3VmgRAhpuo+9K4l/3wV3s6MJT/KYnAK9y8J
ZgfIPxz88NtFMN9iiMG1D53Dn0reWVlHxYciNuaCp+0KueIHoI17eko8cdLiA6Ef
MgfdG+RCzgwARWGAtQsgWSl4vflVy2PFPEz0tv/bal8xa5meLMFrUKTX5hgUvYU/
Z6tGn6D/Qqc6f1zLXbBwHSs09dR2CQzreExZBfMzQsNhFRAbd03OIozUhfJFfbdT
6u9AWpQKXCBfTkBdYiJ23//OYb2MI3jSNwLgjt7RETeJ9r/tSQdirpLsQBqvFAnZ
0E6yove+7u7Y/9waLd64NnHi/Hm3lCXRSHNboTXns5lndcEZOitHTtNCjv0xyBZm
2tIMPNuzjsmhDYAPexZ3FL//2wmUspO8IFgV6dtxQ/PeEMMA3KgqlbbC1j+Qa3bb
bP6MvPJwNQzcmRk13NfIRmPVNnGuV/u3gm3c
-----END CERTIFICATE-----)CERT";

struct AlarmState {
  bool initialized = false;
  bool active = false;
  bool notificationDelivered = false;
  unsigned long lastAttempt = 0;
  unsigned long lastDelivered = 0;
};

AlarmState temperatureAlarm, temperatureSensorAlarm, phAlarm, phSensorAlarm;
AlarmState waterAlarm, waterSensorAlarm;

enum WaterLevelState {
  WATER_LEVEL_LOW,
  WATER_LEVEL_MIDDLE,
  WATER_LEVEL_HIGH,
  WATER_LEVEL_SENSOR_ERROR
};

WaterLevelState getWaterLevelState() {
  if (!level1Detected && !level2Detected) return WATER_LEVEL_LOW;
  if (level1Detected && !level2Detected) return WATER_LEVEL_MIDDLE;
  if (level1Detected && level2Detected) return WATER_LEVEL_HIGH;
  return WATER_LEVEL_SENSOR_ERROR; // Upper sensor WET while lower sensor is DRY.
}

const char *waterLevelName(WaterLevelState state) {
  switch (state) {
    case WATER_LEVEL_LOW: return "LOW";
    case WATER_LEVEL_MIDDLE: return "MIDDLE";
    case WATER_LEVEL_HIGH: return "HIGH";
    default: return "SENSOR ERROR";
  }
}

// Self-contained dashboard: no Internet-hosted fonts, scripts, or images.
const char DASHBOARD_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="theme-color" content="#0f172a">
<title>Water Monitor</title>
<style>
:root{--bg:#0f172a;--panel:#1e293b;--line:#475569;--text:#f8fafc;--muted:#cbd5e1;--ok:#22c55e;--warn:#f59e0b;--bad:#ef4444;--info:#38bdf8}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);font-family:system-ui,-apple-system,"Segoe UI",sans-serif;line-height:1.5}
.wrap{width:min(1040px,100%);margin:auto;padding:24px}.top{display:flex;gap:16px;align-items:center;justify-content:space-between;margin-bottom:20px}
h1{font-size:clamp(1.65rem,5vw,2.5rem);margin:0;letter-spacing:-.03em}.sub{color:var(--muted);margin:4px 0 0}.live{display:flex;align-items:center;gap:8px;color:var(--muted);white-space:nowrap}
.dot{width:12px;height:12px;border-radius:50%;background:var(--ok);box-shadow:0 0 0 4px #22c55e25}.grid{display:grid;grid-template-columns:repeat(3,1fr);gap:16px}
.card{background:var(--panel);border:1px solid var(--line);border-radius:16px;padding:20px;min-height:168px}.wide{grid-column:span 3;min-height:auto}
.label{color:var(--muted);font-size:.9rem;text-transform:uppercase;letter-spacing:.08em}.value{font-size:clamp(2rem,7vw,3.4rem);font-weight:750;letter-spacing:-.05em;margin:8px 0}.unit{font-size:1rem;color:var(--muted);letter-spacing:0}
.badge{display:inline-flex;border-radius:999px;padding:5px 11px;font-weight:700;font-size:.82rem;background:#334155;color:var(--text)}.ok{background:#14532d;color:#dcfce7}.warn{background:#78350f;color:#fef3c7}.bad{background:#7f1d1d;color:#fee2e2}.info{background:#0c4a6e;color:#e0f2fe}
.sensors{display:grid;grid-template-columns:1fr 1fr;gap:12px;margin-top:14px}.sensor{border:1px solid var(--line);border-radius:12px;padding:12px}.sensor b{display:block;font-size:1.15rem;margin-top:3px}
.footer{display:flex;flex-wrap:wrap;gap:12px 22px;justify-content:space-between;color:var(--muted);font-size:.9rem}.offline .dot{background:var(--bad);box-shadow:0 0 0 4px #ef444425}
@media(max-width:720px){.wrap{padding:16px}.top{align-items:flex-start;flex-direction:column}.grid{grid-template-columns:1fr}.wide{grid-column:span 1}.card{min-height:150px}}
@media(prefers-reduced-motion:no-preference){.dot{transition:background .2s ease}.badge{transition:background .2s ease}}
</style></head>
<body><main class="wrap"><header class="top"><div><h1>Water Monitor</h1><p class="sub">Live ESP32 sensor dashboard</p></div><div class="live"><span class="dot"></span><span id="connection">Connecting...</span></div></header>
<section class="grid" aria-live="polite">
<article class="card"><div class="label">pH level</div><div class="value" id="ph">--</div><span class="badge" id="phState">WAITING</span></article>
<article class="card"><div class="label">Temperature</div><div class="value"><span id="temp">--</span><span class="unit"> °C</span></div><span class="badge" id="tempState">WAITING</span></article>
<article class="card"><div class="label">Water level</div><div class="value" id="water">--</div><span class="badge" id="waterState">WAITING</span></article>
<article class="card wide"><div class="label">Level sensors</div><div class="sensors"><div class="sensor">Lower sensor S1<b id="s1">--</b></div><div class="sensor">Upper sensor S2<b id="s2">--</b></div></div></article>
<article class="card wide"><div class="footer"><span id="wifi">Wi-Fi: --</span><span id="telegram">Telegram: --</span><span id="updated">Updated: --</span></div></article>
</section></main>
<script>
const $=id=>document.getElementById(id);function badge(el,state,tone){el.textContent=state;el.className='badge '+tone}
async function refresh(){try{const r=await fetch('/api/status',{cache:'no-store'});if(!r.ok)throw 0;const d=await r.json();$('ph').textContent=d.ph===null?'--':d.ph.toFixed(2);badge($('phState'),d.phState,d.phTone);$('temp').textContent=d.temperature===null?'--':d.temperature.toFixed(1);badge($('tempState'),d.tempState,d.tempTone);$('water').textContent=d.water;badge($('waterState'),d.waterState,d.waterTone);badge($('s1'),d.s1,d.s1==='WET'?'ok':'info');badge($('s2'),d.s2,d.s2==='WET'?'ok':'info');$('wifi').textContent='Wi-Fi: '+d.wifi;$('telegram').textContent='Telegram: '+d.telegram;$('updated').textContent='Updated: '+new Date().toLocaleTimeString();$('connection').textContent='Live';document.body.classList.remove('offline')}catch(e){$('connection').textContent='Reconnecting...';document.body.classList.add('offline')}}
refresh();setInterval(refresh,2000);
</script></body></html>)HTML";

void printLcdRow(uint8_t row, const char *text) {
  char output[LCD_COLUMNS + 1];
  snprintf(output, sizeof(output), "%-20.20s", text);
  lcd.setCursor(0, row);
  lcd.print(output);
}

float readFilteredPhVoltage() {
  uint32_t samples[PH_SAMPLE_COUNT];
  for (uint8_t i = 0; i < PH_SAMPLE_COUNT; i++)
    samples[i] = analogReadMilliVolts(PH_SENSOR_PIN);
  for (uint8_t i = 1; i < PH_SAMPLE_COUNT; i++) {
    uint32_t current = samples[i];
    int8_t j = i - 1;
    while (j >= 0 && samples[j] > current) {
      samples[j + 1] = samples[j];
      j--;
    }
    samples[j + 1] = current;
  }
  constexpr uint8_t discardCount = PH_SAMPLE_COUNT / 5;
  uint32_t sum = 0;
  uint8_t count = 0;
  for (uint8_t i = discardCount; i < PH_SAMPLE_COUNT - discardCount; i++) {
    sum += samples[i];
    count++;
  }
  return (static_cast<float>(sum) / count) / 1000.0f;
}

float voltageToPh(float voltage) {
  return PH_NEUTRAL_VALUE + (voltage - PH_NEUTRAL_VOLTAGE) * PH_SLOPE;
}

void updateLevelSensors(unsigned long now) {
  bool raw1 = digitalRead(LEVEL_SENSOR_1_PIN) == LEVEL_DETECTED_STATE;
  bool raw2 = digitalRead(LEVEL_SENSOR_2_PIN) == LEVEL_DETECTED_STATE;
  if (raw1 != level1RawPrevious) {
    level1RawPrevious = raw1;
    level1ChangedAt = now;
  }
  if (raw2 != level2RawPrevious) {
    level2RawPrevious = raw2;
    level2ChangedAt = now;
  }
  if (now - level1ChangedAt >= LEVEL_DEBOUNCE_MS) level1Detected = raw1;
  if (now - level2ChangedAt >= LEVEL_DEBOUNCE_MS) level2Detected = raw2;
}

void updateTemperature(unsigned long now) {
  if (now - temperatureRequestAt < TEMPERATURE_CONVERSION_MS) return;
  float reading = temperatureSensors.getTempCByIndex(0);
  waterTemperatureC = (reading != DEVICE_DISCONNECTED_C && reading >= -55.0f &&
                       reading <= 125.0f) ? reading : NAN;
  temperatureSensors.requestTemperatures();
  temperatureRequestAt = now;
}

void loadSettings() {
  preferences.begin("watermon", true);
  String token = preferences.getString("tg_token", "");
  String chat = preferences.getString("tg_chat", "");
  String savedCloudKey = preferences.getString("cloud_key", "");
  strlcpy(telegramBotToken, token.c_str(), sizeof(telegramBotToken));
  strlcpy(telegramChatId, chat.c_str(), sizeof(telegramChatId));
  if (savedCloudKey.length())
    strlcpy(cloudDeviceKey, savedCloudKey.c_str(), sizeof(cloudDeviceKey));
  temperatureMinimum = preferences.getFloat("temp_min", 24.0f);
  temperatureMaximum = preferences.getFloat("temp_max", 29.0f);
  phMinimum = preferences.getFloat("ph_min", 6.0f);
  phMaximum = preferences.getFloat("ph_max", 7.5f);
  preferences.end();
  if (temperatureMinimum >= temperatureMaximum) {
    temperatureMinimum = 24.0f; temperatureMaximum = 29.0f;
  }
  if (phMinimum < 0 || phMinimum >= phMaximum || phMaximum > 14) {
    phMinimum = 6.0f; phMaximum = 7.5f;
  }
}

void updateParameterDefaults() {
  char value[12];
  telegramTokenParameter.setValue(telegramBotToken, 63);
  telegramChatParameter.setValue(telegramChatId, 31);
  cloudKeyParameter.setValue(cloudDeviceKey, 95);
  snprintf(value, sizeof(value), "%.1f", temperatureMinimum);
  temperatureMinimumParameter.setValue(value, 8);
  snprintf(value, sizeof(value), "%.1f", temperatureMaximum);
  temperatureMaximumParameter.setValue(value, 8);
  snprintf(value, sizeof(value), "%.2f", phMinimum);
  phMinimumParameter.setValue(value, 8);
  snprintf(value, sizeof(value), "%.2f", phMaximum);
  phMaximumParameter.setValue(value, 8);
}

void initializeTelegram() {
  if (telegramBot) { delete telegramBot; telegramBot = nullptr; }
  telegramClient.setCACert(TELEGRAM_CERTIFICATE_ROOT);
  if (strlen(telegramBotToken) && strlen(telegramChatId))
    telegramBot = new UniversalTelegramBot(String(telegramBotToken), telegramClient);
}

void resetAlarmNotifications() {
  AlarmState *alarms[] = {&temperatureAlarm, &temperatureSensorAlarm,
                          &phAlarm, &phSensorAlarm, &waterAlarm,
                          &waterSensorAlarm};
  for (AlarmState *alarm : alarms) *alarm = AlarmState();
}

void savePortalParameters() {
  strlcpy(telegramBotToken, telegramTokenParameter.getValue(), sizeof(telegramBotToken));
  strlcpy(telegramChatId, telegramChatParameter.getValue(), sizeof(telegramChatId));
  strlcpy(cloudDeviceKey, cloudKeyParameter.getValue(), sizeof(cloudDeviceKey));
  float tMin = String(temperatureMinimumParameter.getValue()).toFloat();
  float tMax = String(temperatureMaximumParameter.getValue()).toFloat();
  float pMin = String(phMinimumParameter.getValue()).toFloat();
  float pMax = String(phMaximumParameter.getValue()).toFloat();
  if (tMin < tMax) { temperatureMinimum = tMin; temperatureMaximum = tMax; }
  if (pMin >= 0 && pMin < pMax && pMax <= 14) { phMinimum = pMin; phMaximum = pMax; }
  preferences.begin("watermon", false);
  preferences.putString("tg_token", telegramBotToken);
  preferences.putString("tg_chat", telegramChatId);
  preferences.putString("cloud_key", cloudDeviceKey);
  preferences.putFloat("temp_min", temperatureMinimum);
  preferences.putFloat("temp_max", temperatureMaximum);
  preferences.putFloat("ph_min", phMinimum);
  preferences.putFloat("ph_max", phMaximum);
  preferences.end();
  initializeTelegram();
  resetAlarmNotifications();
  settingsSavedAt = millis();
  Serial.println(F("Configuration saved; secret values hidden."));
}

void startConfigurationPortal() {
  stopDashboardServer();
  updateParameterDefaults();
  printLcdRow(0, "*** WiFi SETUP ***");
  printLcdRow(1, "WaterMonitor-Setup");
  printLcdRow(2, "Password: water1234");
  printLcdRow(3, "Web opens automatic");
  Serial.println(F("Setup AP: WaterMonitor-Setup, open http://192.168.4.1"));
  wifiManager.startConfigPortal(CONFIG_AP_NAME, CONFIG_AP_PASSWORD);
}

void handleConfigButton(unsigned long now) {
  bool pressed = digitalRead(CONFIG_BUTTON_PIN) == LOW;
  if (pressed && !configButtonWasPressed) {
    configButtonWasPressed = true;
    configButtonHandled = false;
    configButtonPressedAt = now;
  }
  if (pressed && !configButtonHandled && now - configButtonPressedAt >= CONFIG_BUTTON_HOLD_MS) {
    configButtonHandled = true;
    startConfigurationPortal();
  }
  if (!pressed) { configButtonWasPressed = false; configButtonHandled = false; }
}

bool telegramReady() {
  return telegramBot && WiFi.status() == WL_CONNECTED && time(nullptr) > 1700000000;
}

bool sendTelegramMessage(const String &message) {
  if (!telegramReady()) return false;
  bool sent = telegramBot->sendMessage(String(telegramChatId), message, "");
  Serial.println(sent ? F("Telegram notification sent.") : F("Telegram notification failed."));
  return sent;
}

void processAlarm(AlarmState &state, bool active, const String &alarmMessage,
                  const String &recoveryMessage, unsigned long now) {
  if (!state.initialized) {
    state.initialized = true;
    state.active = active;
    state.notificationDelivered = !active;
  } else if (state.active != active) {
    state.active = active;
    state.notificationDelivered = false;
    state.lastAttempt = 0;
  }
  bool reminder = state.active && state.notificationDelivered &&
                  now - state.lastDelivered >= TELEGRAM_REMINDER_MS;
  bool retry = !state.notificationDelivered &&
               (state.lastAttempt == 0 || now - state.lastAttempt >= TELEGRAM_RETRY_MS);
  if (!reminder && !retry) return;
  state.lastAttempt = now;
  String message = state.active ? alarmMessage : recoveryMessage;
  if (reminder) message = String("Reminder: ") + message;
  if (sendTelegramMessage(message)) {
    state.notificationDelivered = true;
    state.lastDelivered = now;
  }
}

bool temperatureOutOfRange() {
  if (isnan(waterTemperatureC)) return false;
  if (!temperatureAlarm.active)
    return waterTemperatureC < temperatureMinimum || waterTemperatureC > temperatureMaximum;
  return waterTemperatureC < temperatureMinimum + TEMPERATURE_HYSTERESIS ||
         waterTemperatureC > temperatureMaximum - TEMPERATURE_HYSTERESIS;
}

bool phOutOfRange() {
  if (isnan(phValue)) return false;
  if (!phAlarm.active) return phValue < phMinimum || phValue > phMaximum;
  return phValue < phMinimum + PH_HYSTERESIS || phValue > phMaximum - PH_HYSTERESIS;
}

void handleDashboardStatus() {
  WaterLevelState waterState = getWaterLevelState();
  const char *phState = "NORMAL";
  const char *phTone = "ok";
  const char *tempState = "NORMAL";
  const char *tempTone = "ok";
  const char *waterTone = "ok";

  if (isnan(phValue)) { phState = "SENSOR ERROR"; phTone = "bad"; }
  else if (phValue < phMinimum) { phState = "LOW"; phTone = "warn"; }
  else if (phValue > phMaximum) { phState = "HIGH"; phTone = "warn"; }

  if (isnan(waterTemperatureC)) { tempState = "SENSOR ERROR"; tempTone = "bad"; }
  else if (waterTemperatureC < temperatureMinimum) { tempState = "LOW"; tempTone = "warn"; }
  else if (waterTemperatureC > temperatureMaximum) { tempState = "HIGH"; tempTone = "warn"; }

  if (waterState == WATER_LEVEL_LOW) waterTone = "warn";
  else if (waterState == WATER_LEVEL_SENSOR_ERROR) waterTone = "bad";

  String json;
  json.reserve(420);
  json += F("{\"ph\":");
  json += isnan(phValue) ? "null" : String(phValue, 2);
  json += F(",\"phState\":\""); json += phState;
  json += F("\",\"phTone\":\""); json += phTone;
  json += F("\",\"temperature\":");
  json += isnan(waterTemperatureC) ? "null" : String(waterTemperatureC, 1);
  json += F(",\"tempState\":\""); json += tempState;
  json += F("\",\"tempTone\":\""); json += tempTone;
  json += F("\",\"water\":\""); json += waterLevelName(waterState);
  json += F("\",\"waterState\":\""); json += waterLevelName(waterState);
  json += F("\",\"waterTone\":\""); json += waterTone;
  json += F("\",\"s1\":\""); json += level1Detected ? "WET" : "DRY";
  json += F("\",\"s2\":\""); json += level2Detected ? "WET" : "DRY";
  json += F("\",\"wifi\":\"");
  json += WiFi.status() == WL_CONNECTED ? "CONNECTED" : "OFFLINE";
  json += F("\",\"telegram\":\""); json += telegramBot ? "READY" : "NOT SET";
  json += F("\",\"cloud\":\"");
  json += lastCloudUploadSuccess ? (cloudLastUploadOk ? "SYNCED" : "ERROR") : "WAITING";
  json += F("\"}");

  dashboardServer.sendHeader("Cache-Control", "no-store");
  dashboardServer.send(200, "application/json; charset=utf-8", json);
}

void configureDashboardRoutes() {
  if (dashboardRoutesConfigured) return;
  dashboardServer.on("/", HTTP_GET, []() {
    dashboardServer.sendHeader("Cache-Control", "no-store");
    dashboardServer.send_P(200, "text/html; charset=utf-8", DASHBOARD_HTML);
  });
  dashboardServer.on("/api/status", HTTP_GET, handleDashboardStatus);
  dashboardServer.onNotFound([]() {
    dashboardServer.sendHeader("Location", "/", true);
    dashboardServer.send(302, "text/plain", "");
  });
  dashboardRoutesConfigured = true;
}

void startDashboardServer() {
  if (dashboardStarted || WiFi.status() != WL_CONNECTED ||
      wifiManager.getConfigPortalActive()) return;
  configureDashboardRoutes();
  dashboardServer.begin();
  dashboardStarted = true;
  Serial.print(F("Dashboard: http://"));
  Serial.println(WiFi.localIP());
}

void stopDashboardServer() {
  if (!dashboardStarted) return;
  dashboardServer.stop();
  dashboardStarted = false;
}

void updateAlerts(unsigned long now) {
  bool tempValid = !isnan(waterTemperatureC);
  bool pHValid = !isnan(phValue);
  WaterLevelState waterState = getWaterLevelState();
  processAlarm(temperatureSensorAlarm, !tempValid,
               "ALERT: DS18B20 temperature sensor error.",
               "RECOVERY: Temperature sensor is working again.", now);
  if (tempValid)
    processAlarm(temperatureAlarm, temperatureOutOfRange(),
      String("ALERT: Temperature ") + String(waterTemperatureC, 1) + " C is outside " +
      String(temperatureMinimum, 1) + "-" + String(temperatureMaximum, 1) + " C.",
      String("RECOVERY: Temperature is normal at ") + String(waterTemperatureC, 1) + " C.", now);
  processAlarm(phSensorAlarm, !pHValid, "ALERT: pH sensor reading is invalid.",
               "RECOVERY: pH sensor reading is valid again.", now);
  if (pHValid)
    processAlarm(phAlarm, phOutOfRange(),
      String("ALERT: pH ") + String(phValue, 2) + " is outside " +
      String(phMinimum, 2) + "-" + String(phMaximum, 2) + ".",
      String("RECOVERY: pH is normal at ") + String(phValue, 2) + ".", now);
  processAlarm(waterSensorAlarm, waterState == WATER_LEVEL_SENSOR_ERROR,
      "ALERT: Water sensors conflict. S1 is DRY but S2 is WET. Check sensor order and wiring.",
      "RECOVERY: Water sensor states are valid again.", now);
  if (waterState != WATER_LEVEL_SENSOR_ERROR) {
    processAlarm(waterAlarm, waterState == WATER_LEVEL_LOW,
        "ALERT: Water level is LOW. Both S1 and S2 are DRY.",
        String("RECOVERY: Water level is now ") + waterLevelName(waterState) + ".", now);
  }
}

void updateCloudTelemetry(unsigned long now) {
  if (WiFi.status() != WL_CONNECTED || strlen(cloudDeviceKey) == 0 ||
      time(nullptr) <= 1700000000 || isnan(phValue) || isnan(waterTemperatureC)) return;
  if (lastCloudUploadAttempt != 0 &&
      now - lastCloudUploadAttempt < CLOUD_UPLOAD_INTERVAL_MS) return;
  lastCloudUploadAttempt = now;

  uint64_t chipId = ESP.getEfuseMac();
  char deviceId[28];
  snprintf(deviceId, sizeof(deviceId), "water-%04X%08X",
           static_cast<uint16_t>(chipId >> 32), static_cast<uint32_t>(chipId));

  String payload;
  payload.reserve(180);
  payload += F("{\"deviceId\":\""); payload += deviceId;
  payload += F("\",\"ph\":"); payload += String(phValue, 2);
  payload += F(",\"temperature\":"); payload += String(waterTemperatureC, 2);
  payload += F(",\"sensor1\":"); payload += level1Detected ? F("true") : F("false");
  payload += F(",\"sensor2\":"); payload += level2Detected ? F("true") : F("false");
  payload += F(",\"waterLevel\":\""); payload += waterLevelName(getWaterLevelState());
  payload += F("\"}");

  WiFiClientSecure cloudClient;
  cloudClient.setCACert(CLOUD_ROOT_CA);
  HTTPClient http;
  http.setConnectTimeout(CLOUD_HTTP_TIMEOUT_MS);
  http.setTimeout(CLOUD_HTTP_TIMEOUT_MS);
  if (!http.begin(cloudClient, CLOUD_API_URL)) {
    cloudLastUploadOk = false;
    Serial.println(F("Cloud sync: unable to start HTTPS connection."));
    return;
  }
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-Device-Key", cloudDeviceKey);
  int statusCode = http.POST(payload);
  cloudLastUploadOk = statusCode == HTTP_CODE_CREATED;
  if (cloudLastUploadOk) {
    lastCloudUploadSuccess = now;
    Serial.printf("Cloud sync: uploaded successfully (HTTP %d).\n", statusCode);
  } else if (statusCode > 0) {
    Serial.printf("Cloud sync: failed (HTTP %d).\n", statusCode);
  } else {
    Serial.printf("Cloud sync: failed (%s).\n",
                  HTTPClient::errorToString(statusCode).c_str());
  }
  http.end();
}

void updateDisplay() {
  char row[LCD_COLUMNS + 1];

  // Give the user a complete, stable setup screen instead of sensor rows.
  if (settingsSavedAt != 0 && millis() - settingsSavedAt < 3000) {
    printLcdRow(0, "SETTINGS SAVED");
    printLcdRow(1, "WiFi SAVED TO MEMORY");
    printLcdRow(2, "Connecting...");
    printLcdRow(3, "Please wait");
    return;
  }
  if (wifiManager.getConfigPortalActive()) {
    printLcdRow(0, "*** WiFi SETUP ***");
    printLcdRow(1, "WaterMonitor-Setup");
    printLcdRow(2, "Password: water1234");
    // Alternate between the expected automatic popup and the manual fallback.
    if ((millis() / 4000UL) % 2 == 0) printLcdRow(3, "Web opens automatic");
    else printLcdRow(3, "If not:192.168.4.1");
    return;
  }

  if (isnan(phValue)) {
    snprintf(row, sizeof(row), "pH: SENSOR ERROR");
  } else if (phValue < phMinimum) {
    snprintf(row, sizeof(row), "pH:%5.2f  LOW", phValue);
  } else if (phValue > phMaximum) {
    snprintf(row, sizeof(row), "pH:%5.2f  HIGH", phValue);
  } else {
    snprintf(row, sizeof(row), "pH:%5.2f  NORMAL", phValue);
  }
  printLcdRow(0, row);

  if (isnan(waterTemperatureC)) {
    snprintf(row, sizeof(row), "TEMP: SENSOR ERROR");
  } else if (waterTemperatureC < temperatureMinimum) {
    snprintf(row, sizeof(row), "TEMP:%4.1fC  LOW", waterTemperatureC);
  } else if (waterTemperatureC > temperatureMaximum) {
    snprintf(row, sizeof(row), "TEMP:%4.1fC  HIGH", waterTemperatureC);
  } else {
    snprintf(row, sizeof(row), "TEMP:%4.1fC  NORMAL", waterTemperatureC);
  }
  printLcdRow(1, row);

  WaterLevelState waterState = getWaterLevelState();
  if (waterState == WATER_LEVEL_SENSOR_ERROR)
    snprintf(row, sizeof(row), "ERROR S1:DRY S2:WET");
  else
    snprintf(row, sizeof(row), "%-6s S1:%s S2:%s", waterLevelName(waterState),
             level1Detected ? "WET" : "DRY", level2Detected ? "WET" : "DRY");
  printLcdRow(2, row);

  // Alternate the system message with the dashboard address.
  if (dashboardStarted && WiFi.status() == WL_CONNECTED &&
      (millis() / 5000UL) % 2 == 1) {
    String webAddress = String("WEB:") + WiFi.localIP().toString();
    printLcdRow(3, webAddress.c_str());
    return;
  }

  if (temperatureSensorAlarm.active || phSensorAlarm.active || waterSensorAlarm.active) {
    printLcdRow(3, "CHECK SENSOR WIRING");
  } else if (temperatureAlarm.active || phAlarm.active || waterAlarm.active) {
    printLcdRow(3, telegramBot ? "ALERT SENT TELEGRAM" : "ALERT - SET TELEGRAM");
  } else if (WiFi.status() != WL_CONNECTED) {
    printLcdRow(3, "WiFi NOT CONNECTED");
  } else if (!telegramBot) {
    printLcdRow(3, "TELEGRAM NOT SET");
  } else if (lastCloudUploadAttempt != 0 && !cloudLastUploadOk) {
    printLcdRow(3, "CLOUD SYNC ERROR");
  } else if (lastCloudUploadSuccess != 0) {
    printLcdRow(3, "SYSTEM + CLOUD OK");
  } else {
    printLcdRow(3, "CLOUD CONNECTING...");
  }
}

void printDebugOutput() {
  Serial.println(F("---------- Sensor data ----------"));
  Serial.printf("pH: %.2f (allowed %.2f-%.2f)\n", phValue, phMinimum, phMaximum);
  if (isnan(waterTemperatureC)) Serial.println(F("Temperature: SENSOR ERROR"));
  else Serial.printf("Temperature: %.2f C (allowed %.1f-%.1f C)\n",
                     waterTemperatureC, temperatureMinimum, temperatureMaximum);
  Serial.printf("S1: %s, S2: %s, water level: %s\n",
                level1Detected ? "WET" : "DRY",
                level2Detected ? "WET" : "DRY",
                waterLevelName(getWaterLevelState()));
  Serial.printf("Wi-Fi: %s, Telegram configured: %s, Cloud: %s\n\n",
                WiFi.status() == WL_CONNECTED ? "CONNECTED" : "OFFLINE",
                telegramBot ? "YES" : "NO",
                lastCloudUploadSuccess ? (cloudLastUploadOk ? "SYNCED" : "ERROR") : "WAITING");
}

void setup() {
  Serial.begin(115200);
  pinMode(PH_SENSOR_PIN, INPUT);
  pinMode(LEVEL_SENSOR_1_PIN, INPUT);
  pinMode(LEVEL_SENSOR_2_PIN, INPUT);
  pinMode(CONFIG_BUTTON_PIN, INPUT_PULLUP);
  analogReadResolution(12);
  analogSetPinAttenuation(PH_SENSOR_PIN, ADC_11db);
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  lcd.begin();
  lcd.backlight();
  lcd.clear();
  printLcdRow(0, "WATER MONITOR");
  printLcdRow(1, "Starting sensors...");
  printLcdRow(2, "Loading saved setup");
  printLcdRow(3, "Please wait");

  temperatureSensors.begin();
  temperatureSensors.setResolution(12);
  temperatureSensors.setWaitForConversion(false);
  temperatureSensors.requestTemperatures();
  temperatureRequestAt = millis();
  unsigned long now = millis();
  level1RawPrevious = digitalRead(LEVEL_SENSOR_1_PIN) == LEVEL_DETECTED_STATE;
  level2RawPrevious = digitalRead(LEVEL_SENSOR_2_PIN) == LEVEL_DETECTED_STATE;
  level1Detected = level1RawPrevious;
  level2Detected = level2RawPrevious;
  level1ChangedAt = level2ChangedAt = now;

  loadSettings();
  updateParameterDefaults();
  initializeTelegram();
  wifiManager.addParameter(&telegramTokenParameter);
  wifiManager.addParameter(&telegramChatParameter);
  wifiManager.addParameter(&cloudKeyParameter);
  wifiManager.addParameter(&temperatureMinimumParameter);
  wifiManager.addParameter(&temperatureMaximumParameter);
  wifiManager.addParameter(&phMinimumParameter);
  wifiManager.addParameter(&phMaximumParameter);
  wifiManager.setSaveParamsCallback(savePortalParameters);
  wifiManager.setConfigPortalBlocking(false);
  // Redirect Android/iOS/Windows network checks to the setup page.
  wifiManager.setCaptivePortalEnable(true);
  wifiManager.autoConnect(CONFIG_AP_NAME, CONFIG_AP_PASSWORD);
  Serial.println(F("ESP32 monitoring system started."));
}

void loop() {
  unsigned long now = millis();
  wifiManager.process();
  handleConfigButton(now);

  if (wifiManager.getConfigPortalActive() || WiFi.status() != WL_CONNECTED) {
    stopDashboardServer();
  } else {
    startDashboardServer();
  }
  if (dashboardStarted) dashboardServer.handleClient();

  updateLevelSensors(now);
  updateTemperature(now);
  if (WiFi.status() == WL_CONNECTED && !ntpStarted) {
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    ntpStarted = true;
  }
  if (now - lastDisplayUpdate >= DISPLAY_INTERVAL_MS) {
    lastDisplayUpdate = now;
    phVoltage = readFilteredPhVoltage();
    float calculatedPh = voltageToPh(phVoltage);
    phValue = (calculatedPh >= 0 && calculatedPh <= 14) ? calculatedPh : NAN;
    updateAlerts(now);
    updateDisplay();
  }
  updateCloudTelemetry(now);
  if (now - lastSerialUpdate >= SERIAL_INTERVAL_MS) {
    lastSerialUpdate = now;
    printDebugOutput();
  }
}
