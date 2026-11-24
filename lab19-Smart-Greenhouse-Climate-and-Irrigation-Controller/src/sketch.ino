#include <Arduino.h>                                                                    // базова бібліотека функцій мікроконтролера
#include <Wire.h>                                                                       // апаратний інтерфейс двопровідної шини даних
#include <LiquidCrystal_I2C.h>                                                          // драйвер символьного дисплея теплиці
#include <DHT.h>                                                                        // бібліотека цифрового датчика температури і вологості
#include <WiFi.h>                                                                       // модуль бездротового мережевого підключення
#include <PubSubClient.h>                                                               // клієнт публікації телеметрії через брокер повідомлень
#include "ai_irrigation_opt.h"                                                          // модуль штучного інтелекту оптимізації поливу

#define PIN_DHT 15                                                                      // лінія даних цифрового сенсора клімату повітря
#define DHT_TYPE DHT22                                                                  // модель цифрового датчика температури і вологості
#define PIN_SOIL_POT 35                                                                 // аналоговий вхід датчика вологості субстрату
#define PIN_LDR 34                                                                      // аналоговий вхід фоторезистора природного світла
#define PIN_RELAY_PUMP 18                                                               // вивід керування силовим реле водяної помпи
#define PIN_RELAY_VENT 5                                                                // вивід керування силовим реле витяжного вентилятора
#define PIN_RELAY_LIGHT 17                                                              // вивід керування силовим реле фітопідсвічування

const char* WIFI_SSID = "Wokwi-GUEST";                                                  // назва бездротової точки доступу симулятора
const char* WIFI_PASS = "";                                                             // пароль підключення до бездротової мережі
const char* MQTT_HOST = "broker.hivemq.com";                                            // адреса публічного брокера повідомлень агрокомплексу
const uint16_t MQTT_PORT = 1883;                                                        // номер мережевого порту обміну повідомленнями
const char* TOPIC_TELEMETRY = "agro/lab19/telemetry";                                   // топік публікації числових показників мікроклімату
const char* TOPIC_RELAYS = "agro/lab19/relays";                                         // топік публікації станів трьох силових реле
const char* TOPIC_REPORT = "agro/lab19/llm_report";                                     // топік публікації автоматичного звіту моделі
const char* TOPIC_COMMAND = "agro/lab19/command";                                       // топік прийому зовнішніх команд керування

DHT dht(PIN_DHT, DHT_TYPE);                                                             // об єкт опитування датчика температури та вологості
LiquidCrystal_I2C lcd(0x27, 16, 2);                                                     // об єкт керування символьним екраном теплиці
WiFiClient wifiClient;                                                                  // мережевий клієнт бездротового з єднання
PubSubClient mqttClient(wifiClient);                                                    // клієнт обміну пакетами з брокером повідомлень
AgroAiOptimizer aiOptimizer;                                                            // екземпляр моделі штучного інтелекту агрокомплексу
AgroAiDecision currentAi;                                                               // структура поточних розрахунків моделі оптимізації

float airTempC = 24.0f;                                                                 // поточна температура повітря у теплиці
float airHumPct = 55.0f;                                                                // поточна відносна вологість повітря у відсотках
float soilHumPct = 50.0f;                                                               // поточна вологість грунтового субстрату у відсотках
float lightPct = 65.0f;                                                                 // поточний рівень природного освітлення у відсотках
bool prevPumpState = false;                                                             // попередній стан реле водяної помпи
bool prevVentState = false;                                                             // попередній стан реле витяжного вентилятора
bool prevLightState = false;                                                            // попередній стан реле фітопідсвічування
unsigned long lastSensorReadMs = 0;                                                     // мітка часу останнього опитування трьох датчиків
unsigned long lastSecondTickMs = 0;                                                     // мітка часу секундного кроку таймера поливу
unsigned long lastDisplayMs = 0;                                                        // мітка часу оновлення інформації на дисплеї
unsigned long lastSerialTableMs = 0;                                                    // мітка часу виводу таблиці у термінал
unsigned long lastMqttPubMs = 0;                                                        // мітка часу публікації пакетів у брокер
unsigned long lastMqttRetryMs = 0;                                                      // мітка часу останньої спроби підключення брокера

void logEvent(const char* category, const char* message) {                              // вивід датованої події у послідовний порт
  unsigned long totalSec = millis() / 1000;                                             // переведення часу роботи системи у секунди
  unsigned long minutes = totalSec / 60;                                                // обчислення кількості повних хвилин
  unsigned long seconds = totalSec % 60;                                                // обчислення залишку секунд
  char stamp[16];                                                                       // буфер форматування часової позначки
  snprintf(stamp, sizeof(stamp), "[%02lu:%02lu]", minutes, seconds);                    // формування рядка хвилин та секунд
  Serial.print(stamp);                                                                  // друк часової позначки з лівого краю
  Serial.print(" [");                                                                   // друк відкриваючої дужки категорії
  Serial.print(category);                                                               // вивід назви функціональної підсистеми
  Serial.print("] ");                                                                   // друк закриваючої дужки з пробілом
  Serial.println(message);                                                              // вивід текстового опису зареєстрованої події
}                                                                                       // завершення функції журналювання подій

void applyRelays() {                                                                    // комутація трьох силових реле за рішенням моделі
  digitalWrite(PIN_RELAY_PUMP, currentAi.pumpRelayOn ? HIGH : LOW);                     // перемикання лінії реле водяної помпи поливу
  digitalWrite(PIN_RELAY_VENT, currentAi.ventRelayOn ? HIGH : LOW);                     // перемикання лінії реле витяжного вентилятора
  digitalWrite(PIN_RELAY_LIGHT, currentAi.lightRelayOn ? HIGH : LOW);                   // перемикання лінії реле фітолампи освітлення

  if (currentAi.pumpRelayOn != prevPumpState) {                                         // перевірка зміни стану реле водяної помпи
    prevPumpState = currentAi.pumpRelayOn;                                              // збереження нового стану реле водяної помпи
    logEvent("RELAY_PUMP", prevPumpState ? "Water pump ON" : "Water pump OFF");         // запис події комутації помпи у журнал
  }                                                                                     // завершення перевірки реле водяної помпи

  if (currentAi.ventRelayOn != prevVentState) {                                         // перевірка зміни стану реле вентилятора
    prevVentState = currentAi.ventRelayOn;                                              // збереження нового стану реле вентилятора
    logEvent("RELAY_VENT", prevVentState ? "Exhaust fan ON" : "Exhaust fan OFF");       // запис події комутації витяжки у журнал
  }                                                                                     // завершення перевірки реле вентилятора

  if (currentAi.lightRelayOn != prevLightState) {                                       // перевірка зміни стану реле фітопідсвічування
    prevLightState = currentAi.lightRelayOn;                                            // збереження нового стану реле фітопідсвічування
    logEvent("RELAY_LIGHT", prevLightState ? "Grow light ON" : "Grow light OFF");       // запис події комутації фітолампи у журнал
  }                                                                                     // завершення перевірки реле фітопідсвічування
}                                                                                       // завершення функції керування силових реле

void readAgroSensors() {                                                                // неблокуючий збір показників трьох датчиків теплиці
  float readT = dht.readTemperature();                                                  // зчитування температури повітря з цифрового сенсора
  float readH = dht.readHumidity();                                                     // зчитування вологості повітря з цифрового сенсора
  if (!isnan(readT) && readT > -40.0f && readT < 85.0f) {                               // перевірка коректності значення температури повітря
    airTempC = readT;                                                                   // оновлення поточної температури повітря теплиці
  }                                                                                     // завершення валідації температури повітря
  if (!isnan(readH) && readH >= 0.0f && readH <= 100.0f) {                              // перевірка коректності значення вологості повітря
    airHumPct = readH;                                                                  // оновлення поточної відносної вологості повітря
  }                                                                                     // завершення валідації вологості повітря

  int rawSoil = analogRead(PIN_SOIL_POT);                                               // зчитування аналогового сигналу вологості субстрату
  float mappedSoil = ((float)rawSoil / 4095.0f) * 100.0f;                               // перерахунок сигналу субстрату у відсотки вологості
  soilHumPct = 0.8f * soilHumPct + 0.2f * mappedSoil;                                   // експоненційне згладжування вологості грунту від шуму

  int rawLdr = analogRead(PIN_LDR);                                                     // зчитування аналогового сигналу фоторезистора світла
  float mappedLight = ((float)(4095 - rawLdr) / 4095.0f) * 100.0f;                      // перерахунок сигналу фоторезистора у відсотки світла
  lightPct = 0.8f * lightPct + 0.2f * mappedLight;                                      // експоненційне згладжування рівня освітленості

  currentAi = aiOptimizer.evaluate(airTempC, airHumPct, soilHumPct, lightPct);          // виконання інтелектуальної оптимізації правил та поливу
  applyRelays();                                                                        // застосування оновлених станів трьох силових реле
}                                                                                       // завершення функції опитування датчиків агрокомплексу

void updateLcdScreen() {                                                                // оновлення параметрів мікроклімату та реле на дисплеї
  char line1[17];                                                                       // буфер першого рядка символьного екрана
  char line2[17];                                                                       // буфер другого рядка символьного екрана
  snprintf(line1, sizeof(line1), "T:%2.0fC S:%2.0f%% L:%2.0f%%",                        // шаблон рядка температури грунту та світла
           airTempC, soilHumPct, lightPct);                                             // підстановка температури вологості та світла
  char pCh = currentAi.pumpRelayOn ? '1' : '0';                                         // символ стану реле водяної помпи
  char vCh = currentAi.ventRelayOn ? '1' : '0';                                         // символ стану реле витяжного вентилятора
  char lCh = currentAi.lightRelayOn ? '1' : '0';                                        // символ стану реле фітолампи освітлення
  snprintf(line2, sizeof(line2), "P:%c V:%c L:%c %3lds",                                // шаблон рядка станів реле і часу поливу
           pCh, vCh, lCh, currentAi.pumpDurationSec);                                   // підстановка станів реле та секунд помпи

  lcd.setCursor(0, 0);                                                                  // встановлення курсора на початок першого рядка
  lcd.print(line1);                                                                     // вивід показників клімату на перший рядок
  lcd.print("  ");                                                                      // очищення залишкових символів першого рядка
  lcd.setCursor(0, 1);                                                                  // встановлення курсора на початок другого рядка
  lcd.print(line2);                                                                     // вивід станів реле та часу помпи
  lcd.print("  ");                                                                      // очищення залишкових символів другого рядка
}                                                                                       // завершення функції оновлення символьного екрана

void mqttCallback(char* topic, byte* payload, unsigned int length) {                    // обробник вхідних повідомлень від брокера
  char cmd[64];                                                                         // буфер для збереження тексту отриманої команди
  unsigned int copyLen = (length < 63) ? length : 63;                                   // обмеження довжини повідомлення розміром буфера
  memcpy(cmd, payload, copyLen);                                                        // копіювання байтів повідомлення у текстовий буфер
  cmd[copyLen] = '\0';                                                                  // додавання нульового символу завершення рядка
  logEvent("MQTT_IN", cmd);                                                             // реєстрація отриманої дистанційної команди у журналі
}                                                                                       // завершення обробника вхідних пакетів брокера

void ensureMqttConnected() {                                                            // неблокуюча підтримка з єднання з брокером
  if (WiFi.status() != WL_CONNECTED) {                                                  // перевірка наявності активного бездротового зв язку
    return;                                                                             // вихід за відсутності підключення до мережі
  }                                                                                     // завершення перевірки статусу бездротової мережі
  if (mqttClient.connected()) {                                                         // перевірка активного стану сесії брокера
    return;                                                                             // вихід якщо клієнт уже успішно підключений
  }                                                                                     // завершення перевірки підключення клієнта
  if (lastMqttRetryMs > 0 && millis() - lastMqttRetryMs < 5000) {                       // перевірка інтервалу між спробами підключення
    return;                                                                             // очікування п ять секунд перед новою спробою
  }                                                                                     // завершення перевірки інтервалу повторного з єднання
  lastMqttRetryMs = millis();                                                           // оновлення мітки часу спроби підключення
  char clientId[32];                                                                    // буфер формування унікального ідентифікатора клієнта
  snprintf(clientId, sizeof(clientId), "AgroUnit-%04lu", millis() % 10000);             // генерація імені клієнта для публічного брокера
  if (mqttClient.connect(clientId)) {                                                   // встановлення сесії з брокером повідомлень
    mqttClient.subscribe(TOPIC_COMMAND);                                                // підписка на топік зовнішніх команд керування
    logEvent("MQTT", "Connected to HiveMQ broker and subscribed");                      // запис у журнал про успішне з єднання з брокером
  }                                                                                     // завершення обробки підключення до брокера
}                                                                                       // завершення функції підтримки з єднання з брокером

void publishAgroMqtt() {                                                                // публікація телеметрії та статусів реле у топіки
  if (!mqttClient.connected()) {                                                        // перевірка наявності активного з єднання з брокером
    return;                                                                             // скасування відправки за відсутності зв язку
  }                                                                                     // завершення перевірки активності брокера
  char payloadTelemetry[160];                                                           // буфер пакету числових показників мікроклімату
  const char* fmtT = "{\"t\":%.1f,\"ha\":%.1f,\"hs\":%.1f,\"l\":%.1f,\"ev\":%.2f}";     // шаблон полів температури вологості та випаровування
  snprintf(payloadTelemetry, sizeof(payloadTelemetry), fmtT,                            // форматування рядка телеметрії сенсорів
           airTempC, airHumPct, soilHumPct, lightPct, currentAi.evapRateMmDay);         // підстановка поточних виміряних та розрахованих величин
  mqttClient.publish(TOPIC_TELEMETRY, payloadTelemetry);                                // публікація числових даних у топік телеметрії

  char payloadRelays[140];                                                              // буфер пакету станів реле та економії води
  const char* fmtR = "{\"pump\":%d,\"vent\":%d,\"light\":%d,\"s\":%ld,\"save\":%.1f}";  // шаблон полів трьох реле та тривалості поливу
  snprintf(payloadRelays, sizeof(payloadRelays), fmtR,                                  // форматування рядка виконавчих механізмів
           currentAi.pumpRelayOn ? 1 : 0, currentAi.ventRelayOn ? 1 : 0,                // підстановка станів реле помпи і вентилятора
           currentAi.lightRelayOn ? 1 : 0, currentAi.pumpDurationSec,                   // підстановка стану фітолампи та часу поливу
           currentAi.waterSavedPct);                                                    // підстановка розрахованого відсотка економії води
  mqttClient.publish(TOPIC_RELAYS, payloadRelays);                                      // публікація станів реле у відповідний топік
  mqttClient.publish(TOPIC_REPORT, currentAi.llmAutoReport);                            // публікація автоматичного звіту штучного інтелекту
  logEvent("MQTT_PUB", "Telemetry relays and AI report published to HiveMQ");           // реєстрація успішної передачі пакетів у консоль
}                                                                                       // завершення функції публікації даних у брокер

void printDataTable() {                                                                 // вивід періодичної таблиці телеметрії у консоль
  Serial.println();                                                                     // порожній рядок для відділення блоку таблиці
  Serial.println("Agro Complex Telemetry Table:");                                      // заголовок діагностичної таблиці агрокомплексу
  Serial.print("System Stage: ");                                                       // вивід мітки поточного режиму роботи системи
  Serial.println(aiOptimizer.getStageTitle(currentAi.stage));                           // вивід назви активного класифікованого режиму
  Serial.print("Air Temperature: ");                                                    // вивід мітки температури повітря теплиці
  Serial.print(airTempC, 1);                                                            // вивід значення температури повітря з одним знаком
  Serial.print(" C / Vent Threshold: ");                                                // вивід роздільника адаптивного порогу вентиляції
  Serial.print(currentAi.optTempThreshC, 1);                                            // вивід оптимізованого порогу ввімкнення витяжки
  Serial.println(" C");                                                                 // вивід позначки градусів Цельсія
  Serial.print("Air Humidity: ");                                                       // вивід мітки відносної вологості повітря
  Serial.print(airHumPct, 1);                                                           // вивід числового значення вологості повітря
  Serial.println(" %");                                                                 // вивід символу відсотка вологості повітря
  Serial.print("Substrate Moisture: ");                                                 // вивід мітки вологості грунтового субстрату
  Serial.print(soilHumPct, 1);                                                          // вивід поточної вологості субстрату у відсотках
  Serial.print(" % / AI Threshold: ");                                                  // вивід роздільника адаптивного порогу поливу
  Serial.print(currentAi.optSoilThreshPct, 1);                                          // вивід розрахованого моделлю порогу вологості
  Serial.println(" %");                                                                 // вивід символу відсотка порогу субстрату
  Serial.print("Natural Light Level: ");                                                // вивід мітки рівня природного освітлення
  Serial.print(lightPct, 1);                                                            // вивід рівня освітленості теплиці у відсотках
  Serial.println(" %");                                                                 // вивід символу відсотка освітленості
  Serial.print("AI Evaporation Rate: ");                                                // вивід мітки прогнозованої швидкості випаровування
  Serial.print(currentAi.evapRateMmDay, 2);                                             // вивід швидкості випаровування з двома знаками
  Serial.println(" mm/day");                                                            // вивід одиниці вимірювання випаровування за добу
  Serial.print("AI Pump Duration: ");                                                   // вивід мітки розрахованого часу роботи помпи
  Serial.print(currentAi.pumpDurationSec);                                              // вивід кількості секунд дозованого поливу
  Serial.print(" s / Water Saved: ");                                                   // вивід роздільника показника економії води
  Serial.print(currentAi.waterSavedPct, 1);                                             // вивід відсотка зекономленої поливної води
  Serial.println(" %");                                                                 // вивід символу відсотка економії
  Serial.print("Relays [Pump / Vent / Light]: ");                                       // вивід мітки станів трьох силових реле
  Serial.print(currentAi.pumpRelayOn ? "ON / " : "OFF / ");                             // вивід поточного стану реле водяної помпи
  Serial.print(currentAi.ventRelayOn ? "ON / " : "OFF / ");                             // вивід поточного стану реле вентилятора
  Serial.println(currentAi.lightRelayOn ? "ON" : "OFF");                                // вивід поточного стану реле фітопідсвічування
  Serial.println(currentAi.llmAutoReport);                                              // вивід автоматичного текстового звіту моделі
  Serial.println();                                                                     // завершальний порожній рядок після таблиці
}                                                                                       // завершення функції виводу таблиці телеметрії

void setup() {                                                                          // початкове налаштування апаратних вузлів агрокомплексу
  Serial.begin(115200);                                                                 // ініціалізація послідовного порту для моніторингу
  delay(250);                                                                           // коротка пауза стабілізації живлення після старту
  Serial.println();                                                                     // порожній рядок перед стартовим банером
  Serial.println("===================");                                                // верхня рамка стартового інформаційного банера
  Serial.println("Smart Greenhouse Climate and Irrigation Controller");                 // назва агропроєкту англійською мовою
  Serial.println("===================");                                                // нижня рамка стартового інформаційного банера
  Serial.println();                                                                     // відступ порожнім рядком після заголовка

  pinMode(PIN_SOIL_POT, INPUT);                                                         // налаштування аналогового входу вологості субстрату
  pinMode(PIN_LDR, INPUT);                                                              // налаштування аналогового входу датчика освітленості
  pinMode(PIN_RELAY_PUMP, OUTPUT);                                                      // налаштування виходу керування реле водяної помпи
  pinMode(PIN_RELAY_VENT, OUTPUT);                                                      // налаштування виходу керування реле вентилятора
  pinMode(PIN_RELAY_LIGHT, OUTPUT);                                                     // налаштування виходу керування реле фітолампи
  digitalWrite(PIN_RELAY_PUMP, LOW);                                                    // початкове вимкнення реле водяної помпи
  digitalWrite(PIN_RELAY_VENT, LOW);                                                    // початкове вимкнення реле витяжного вентилятора
  digitalWrite(PIN_RELAY_LIGHT, LOW);                                                   // початкове вимкнення реле фітопідсвічування

  dht.begin();                                                                          // ініціалізація цифрового датчика клімату повітря
  aiOptimizer.init();                                                                   // початкове налаштування моделі оптимізації поливу
  Wire.begin(21, 22);                                                                   // запуск апаратної двопровідної шини дисплея
  lcd.init();                                                                           // ініціалізація контролера символьного дисплея
  lcd.backlight();                                                                      // увімкнення підсвічування символьного екрана
  lcd.setCursor(0, 0);                                                                  // переміщення курсора у початок першого рядка
  lcd.print("AGRO AI SYSTEM");                                                          // вивід привітального заголовка на екран
  lcd.setCursor(0, 1);                                                                  // переміщення курсора у початок другого рядка
  lcd.print("INITIALIZING...");                                                         // вивід статусу початкового запуску системи
  logEvent("SYSTEM", "Sensors relays and display initialized");                         // запис у журнал про готовність периферії

  WiFi.begin(WIFI_SSID, WIFI_PASS);                                                     // запуск підключення до бездротової мережі
  Serial.print("Connecting to WiFi");                                                   // вивід повідомлення про пошук мережі
  unsigned long wifiStart = millis();                                                   // фіксація часу початку підключення до мережі
  while (WiFi.status() != WL_CONNECTED && millis() - wifiStart < 5000) {                // очікування з єднання з лімітом п ять секунд
    delay(200);                                                                         // інтервал між перевірками статусу підключення
    Serial.print(".");                                                                  // друк індикатора прогресу у консоль
  }                                                                                     // завершення циклу очікування бездротової мережі
  Serial.println();                                                                     // перехід на новий рядок після підключення

  mqttClient.setServer(MQTT_HOST, MQTT_PORT);                                           // налаштування адреси та порту брокера
  mqttClient.setCallback(mqttCallback);                                                 // призначення функції прийому вхідних команд
  if (WiFi.status() == WL_CONNECTED) {                                                  // перевірка успішного отримання мережевої адреси
    logEvent("NETWORK", "WiFi connected for HiveMQ telemetry");                         // підтвердження підключення до бездротової мережі
    Serial.print("IP Address: ");                                                       // вивід заголовка мережевої адреси
    Serial.println(WiFi.localIP());                                                     // вивід отриманої адреси мікроконтролера
    ensureMqttConnected();                                                              // встановлення першого з єднання з брокером
  } else {                                                                              // обробка автономного режиму без мережі
    logEvent("NETWORK", "WiFi offline running autonomous AI rules");                    // повідомлення про перехід у локальний режим
  }                                                                                     // завершення перевірки бездротового зв язку

  for (int i = 0; i < 6; i++) {                                                         // початкове калібрування фільтрів аналогових датчиків
    readAgroSensors();                                                                  // зчитування датчиків та встановлення початкових реле
  }                                                                                     // завершення циклу стабілізації стартових показників
  updateLcdScreen();                                                                    // вивід початкових показників на символьний дисплей
  logEvent("AI_AGRO", currentAi.llmAutoReport);                                         // вивід стартового автозвіту штучного інтелекту
  unsigned long bootDoneMs = millis();                                                  // фіксація моменту завершення стартової ініціалізації
  lastSensorReadMs = bootDoneMs;                                                        // синхронізація таймера опитування датчиків
  lastDisplayMs = bootDoneMs;                                                           // синхронізація таймера оновлення дисплея
  lastSerialTableMs = bootDoneMs;                                                       // синхронізація таймера першого виводу таблиці
  lastMqttPubMs = bootDoneMs;                                                           // синхронізація таймера публікації у брокер
}                                                                                       // завершення функції початкового налаштування

void loop() {                                                                           // головний безперервний цикл роботи агрокомплексу
  ensureMqttConnected();                                                                // перевірка та підтримка активності брокера
  if (mqttClient.connected()) {                                                         // перевірка наявності підключення до брокера
    mqttClient.loop();                                                                  // обробка фонових пакетів клієнта повідомлень
  }                                                                                     // завершення обслуговування клієнта брокера

  if (millis() - lastSecondTickMs >= 1000) {                                            // відлік однієї секунди для таймера помпи
    lastSecondTickMs = millis();                                                        // оновлення мітки секундного інтервалу
    aiOptimizer.tickSecond();                                                           // зменшення залишкового часу активного поливу
  }                                                                                     // завершення секундного кроку таймера

  if (millis() - lastSensorReadMs >= 500) {                                             // періодичне опитування датчиків кожні пів секунди
    lastSensorReadMs = millis();                                                        // оновлення мітки часу опитування сенсорів
    readAgroSensors();                                                                  // зчитування трьох датчиків та оптимізація реле
  }                                                                                     // завершення блоку опитування датчиків

  if (millis() - lastDisplayMs >= 500) {                                                // періодичне оновлення дисплея кожні пів секунди
    lastDisplayMs = millis();                                                           // оновлення мітки часу виводу на екран
    updateLcdScreen();                                                                  // відображення поточних даних на символьному дисплеї
  }                                                                                     // завершення блоку оновлення дисплея

  if (millis() - lastSerialTableMs >= 4000) {                                           // періодичний друк таблиці кожні чотири секунди
    lastSerialTableMs = millis();                                                       // оновлення мітки часу термінального звіту
    printDataTable();                                                                   // вивід структурованої таблиці параметрів у консоль
  }                                                                                     // завершення блоку термінальної таблиці

  if (millis() - lastMqttPubMs >= 5000) {                                               // періодична публікація у брокер кожні п ять секунд
    lastMqttPubMs = millis();                                                           // оновлення мітки часу мережевої публікації
    publishAgroMqtt();                                                                  // відправка телеметрії та статусів реле у топіки
  }                                                                                     // завершення блоку публікації у брокер
}                                                                                       // завершення головного циклу програми