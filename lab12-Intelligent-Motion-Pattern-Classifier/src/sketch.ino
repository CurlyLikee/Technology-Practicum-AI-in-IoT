#include <Wire.h>                                                                       // Бібліотека шини даних I2C
#include <WiFi.h>                                                                       // Бібліотека підключення до бездротової мережі
#include <WiFiClientSecure.h>                                                           // Клієнт захищеного з'єднання TLS
#include <HTTPClient.h>                                                                 // Клієнт надсилання мережевих запитів
#include <Adafruit_MPU6050.h>                                                           // Драйвер цифрового сенсора руху
#include <Adafruit_Sensor.h>                                                            // Базовий уніфікований інтерфейс сенсорів
#include "esp_log.h"                                                                    // Керування рівнями журналювання системи
#include "driver/ledc.h"                                                                // Апаратний драйвер генератора ШІМ
#include "ai_classifier.h"                                                              // Модуль штучного інтелекту класифікації

const int PIN_BUTTON = 15;                                                              // Контакт кнопки апаратного скидання тривоги
const int PIN_LED_GREEN = 18;                                                           // Контакт зеленого індикатора спокою
const int PIN_LED_RED = 19;                                                             // Контакт червоного індикатора небезпеки
const int PIN_BUZZER = 23;                                                              // Контакт звукового випромінювача сирени

const char* WIFI_SSID = "Wokwi-GUEST";                                                  // Назва точки доступу бездротової мережі
const char* WIFI_PASS = "";                                                             // Пароль точки доступу бездротової мережі

const char* IO_KEY = "YOUR_ADAFRUIT_IO_KEY";                                // Ключ автентифікації хмарного сервісу
const char* IO_URL = "https://io.adafruit.com/api/v2/alonahruieva/groups/default/data"; // Кінцева адреса групового оновлення даних

struct AdafruitPayload {                                                                // Структура черги повідомлень хмари
  float peak;                                                                           // Пікове значення для передачі
  float variance;                                                                       // Поточна дисперсія для передачі
  bool alarm;                                                                           // Статус тривоги для передачі
  volatile bool hasNewData;                                                             // Прапорець наявності нових даних
};                                                                                      // Завершення структури черги повідомлень

AdafruitPayload aioPayload = {0.0f, 0.0f, false, false};                                // Ініціалізація буфера відправки в хмару

Adafruit_MPU6050 mpu;                                                                   // Екземпляр об'єкта сенсора руху
MotionClassifier classifier;                                                            // Екземпляр класифікатора штучного інтелекту

bool alarmState = false;                                                                // Загальний поточний стан системи тривоги
MotionClass lastReportedClass = CLASS_CALM;                                             // Останній переданий клас руху
unsigned long lastSampleTime = 0;                                                       // Часова мітка попереднього опитування сенсора
const unsigned long SAMPLE_INTERVAL = 40;                                               // Інтервал опитування вимірювача
unsigned long lastReportTime = 0;                                                       // Часова мітка останнього звіту в термінал
const unsigned long REPORT_INTERVAL = 500;                                              // Інтервал регулярного виводу звіту
unsigned long lastAdafruitSend = 0;                                                     // Часова мітка відправки в хмарний сервіс
const unsigned long ADAFRUIT_INTERVAL = 10000;                                          // Інтервал періодичної телеметрії в хмару

int lastButtonState = HIGH;                                                             // Попередній зчитаний стан кнопки скидання
unsigned long lastButtonTime = 0;                                                       // Часова мітка перевірки антибрязкоту кнопки

int currentBuzzerFreq = 0;                                                              // Поточна робоча частота генератора звуку
unsigned long calmStartTime = 0;                                                        // Часова мітка початку стану стійкого спокою

void setBuzzerTone(int freq) {                                                          // Функція безпечного керування тоном сирени
  if (freq != currentBuzzerFreq) {                                                      // Перевірка зміни необхідної частоти сигналу
    currentBuzzerFreq = freq;                                                           // Оновлення зафіксованої поточної частоти
    if (freq > 0) {                                                                     // Перевірка запиту на активне звучання
      ledc_set_freq(LEDC_LOW_SPEED_MODE, LEDC_TIMER_0, freq);                           // Встановлення нової робочої частоти таймера
      ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 512);                          // Задання робочого коефіцієнта заповнення
      ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);                            // Оновлення регістрів генератора звуку
    } else {                                                                            // Умова вимкнення звукового сигналу
      ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);                            // Скидання заповнення сигналу в нуль
      ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);                            // Зупинка вихідного звукового сигналу
    }                                                                                   // Завершення перевірки вимкнення звуку
  }                                                                                     // Завершення блоку зміни частоти
}                                                                                       // Завершення функції генерації тону

bool checkButtonPress() {                                                               // Функція опитування кнопки з антибрязкотом
  int reading = digitalRead(PIN_BUTTON);                                                // Зчитування логічного рівня з контакту кнопки
  unsigned long now = millis();                                                         // Отримання поточного системного часу
  if (reading == LOW && lastButtonState == HIGH && (now - lastButtonTime > 150)) {      // Перевірка спадного фронту та затримки
    lastButtonTime = now;                                                               // Оновлення часу останнього спрацьовування
    lastButtonState = reading;                                                          // Збереження нового стану контакту
    return true;                                                                        // Підтвердження натискання кнопки скидання
  }                                                                                     // Завершення перевірки фронту натискання
  lastButtonState = reading;                                                            // Оновлення поточного положення кнопки
  return false;                                                                         // Повернення статусу відсутності натискання
}                                                                                       // Завершення функції обробки кнопки

void sendJsonReport(const ClassificationResult& res, float ax, float ay, float az) {    // Функція виводу звіту в термінал
  char buf[256];                                                                        // Буфер символів для форматування рядка
  snprintf(buf, sizeof(buf),                                                            // Атомарне формування рядка пакета даних
    "{\"timestamp\":%lu,\"ax\":%.2f,\"ay\":%.2f,\"az\":%.2f,"                           // Формування полів мітки часу та осей
    "\"variance\":%.3f,\"peak\":%.2f,\"class\":\"%s\","                                 // Формування полів дисперсії піка та класу
    "\"confidence\":%.2f,\"alarm\":%s}",                                                // Формування полів достовірності та тривоги
    millis(), ax, ay, az, res.variance, res.peak, res.className,                        // Передача значень часу осей та метрик
    res.confidence, alarmState ? "true" : "false");                                     // Передача показника впевненості та статусу
  Serial.println(buf);                                                                  // Вивід сформованого пакета в термінал
}                                                                                       // Завершення функції виводу телеметрії

void queueAdafruitSend(float peak, float variance, bool alarm) {                        // Функція постановки телеметрії в чергу
  aioPayload.peak = peak;                                                               // Збереження амплітудного піка для передачі
  aioPayload.variance = variance;                                                       // Збереження розрахованої дисперсії вікна
  aioPayload.alarm = alarm;                                                             // Збереження поточного статусу небезпеки
  aioPayload.hasNewData = true;                                                         // Встановлення прапорця наявності даних
}                                                                                       // Завершення функції постановки в чергу

void adafruitTask(void *pvParameters) {                                                 // Асинхронний фоновий потік передачі даних
  unsigned long lastSend = 0;                                                           // Часова мітка останнього мережевого запиту
  while (1) {                                                                           // Нескінченний цикл чергування потоку
    unsigned long now = millis();                                                       // Отримання поточної системної мітки часу
    bool hasData = aioPayload.hasNewData;                                               // Опитування наявності нових даних для хмари
    bool isConnected = WiFi.status() == WL_CONNECTED;                                   // Перевірка наявності активного підключення
    if (hasData && isConnected && (now - lastSend >= 5000)) {                           // Контроль періоду безпечної передачі у хмару
      aioPayload.hasNewData = false;                                                    // Скидання прапорця наявності нових даних
      lastSend = now;                                                                   // Оновлення часу останньої відправки даних

      float p = aioPayload.peak;                                                        // Локальна копія пікового прискорення
      float v = aioPayload.variance;                                                    // Локальна копія показника дисперсії
      bool a = aioPayload.alarm;                                                        // Локальна копія аварійного статусу

      WiFiClientSecure client;                                                          // Створення екземпляра захищеного клієнта
      client.setInsecure();                                                             // Пропуск перевірки сертифіката сервера

      HTTPClient http;                                                                  // Створення об'єкта виконання мережевого запиту
      http.setTimeout(3500);                                                            // Встановлення тайм-ауту очікування відповіді
      http.begin(client, IO_URL);                                                       // Ініціалізація з'єднання за вказаною адресою
      http.addHeader("Content-Type", "application/json");                               // Додавання заголовка типу переданого контенту
      http.addHeader("X-AIO-Key", IO_KEY);                                              // Додавання заголовка ключа доступу сервісу

      String payload = "{\"feeds\":[";                                                  // Початок тіла групового запиту каналів
      payload += "{\"key\":\"motion-peak\",\"value\":\"" + String(p, 2) + "\"},";       // Формування даних каналу піку прискорення
      payload += "{\"key\":\"motion-variance\",\"value\":\"" + String(v, 3) + "\"},";   // Формування даних каналу дисперсії
      payload += "{\"key\":\"motion-alarm\",\"value\":\"" + String(a ? 1 : 0) + "\"}";  // Формування даних каналу статусу тривоги
      payload += "]}";                                                                  // Завершення тіла запиту пакетів

      int httpCode = http.POST(payload);                                                // Відправка сформованого пакета методом POST
      if (httpCode > 0) {                                                               // Перевірка отримання успішного коду відповіді
        Serial.print("[Adafruit IO] Sent! Code: ");                                     // Вивід текстового підтвердження відправки
        Serial.println(httpCode);                                                       // Вивід числового коду відповіді сервера
      } else {                                                                          // Умова виникнення мережевого збою
        Serial.print("[Adafruit IO] Error: ");                                          // Вивід текстового повідомлення про збій
        Serial.println(http.errorToString(httpCode));                                   // Розшифровка та вивід коду помилки клієнта
      }                                                                                 // Завершення перевірки результату відправки

      http.end();                                                                       // Закриття поточного мережевого з'єднання
    }                                                                                   // Завершення блоку відправки телеметрії
    vTaskDelay(pdMS_TO_TICKS(500));                                                     // Призупинення виконання потоку операційної системи
  }                                                                                     // Завершення нескінченного циклу потоку
}                                                                                       // Завершення функції потоку FreeRTOS

void updateIndicators() {                                                               // Функція керування звуком та індикацією
  if (alarmState) {                                                                     // Перевірка активності стану небезпеки
    digitalWrite(PIN_LED_GREEN, LOW);                                                   // Вимкнення зеленого індикатора спокою
    if ((millis() / 150) % 2 == 0) {                                                    // Періодичне чергування фаз звукової сирени
      digitalWrite(PIN_LED_RED, HIGH);                                                  // Увімкнення червоного світлодіода тривоги
      setBuzzerTone(1800);                                                              // Генерація високого тону звукового сигналу
    } else {                                                                            // Друга чергова фаза звучання сирени
      digitalWrite(PIN_LED_RED, LOW);                                                   // Короткочасне пригасання червоного світлодіода
      setBuzzerTone(1200);                                                              // Генерація низького тону звукового сигналу
    }                                                                                   // Завершення чергування тональностей сирени
  } else {                                                                              // Умова повернення системи до штатного спокою
    digitalWrite(PIN_LED_GREEN, HIGH);                                                  // Увімкнення зеленого світлодіода готовності
    digitalWrite(PIN_LED_RED, LOW);                                                     // Вимкнення червоного аварійного світлодіода
    setBuzzerTone(0);                                                                   // Повне вимкнення звукового сигналу сирени
  }                                                                                     // Завершення розгалуження станів індикації
}                                                                                       // Завершення функції оновлення індикаторів

void setup() {                                                                          // Головна функція ініціалізації периферії
  Serial.begin(115200);                                                                 // Ініціалізація послідовного порту моніторингу
  esp_log_level_set("*", ESP_LOG_NONE);                                                 // Блокування загальних системних повідомлень
  esp_log_level_set("ledc", ESP_LOG_NONE);                                              // Придушення налагоджувальних звітів генератора ШІМ

  Serial.println();                                                                     // Вивід порожнього рядка для відступу
  Serial.println("=====================================");                              // Вивід верхнього декоративного розділювача
  Serial.println("Intelligent Motion Pattern Classifier");                              // Вивід назви проєкту в послідовний порт
  Serial.println("=====================================");                              // Вивід нижнього декоративного розділювача
  Serial.println();                                                                     // Вивід порожнього рядка в термінал

  Wire.begin(21, 22);                                                                   // Ініціалізація інтерфейсу зв'язку з сенсором

  pinMode(PIN_BUTTON, INPUT_PULLUP);                                                    // Налаштування входу кнопки з підтяжкою до живлення
  pinMode(PIN_LED_GREEN, OUTPUT);                                                       // Налаштування контакту зеленого індикатора на вивід
  pinMode(PIN_LED_RED, OUTPUT);                                                         // Налаштування контакту червоного індикатора на вивід

  ledc_timer_config_t ledc_timer = {                                                    // Конфігурація апаратного таймера генератора тонів
    .speed_mode = LEDC_LOW_SPEED_MODE,                                                  // Режим низької швидкості роботи модуля таймера
    .duty_resolution = LEDC_TIMER_10_BIT,                                               // Роздільна здатність таймера десять біт
    .timer_num = LEDC_TIMER_0,                                                          // Вибір першого апаратного таймера
    .freq_hz = 2000,                                                                    // Початкова базова частота звукового генератора
    .clk_cfg = LEDC_AUTO_CLK                                                            // Автоматичний вибір тактового сигналу
  };                                                                                    // Завершення структури конфігурації таймера
  ledc_timer_config(&ledc_timer);                                                       // Застосування параметрів налаштування таймера

  ledc_channel_config_t ledc_channel = {                                                // Конфігурація вихідного каналу генератора
    .gpio_num = PIN_BUZZER,                                                             // Прив'язка виводу звукового випромінювача
    .speed_mode = LEDC_LOW_SPEED_MODE,                                                  // Призначення відповідного швидкісного режиму
    .channel = LEDC_CHANNEL_0,                                                          // Вибір нульового каналу генератора сигналу
    .intr_type = LEDC_INTR_DISABLE,                                                     // Вимкнення апаратних переривань каналу
    .timer_sel = LEDC_TIMER_0,                                                          // Прив'язка до налаштованого апаратного таймера
    .duty = 0,                                                                          // Початковий нульовий коефіцієнт заповнення
    .hpoint = 0                                                                         // Початкова точка фази вихідного сигналу
  };                                                                                    // Завершення структури конфігурації каналу
  ledc_channel_config(&ledc_channel);                                                   // Застосування конфігурації апаратного каналу

  digitalWrite(PIN_LED_GREEN, HIGH);                                                    // Встановлення активного рівня зеленого індикатора
  digitalWrite(PIN_LED_RED, LOW);                                                       // Встановлення неактивного рівня червоного діода

  if (!mpu.begin()) {                                                                   // Перевірка готовності ініціалізації сенсора
    Serial.println("MPU6050 initialization failed!");                                   // Повідомлення про збій підключення вимірювача
    while (1) {                                                                         // Аварійний нескінченний цикл сигналізації
      digitalWrite(PIN_LED_RED, HIGH);                                                  // Увімкнення червоного аварійного світлодіода
      delay(200);                                                                       // Затримка свічення аварійного діода
      digitalWrite(PIN_LED_RED, LOW);                                                   // Вимкнення червоного аварійного світлодіода
      delay(200);                                                                       // Затримка паузи аварійного миготіння
    }                                                                                   // Завершення аварійного циклу індикації
  }                                                                                     // Завершення блоку перевірки підключення

  mpu.setAccelerometerRange(MPU6050_RANGE_4_G);                                         // Налаштування діапазону вимірювання прискорення
  mpu.setGyroRange(MPU6050_RANGE_250_DEG);                                              // Налаштування діапазону кутової швидкості
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);                                           // Встановлення смуги вбудованого цифрового фільтра

  Serial.println("System successfully started");                                        // Вивід підтвердження готовності системи
  Serial.println("MPU6050 sensor initialized");                                         // Вивід підтвердження налаштування вимірювача

  Serial.print("Connecting to Wi-Fi");                                                  // Початок процесу підключення до мережі
  Serial.flush();                                                                       // Очікування завершення передачі байтів у порт
  delay(50);                                                                            // Технічна затримка стабілізації передачі

  WiFi.begin(WIFI_SSID, WIFI_PASS);                                                     // Запуск з'єднання з бездротовою мережею
  int wifiTimeout = 0;                                                                  // Лічильник спроб встановлення з'єднання
  while (WiFi.status() != WL_CONNECTED && wifiTimeout < 20) {                           // Очікування відповіді від точки доступу
    delay(250);                                                                         // Інтервал між перевірками статусу мережі
    Serial.print(".");                                                                  // Вивід крапки процесу з'єднання в термінал
    wifiTimeout++;                                                                      // Збільшення лічильника пройдених спроб
  }                                                                                     // Завершення циклу очікування зв'язку
  if (WiFi.status() == WL_CONNECTED) {                                                  // Перевірка успішності підключення до мережі
    Serial.println(" Connected!");                                                      // Сповіщення про успішне встановлення зв'язку
  } else {                                                                              // Умова відсутності мережевого з'єднання
    Serial.println(" Offline mode.");                                                   // Перехід системи в локальний автономний режим
  }                                                                                     // Завершення перевірки статусу підключення

  xTaskCreatePinnedToCore(                                                              // Створення асинхронного фонового завдання
    adafruitTask,                                                                       // Вказівник на функцію виконання завдання
    "adafruitTask",                                                                     // Текстове ім'я фонового процесу передачі
    8192,                                                                               // Розмір виділеного стека оперативної пам'яті
    NULL,                                                                               // Параметри передачі у створену функцію
    1,                                                                                  // Пріоритет виконання фонового завдання
    NULL,                                                                               // Дескриптор для керування створеною задачею
    0                                                                                   // Призначення виконання на нульове ядро
  );                                                                                    // Завершення створення завдання FreeRTOS

  Serial.println();                                                                     // Додатковий відступ у вікні послідовного порту
  Serial.println("Waiting for motion...");                                              // Вивід статусу переходу в режим очікування
  Serial.println();                                                                     // Розділовий відступ у терміналі моніторингу
}                                                                                       // Завершення функції початкового налаштування

void loop() {                                                                           // Головний циклічний процес роботи пристрою
  unsigned long currentMillis = millis();                                               // Зчитування поточної мітки системного часу

  if (checkButtonPress()) {                                                             // Перевірка події натискання кнопки скидання
    alarmState = false;                                                                 // Скидання активного стану небезпеки
    classifier.reset();                                                                 // Очищення накопичених зразків класифікатора
    lastReportedClass = CLASS_CALM;                                                     // Скидання останнього зафіксованого класу
    setBuzzerTone(0);                                                                   // Миттєве вимкнення звучання сирени
    queueAdafruitSend(0.0f, 0.0f, false);                                               // Постановка пакету скидання в чергу хмари
    ClassificationResult r;                                                             // Створення тимчасового об'єкта звіту
    r.motionClass = CLASS_CALM;                                                         // Встановлення базового класу спокою
    r.className = "calm";                                                               // Призначення текстового імені стану
    r.variance = 0.0f;                                                                  // Обнулення показника розрахованої дисперсії
    r.peak = 0.0f;                                                                      // Обнулення показника пікової амплітуди
    r.confidence = 1.0f;                                                                // Встановлення максимальної впевненості
    r.isAlarm = false;                                                                  // Скидання ознаки аварійного сигналу
    lastReportTime = currentMillis;                                                     // Оновлення часу формування останнього звіту
    sendJsonReport(r, 0.0f, 0.0f, 9.81f);                                               // Відправка зведеного пакета скидання в термінал
  }                                                                                     // Завершення обробки кнопки скидання

  if (currentMillis - lastSampleTime >= SAMPLE_INTERVAL) {                              // Перевірка настання чергового такту вимірювання
    lastSampleTime = currentMillis;                                                     // Фіксація часу здійснення поточного виміру

    sensors_event_t a, g, temp;                                                         // Оголошення структур отримання даних сенсора
    mpu.getEvent(&a, &g, &temp);                                                        // Зчитування векторних даних з сенсора руху

    classifier.addSample(a.acceleration.x, a.acceleration.y, a.acceleration.z);         // Передача миттєвих просторових вимірів у буфер
    ClassificationResult result = classifier.classify();                                // Виклик процедури інтелектуальної класифікації

    bool stateChanged = false;                                                          // Прапорець фіксації зміни стану об'єкта

    if (result.motionClass != lastReportedClass) {                                      // Перевірка зміни визначеної категорії руху
      lastReportedClass = result.motionClass;                                           // Оновлення збереженого класу механічного стану
      stateChanged = true;                                                              // Встановлення ознаки необхідності звітування
    }                                                                                   // Завершення перевірки зміни категорії

    if (result.isAlarm && !alarmState) {                                                // Перевірка виявлення нового тривожного удару
      alarmState = true;                                                                // Активація глобального стану тривоги системи
      calmStartTime = 0;                                                                // Скидання таймера відліку тривалого спокою
      stateChanged = true;                                                              // Встановлення вимоги негайного сповіщення
      queueAdafruitSend(result.peak, result.variance, true);                            // Термінова передача сигналу тривоги в хмару
    } else if (alarmState) {                                                            // Обробка системи під час активного режиму тривоги
      if (result.motionClass == CLASS_CALM) {                                           // Перевірка настання безпечного стану спокою
        if (calmStartTime == 0) {                                                       // Перевірка початку відліку періоду затухання
          calmStartTime = currentMillis;                                                // Фіксація початкового часу відновлення спокою
        } else if (currentMillis - calmStartTime > 3000) {                              // Перевірка тривалості безперервного спокою
          alarmState = false;                                                           // Автоматичне зняття статусу тривоги
          calmStartTime = 0;                                                            // Обнулення таймера відліку спокою
          setBuzzerTone(0);                                                             // Автоматичне вимкнення звукової сирени
          stateChanged = true;                                                          // Фіксація зміни стану для негайного звіту
          queueAdafruitSend(result.peak, result.variance, false);                       // Передача сигналу зняття тривоги у хмару
        }                                                                               // Завершення перевірки часу відновлення спокою
      } else {                                                                          // Умова появи нових залишкових коливань
        calmStartTime = 0;                                                              // Скидання таймера відновлення спокою
      }                                                                                 // Завершення перевірки характеру затухання
    }                                                                                   // Завершення блоку керування аварійним станом

    if (stateChanged || (currentMillis - lastReportTime >= REPORT_INTERVAL)) {          // Перевірка потреби виводу поточного пакета
      lastReportTime = currentMillis;                                                   // Оновлення мітки часу останнього звітування
      sendJsonReport(result, a.acceleration.x, a.acceleration.y, a.acceleration.z);     // Формування та відправка рядка JSON у порт
    }                                                                                   // Завершення блоку виводу в термінал

    if (currentMillis - lastAdafruitSend >= ADAFRUIT_INTERVAL) {                        // Перевірка інтервалу регулярної телеметрії
      lastAdafruitSend = currentMillis;                                                 // Фіксація часу чергової відправки в хмару
      queueAdafruitSend(result.peak, result.variance, alarmState);                      // Постановка телеметрії у фонову чергу
    }                                                                                   // Завершення перевірки періоду відправки
  }                                                                                     // Завершення блоку вимірювального циклу

  updateIndicators();                                                                   // Оновлення стану світлодіодів та зумера
}                                                                                       // Завершення головного циклічного методу