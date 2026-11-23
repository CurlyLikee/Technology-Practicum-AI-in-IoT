#define BLYNK_TEMPLATE_ID "TMPL4EeofGc6c"                                               // ідентифікатор шаблону хмарної платформи
#define BLYNK_TEMPLATE_NAME "Hospital Security"                                         // назва проєкту у середовищі блінк
#define BLYNK_AUTH_TOKEN "jWU3Dc5HYdtSo0vTUdU2tyNqAM04OnHW"                             // автентифікаційний токен сервісу

#include <Arduino.h>                                                                    // основна бібліотека платформи ардуіно
#include <Wire.h>                                                                       // бібліотека апаратного інтерфейсу i2c
#include <LiquidCrystal_I2C.h>                                                          // драйвер символьного дисплея
#include <ESP32Servo.h>                                                                 // керування сервоприводом ригеля замка
#include <WiFi.h>                                                                       // мережевий стек бездротового зв язку
#include <BlynkSimpleEsp32.h>                                                           // клієнтська бібліотека протоколу блінк
#include "driver/ledc.h"                                                                // апаратний генератор сигналів esp idf
#include "esp_log.h"                                                                    // керування системними журналами контролера
#include "ai_security_audit.h"                                                          // модуль штучного інтелекту аудиту доступу

#define PIN_SERVO 18                                                                    // вивід керування сервоприводом замка
#define PIN_LED_GREEN 2                                                                 // вивід зеленого світлодіода успішного входу
#define PIN_LED_RED 4                                                                   // вивід червоного світлодіода заборони та тривоги
#define PIN_BUZZER 27                                                                   // вивід звукового випромінювача сирени
#define PIN_BTN_DOCTOR 32                                                               // кнопка симуляції rfid картки чергового лікаря
#define PIN_BTN_NURSE 33                                                                // кнопка симуляції картки денного медперсоналу
#define PIN_BTN_INTRUDER 25                                                             // кнопка симуляції невідомої картки порушника
#define PIN_BTN_SHIFT 26                                                                // кнопка оперативного перемикання зміни день або ніч

const char* WIFI_SSID = "Wokwi-GUEST";                                                  // назва точки доступу віртуальної мережі
const char* WIFI_PASS = "";                                                             // пароль бездротового підключення до мережі
const int SERVO_LOCKED_POS = 0;                                                         // кут закритого ригеля нуль градусів
const int SERVO_UNLOCKED_POS = 90;                                                      // кут відкритого замка дев яносто градусів

LiquidCrystal_I2C lcd(0x27, 16, 2);                                                     // об єкт керування символьним дисплеєм
Servo lockServo;                                                                        // об єкт керування приводом дверей
AISecurityAudit aiAudit;                                                                // екземпляр моделі штучного інтелекту аудиту
ShiftSchedule currentShift = SHIFT_DAY;                                                 // початковий розклад робочого часу

int currentBuzzerFreq = 0;                                                              // поточна зафіксована частота зумера
bool doorUnlocked = false;                                                              // прапорець відкритого фізичного ригеля
unsigned long doorUnlockTime = 0;                                                       // момент часу розблокування замка
unsigned long alarmStopTime = 0;                                                        // момент вимкнення звукового сигналу тривоги
unsigned long lastDataTableTime = 0;                                                    // час останнього виводу діагностичної таблиці
String currentScannedName = "Ready";                                                    // ім я останнього пред явника картки
String currentStatusText = "Standby";                                                   // текстовий статус контролю пропускного пункту
float currentRiskValue = 0.0f;                                                          // поточний індекс загрози від аудиту

void setBuzzerTone(int freq) {                                                          // безпечне керування частотою через таймер
  if (freq != currentBuzzerFreq) {                                                      // перевірка необхідності зміни генерації
    currentBuzzerFreq = freq;                                                           // збереження нового значення частоти
    if (freq > 0) {                                                                     // умова ввімкнення звукового випромінювача
      ledc_set_freq(LEDC_LOW_SPEED_MODE, LEDC_TIMER_0, freq);                           // встановлення частоти генерації сигналу
      ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 512);                          // заповнення прямокутного імпульсу п ятдесят відсотків
      ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);                            // застосування встановленого рівня шпаруватості
    } else {                                                                            // умова повного вимкнення звучання
      ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);                            // обнулення шпаруватості каналу зумера
      ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);                            // вимикання генерації імпульсів на виводі
    }                                                                                   // завершення розгалуження керування частотою
  }                                                                                     // завершення умови оновлення стану зумера
}                                                                                       // завершення функції керування зумером

void logEvent(const char* category, const char* message) {                              // фіксація системної події у послідовний порт
  unsigned long sec = millis() / 1000;                                                  // перерахунок часу роботи у секунди
  unsigned long m = sec / 60;                                                           // обчислення поточної хвилини
  unsigned long s = sec % 60;                                                           // обчислення секунд залишку
  char timeBuf[16];                                                                     // буфер форматування рядка часу
  snprintf(timeBuf, sizeof(timeBuf), "[%02lu:%02lu]", m, s);                            // запис форматованого часу хвилини та секунди
  Serial.print(timeBuf);                                                                // вивід позначки часу у консоль
  Serial.print(" [");                                                                   // початок секції категорії події
  Serial.print(category);                                                               // вивід текстової мітки підсистеми
  Serial.print("] ");                                                                   // завершення секції мітки підсистеми
  Serial.println(message);                                                              // вивід опису зареєстрованої події безпеки
}                                                                                       // завершення функції журналу подій

void showLcdStatus(const char* line1, const char* line2) {                              // вивід оперативної інформації на екран дисплея
  lcd.clear();                                                                          // очищення обох рядків символьного екрана
  lcd.setCursor(0, 0);                                                                  // встановлення позиції на перший рядок
  lcd.print(line1);                                                                     // вивід тексту першого рядка індикації
  lcd.setCursor(0, 1);                                                                  // встановлення позиції на другий рядок
  lcd.print(line2);                                                                     // вивід тексту другого рядка індикації
}                                                                                       // завершення функції оновлення дисплея

void syncBlynkTelemetry(const SecurityAuditResult& res, const char* name) {             // передача телеметрії у хмарний сервіс блінк
  if (Blynk.connected()) {                                                              // перевірка активності з єднання з сервером
    Blynk.virtualWrite(V0, doorUnlocked ? 1 : 0);                                       // відправка фізичного стану ригеля замка
    Blynk.virtualWrite(V1, name);                                                       // відправка імені пред явника картки
    Blynk.virtualWrite(V2, currentStatusText);                                          // передача поточного статусу доступу
    Blynk.virtualWrite(V3, (int)res.riskScore);                                         // передача числового індексу загрози штучного інтелекту
    Blynk.virtualWrite(V4, (currentShift == SHIFT_DAY) ? 0 : 1);                        // передача активного режиму робочої зміни
    Blynk.virtualWrite(V5, res.auditLog);                                               // передача тексту звіту нейромережевого аудиту
  }                                                                                     // завершення передачі даних через блінк
}                                                                                       // завершення функції синхронізації

void processCardScan(CardRole role, const char* name) {                                 // алгоритм верифікації прикладеного rfid ключа
  currentScannedName = name;                                                            // фіксація імені пред явника ідентифікатора
  logEvent("RFID", name);                                                               // запис факту сканування картки у журнал
  SecurityAuditResult audit = aiAudit.evaluateAccess(role, currentShift);               // запуск оцінки ризику модулем штучного інтелекту
  currentRiskValue = audit.riskScore;                                                   // збереження обчисленого відсотка загрози

  if (audit.decision == DECISION_GRANTED) {                                             // обробка дозволеного проходу
    currentStatusText = "Granted";                                                      // встановлення текстового статусу схвалення
    doorUnlocked = true;                                                                // активація прапорця відмикання ригеля
    doorUnlockTime = millis();                                                          // фіксація часу початку відчинення замка
    lockServo.write(SERVO_UNLOCKED_POS);                                                // поворот сервоприводу у відкрите положення
    digitalWrite(PIN_LED_GREEN, HIGH);                                                  // увімкнення зеленого індикатора доступу
    digitalWrite(PIN_LED_RED, LOW);                                                     // вимкнення червоного аварійного індикатора
    setBuzzerTone(2000);                                                                // подача короткого сигналу дозволу дві тисячі герц
    alarmStopTime = millis() + 50;                                                      // встановлення тривалості звуку п ятдесят мілісекунд
    showLcdStatus(name, "ACCESS GRANTED");                                              // відображення успішного входу на екрані
    logEvent("AI_AUDIT", audit.auditLog);                                               // вивід висновку штучного інтелекту у термінал
  } else if (audit.decision == DECISION_LOCKOUT) {                                      // обробка виявлення серії порушень або атаки
    currentStatusText = "LOCKOUT";                                                      // встановлення статусу повного блокування
    doorUnlocked = false;                                                               // примусове закриття ригеля замка
    lockServo.write(SERVO_LOCKED_POS);                                                  // поворот сервоприводу у закриту позицію
    digitalWrite(PIN_LED_GREEN, LOW);                                                   // вимкнення зеленого світлодіода
    digitalWrite(PIN_LED_RED, HIGH);                                                    // активація червоного індикатора тривоги
    setBuzzerTone(800);                                                                 // увімкнення сирени небезпеки вісімсот герц
    alarmStopTime = millis() + 120;                                                     // тривалість звучання сто двадцять мілісекунд
    showLcdStatus("TERMINAL LOCKED", "ALERT CRITICAL");                                 // вивід попередження про аварійне блокування
    logEvent("SECURITY_ALERT", audit.auditLog);                                         // вивід критичного сповіщення у журнал
  } else {                                                                              // обробка заборони проходу за правилами політики
    currentStatusText = "Denied";                                                       // встановлення статусу відхилення проходу
    doorUnlocked = false;                                                               // фіксація ригеля замка у закритому стані
    lockServo.write(SERVO_LOCKED_POS);                                                  // утримання сервоприводу у нульовому положенні
    digitalWrite(PIN_LED_GREEN, LOW);                                                   // вимкнення дозволу проходу
    digitalWrite(PIN_LED_RED, HIGH);                                                    // увімкнення червоного діода заборони
    setBuzzerTone(1000);                                                                // попереджувальний звуковий сигнал тисяча герц
    alarmStopTime = millis() + 80;                                                      // тривалість звучання вісімдесят мілісекунд
    if (audit.decision == DECISION_DENIED_SHIFT) {                                      // розмежування причини відмови через нічний час
      showLcdStatus(name, "OFF-SHIFT NIGHT");                                           // індикація заборони нічного входу на екрані
    } else {                                                                            // відхилення через сторонній ідентифікатор
      showLcdStatus(name, "ACCESS DENIED");                                             // індикація відмови невідомого ключа
    }                                                                                   // завершення вибору тексту помилки
    logEvent("AI_AUDIT", audit.auditLog);                                               // вивід деталізації аудиту у термінал
  }                                                                                     // завершення умовного розгалуження перевірки

  syncBlynkTelemetry(audit, name);                                                      // синхронізація отриманих даних з хмарою блінк
}                                                                                       // завершення обробки сканування картки

void toggleShiftMode() {                                                                // ручне перемикання розкладу робочої зміни
  if (currentShift == SHIFT_DAY) {                                                      // перевірка чи зараз активна денна зміна
    currentShift = SHIFT_NIGHT;                                                         // перемикання на нічне чергування
    logEvent("SCHEDULE", "Shift switched to NIGHT");                                    // запис нічного графіка у журнал
    showLcdStatus("SCHEDULE CHANGED", "MODE: NIGHT");                                   // індикація зміни режиму на дисплеї
  } else {                                                                              // якщо активне нічне чергування
    currentShift = SHIFT_DAY;                                                           // перемикання на денний графік роботи
    logEvent("SCHEDULE", "Shift switched to DAY");                                      // запис денного графіка у журнал
    showLcdStatus("SCHEDULE CHANGED", "MODE: DAY");                                     // індикація зміни режиму на дисплеї
  }                                                                                     // завершення вибору режиму зміни

  if (Blynk.connected()) {                                                              // перевірка зв язку з хмарою
    Blynk.virtualWrite(V4, (currentShift == SHIFT_DAY) ? 0 : 1);                        // оновлення стану віджета зміни у додатку
  }                                                                                     // завершення оновлення у блінк
}                                                                                       // завершення функції зміни розкладу

void checkButtons() {                                                                   // неблокуюче опитування тактових кнопок
  static unsigned long lastBtnTime = 0;                                                 // збереження часу останнього натискання кнопки
  if (millis() - lastBtnTime < 350) {                                                   // фільтрація брязкоту контактів триста п ятдесят мілісекунд
    return;                                                                             // достроковий вихід при наявності брязкоту
  }                                                                                     // завершення фільтрації брязкоту

  if (digitalRead(PIN_BTN_DOCTOR) == LOW) {                                             // детекція натискання картки чергового лікаря
    lastBtnTime = millis();                                                             // фіксація моменту спрацювання кнопки
    processCardScan(ROLE_CHIEF_DOCTOR, "Dr. Watson");                                   // виклик верифікації для облікового запису лікаря
  } else if (digitalRead(PIN_BTN_NURSE) == LOW) {                                       // детекція картки медичного персоналу
    lastBtnTime = millis();                                                             // фіксація моменту спрацювання кнопки
    processCardScan(ROLE_STAFF_NURSE, "Nurse Mary");                                    // виклик перевірки картки медсестри
  } else if (digitalRead(PIN_BTN_INTRUDER) == LOW) {                                    // детекція неавторизованого ключа сторонньої особи
    lastBtnTime = millis();                                                             // фіксація моменту спрацювання кнопки
    processCardScan(ROLE_UNKNOWN, "Unknown UID");                                       // виклик перевірки для невідомого ключа
  } else if (digitalRead(PIN_BTN_SHIFT) == LOW) {                                       // детекція кнопки перемикання робочої зміни
    lastBtnTime = millis();                                                             // фіксація моменту спрацювання кнопки
    toggleShiftMode();                                                                  // виклик зміни робочого графіка медустанови
  }                                                                                     // завершення перевірки станів кнопок
}                                                                                       // завершення функції опитування кнопок

void printDataTable() {                                                                 // вивід періодичної структурованої таблиці у консоль
  if (millis() - lastDataTableTime >= 4000) {                                           // перевірка інтервалу чотири секунди за таймером
    lastDataTableTime = millis();                                                       // оновлення мітки часу останнього виводу
    Serial.println();                                                                   // порожній рядок для візуального відділення таблиці
    Serial.println("Status Table:");                                                    // заголовок структурованої таблиці стану
    Serial.print("Shift Schedule: ");                                                   // вивід назви параметра робочої зміни
    Serial.println((currentShift == SHIFT_DAY) ? "Day Shift" : "Night Shift");          // відображення поточної активної зміни
    Serial.print("Door Lock Status: ");                                                 // вивід мітки стану замка дверей
    Serial.println(doorUnlocked ? "Unlocked" : "Locked");                               // відображення фізичного положення ригеля
    Serial.print("Last Card Holder: ");                                                 // вивід мітки останньої піднесеної картки
    Serial.println(currentScannedName);                                                 // відображення імені користувача
    Serial.print("Access Verification: ");                                              // вивід параметра верифікації
    Serial.println(currentStatusText);                                                  // відображення рішення контролю доступу
    Serial.print("AI Anomaly Risk: ");                                                  // вивід мітки рівня загрози штучного інтелекту
    Serial.print(currentRiskValue, 1);                                                  // вивід числового відсотка ризику з одним знаком
    Serial.println(" %");                                                               // вивід одиниці вимірювання відсоток
    Serial.print("Lockout State: ");                                                    // вивід мітки аварійного блокування термінала
    Serial.println(aiAudit.isLockedOut() ? "ACTIVE" : "Inactive");                      // відображення прапорця блокування
    Serial.println();                                                                   // відступ порожнім рядком для зручності читання
  }                                                                                     // завершення умови часового інтервалу
}                                                                                       // завершення функції друку таблиці

BLYNK_WRITE(V0) {                                                                       // дистанційне керування ригелем замка через блінк
  int val = param.asInt();                                                              // зчитування команди користувача з мобільного віджета
  if (val == 1 && !aiAudit.isLockedOut()) {                                             // перевірка команди відкриття та відсутності тривоги
    doorUnlocked = true;                                                                // встановлення прапорця відмикання ригеля
    doorUnlockTime = millis();                                                          // фіксація моменту розблокування замка
    lockServo.write(SERVO_UNLOCKED_POS);                                                // відмикання дверей сервоприводом
    digitalWrite(PIN_LED_GREEN, HIGH);                                                  // увімкнення зеленого діода підтвердження
    digitalWrite(PIN_LED_RED, LOW);                                                     // вимкнення червоного діода
    showLcdStatus("REMOTE OVERRIDE", "DOOR UNLOCKED");                                  // відображення дистанційного відкриття
    logEvent("BLYNK_CMD", "Door unlocked via remote application");                      // запис факту віддаленого відмикання у журнал
  }                                                                                     // завершення обробки дистанційної команди
}                                                                                       // завершення обробника віртуального піна блінк

BLYNK_WRITE(V4) {                                                                       // дистанційна зміна робочого графіка через блінк
  int val = param.asInt();                                                              // зчитування значення перемикача з мобільного додатка
  currentShift = (val == 1) ? SHIFT_NIGHT : SHIFT_DAY;                                  // встановлення вибраної зміни день або ніч
  const char* sTxt = (currentShift == SHIFT_DAY) ? "MODE: DAY" : "MODE: NIGHT";         // вибір тексту нової зміни для дисплея
  showLcdStatus("SCHEDULE CHANGED", sTxt);                                              // оновлення режиму зміни на екрані дисплея
  logEvent("BLYNK_CMD", "Shift updated via mobile application");                        // фіксація зміни графіка у терміналі
}                                                                                       // завершення функції обробки зміни з додатку

void setup() {                                                                          // початкова конфігурація периферії та модулів
  Serial.begin(115200);                                                                 // ініціалізація послідовного порту мікроконтролера
  delay(300);                                                                           // пауза стабілізації напруги живлення
  Serial.println();                                                                     // порожній рядок перед стартовим банером проекту
  Serial.println("===============================");                                    // верхня горизонтальна межа банера
  Serial.println("Hospital Secure Access AI Audit");                                    // назва інженерного проекту у стилі тайтл кейс
  Serial.println("===============================");                                    // нижня межа стартового інформаційного банера
  Serial.println();                                                                     // порожній рядок після інформаційного банера

  esp_log_level_set("*", ESP_LOG_NONE);                                                 // вимкнення зайвих системних повідомлень ядра
  esp_log_level_set("ledc", ESP_LOG_NONE);                                              // запобігання системним попередженням модуля генератора

  ledc_timer_config_t ledc_timer = {                                                    // налаштування параметрів апаратного таймера
    .speed_mode = LEDC_LOW_SPEED_MODE,                                                  // режим низькошвидкісної синхронізації каналу
    .duty_resolution = LEDC_TIMER_10_BIT,                                               // роздільна здатність шпаруватості десять біт
    .timer_num = LEDC_TIMER_0,                                                          // використання нульового таймера
    .freq_hz = 2000,                                                                    // початкова частота дві тисячі герц
    .clk_cfg = LEDC_AUTO_CLK                                                            // автоматичний вибір джерела тактового сигналу
  };                                                                                    // завершення структури конфігурації таймера
  ledc_timer_config(&ledc_timer);                                                       // застосування конфігурації апаратного таймера

  ledc_channel_config_t ledc_channel = {                                                // конфігурація апаратного каналу генерації звуку
    .gpio_num = PIN_BUZZER,                                                             // прив язка виводу звукового зумера
    .speed_mode = LEDC_LOW_SPEED_MODE,                                                  // режим синхронізації генератора
    .channel = LEDC_CHANNEL_0,                                                          // використання нульового апаратного каналу
    .intr_type = LEDC_INTR_DISABLE,                                                     // деактивація переривань каналу
    .timer_sel = LEDC_TIMER_0,                                                          // прив язка до нульового таймера
    .duty = 0,                                                                          // початкова нульова шпаруватість вимкненого звуку
    .hpoint = 0                                                                         // початкова фазова точка імпульсу
  };                                                                                    // завершення структури налаштування каналу
  ledc_channel_config(&ledc_channel);                                                   // ініціалізація апаратного каналу зумера

  pinMode(PIN_LED_GREEN, OUTPUT);                                                       // налаштування виводу зеленого діода на вихід
  pinMode(PIN_LED_RED, OUTPUT);                                                         // налаштування виводу червоного діода на вихід
  pinMode(PIN_BTN_DOCTOR, INPUT_PULLUP);                                                // підключення кнопки лікаря з внутрішньою підтяжкою
  pinMode(PIN_BTN_NURSE, INPUT_PULLUP);                                                 // підключення кнопки медсестри з внутрішньою підтяжкою
  pinMode(PIN_BTN_INTRUDER, INPUT_PULLUP);                                              // підключення кнопки порушника з підтяжкою до живлення
  pinMode(PIN_BTN_SHIFT, INPUT_PULLUP);                                                 // підключення кнопки зміни з внутрішньою підтяжкою

  digitalWrite(PIN_LED_GREEN, LOW);                                                     // початковий вимкнений стан зеленого діода
  digitalWrite(PIN_LED_RED, LOW);                                                       // початковий вимкнений стан червоного діода

  ESP32PWM::allocateTimer(1);                                                           // виділення першого таймера для керування сервоприводом
  lockServo.setPeriodHertz(50);                                                         // стандартна частота сервоприводу п ятдесят герц
  lockServo.attach(PIN_SERVO, 500, 2400);                                               // прив язка виводу сервоприводу з шириною імпульсів
  lockServo.write(SERVO_LOCKED_POS);                                                    // переведення ригеля у вихідне закрите положення

  Wire.begin(21, 22);                                                                   // ініціалізація шини зв язку дисплея
  lcd.init();                                                                           // запуск контролера символьного дисплея
  lcd.backlight();                                                                      // увімкнення світлодіодного підсвічування екрана
  showLcdStatus("MED ACCESS GUARD", "BOOTING AI...");                                   // вивід повідомлення про завантаження системи

  aiAudit.init();                                                                       // ініціалізація внутрішніх лічильників штучного інтелекту
  logEvent("SYSTEM", "Hardware peripherals initialized successfully");                  // підтвердження готовності апаратних засобів

  WiFi.begin(WIFI_SSID, WIFI_PASS);                                                     // старт процедури підключення до бездротової мережі
  Serial.print("Connecting to WiFi");                                                   // вивід стартового рядка підключення
  unsigned long wifiStart = millis();                                                   // фіксація моменту початку пошуку мережі
  while (WiFi.status() != WL_CONNECTED && millis() - wifiStart < 5000) {                // очікування підключення з лімітом п ять секунд
    delay(200);                                                                         // коротка пауза між спробами опитування
    Serial.print(".");                                                                  // вивід крапки прогресу підключення
  }                                                                                     // завершення циклу очікування зв язку
  Serial.println();                                                                     // перехід на новий рядок після завершення

  if (WiFi.status() == WL_CONNECTED) {                                                  // перевірка успішності мережевого з єднання
    logEvent("NETWORK", "WiFi connection established");                                 // реєстрація підключення бездротової мережі
    Serial.print("IP: ");                                                               // вивід мітки локальної адреси
    Serial.println(WiFi.localIP());                                                     // друк отриманої мережевої адреси мікроконтролера
    Blynk.config(BLYNK_AUTH_TOKEN);                                                     // застосування конфігурації токена платформи
    Blynk.connect(1500);                                                                // неблокуюча спроба з єднання півтори секунди
    if (Blynk.connected()) {                                                            // перевірка чи встановлено канал зв язку
      logEvent("BLYNK", "Connected to Blynk cloud service");                            // фіксація з єднання з хмарним сервером
    } else {                                                                            // обробка відсутності з єднання з хмарою
      logEvent("BLYNK", "Running local mode pending valid token");                      // фіксація автономного режиму симулятора
    }                                                                                   // завершення умови перевірки зв язку
  } else {                                                                              // обробка таймауту мережевого підключення
    logEvent("NETWORK", "WiFi unavailable running offline mode");                       // фіксація автономної роботи системи захисту
  }                                                                                     // завершення блоку налаштування зв язку

  showLcdStatus("SYSTEM READY", "SHIFT: DAY");                                          // вивід фінального повідомлення готовності термінала
  logEvent("SYSTEM", "Hospital access control operational");                            // запис готовності пропускного вузла у журнал
}                                                                                       // завершення функції початкового налаштування

void loop() {                                                                           // головний безперервний цикл мікроконтролера
  if (Blynk.connected()) {                                                              // перевірка наявності зв язку з хмарою блінк
    Blynk.run();                                                                        // обробка мережевих пакетів віджетів додатку
  }                                                                                     // завершення виклику циклу хмари

  checkButtons();                                                                       // опитування стану кнопок ідентифікаційних карток

  if (doorUnlocked && millis() - doorUnlockTime >= 3000) {                              // перевірка таймауту відкритого замка три секунди
    doorUnlocked = false;                                                               // скидання стану розблокованого входу
    lockServo.write(SERVO_LOCKED_POS);                                                  // повернення сервоприводу у закритий стан
    digitalWrite(PIN_LED_GREEN, LOW);                                                   // вимкнення зеленого діода підтвердження
    currentStatusText = "Locked";                                                       // оновлення статусу на закритий стан замка
    const char* sTxt = (currentShift == SHIFT_DAY) ? "SHIFT: DAY" : "SHIFT: NIGHT";     // визначення активної зміни для дисплея
    showLcdStatus("DOOR LOCKED", sTxt);                                                 // відображення закриття дверей на екрані
    logEvent("HARDWARE", "Bolt engaged door locked securely");                          // запис закриття ригеля замка у термінал
    if (Blynk.connected()) {                                                            // перевірка доступності сервера
      Blynk.virtualWrite(V0, 0);                                                        // відправка закритого стану замка у додаток
      Blynk.virtualWrite(V2, currentStatusText);                                        // передача текстового статусу замка
    }                                                                                   // завершення оновлення стану в хмарі
  }                                                                                     // завершення блоку автоматичного замикання

  if (currentBuzzerFreq > 0 && millis() >= alarmStopTime) {                             // перевірка закінчення звукового сигналу зумера
    setBuzzerTone(0);                                                                   // вимкнення звукового випромінювача сирени
  }                                                                                     // завершення обробки вимкнення звуку

  if (!doorUnlocked && !aiAudit.isLockedOut()) {                                        // контроль вимкнення червоного діода після заборони
    if (millis() >= alarmStopTime + 500) {                                              // пауза пів секунди після завершення звуку
      digitalWrite(PIN_LED_RED, LOW);                                                   // вимкнення червоного світлодіода заборони
    }                                                                                   // завершення перевірки інтервалу
  }                                                                                     // завершення контролю червоного діода

  printDataTable();                                                                     // періодичний вивід структурованої таблиці параметрів
}                                                                                       // завершення головного нескінченного циклу