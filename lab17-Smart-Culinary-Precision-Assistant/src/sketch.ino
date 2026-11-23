#include <Arduino.h>                                                                    // базова бібліотека функцій мікроконтролера
#include <Wire.h>                                                                       // апаратний інтерфейс двопровідної шини даних
#include <LiquidCrystal_I2C.h>                                                          // драйвер символьного дисплея
#include <OneWire.h>                                                                    // протокол обміну даними з цифровим термометром
#include <DallasTemperature.h>                                                          // бібліотека керування датчиком температури
#include <WiFi.h>                                                                       // модуль бездротового мережевого підключення
#include <HTTPClient.h>                                                                 // клієнт передачі телеметрії через веб запити
#include "driver/ledc.h"                                                                // апаратний генератор сигналів звукового таймера
#include "esp_log.h"                                                                    // модуль керування системними повідомленнями ядра
#include "ai_chef.h"                                                                    // модуль штучного інтелекту кулінарного асистента

#define PIN_DS18B20 14                                                                  // лінія даних цифрового температурного щупа
#define PIN_WEIGHT_POT 34                                                               // аналоговий вхід імітації тензодатчика маси
#define PIN_BUZZER 27                                                                   // лінія керування п єзодинаміком таймера
#define PIN_BTN_MODE 32                                                                 // кнопка циклічного вибору режиму приготування
#define PIN_BTN_START 33                                                                // кнопка запуску та зупинки зворотного відліку
#define PIN_BTN_RESET 25                                                                // кнопка скидання таймера та звукового сигналу

const char* WIFI_SSID = "Wokwi-GUEST";                                                  // назва бездротової точки доступу симулятора
const char* WIFI_PASS = "";                                                             // пароль підключення до бездротової мережі
const char* TS_API_KEY = "YQ02WJAGLGM7C81Z";                                            // ключ запису кривої нагрівання у хмарний канал
const char* TS_URL = "http://api.thingspeak.com/update";                                // адреса сервера прийому телеметричних пакетів

LiquidCrystal_I2C lcd(0x27, 16, 2);                                                     // об єкт керування символьним екраном
OneWire oneWire(PIN_DS18B20);                                                           // екземпляр шини цифрового датчика температури
DallasTemperature tempSensor(&oneWire);                                                 // об єкт опитування температурного щупа
CulinaryAiChef aiChef;                                                                  // екземпляр моделі штучного інтелекту кулінара
ChefAiAssessment currentAi;                                                             // структура поточних розрахунків моделі

CookingMode activeMode = MODE_POULTRY;                                                  // поточний вибраний режим приготування страви
bool timerRunning = false;                                                              // прапорець активного зворотного відліку часу
bool completionAlerted = false;                                                         // прапорець одноразового звукового сповіщення готовності
int currentBuzzerFreq = 0;                                                              // поточна встановлена частота звукового зумера
float currentWeightG = 500.0f;                                                          // виміряна маса продукту у грамах
float currentTempC = 24.0f;                                                             // поточна внутрішня температура страви
int lastHttpStatus = 0;                                                                 // код відповіді останньої передачі даних у хмару
unsigned long lastSensorReadMs = 0;                                                     // мітка часу опитування датчиків ваги і тепла
unsigned long lastSecondTickMs = 0;                                                     // мітка часу секундного кроку зворотного відліку
unsigned long lastDisplayMs = 0;                                                        // мітка часу оновлення інформації на дисплеї
unsigned long lastSerialTableMs = 0;                                                    // мітка часу виводу таблиці у термінал
unsigned long lastCloudSyncMs = 0;                                                      // мітка часу відправки даних у хмарний сервіс
unsigned long buzzerStopMs = 0;                                                         // момент автоматичного вимкнення звукового сигналу

void setBuzzerTone(int freq) {                                                          // безпечне керування частотою зумера через таймер
  if (freq != currentBuzzerFreq) {                                                      // перевірка зміни заданої частоти звучання
    currentBuzzerFreq = freq;                                                           // збереження нової активної частоти
    if (freq > 0) {                                                                     // перевірка умови увімкнення звукового сигналу
      ledc_set_freq(LEDC_LOW_SPEED_MODE, LEDC_TIMER_0, freq);                           // встановлення частоти апаратного генератора
      ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 512);                          // встановлення половинного заповнення імпульсу
      ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);                            // застосування нового значення шпаруватості
    } else {                                                                            // гілка повного вимкнення звукового випромінювача
      ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);                            // обнулення шпаруватості каналу зумера
      ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);                            // зупинка генерації імпульсів на виводі
    }                                                                                   // завершення умови керування шпаруватістю
  }                                                                                     // завершення перевірки зміни частоти
}                                                                                       // завершення функції керування зумером

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

void readKitchenSensors() {                                                             // опитування датчика маси та цифрового термометра
  int rawAdc = analogRead(PIN_WEIGHT_POT);                                              // зчитування аналогового сигналу потенціометра маси
  float mappedWeight = 50.0f + ((float)rawAdc / 4095.0f) * 1950.0f;                     // перерахунок сигналу у масу до двох тисяч грамів
  currentWeightG = 0.8f * currentWeightG + 0.2f * mappedWeight;                         // згладжування показників ваги від шуму

  tempSensor.requestTemperatures();                                                     // запит вимірювання температури по шині
  float measuredC = tempSensor.getTempCByIndex(0);                                      // отримання значення температури у градусах Цельсія
  if (measuredC > -40.0f && measuredC < 150.0f) {                                       // перевірка коректності зчитаного значення датчика
    currentTempC = measuredC;                                                           // оновлення поточної температури страви
  }                                                                                     // завершення валідації температурного заміру

  currentAi = aiChef.evaluate(currentWeightG, currentTempC, activeMode, timerRunning);  // виконання інтелектуального перерахунку часу та стадії

  if (currentAi.stage == STAGE_OVERHEAT) {                                              // перевірка переходу у критичний режим перегріву
    setBuzzerTone(2400);                                                                // ввімкнення тривожного сигналу перегріву страви
    buzzerStopMs = millis() + 120;                                                      // обмеження тривалості звукового імпульсу
  } else if (currentAi.stage == STAGE_READY && !completionAlerted) {                    // перевірка моменту досягнення готовності страви
    completionAlerted = true;                                                           // встановлення прапорця виконаного сповіщення
    timerRunning = false;                                                               // автоматична зупинка таймера після готовності
    setBuzzerTone(1800);                                                                // подача сигналу завершення приготування
    buzzerStopMs = millis() + 100;                                                      // коротка тривалість сигналу сто мілісекунд
    logEvent("AI_CHEF", currentAi.llmInstruction);                                      // запис повідомлення про готовність у журнал
  }                                                                                     // завершення перевірки звукових сповіщень
}                                                                                       // завершення функції опитування сенсорів

void updateLcdScreen() {                                                                // оновлення кулінарних параметрів на дисплеї
  char line1[17];                                                                       // буфер першого рядка символьного екрана
  char line2[17];                                                                       // буфер другого рядка символьного екрана
  const char* modeStr = aiChef.getModeTitle(activeMode);                                // отримання текстової назви поточного режиму
  snprintf(line1, sizeof(line1), "%-7s %4.0fg", modeStr, currentWeightG);               // формування рядка режиму та маси у грамах

  if (currentAi.stage == STAGE_OVERHEAT) {                                              // відображення попередження при перегріві
    snprintf(line2, sizeof(line2), "%4.1fC OVERHEAT!", currentTempC);                   // формування тексту тривоги перегріву
  } else if (currentAi.stage == STAGE_READY) {                                          // відображення повідомлення про готовність
    snprintf(line2, sizeof(line2), "%4.1fC DISH READY", currentTempC);                  // формування тексту завершення приготування
  } else {                                                                              // штатне відображення температури та таймера
    long mLeft = currentAi.remainingSeconds / 60;                                       // розрахунок залишкових хвилин таймера
    long sLeft = currentAi.remainingSeconds % 60;                                       // розрахунок залишкових секунд таймера
    snprintf(line2, sizeof(line2), "%4.1fC T:%02ld:%02ld", currentTempC, mLeft, sLeft); // формування рядка температури і зворотного відліку
  }                                                                                     // завершення вибору формату другого рядка

  lcd.setCursor(0, 0);                                                                  // встановлення курсора на початок першого рядка
  lcd.print(line1);                                                                     // вивід назви режиму та маси продукту
  lcd.print("   ");                                                                     // очищення залишкових символів першого рядка
  lcd.setCursor(0, 1);                                                                  // встановлення курсора на початок другого рядка
  lcd.print(line2);                                                                     // вивід температури та скоригованого таймера
  lcd.print("   ");                                                                     // очищення залишкових символів другого рядка
}                                                                                       // завершення функції оновлення екрана

void checkButtons() {                                                                   // неблокуюче опитування тактових кнопок керування
  static unsigned long lastPressMs = 0;                                                 // мітка часу останнього натискання кнопки
  if (millis() - lastPressMs < 300) {                                                   // фільтрація брязкоту контактів триста мілісекунд
    return;                                                                             // вихід до завершення інтервалу стабілізації
  }                                                                                     // завершення перевірки інтервалу антибрязкоту

  if (digitalRead(PIN_BTN_MODE) == LOW) {                                               // перевірка натискання кнопки вибору режиму
    lastPressMs = millis();                                                             // фіксація моменту перемикання режиму
    int nextMode = ((int)activeMode + 1) % 3;                                           // обчислення індексу наступного кулінарного режиму
    activeMode = (CookingMode)nextMode;                                                 // застосування нового вибраного режиму
    completionAlerted = false;                                                          // скидання прапорця завершення приготування
    setBuzzerTone(1500);                                                                // короткий звуковий відгук перемикання режиму
    buzzerStopMs = millis() + 40;                                                       // тривалість звукового відгуку сорок мілісекунд
    logEvent("MODE", aiChef.getModeTitle(activeMode));                                  // запис нового кулінарного режиму у журнал
  } else if (digitalRead(PIN_BTN_START) == LOW) {                                       // перевірка натискання кнопки старту або паузи
    lastPressMs = millis();                                                             // фіксація моменту натискання кнопки запуску
    timerRunning = !timerRunning;                                                       // перемикання стану активності зворотного відліку
    completionAlerted = false;                                                          // дозвіл нового сповіщення після запуску
    setBuzzerTone(2000);                                                                // подача короткого сигналу підтвердження дії
    buzzerStopMs = millis() + 50;                                                       // тривалість сигналу п ятдесят мілісекунд
    const char* tMsg = timerRunning ? "Countdown started" : "Countdown paused";         // вибір текстового статусу роботи таймера
    logEvent("TIMER", tMsg);                                                            // реєстрація зміни стану таймера у консолі
  } else if (digitalRead(PIN_BTN_RESET) == LOW) {                                       // перевірка натискання кнопки скидання
    lastPressMs = millis();                                                             // фіксація моменту скидання налаштувань
    timerRunning = false;                                                               // зупинка активного відліку таймера
    completionAlerted = false;                                                          // скидання прапорця готовності страви
    aiChef.resetTimerOffset();                                                          // обнулення лічильника відпрацьованих секунд
    setBuzzerTone(1200);                                                                // подача короткого тону підтвердження скидання
    buzzerStopMs = millis() + 40;                                                       // тривалість тону сорок мілісекунд
    logEvent("RESET", "Timer and thermal counter reset to initial state");              // запис події скидання таймера у термінал
  }                                                                                     // завершення обробки тактових кнопок
}                                                                                       // завершення функції опитування клавіш

void sendToThingSpeak() {                                                               // передача кривої нагрівання на хмарну платформу
  if (WiFi.status() != WL_CONNECTED) {                                                  // перевірка наявності бездротового з єднання
    return;                                                                             // скасування відправки за відсутності мережі
  }                                                                                     // завершення перевірки статусу мережі

  HTTPClient http;                                                                      // створення клієнта мережевого запиту
  String url = String(TS_URL) + "?api_key=" + TS_API_KEY;                               // формування базової адреси запиту з ключем
  url += "&field1=" + String(currentTempC, 1);                                          // додавання поля поточної температури страви
  url += "&field2=" + String(currentWeightG, 0);                                        // додавання поля маси продукту у грамах
  url += "&field3=" + String(currentAi.remainingSeconds);                               // додавання поля залишкового часу таймера
  url += "&field4=" + String(currentAi.readinessPercent, 1);                            // додавання поля відсотка готовності страви
  url += "&field5=" + String(currentAi.heatingRateCPerMin, 2);                          // додавання поля швидкості нагрівання
  url += "&field6=" + String((int)activeMode);                                          // додавання поля коду активного режиму

  http.begin(url);                                                                      // ініціалізація з єднання з хмарним сервером
  lastHttpStatus = http.GET();                                                          // виконання запиту передачі телеметричного пакета
  http.end();                                                                           // закриття мережевого з єднання після передачі
  const char* cMsg = (lastHttpStatus == 200) ? "ThingSpeak updated" : "Sync error";     // вибір повідомлення про результат передачі
  logEvent("CLOUD", cMsg);                                                              // запис результату хмарної синхронізації
}                                                                                       // завершення функції відправки у хмару

void printDataTable() {                                                                 // вивід періодичної таблиці телеметрії у консоль
  Serial.println();                                                                     // порожній рядок для відділення блоку таблиці
  Serial.println("Culinary Telemetry Table:");                                          // заголовок діагностичної таблиці стану
  Serial.print("Selected Mode: ");                                                      // вивід мітки активного режиму приготування
  Serial.println(aiChef.getModeTitle(activeMode));                                      // вивід назви поточного вибраного режиму
  Serial.print("AI Recommended Mode: ");                                                // вивід мітки рекомендованого режиму за вагою
  Serial.println(aiChef.getModeTitle(currentAi.recommendedMode));                       // вивід рекомендованого штучним інтелектом режиму
  Serial.print("Dish Weight: ");                                                        // вивід мітки поточної маси страви
  Serial.print(currentWeightG, 0);                                                      // вивід числового значення маси без дробів
  Serial.println(" g");                                                                 // вивід одиниці вимірювання грам
  Serial.print("Internal Temp: ");                                                      // вивід мітки внутрішньої температури продукту
  Serial.print(currentTempC, 1);                                                        // вивід поточної температури з одним знаком
  Serial.print(" C / Target: ");                                                        // вивід роздільника цільової температури
  Serial.print(currentAi.targetTempC, 1);                                               // вивід цільової температури режиму
  Serial.println(" C");                                                                 // вивід позначки градусів Цельсія
  Serial.print("Heating Rate: ");                                                       // вивід мітки швидкості прогрівання
  Serial.print(currentAi.heatingRateCPerMin, 2);                                        // вивід швидкості нагрівання за хвилину
  Serial.println(" C/min");                                                             // вивід одиниці швидкості нагрівання
  Serial.print("AI Adjusted Timer: ");                                                  // вивід мітки скоригованого часу таймера
  Serial.print(currentAi.remainingSeconds);                                             // вивід кількості секунд до готовності
  Serial.println(" s");                                                                 // вивід одиниці часу секунди
  Serial.print("Readiness Progress: ");                                                 // вивід мітки ступеня готовності страви
  Serial.print(currentAi.readinessPercent, 1);                                          // вивід відсотка готовності продукту
  Serial.println(" %");                                                                 // вивід символу відсотка
  Serial.print("Timer State: ");                                                        // вивід мітки активності зворотного відліку
  Serial.println(timerRunning ? "RUNNING" : "PAUSED");                                  // вивід текстового стану таймера
  Serial.println(currentAi.llmInstruction);                                             // вивід інструкції користувачу від моделі
  Serial.println();                                                                     // завершальний порожній рядок після таблиці
}                                                                                       // завершення функції виводу таблиці

void setup() {                                                                          // початкове налаштування апаратних вузлів системи
  Serial.begin(115200);                                                                 // ініціалізація послідовного порту для моніторингу
  delay(250);                                                                           // коротка пауза стабілізації живлення після старту
  Serial.println();                                                                     // порожній рядок перед стартовим банером
  Serial.println("===================");                                                // верхня рамка стартового інформаційного банера
  Serial.println("Smart Culinary Precision Assistant");                                 // назва кулінарного проєкту англійською мовою
  Serial.println("===================");                                                // нижня рамка стартового інформаційного банера
  Serial.println();                                                                     // відступ порожнім рядком після заголовка

  esp_log_level_set("*", ESP_LOG_NONE);                                                 // вимкнення системних налагоджувальних повідомлень
  esp_log_level_set("ledc", ESP_LOG_NONE);                                              // блокування попереджень драйвера звукового таймера

  ledc_timer_config_t ledc_timer = {                                                    // конфігурація апаратного таймера звукового каналу
    .speed_mode = LEDC_LOW_SPEED_MODE,                                                  // вибір низькошвидкісного режиму роботи таймера
    .duty_resolution = LEDC_TIMER_10_BIT,                                               // встановлення розрядності шпаруватості десять біт
    .timer_num = LEDC_TIMER_0,                                                          // призначення нульового апаратного таймера
    .freq_hz = 2000,                                                                    // початкова базова частота дві тисячі герц
    .clk_cfg = LEDC_AUTO_CLK                                                            // автоматичний вибір джерела тактування
  };                                                                                    // завершення структури параметрів таймера
  ledc_timer_config(&ledc_timer);                                                       // застосування налаштувань таймера генератора

  ledc_channel_config_t ledc_channel = {                                                // конфігурація каналу керування п єзодинаміком
    .gpio_num = PIN_BUZZER,                                                             // прив язка виводу звукового випромінювача
    .speed_mode = LEDC_LOW_SPEED_MODE,                                                  // узгодження режиму швидкості з таймером
    .channel = LEDC_CHANNEL_0,                                                          // вибір нульового каналу модуляції
    .intr_type = LEDC_INTR_DISABLE,                                                     // вимкнення переривань генератора імпульсів
    .timer_sel = LEDC_TIMER_0,                                                          // підключення каналу до нульового таймера
    .duty = 0,                                                                          // початковий вимкнений стан без звуку
    .hpoint = 0                                                                         // нульовий зсув фази вихідного сигналу
  };                                                                                    // завершення структури конфігурації каналу
  ledc_channel_config(&ledc_channel);                                                   // ініціалізація апаратного каналу зумера

  pinMode(PIN_WEIGHT_POT, INPUT);                                                       // налаштування аналогового входу датчика маси
  pinMode(PIN_BTN_MODE, INPUT_PULLUP);                                                  // увімкнення внутрішньої підтяжки кнопки режиму
  pinMode(PIN_BTN_START, INPUT_PULLUP);                                                 // увімкнення внутрішньої підтяжки кнопки старту
  pinMode(PIN_BTN_RESET, INPUT_PULLUP);                                                 // увімкнення внутрішньої підтяжки кнопки скидання

  Wire.begin(21, 22);                                                                   // запуск апаратної шини дисплея
  lcd.init();                                                                           // ініціалізація контролера символьного дисплея
  lcd.backlight();                                                                      // увімкнення підсвічування символьного екрана
  lcd.setCursor(0, 0);                                                                  // переміщення курсора у початок першого рядка
  lcd.print("AI CHEF SYSTEM");                                                          // вивід привітального заголовка на екран
  lcd.setCursor(0, 1);                                                                  // переміщення курсора у початок другого рядка
  lcd.print("INITIALIZING...");                                                         // вивід статусу початкового запуску

  tempSensor.begin();                                                                   // ініціалізація цифрового датчика температури
  tempSensor.requestTemperatures();                                                     // первинний запит температури продукту
  float initC = tempSensor.getTempCByIndex(0);                                          // зчитування стартового температурного показника
  if (initC > -40.0f && initC < 150.0f) {                                               // перевірка валідності початкової температури
    currentTempC = initC;                                                               // збереження початкової температури страви
  }                                                                                     // завершення перевірки стартової температури
  aiChef.init(currentTempC);                                                            // початкова калібрування модуля штучного інтелекту
  logEvent("SYSTEM", "Culinary sensors and display initialized");                       // запис у журнал про готовність периферії

  WiFi.begin(WIFI_SSID, WIFI_PASS);                                                     // запуск підключення до бездротової мережі
  Serial.print("Connecting to WiFi");                                                   // вивід повідомлення про пошук мережі
  unsigned long wifiStart = millis();                                                   // фіксація часу початку підключення
  while (WiFi.status() != WL_CONNECTED && millis() - wifiStart < 5000) {                // очікування з єднання з лімітом п ять секунд
    delay(200);                                                                         // інтервал між перевірками статусу підключення
    Serial.print(".");                                                                  // друк індикатора прогресу у консоль
  }                                                                                     // завершення циклу очікування бездротової мережі
  Serial.println();                                                                     // перехід на новий рядок після підключення

  if (WiFi.status() == WL_CONNECTED) {                                                  // перевірка успішного отримання мережевої адреси
    logEvent("NETWORK", "WiFi connected for ThingSpeak telemetry");                     // підтвердження підключення до бездротової мережі
    Serial.print("IP Address: ");                                                       // вивід заголовка мережевої адреси
    Serial.println(WiFi.localIP());                                                     // вивід отриманої адреси мікроконтролера
  } else {                                                                              // обробка автономного режиму без мережі
    logEvent("NETWORK", "WiFi offline running local AI chef mode");                     // повідомлення про перехід у локальний режим
  }                                                                                     // завершення перевірки бездротового зв язку

  readKitchenSensors();                                                                 // первинне зчитування маси та температури страви
  updateLcdScreen();                                                                    // вивід початкових показників на дисплей
  logEvent("AI_CHEF", currentAi.llmInstruction);                                        // вивід стартової рекомендації штучного інтелекту
}                                                                                       // завершення функції початкового налаштування

void loop() {                                                                           // головний безперервний цикл роботи пристрою
  checkButtons();                                                                       // опитування кнопок вибору режиму та керування таймером

  if (currentBuzzerFreq > 0 && millis() >= buzzerStopMs) {                              // перевірка часу завершення звукового імпульсу
    setBuzzerTone(0);                                                                   // вимкнення зумера після відпрацювання сигналу
  }                                                                                     // завершення контролю тривалості звуку

  if (timerRunning && millis() - lastSecondTickMs >= 1000) {                            // відлік однієї секунди активного таймера
    lastSecondTickMs = millis();                                                        // оновлення мітки секундного інтервалу
    aiChef.tickSecond();                                                                // зменшення залишкового часу у моделі
  }                                                                                     // завершення секундного кроку таймера

  if (millis() - lastSensorReadMs >= 300) {                                             // періодичне опитування датчиків кожні триста мілісекунд
    lastSensorReadMs = millis();                                                        // оновлення мітки часу опитування сенсорів
    readKitchenSensors();                                                               // зчитування маси і температури з перерахунком моделі
  }                                                                                     // завершення блоку опитування датчиків

  if (millis() - lastDisplayMs >= 400) {                                                // періодичне оновлення дисплея кожні чотириста мілісекунд
    lastDisplayMs = millis();                                                           // оновлення мітки часу виводу на екран
    updateLcdScreen();                                                                  // відображення поточних даних на символьному дисплеї
  }                                                                                     // завершення блоку оновлення дисплея

  if (millis() - lastSerialTableMs >= 4000) {                                           // періодичний друк таблиці кожні чотири секунди
    lastSerialTableMs = millis();                                                       // оновлення мітки часу термінального звіту
    printDataTable();                                                                   // вивід структурованої таблиці параметрів у консоль
  }                                                                                     // завершення блоку термінальної таблиці

  if (millis() - lastCloudSyncMs >= 15000) {                                            // періодична відправка у хмару кожні п ятнадцять секунд
    lastCloudSyncMs = millis();                                                         // оновлення мітки часу хмарної телеметрії
    sendToThingSpeak();                                                                 // передача пакета кривої нагрівання у хмару
  }                                                                                     // завершення блоку хмарної синхронізації
}                                                                                       // завершення головного циклу програми