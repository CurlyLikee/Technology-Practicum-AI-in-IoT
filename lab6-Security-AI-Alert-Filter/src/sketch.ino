#include <Arduino.h>                                                                          // Підключення основного фреймворку Arduino для платформи ESP32
#include <WiFi.h>                                                                             // Підключення бібліотеки бездротової мережі WiFi
#include <HTTPClient.h>                                                                       // Підключення клієнта протоколу HTTP для взаємодії з хмарою
#include "esp_log.h"                                                                          // Підключення заголовка системного логування ядра операційної системи
#include "ai_filter.h"                                                                        // Підключення власного модуля фільтрації тривог та моделі оцінки

void triggerAlarmSound();                                                                     // Попереднє оголошення функції відтворення сирени
void sendToThingSpeak(SecurityClass classification, float confidence, FeatureVector feat);    // Попереднє оголошення відправки телеметрії в хмару
void notifyEvent(SecurityClass classification, float confidence, FeatureVector feat);         // Попереднє оголошення обробки подій та індикації
void printCommandTable();                                                                     // Попереднє оголошення функції друку таблиці команд
void processSerialCommands();                                                                 // Попереднє оголошення обробки текстових команд з терміналу

const int PIN_PIR = 19;                                                                       // Номер виводу для підключення інфрачервоного датчика руху
const int PIN_REED = 4;                                                                       // Номер виводу для підключення магнітного геркона дверей
const int PIN_LED_RED = 16;                                                                   // Номер виводу червоного світлодіода сигналізації тривоги
const int PIN_LED_GREEN = 17;                                                                 // Номер виводу зеленого світлодіода статусу охорони
const int PIN_LED_BLUE = 5;                                                                   // Номер виводу синього світлодіода мережевої передачі
const int PIN_BUZZER = 18;                                                                    // Номер виводу для керування звуковим випромінювачем

const char *WIFI_SSID = "Wokwi-GUEST";                          // Назва віртуальної бездротової точки доступу
const char *WIFI_PASSWORD = "";                                 // Пароль доступу до бездротової мережі

const char *thingspeakServer = "api.thingspeak.com";            // Мережева адреса сервера хмарного сховища
const char *apiKey = "5FZCYBL5A51UMR5H";                        // Справжній індивідуальний ключ доступу до сервісу

bool isArmed = true;                                            // Змінна стану активності охоронного режиму системи
bool lastReedState = LOW;                                       // Збережене попереднє значення стану контакту геркона
bool reedBreached = false;                                      // Прапорець фіксації факту відкриття дверей
unsigned long reedBreachTimestamp = 0;                          // Часова мітка моменту відкриття дверей

bool isPirActive = false;                                       // Прапорець поточної активності сенсора присутності
unsigned long pirStartTimestamp = 0;                            // Часова мітка початку фіксації руху
unsigned long pirDuration = 0;                                  // Обчислена сумарна тривалість сигналу руху
int pirPulseCounter = 0;                                        // Лічильник кількості зареєстрованих імпульсів сенсора

unsigned long lastTelemetryTimestamp = 0;                       // Часова мітка останнього відправлення даних на сервер
SecurityClass lastClassification = CLASS_NORMAL;                // Збережений останній визначений клас для телеметрії
float lastConfidence = 1.0;                                     // Збережений останній рівень впевненості для телеметрії
FeatureVector lastFeat = {false, 0, 0, -1};                     // Збережений останній набір параметрів сенсорів

void playToneManual(int freq, int durationMs)                   // Оголошення функції генерації звукового сигналу
{                                                               // Функція прямої генерації звукових коливань заданої частоти
  int halfPeriodUs = 1000000 / (freq * 2);                      // Розрахунок половини періоду хвилі у мікросекундах
  long cycles = ((long)freq * durationMs) / 1000;               // Розрахунок загальної кількості необхідних циклів
  for (long i = 0; i < cycles; i++)                             // Початок циклу генерації звукових імпульсів
  {                                                             // Цикл генерації заданої кількості звукових хвиль
    digitalWrite(PIN_BUZZER, HIGH);                             // Подача високого рівня на пін випромінювача
    delayMicroseconds(halfPeriodUs);                            // Очікування тривалості напівперіоду сигналу
    digitalWrite(PIN_BUZZER, LOW);                              // Подача низького рівня на пін випромінювача
    delayMicroseconds(halfPeriodUs);                            // Очікування завершення повного періоду коливання
  }                                                             // Кінець циклу формування прямокутного сигналу
}                                                               // Кінець функції ручної генерації тону

void triggerAlarmSound()                                        // Оголошення функції відтворення сирени тривоги
{                                                               // Функція відтворення модульованого сигналу тривоги
  for (int i = 0; i < 4; i++)                                   // Початок циклу відтворення серії звукових сигналів
  {                                                             // Цикл повторення послідовності звукових тонів
    playToneManual(2000, 150);                                  // Генерація високого тону дві тисячі герц
    delay(50);                                                  // Коротка технологічна пауза між частотами
    playToneManual(1200, 150);                                  // Генерація низького тону тисяча двісті герц
    delay(50);                                                  // Пауза перед наступною серією сигналів тривоги
  }                                                             // Кінець циклу відтворення звукової сирени
}                                                               // Кінець функції тривожного звукового сигналу

void sendToThingSpeak(SecurityClass classification, float confidence, FeatureVector feat)       // Функція передачі телеметрії на сервер
{                                                                                               // Початок тіла функції відправки даних
  if (WiFi.status() == WL_CONNECTED)                                                            // Перевірка наявності активного підключення до мережі
  {                                                                                             // Початок блоку відправки даних через мережу
    HTTPClient http;                                                                            // Створення об'єкта клієнта для виконання мережевого запиту

    String url = "http://";                                                                     // Початкова ініціалізація текстового рядка адреси
    url += thingspeakServer;                                                                    // Додавання адреси хмарного хоста
    url += "/update?api_key=";                                                                  // Додавання шляху оновлення даних та параметра ключа
    url += apiKey;                                                                              // Додавання унікального ключа запису користувача
    url += "&field1=" + String((int)classification);                                            // Додавання поля з цифровим номером класу загрози
    url += "&field2=" + String(confidence * 100.0, 1);                                          // Додавання поля з відсотком впевненості моделі
    url += "&field3=" + String(feat.reedOpen ? 1 : 0);                                          // Додавання стану датчика відкриття дверей
    url += "&field4=" + String(feat.motionDurationMs);                                          // Додавання виміряної тривалості виявленого руху

    http.begin(url);                                                                            // Ініціалізація з'єднання за сформованою адресою
    int httpCode = http.GET();                                                                  // Відправка сформованого мережевого HTTP запиту

    if (httpCode == HTTP_CODE_OK)                                                               // Перевірка чи повернув сервер статус успішного виконання
    {                                                                                           // Початок обробки відповіді успішного запиту
      String response = http.getString();                                                       // Читання тіла відповіді від сервісу
      response.trim();                                                                          // Видалення зайвих символів пробілу з відповіді

      long entryId = response.toInt();                                                          // Перетворення відповіді сервера у цілочисельний номер запису
      if (entryId > 0)                                                                          // Перевірка отримання коректного номера нового запису
      {                                                                                         // Початок гілки успішного додавання запису
        Serial.print("[THINGSPEAK] Data accepted! Entry ID: ");                                 // Виведення повідомлення про успішне прийняття даних
        Serial.println(entryId);                                                                // Друк збереженого номера нового запису в базі
      }                                                                                         // Кінець гілки успішного прийняття даних
      else                                                                                      // Гілка якщо сервер відхилив запис
      {                                                                                         // Початок блоку повідомлення про помилку відхилення
        Serial.print("[THINGSPEAK] Rejected by server (Response: ");                            // Початок друку повідомлення про відхилення запиту
        Serial.print(response);                                                                 // Друк отриманого коду відхилення від сервера
        Serial.println("). Check Write API Key or 15-second rate limit.");                      // Пояснення щодо можливої причини відхилення
      }                                                                                         // Кінець блоку повідомлення про помилку
    }                                                                                           // Кінець блоку успішного отримання коду сервера
    else if (httpCode > 0)                                                                      // Перевірка отримання іншого коду стану протоколу
    {                                                                                           // Початок виведення отриманого коду помилки
      Serial.printf("[THINGSPEAK] Server returned HTTP code: %d\n", httpCode);                  // Форматований друк коду відповіді сервера
    }                                                                                           // Кінець блоку друку коду стану
    else                                                                                        // Гілка помилки зв'язку з сервером
    {                                                                                                 // Початок обробки збою підключення
      Serial.printf("[THINGSPEAK] Connection failed: %s\n", http.errorToString(httpCode).c_str());    // Виведення опису апаратної або мережевої помилки
    }                                                                                                 // Кінець обробки помилки з'єднання

    http.end();                                                                             // Закриття мережевого з'єднання та звільнення пам'яті
  }                                                                                         // Кінець блоку активного Wi-Fi
  else                                                                                      // Гілка відсутності мережі
  {                                                                                         // Початок блоку роботи без зв'язку
    Serial.println("[THINGSPEAK] WiFi not connected. Telemetry skipped.");                  // Сповіщення про відсутність мережі Wi-Fi
  }                                                                                         // Кінець гілки відсутності зв'язку
}                                                                                           // Кінець функції передачі даних на сервіс

void notifyEvent(SecurityClass classification, float confidence, FeatureVector feat)        // Оголошення функції сповіщення та обробки події
{                                                                                           // Функція обробки результату аналізу та індикації
  if (!isArmed)                                                                             // Перевірка активності режиму охорони
  {                                                                                         // Перевірка чи знята система з режиму охорони
    Serial.println();                                                                       // Друк порожнього рядка для вирівнювання
    Serial.println("[SECURITY] System is DISARMED.");                                       // Повідомлення про ігнорування події без охорони
    return;                                                                                 // Негайний вихід з функції при вимкненій охороні
  }                                                                                         // Кінець перевірки статусу системи охорони

  digitalWrite(PIN_LED_BLUE, HIGH);                                                         // Увімкнення синього індикатора мережевої активності

  Serial.println();                                                                         // Друк порожнього рядка для естетичного відступу
  Serial.println("-------------------------------------------------");                      // Друк розділювальної смуги інформаційного блоку
  Serial.print("[AI-INFERENCE] Result: ");                                                  // Виведення заголовка класифікації інтелектуального аналізу
  Serial.println(getClassName(classification));                                             // Виведення назви визначеного класу події
  Serial.print("Model Confidence: ");                                                       // Виведення заголовка відсотка впевненості
  Serial.print(confidence * 100.0, 1);                                                      // Виведення числового значення впевненості моделі
  Serial.println("%");                                                                      // Виведення знака відсотка та перенесення рядка
  Serial.print("Features: Reed=");                                                          // Виведення підпису параметрів геркона
  Serial.print(feat.reedOpen ? "OPEN" : "CLOSED");                                          // Виведення фактичного стану дверей
  Serial.print(" | PIR Duration=");                                                         // Виведення тексту виміряної тривалості руху
  Serial.print(feat.motionDurationMs);                                                      // Виведення мілісекунд активності сенсора
  Serial.print(" ms | Pulses=");                                                            // Текстовий розділювач кількості імпульсів
  Serial.println(feat.pulseCount);                                                          // Виведення підрахованої кількості імпульсів сенсора

  if (classification == CLASS_HUMAN_INTRUSION)                                              // Перевірка умови класифікації вторгнення людини
  {                                                                                         // Перевірка чи класифіковано загрозу як вторгнення людини
    Serial.println("[CRITICAL] INTRUSION CONFIRMED! Alarm siren triggered!");               // Друк повідомлення про підтверджену загрозу
    digitalWrite(PIN_LED_RED, HIGH);                                                        // Увімкнення червоного аварійного світлодіода
    digitalWrite(PIN_LED_GREEN, LOW);                                                       // Вимкнення зеленого світлодіода спокійного стану
    triggerAlarmSound();                                                                    // Запуск тривожного звукового сигналу сирени
    digitalWrite(PIN_LED_RED, LOW);                                                         // Вимкнення червоного тривожного діода після тривоги
    digitalWrite(PIN_LED_GREEN, HIGH);                                                      // Відновлення зеленого світлодіода готовності
  }                                                                                         // Кінець блоку дій при вторгненні
  else if (classification == CLASS_PET)                                                     // Перевірка умови класифікації руху тварини
  {                                                                                         // Перевірка чи класифіковано рух як домашнього улюбленця
    Serial.println("[AI FILTER] False alarm rejected: Pet movement detected.");             // Повідомлення про відхилення хибної тривоги
    playToneManual(800, 80);                                                                // Відтворення короткого попереджувального сигналу звуку
  }                                                                                         // Кінець блоку дій при русі тварини
  else if (classification == CLASS_NOISE)                                                   // Перевірка умови класифікації апаратного шуму
  {                                                                                         // Перевірка чи визначено сигнал як короткий шум
    Serial.println("[AI FILTER] False alarm rejected: Sensor noise / glitch.");             // Сповіщення про фільтрацію апаратного збою
  }                                                                                         // Кінець ланцюжка обробки типів подій

  lastClassification = classification;                                                      // Оновлення останнього класифікованого класу події
  lastConfidence = confidence;                                                              // Оновлення збереженого рівня впевненості моделі
  lastFeat = feat;                                                                          // Оновлення останнього вектора виміряних ознак

  if (millis() - lastTelemetryTimestamp >= 16000 || lastTelemetryTimestamp == 0)            // Перевірка чи минуло шістнадцять секунд від останньої передачі
  {                                                                                         // Початок негайної відправки актуальних даних
    sendToThingSpeak(classification, confidence, feat);                                     // Відправка збережених параметрів на сервер аналітики
    lastTelemetryTimestamp = millis();                                                      // Оновлення часової мітки успішної відправки телеметрії
  }                                                                                         // Кінець умови перевірки часового інтервалу

  digitalWrite(PIN_LED_BLUE, LOW);                                                          // Вимкнення синього світлодіода обміну даними
  Serial.println("-------------------------------------------------");                      // Закриття інформаційного блоку у терміналі
}                                                                                           // Кінець функції notifyEvent

void printCommandTable()                                                                    // Оголошення функції виведення таблиці команд
{                                                                                           // Функція виведення структурованої таблиці доступних команд
  Serial.println();                                                                         // Друк розділювального порожнього рядка
  Serial.println("+--------------+------------------------------------------------+");      // Друк верхньої рамки таблиці
  Serial.println("| Command      | Description                                    |");      // Друк назв колонок таблиці команд
  Serial.println("+--------------+------------------------------------------------+");      // Друк розділювача між заголовком і тілом
  Serial.println("| ARM          | Arm security system                            |");      // Опис команди активації охорони
  Serial.println("| DISARM       | Disarm security system                         |");      // Опис команди деактивації охорони
  Serial.println("| STATUS       | Print current system and sensor states         |");      // Опис команди перевірки стану системи
  Serial.println("| POLICY:DAY   | Apply day timing policy                        |");      // Опис команди перемикання на денний режим
  Serial.println("| POLICY:NIGHT | Apply night timing policy                      |");      // Опис команди перемикання на нічний режим
  Serial.println("| POLICY:AWAY  | Apply away timing policy                       |");      // Опис команди перемикання на режим відсутності
  Serial.println("| TEST:NOISE   | Simulate sensor noise                          |");      // Опис команди симуляції апаратного шуму
  Serial.println("| TEST:PET     | Simulate pet movement                          |");      // Опис команди симуляції тварини
  Serial.println("| TEST:HUMAN   | Simulate human intrusion                       |");      // Опис команди симуляції вторгнення людини
  Serial.println("+--------------+------------------------------------------------+");      // Друк нижньої граничної лінії таблиці
}                                                                                           // Кінець функції друку таблиці команд

void processSerialCommands()                                                                // Оголошення функції читання команд терміналу
{                                                                             // Функція читання та інтерпретації команд користувача
  if (Serial.available() > 0)                                                 // Перевірка наявності даних у буфері порту
  {                                                                           // Перевірка чи надійшли нові байти у послідовний порт
    String cmd = Serial.readStringUntil('\n');                                // Читання введеного рядка до символу переносу
    cmd.trim();                                                               // Видалення пробільних символів на початку і в кінці команди
    if (cmd.equalsIgnoreCase("DISARM"))                                       // Перевірка команди зняття з охорони
    {                                                                         // Перевірка введення команди зняття з охорони
      isArmed = false;                                                        // Зміна прапорця активності на стан вимкнено
      digitalWrite(PIN_LED_GREEN, LOW);                                       // Вимкнення зеленого світлодіода активності
      digitalWrite(PIN_LED_RED, LOW);                                         // Вимкнення червоного світлодіода сигналізації
      digitalWrite(PIN_BUZZER, LOW);                                          // Вимкнення живлення звукового випромінювача
      Serial.println();                                                       // Друк порожнього рядка для читабельності
      Serial.println("[SECURITY] System DISARMED. Monitoring suspended.");    // Повідомлення про перехід у режим деактивації
    }                                                                         // Кінець обробки команди зняття
    else if (cmd.equalsIgnoreCase("ARM"))                                     // Перевірка команди активації охорони
    {                                                                         // Перевірка введення команди постановки на охорону
      isArmed = true;                                                         // Зміна статусу охорони на активний
      digitalWrite(PIN_LED_GREEN, HIGH);                                      // Увімкнення світлодіода активної охорони
      Serial.println();                                                       // Друк розділювального порожнього рядка
      Serial.println("[SECURITY] System ARMED.");                             // Повідомлення про успішну активацію охорони
    }                                                                         // Кінець обробки команди постановки
    else if (cmd.equalsIgnoreCase("POLICY:DAY"))                              // Перевірка вибору денного режиму
    {                                                                         // Перевірка вибору денної політики
      applyLLMPolicy(MODE_DAY);                                               // Застосування конфігурації денного режиму
    }                                                                         // Кінець обробки денного режиму
    else if (cmd.equalsIgnoreCase("POLICY:NIGHT"))                            // Перевірка вибору нічного режиму
    {                                                                         // Перевірка вибору нічної політики
      applyLLMPolicy(MODE_NIGHT);                                             // Застосування конфігурації нічного режиму
    }                                                                         // Кінець обробки нічного режиму
    else if (cmd.equalsIgnoreCase("POLICY:AWAY"))                             // Перевірка вибору режиму відсутності
    {                                                                         // Перевірка вибору політики відсутності
      applyLLMPolicy(MODE_AWAY);                                              // Застосування суворого режиму відсутності
    }                                                                         // Кінець обробки режиму відсутності
    else if (cmd.equalsIgnoreCase("STATUS"))                                  // Перевірка команди запиту статусу системи
    {                                                                                                                   // Перевірка виклику запиту системного статусу
      Serial.println();                                                                                                 // Друк порожнього рядка для розділення
      Serial.println("===== CURRENT SYSTEM STATUS =====");                                                              // Друк заголовка блоку поточного стану
      Serial.print("Security State: ");                                                                                 // Виведення напису поточного режиму охорони
      Serial.println(isArmed ? "ARMED (ON)" : "DISARMED (OFF)");                                                        // Відображення текстового статусу охорони
      Serial.print("Policy Mode: ");                                                                                    // Виведення підпису поточного режиму політики
      Serial.println(currentPolicy.mode == MODE_DAY ? "DAY" : (currentPolicy.mode == MODE_NIGHT ? "NIGHT" : "AWAY"));   // Друк активного режиму
      Serial.print("Noise Threshold: ");                                                                                // Виведення тексту порогу рівня шуму
      Serial.print(currentPolicy.noiseThresholdMs);                                                                     // Друк числового значення порогу шуму
      Serial.print(" ms | Pet Max Duration: ");                                                                         // Текст тривалості руху тварини
      Serial.print(currentPolicy.petMaxDurationMs);                                                                     // Друк часу руху тварини
      Serial.print(" ms | Verification Window: ");                                                                      // Текст вікна верифікації
      Serial.print(currentPolicy.verificationWindowMs);                                                                 // Друк тривалості часового вікна
      Serial.println(" ms");                                                                                            // Виведення одиниці виміру мілісекунд
      Serial.print("Reed State: ");                                                                                     // Підпис поточного значення на контакті геркона
      Serial.print(digitalRead(PIN_REED) == HIGH ? "OPEN" : "CLOSED");                                                  // Відображення фізичного стану геркона
      Serial.print(" | PIR State: ");                                                                                   // Підпис поточного значення датчика руху
      Serial.println(digitalRead(PIN_PIR) == HIGH ? "MOTION" : "IDLE");                                                 // Відображення фізичного сигналу датчика
      Serial.println("=================================");                                                              // Друк нижньої межі блоку статусу
    }                                                                                             // Кінець виведення інформації про статус
    else if (cmd.equalsIgnoreCase("TEST:NOISE"))                                                  // Перевірка команди тестування апаратного шуму
    {                                                                                             // Перевірка команди симуляції шуму
      Serial.println();                                                                           // Друк порожнього рядка для читабельності
      Serial.println("[TEST] Emulating sensor noise...");                                         // Виведення опису запущеного тесту
      FeatureVector feat = {false, 150, 1, -1};                                                   // Формування вектора ознак короткочасного шуму
      float conf = 0;                                                                             // Змінна для отримання впевненості класифікації
      SecurityClass sc = evaluateAIClassifier(feat, conf);                                        // Оцінка штучним інтелектом тестових ознак
      notifyEvent(sc, conf, feat);                                                                // Виклик процедури сповіщення з результатами тесту
    }                                                                                             // Кінець обробки тестування шуму
    else if (cmd.equalsIgnoreCase("TEST:PET"))                                                    // Перевірка команди тестування тварини
    {                                                                                             // Перевірка команди симуляції руху тварини
      Serial.println();                                                                           // Друк порожнього рядка для розділення
      Serial.println("[TEST] Emulating pet movement...");                                         // Опис тесту активності улюбленця
      FeatureVector feat = {false, 1200, 2, -1};                                                  // Вектор ознак типового переміщення тварини
      float conf = 0;                                                                             // Змінна для запису точності прогнозу
      SecurityClass sc = evaluateAIClassifier(feat, conf);                                        // Визначення класу поведінки сенсорів
      notifyEvent(sc, conf, feat);                                                                // Виклик індикації та реєстрації події
    }                                                                                             // Кінець обробки тестування тварини
    else if (cmd.equalsIgnoreCase("TEST:HUMAN"))                                                  // Перевірка команди тестування людини
    {                                                                                             // Перевірка команди тестування вторгнення
      Serial.println();                                                                           // Друк відступу перед результатами тесту
      Serial.println("[TEST] Emulating intrusion: door opened + sustained 3000 ms motion...");    // Повідомлення про запуск симуляції зламу
      FeatureVector feat = {true, 3000, 5, 400};                                                  // Вектор критичного вторгнення зі зламом дверей
      float conf = 0;                                                                             // Змінна оцінки рівня впевненості
      SecurityClass sc = evaluateAIClassifier(feat, conf);                                        // Аналіз моделі на виявлення людини
      notifyEvent(sc, conf, feat);                                                                // Виклик тривоги та індикації проникнення
    }                                                                                             // Кінець обробки тестування людини
    else                                                                                          // Обробка невідомої команди
    {                                                                                             // Гілка виконання якщо введена команда не розпізнана
      printCommandTable();                                                                        // Повторне відображення списку допустимих команд
    }                                                                                             // Кінець блоку перевірки варіантів команд
  }                                                                                               // Кінець перевірки наявності даних у буфері
}                                                                                                 // Кінець функції processSerialCommands

void setup()                                                              // Головна функція ініціалізації мікроконтролера
{                                                                         // Головна функція початкового налаштування мікроконтролера
  esp_log_level_set("*", ESP_LOG_NONE);                                   // Вимкнення виведення внутрішніх повідомлень ядра системи
  Serial.begin(115200);                                                   // Ініціалізація послідовного порту на швидкості сто п'ятнадцять тисяч двісті
  delay(500);                                                             // Пауза для стабілізації роботи порту після старту

  Serial.println();                                                       // Друк порожнього рядка на початку виведення
  Serial.println("=================================================");    // Верхній декоративний розділювач заголовка
  Serial.println("HOME SECURITY SYSTEM: AI FALSE ALARM FILTER");          // Назва системи розумної фільтрації тривог
  Serial.println("=================================================");    // Нижній декоративний розділювач заголовка

  pinMode(PIN_PIR, INPUT);                                                // Конфігурація виводу датчика руху як цифрового входу
  pinMode(PIN_REED, INPUT_PULLUP);                                        // Конфігурація входу геркона з внутрішнім підтягуючим резистором
  pinMode(PIN_LED_RED, OUTPUT);                                           // Конфігурація виводу червоного світлодіода як виходу
  pinMode(PIN_LED_GREEN, OUTPUT);                                         // Конфігурація виводу зеленого світлодіода як виходу
  pinMode(PIN_LED_BLUE, OUTPUT);                                          // Конфігурація виводу синього світлодіода як виходу
  pinMode(PIN_BUZZER, OUTPUT);                                            // Конфігурація виводу звукового сигналу як цифрового виходу

  digitalWrite(PIN_LED_GREEN, HIGH);                                      // Увімкнення індикатора нормальної роботи системи
  digitalWrite(PIN_LED_RED, LOW);                                         // Початкове вимкнення індикатора аварійної тривоги
  digitalWrite(PIN_LED_BLUE, LOW);                                        // Початкове вимкнення індикатора передачі даних

  Serial.print("[WiFi] Connecting to network: ");                         // Виведення повідомлення про початок з'єднання
  Serial.print(WIFI_SSID);                                                // Відображення імені бездротової мережі в терміналі
  Serial.print(" ");                                                      // Друк пробілу перед крапками процесу очікування
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);                                   // Запуск процедури авторизації у бездротовій мережі

  unsigned long startWifi = millis();                                     // Фіксація початкового часу спроби підключення
  while (WiFi.status() != WL_CONNECTED && millis() - startWifi < 4000)    // Цикл очікування підключення до мережі
  {                                                                       // Очікування з'єднання максимум чотири секунди
    delay(250);                                                           // Пауза чверть секунди між опитуваннями
    Serial.print(".");                                                    // Відображення прогресу пошуку мережі у терміналі
  }                                                                       // Кінець циклу очікування з'єднання
  if (WiFi.status() == WL_CONNECTED)                                      // Перевірка успішного з'єднання з мережею
  {                                                                       // Перевірка успішності отримання мережевої адреси
    Serial.println();                                                     // Перехід на новий рядок у терміналі
    Serial.print("[WiFi] Connected successfully! IP address: ");          // Повідомлення про встановлення зв'язку
    Serial.println(WiFi.localIP().toString());                            // Виведення отриманої мережевої адреси мікроконтролера
  }                                                                       // Кінець блоку успішного підключення
  else                                                                    // Обробка відсутності з'єднання з мережею
  {                                                                       // Дії у разі невдалого підключення
    Serial.println();                                                     // Друк переносу рядка
    Serial.println("[WiFi] Operating in standalone / simulation mode.");  // Повідомлення про роботу в автономному режимі
  }                                                                       // Кінець блоку перевірки статусу бездротової мережі

  printCommandTable();                                                    // Виведення переліку всіх доступних команд керування

  applyLLMPolicy(MODE_NIGHT);                                             // Встановлення стандартного режиму політики чутливості
  Serial.println();                                                       // Друк порожнього рядка для розділення блоків
  Serial.println("[SYSTEM] System armed. Monitoring sensor inputs...");   // Інформаційне повідомлення про готовність охорони
}                                                                         // Кінець функції первинної ініціалізації setup

void loop()                                                               // Головна функція циклічної роботи системи
{                                                                         // Головний нескінченний робочий цикл мікроконтролера
  processSerialCommands();                                                // Обробка введених користувачем команд керування

  bool currentReed = (digitalRead(PIN_REED) == HIGH);                                     // Зчитування логічного рівня з виводу геркона
  if (currentReed != lastReedState)                                                       // Перевірка зміни фізичного стану геркона
  {                                                                                       // Перевірка зміни фізичного стану контакту
    lastReedState = currentReed;                                                          // Оновлення збереженого стану геркона для наступної ітерації
    if (currentReed)                                                                      // Перевірка переходу геркона у розімкнений стан
    {                                                                                     // Перевірка умови якщо контакт розімкнувся
      reedBreached = true;                                                                // Встановлення прапорця факту відкриття дверей
      reedBreachTimestamp = millis();                                                     // Фіксація мітки точного часу відкриття
      Serial.println();                                                                   // Друк порожнього рядка для естетичного відступу
      Serial.println("[SENSOR] REED: Door OPENED!");                                      // Повідомлення про початок верифікації
      digitalWrite(PIN_LED_BLUE, HIGH);                                                   // Коротке увімкнення індикації фіксації події
      delay(50);                                                                          // Фіксована тривалість спалаху світлодіода
      digitalWrite(PIN_LED_BLUE, LOW);                                                    // Вимкнення синього діода після реєстрації
    }                                                                                     // Кінець обробки відчинення дверей
    else                                                                                  // Обробка повернення геркона у зачинений стан
    {                                                                                     // Гілка закриття контакту геркона
      Serial.println();                                                                   // Друк порожнього рядка у терміналі
      Serial.println("[SENSOR] REED: Door CLOSED.");                                      // Інформаційне повідомлення про повернення дверей у норму
    }                                                                                     // Кінець перевірки стану відкриття дверей
  }                                                                                       // Кінець обробки зміни стану контакту геркона

  int pirState = digitalRead(PIN_PIR);                                                    // Зчитування поточного рівня сигналу з датчика руху
  if (pirState == HIGH)                                                                   // Перевірка наявності високого рівня сигналу руху
  {                                                                                       // Перевірка виявлення руху сенсором присутності
    if (!isPirActive)                                                                     // Перевірка чи це новий рух
    {                                                                                     // Перевірка чи це перший імпульс нового руху
      isPirActive = true;                                                                 // Встановлення прапорця активної фази виявлення руху
      pirStartTimestamp = millis();                                                       // Збереження часу початку фіксації сигналу сенсором
      pirPulseCounter = 1;                                                                // Встановлення початкового значення лічильника імпульсів
      Serial.println();                                                                   // Друк порожнього рядка перед новим повідомленням
      Serial.println("[SENSOR] PIR: Motion detected!");                                   // Сповіщення про початок реєстрації імпульсу
    }                                                                                     // Кінець обробки нового руху
    else                                                                                  // Гілка продовження активного руху
    {                                                                                     // Якщо імпульс продовжує бути активним
      pirPulseCounter++;                                                                  // Збільшення кількості зареєстрованих відліків руху
    }                                                                                     // Кінець перевірки фази початку сигналу
  }                                                                                       // Кінець перевірки високого рівня руху
  else                                                                                    // Гілка відсутності сигналу руху
  {                                                                                                   // Гілка коли фізичний сигнал руху закінчився
    if (isPirActive)                                                                                  // Перевірка чи був рух щойно активним
    {                                                                                                 // Перевірка чи щойно завершилася активна фаза руху
      pirDuration = millis() - pirStartTimestamp;                                                     // Обчислення повної тривалості руху у мілісекундах
      isPirActive = false;                                                                            // Скидання прапорця поточної активності руху
      Serial.print("[SENSOR] PIR: Motion ended (");                                                   // Початок повідомлення про завершення імпульсу
      Serial.print(pirDuration);                                                                      // Виведення значення тривалості сигналу в мілісекундах
      Serial.println(" ms).");                                                                        // Повідомлення про запуск обробки алгоритмом

      long delayFromReed = -1;                                                                        // Ініціалізація змінної часової затримки від геркона
      bool isReedValidWindow = false;                                                                 // Прапорець попадання у встановлене часове вікно
      if (reedBreached)                                                                               // Перевірка чи було відкрито двері раніше
      {                                                                                               // Перевірка чи було зафіксовано попереднє відкриття дверей
        delayFromReed = (long)(pirStartTimestamp - reedBreachTimestamp);                              // Обчислення інтервалу між відкриттям дверей і рухом
        if (delayFromReed >= 0 && (unsigned long)delayFromReed <= currentPolicy.verificationWindowMs) // Перевірка потрапляння затримки у часове вікно
        {                                                                                             // Перевірка попадання в інтервал політики
          isReedValidWindow = true;                                                                   // Підтвердження узгодженого спрацювання двох сенсорів
        }                                                                                             // Кінець умови валідації часового інтервалу
      }                                                                                               // Кінець перевірки наявності розмикання дверей

      FeatureVector feat;                                                                             // Оголошення структури ознак для класифікації
      feat.reedOpen = isReedValidWindow || currentReed;                                               // Запис сукупного стану розмикання периметра
      feat.motionDurationMs = pirDuration;                                                            // Передача виміряної тривалості сигналу руху
      feat.pulseCount = pirPulseCounter;                                                              // Передача підрахованої кількості імпульсів сенсора
      feat.delayReedToPirMs = delayFromReed;                                                          // Передача часового зсуву між сенсорами

      float confidence = 0.0;                                                                         // Змінна для збереження розрахованого рівня впевненості
      SecurityClass decision = evaluateAIClassifier(feat, confidence);                                // Розрахунок моделі прийняття рішень
      notifyEvent(decision, confidence, feat);                                                        // Виклик функції сповіщення та реагування на тривогу

      if (reedBreached && (millis() - reedBreachTimestamp > currentPolicy.verificationWindowMs))      // Перевірка завершення інтервалу очікування
      {                                                                                               // Перевірка виходу за межі дозволеного вікна
        reedBreached = false;                                                                         // Скидання застарілого прапорця відкриття дверей
      }                                                                                               // Кінець перевірки актуальності вікна верифікації
    }                                                                                                 // Кінець блоку завершення сигналу руху                                                                                     // Кінець блоку завершення сигналу руху
  }                                                                                                   // Кінець блоку обробки завершення імпульсу сенсора

  if (millis() - lastTelemetryTimestamp >= 16000)                                                     // Перевірка настання регулярного шістнадцятисекундного інтервалу
  {                                                                                                   // Початок блоку періодичної відправки телеметрії
    sendToThingSpeak(lastClassification, lastConfidence, lastFeat);                                   // Відправка останнього зафіксованого стану системи
    lastTelemetryTimestamp = millis();                                                                // Оновлення таймера останнього циклу передачі даних
  }                                                                                                   // Кінець блоку регулярного оновлення телеметрії

  delay(20);                                                                                          // Затримка двадцять мілісекунд для стабільності головного циклу
}                                                                                                     // Кінець головної функції loop