#include <Wire.h>                                                                       // Підключення шини зв'язку для датчиків та екрана
#include <Adafruit_GFX.h>                                                               // Базова графічна бібліотека створення інтерфейсу
#include <Adafruit_SSD1306.h>                                                           // Драйвер керування матричним дисплеєм
#include <Adafruit_MPU6050.h>                                                           // Драйвер вимірювання вібрації та температури
#include <Adafruit_Sensor.h>                                                            // Уніфікований інтерфейс зчитування сенсорів
#include <SPI.h>                                                                        // Послідовний інтерфейс швидкісного обміну даними
#include <SD.h>                                                                         // Бібліотека обслуговування файлової системи накопичувача
#include <WiFi.h>                                                                       // Модуль бездротового зв'язку для передачі телеметрії
#include <HTTPClient.h>                                                                 // Клієнт мережевих запитів до хмарного сервісу
#include "driver/ledc.h"                                                                // Апаратний драйвер генератора звуку без системних збоїв
#include "esp_log.h"                                                                    // Керування системними повідомленнями мікроконтролера
#include "ai_diagnostic.h"                                                              // Модуль інтелектуального аналізу стану підшипника

const int PIN_LED = 4;                                                                  // Лінія керування аварійним індикатором перевантаження
const int PIN_BUZZER = 15;                                                              // Лінія підключення п'єзоелектричного випромінювача
const int PIN_SD_CS = 5;                                                                // Лінія вибору накопичувача на послідовній шині
const int SCREEN_WIDTH = 128;                                                           // Горизонтальна роздільна здатність дисплея
const int SCREEN_HEIGHT = 64;                                                           // Вертикальна роздільна здатність дисплея
const int OLED_RESET = -1;                                                              // Відсутність апаратної лінії скидання екрана

const char* WIFI_SSID = "Wokwi-GUEST";                                                  // Назва тестової бездротової мережі доступу
const char* WIFI_PASS = "";                                                             // Пароль доступу до бездротової точки зв'язку
const char* TS_API_KEY = "O0M838USU9QUI6AS";                                            // Ключ запису діагностичних даних у хмару
const char* TS_SERVER_URL = "http://api.thingspeak.com/update";                         // Адреса точки прийому телеметрії сервера

const unsigned long INTERVAL_SAMPLE_MS = 100;                                           // Період опитування сенсора коливань у мілісекундах
const unsigned long INTERVAL_DISPLAY_MS = 500;                                          // Період оновлення графічного табло у мілісекундах
const unsigned long INTERVAL_SD_LOG_MS = 3000;                                          // Період запису в чорний ящик у мілісекундах
const unsigned long INTERVAL_THINGSPEAK_MS = 15000;                                     // Період передачі даних у хмару у мілісекундах
const unsigned long INTERVAL_SERIAL_TABLE_MS = 3000;                                    // Період виводу діагностичної таблиці у мілісекундах
const unsigned long INTERVAL_BLINK_WARN_MS = 500;                                       // Інтервал блимання світлодіода у мілісекундах

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);               // Об'єкт керування графічним дисплеєм
Adafruit_MPU6050 mpu;                                                                   // Об'єкт сенсора просторового прискорення
VibrationAiEngine aiEngine;                                                             // Екземпляр процесора інтелектуальної аналітики
DiagnosticOutput currentDiag;                                                           // Поточні результати оцінки технічного стану

bool isSdReady = false;                                                                 // Прапорець успішного монтування накопичувача
bool isMpuReady = false;                                                                // Прапорець готовності сенсора коливань
bool isDisplayReady = false;                                                            // Прапорець працездатності графічного екрана
unsigned long sdRecordCount = 0;                                                        // Лічильник успішно збережених рядків журналу
int currentBuzzerFreq = 0;                                                              // Поточна встановлена частота звукового випромінювача
unsigned long lastSampleTime = 0;                                                       // Часова мітка останнього опитування сенсора
unsigned long lastDisplayTime = 0;                                                      // Часова мітка оновлення графічного дисплея
unsigned long lastSdLogTime = 0;                                                        // Часова мітка збереження на карту пам'яті
unsigned long lastCloudTime = 0;                                                        // Часова мітка передачі пакета в інтернет
unsigned long lastTableTime = 0;                                                        // Часова мітка звіту в терміналі монітора
unsigned long lastBlinkTime = 0;                                                        // Часова мітка фази миготіння індикатора
unsigned long lastSirenToggleTime = 0;                                                  // Часова мітка модуляції тривожної сирени
bool sirenToggleState = false;                                                          // Стан частотного перемикання звукової тривоги
bool ledBlinkState = false;                                                             // Логічний рівень миготливого попередження
bool lastEmergencyState = false;                                                        // Попередній стан критичної тривоги вузла
float currentAx = 0.0f;                                                                 // Миттєве прискорення вздовж першої осі
float currentAy = 0.0f;                                                                 // Миттєве прискорення вздовж другої осі
float currentAz = 9.8f;                                                                 // Миттєве прискорення вздовж вертикальної осі
float currentTemp = 25.0f;                                                              // Миттєва робоча температура обмоток двигуна
int lastHttpCode = 0;                                                                   // Результат останнього мережевого звернення

void setBuzzerTone(int freq) {                                                          // Керування генератором звуку без системних збоїв
    if (freq != currentBuzzerFreq) {                                                    // Перевірка зміни заданої робочої частоти
        currentBuzzerFreq = freq;                                                       // Оновлення збереженого значення поточної частоти
        if (freq > 0) {                                                                 // Умова увімкнення генерації звукового коливання
            ledc_set_freq(LEDC_LOW_SPEED_MODE, LEDC_TIMER_0, freq);                     // Встановлення робочої частоти таймера
            ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 512);                    // Встановлення шпаруватості сигналу п'ятдесят відсотків
            ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);                      // Активація згенерованого імпульсу на контакті
        } else {                                                                        // Умова повного вимкнення звукового сигналу
            ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);                      // Обнулення шпаруватості сигналу випромінювача
            ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);                      // Зупинка подачі сигналу на звуковий канал
        }                                                                               // Завершення розгалуження керування випромінювачем
    }                                                                                   // Завершення перевірки зміни робочої частоти
}                                                                                       // Завершення функції керування випромінювачем

void formatTime(unsigned long totalMs, char* buffer, size_t maxLen) {                   // Форматування мілісекунд у текстовий годинний вигляд
    unsigned long totalSec = totalMs / 1000;                                            // Переведення відліку у цілі секунди
    unsigned long sec = totalSec % 60;                                                  // Розрахунок поточної секунди хвилини
    unsigned long totalMin = totalSec / 60;                                             // Переведення відліку у цілі хвилини
    unsigned long min = totalMin % 60;                                                  // Розрахунок поточної хвилини години
    unsigned long hours = totalMin / 60;                                                // Розрахунок повної кількості годин роботи
    snprintf(buffer, maxLen, "%02lu:%02lu:%02lu", hours, min, sec);                     // Запис текстового рядка фіксованої довжини
}                                                                                       // Завершення функції перетворення часу

void logEvent(const char* category, const char* message) {                              // Друк журналу подій суворо з лівого краю термінала
    char timeBuf[16];                                                                   // Тимчасовий буфер для форматування мітки часу
    formatTime(millis(), timeBuf, sizeof(timeBuf));                                     // Отримання форматованого часу від старту системи
    Serial.print("[");                                                                  // Друк початкової дужки часового штампа
    Serial.print(timeBuf);                                                              // Друк значення поточного часу роботи системи
    Serial.print("] [");                                                                // Друк роздільника мітки та категорії події
    Serial.print(category);                                                             // Друк назви функціональної підсистеми у журналі
    Serial.print("] ");                                                                 // Друк кінцевого роздільника заголовка події
    Serial.println(message);                                                            // Друк інформаційного тексту зареєстрованої події
    Serial.flush();                                                                     // Примусове проштовхування символів у вихідний потік
}                                                                                       // Завершення функції реєстрації подій системи

void printDataTable() {                                                                 // Періодичний вивід структурованої таблиці параметрів
    char timeBuf[16];                                                                   // Буфер для форматування поточного часу роботи
    formatTime(millis(), timeBuf, sizeof(timeBuf));                                     // Форматування часу поточної діагностичної ітерації
    Serial.println();                                                                   // Порожній рядок для візуального розділення блоків
    Serial.println("--------------------------------------------------");               // Розділювальна лінія блоку діагностичних даних
    Serial.print("Uptime: ");                                                           // Вивід назви параметра тривалості роботи англійською
    Serial.println(timeBuf);                                                            // Друк поточного часу функціонування генератора
    Serial.print("Acceleration X: ");                                                   // Вивід першої осі вібраційного навантаження
    Serial.print(currentAx, 2);                                                         // Друк значення прискорення першої просторової осі
    Serial.print(" Y: ");                                                               // Вивід назви другої осі навантаження
    Serial.print(currentAy, 2);                                                         // Друк значення прискорення другої осі
    Serial.print(" Z: ");                                                               // Вивід назви вертикальної осі гравітації
    Serial.print(currentAz, 2);                                                         // Друк значення вертикального прискорення
    Serial.println(" m/s2");                                                            // Одиниці вимірювання прискорення англійською
    Serial.print("Vibration RMS: ");                                                    // Вивід середньоквадратичного значення амплітуди
    Serial.print(currentDiag.vibrationRms, 2);                                          // Друк розрахованого інтегрального фону вібрацій
    Serial.println(" m/s2");                                                            // Одиниці вимірювання вібраційного прискорення
    Serial.print("Vibration Variance: ");                                               // Вивід статистичної дисперсії сигналу англійською
    Serial.println(currentDiag.variance, 3);                                            // Друк числового значення розкиду амплітуд
    Serial.print("Harmonic Energy: ");                                                  // Вивід енергії гармонік мікротріщин англійською
    Serial.println(currentDiag.harmonicEnergy, 3);                                      // Друк показника прихованих дефектів металу
    Serial.print("Bearing Temperature: ");                                              // Вивід температури вузла англійською мовою
    Serial.print(currentDiag.temperature, 1);                                           // Друк числового значення температури корпусу
    Serial.println(" C");                                                               // Позначення градусів Цельсія англійською
    Serial.print("Bearing Wear Index: ");                                               // Вивід індексу деградації вузла англійською
    Serial.print(currentDiag.bearingWearIndex, 1);                                      // Друк відсотка механічного зносу обладнання
    Serial.println(" %");                                                               // Позначення відсоткової шкали оцінки стану
    Serial.print("Remaining Useful Life: ");                                            // Вивід залишкового ресурсу англійською мовою
    Serial.print(currentDiag.remainingUsefulLife, 1);                                   // Друк залишкового ресурсу генератора
    Serial.println(" %");                                                               // Символ відсотків для залишкового ресурсу
    Serial.print("Health State: ");                                                     // Вивід технічного стану англійською мовою
    Serial.println(currentDiag.stateLabel);                                             // Друк текстового висновку діагностичного модуля
    Serial.print("Black Box Records: ");                                                // Вивід лічильника чорного ящика англійською
    Serial.println(sdRecordCount);                                                      // Друк загальної кількості збережених звітів
    Serial.print("Cloud Telemetry Status: ");                                           // Вивід стану зв'язку з хмарою англійською
    if (lastHttpCode == 200) {                                                          // Перевірка коду успішної доставки даних у хмару
        Serial.println("Delivered 200 OK");                                             // Англійський звіт про успішну доставку пакета
    } else if (lastHttpCode > 0) {                                                      // Перевірка наявності помилки відповіді сервера
        Serial.print("HTTP Error ");                                                    // Англійське повідомлення про помилку зв'язку
        Serial.println(lastHttpCode);                                                   // Друк числового коду помилки сервера
    } else {                                                                            // Умова очікування наступного циклу відправки
        Serial.println("Awaiting Next Uplink");                                         // Англійське повідомлення про паузу між циклами
    }                                                                                   // Завершення аналізу мережевого статусу
    Serial.println("--------------------------------------------------");               // Замикаюча горизонтальна лінія таблиці даних
    Serial.flush();                                                                     // Примусовий вивід сформованої таблиці у консоль
}                                                                                       // Завершення функції друку таблиці телеметрії

void initBuzzerHardware() {                                                             // Налаштування апаратного таймера для звукової сирени
    esp_log_level_set("*", ESP_LOG_NONE);                                               // Вимкнення загальних системних повідомлень ядра
    esp_log_level_set("ledc", ESP_LOG_NONE);                                            // Придушення внутрішніх повідомлень модуля генератора
    ledc_timer_config_t timerConf = {                                                   // Структура конфігурації апаратного таймера
        .speed_mode = LEDC_LOW_SPEED_MODE,                                              // Низькошвидкісний режим роботи каналів таймера
        .duty_resolution = LEDC_TIMER_10_BIT,                                           // Роздільна здатність шпаруватості десять біт
        .timer_num = LEDC_TIMER_0,                                                      // Використання нульового апаратного таймера
        .freq_hz = 2000,                                                                // Базова робоча частота генератора звуку
        .clk_cfg = LEDC_AUTO_CLK                                                        // Автоматичний вибір тактового джерела сигналу
    };                                                                                  // Завершення структури налаштування таймера
    ledc_timer_config(&timerConf);                                                      // Застосування налаштувань апаратного таймера
    ledc_channel_config_t chanConf = {                                                  // Структура підключення виводу до каналу генератора
        .gpio_num = PIN_BUZZER,                                                         // Номер фізичного виводу звукового випромінювача
        .speed_mode = LEDC_LOW_SPEED_MODE,                                              // Швидкісний режим вибраного звукового каналу
        .channel = LEDC_CHANNEL_0,                                                      // Використання першого каналу генератора імпульсів
        .intr_type = LEDC_INTR_DISABLE,                                                 // Вимкнення переривань для звукового каналу
        .timer_sel = LEDC_TIMER_0,                                                      // Прив'язка каналу до налаштованого таймера
        .duty = 0,                                                                      // Початкова нульова шпаруватість для тиші
        .hpoint = 0                                                                     // Початкова фаза зміщення форми сигналу
    };                                                                                  // Завершення конфігурації каналу випромінювача
    ledc_channel_config(&chanConf);                                                     // Ініціалізація каналу керування звуковим сигналом
}                                                                                       // Завершення налаштування випромінювача звуку

void initStorage() {                                                                    // Ініціалізація та підготовка карти пам'яті
    SPI.begin(18, 19, 23, PIN_SD_CS);                                                   // Ініціалізація послідовної шини для накопичувача
    pinMode(PIN_SD_CS, OUTPUT);                                                         // Налаштування виводу вибору мікросхеми на вихід
    digitalWrite(PIN_SD_CS, HIGH);                                                      // Встановлення пасивного рівня на лінії вибору
    delay(50);                                                                          // Коротка пауза перехідних процесів шини пам'яті
    if (SD.begin(PIN_SD_CS, SPI, 4000000)) {                                            // Спроба монтування накопичувача на стабільній частоті
        isSdReady = true;                                                               // Підтвердження готовності файлової системи
        if (!SD.exists("/generator_log.csv")) {                                         // Перевірка наявності головного файлу чорного ящика
            File logFile = SD.open("/generator_log.csv", FILE_WRITE);                   // Створення нового файлу журналу для запису заголовка
            if (logFile) {                                                              // Перевірка успішного відкриття файлу для запису
                logFile.print("Time_ms,RMS,Variance,Temp_C,");                          // Запис першої частини колонок заголовка таблиці
                logFile.println("Wear_pct,RUL_pct,Status,Alarm");                       // Запис другої частини колонок заголовка таблиці
                logFile.close();                                                        // Закриття створеного файлу чорного ящика
            }                                                                           // Завершення перевірки файлу журналу
        }                                                                               // Завершення перевірки наявності файлу чорного ящика
        logEvent("STORAGE", "MicroSD black box storage ready");                         // Підтвердження готовності локального носія
    } else {                                                                            // Обробка ситуації відсутності карти пам'яті
        isSdReady = false;                                                              // Фіксація автономного режиму без локального носія
        logEvent("STORAGE", "MicroSD card offline, autonomous RAM mode");               // Сповіщення про роботу системи в оперативній пам'яті
    }                                                                                   // Завершення перевірки статусу монтування
}                                                                                       // Завершення ініціалізації чорного ящика

void initSensors() {                                                                    // Ініціалізація сенсора вібрації та температури
    if (mpu.begin()) {                                                                  // Спроба запуску сенсора за стандартною адресою
        isMpuReady = true;                                                              // Підтвердження готовності сенсора вимірювань
        mpu.setAccelerometerRange(MPU6050_RANGE_8_G);                                   // Встановлення діапазону вимірювання вісім g
        mpu.setGyroRange(MPU6050_RANGE_500_DEG);                                        // Діапазон кутової швидкості п'ятсот градусів
        mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);                                     // Встановлення смуги цифрового фільтра шуму
        logEvent("HARDWARE", "Sensors and display online");                             // Реєстрація готовності вимірювального комплексу
    } else {                                                                            // Обробка помилки підключення сенсора руху
        isMpuReady = false;                                                             // Встановлення прапорця несправності сенсора
        logEvent("HARDWARE", "Sensors simulated, display online");                      // Реєстрація переходу на тестові показники вібрацій
    }                                                                                   // Завершення перевірки доступності сенсора
}                                                                                       // Завершення запуску датчика вібрацій

void initDisplayModule() {                                                              // Ініціалізація матричного дисплея панелі керування
    if (display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {                                    // Запуск контролера екрана з внутрішнім помпуванням
        isDisplayReady = true;                                                          // Підтвердження успішної ініціалізації матриці
        display.clearDisplay();                                                         // Очищення відеопам'яті перед першим виводом
        display.setTextColor(SSD1306_WHITE);                                            // Встановлення білого кольору відображення символів
        display.setTextSize(1);                                                         // Встановлення базового одинарного розміру шрифту
        display.setCursor(0, 0);                                                        // Переміщення курсора у верхній лівий кут екрана
        display.println("DIESEL GEN MONITOR");                                          // Вивід назви системи на екран приладу
        display.println("Diagnostic system");                                           // Вивід підзаголовка діагностичного комплексу
        display.println("Booting sensors...");                                          // Повідомлення про запуск вимірювальних каналів
        display.display();                                                              // Передача сформованого кадру у пам'ять екрана
    } else {                                                                            // Обробка збою запуску графічного дисплея
        isDisplayReady = false;                                                         // Фіксація відмови роботи матричного дисплея
    }                                                                                   // Завершення перевірки готовності екрана
}                                                                                       // Завершення налаштування графічного дисплея

void initNetwork() {                                                                    // Підключення до бездротової мережі зв'язку
    WiFi.mode(WIFI_STA);                                                                // Встановлення клієнтського режиму бездротового модуля
    WiFi.begin(WIFI_SSID, WIFI_PASS);                                                   // Запуск з'єднання з бездротовою точкою доступу
    Serial.print("Connecting to WiFi ");                                                // Початок виводу повідомлення про підключення
    unsigned long startAttempt = millis();                                              // Фіксація часу старту спроби з'єднання з мережею
    while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 4000) {           // Цикл очікування встановлення з'єднання з лімітом
        delay(200);                                                                     // Коротка пауза між перевірками зв'язку
        Serial.print(".");                                                              // Друк крапки індикації прогресу підключення
    }                                                                                   // Завершення очікування бездротового з'єднання
    Serial.println();                                                                   // Перехід на новий рядок після завершення спроби
    Serial.flush();                                                                     // Проштовхування повідомлення про стан мережі
    if (WiFi.status() == WL_CONNECTED) {                                                // Перевірка результату підключення до мережі
        String ipMsg = String("WiFi connected. IP: ") + WiFi.localIP().toString();      // Формування тексту з отриманою адресою пристрою
        logEvent("NETWORK", ipMsg.c_str());                                             // Реєстрація адреси пристрою у системному журналі
    } else {                                                                            // Обробка випадку відсутності зв'язку
        logEvent("NETWORK", "WiFi connection timed out, autonomous mode");              // Перехід системи в автономний режим без мережі
    }                                                                                   // Завершення аналізу підключення до мережі
}                                                                                       // Завершення процедури запуску зв'язку

void updateDisplay() {                                                                  // Оновлення графічного інтерфейсу та шкал навантаження
    if (!isDisplayReady) return;                                                        // Вихід при відсутності працездатного дисплея
    display.clearDisplay();                                                             // Очищення графічного буфера перед малюванням
    display.setTextSize(1);                                                             // Встановлення базового шрифту інформаційних написів
    display.setCursor(0, 0);                                                            // Позиціювання курсора на верхній рядок екрана
    display.print("GEN #1: ");                                                          // Вивід ідентифікатора контрольованого генератора
    display.println(currentDiag.stateLabel);                                            // Друк поточного стану безпеки великими літерами
    display.drawFastHLine(0, 10, 128, SSD1306_WHITE);                                   // Горизонтальна лінія відокремлення заголовка
    display.setCursor(0, 13);                                                           // Позиціювання курсора для виводу вібрацій
    display.print("Vib: ");                                                             // Текстова мітка поточної амплітуди вібрацій
    display.print(currentDiag.vibrationRms, 2);                                         // Друк значення середньоквадратичного прискорення
    display.println(" m/s2");                                                           // Одиниці вимірювання вібрацій на дисплеї
    display.drawRect(0, 23, 128, 6, SSD1306_WHITE);                                     // Рамка графічної шкали навантаження вузла
    int barWidth = (int)((currentDiag.vibrationRms / 6.0f) * 124.0f);                   // Розрахунок заповнення графічного стовпчика шкали
    if (barWidth > 124) barWidth = 124;                                                 // Обмеження максимальної ширини смужки навантаження
    if (barWidth > 0) {                                                                 // Перевірка наявності ненульового значення для шкали
        display.fillRect(2, 25, barWidth, 2, SSD1306_WHITE);                            // Заповнення внутрішньої частини смужки навантаження
    }                                                                                   // Завершення малювання графічного стовпчика
    display.setCursor(0, 32);                                                           // Встановлення позиції для виводу температури
    display.print("T: ");                                                               // Скорочена мітка робочої температури підшипника
    display.print(currentDiag.temperature, 1);                                          // Друк числового значення температури вузла
    display.print("C  Var: ");                                                          // Мітка статистичної дисперсії коливань
    display.println(currentDiag.variance, 2);                                           // Друк числового значення дисперсії вібрацій
    display.setCursor(0, 43);                                                           // Встановлення позиції показників залишкового життя
    display.print("Wear:");                                                             // Мітка розрахованого індексу зносу підшипника
    display.print((int)currentDiag.bearingWearIndex);                                   // Друк цілого відсотка деградації агрегата
    display.print("% RUL:");                                                            // Мітка залишкового експлуатаційного ресурсу
    display.print((int)currentDiag.remainingUsefulLife);                                // Друк відсотка залишкового часу функціонування
    display.println("%");                                                               // Знак відсотка для залишкового ресурсу вузла
    display.drawFastHLine(0, 52, 128, SSD1306_WHITE);                                   // Нижня розділювальна лінія службового підвалу
    display.setCursor(0, 55);                                                           // Позиціювання курсора на службовий підвал екрана
    display.print("SD:");                                                               // Мітка статусу карти пам'яті чорного ящика
    display.print(isSdReady ? "OK " : "ERR ");                                          // Відображення стану накопичувача даних
    display.print("NET:");                                                              // Мітка статусу бездротового підключення
    display.print(WiFi.status() == WL_CONNECTED ? "ONLINE" : "OFFLINE");                // Відображення статусу зв'язку з хмарним сервісом
    display.display();                                                                  // Виведення підготовленого кадру на фізичний дисплей
}                                                                                       // Завершення процедури відмальовування інтерфейсу

void saveBlackBoxLog(bool forceEmergency) {                                             // Запис телеметричного журналу на накопичувач
    if (!isSdReady) return;                                                             // Вихід при недоступності накопичувача пам'яті
    File logFile = SD.open("/generator_log.csv", FILE_APPEND);                          // Відкриття файлу журналу в режимі дописування рядка
    if (logFile) {                                                                      // Перевірка успішного відкриття файлу накопичувача
        logFile.print(millis());                                                        // Запис поточної системної часової мітки у мілісекундах
        logFile.print(",");                                                             // Друк роздільника стовпчиків формату таблиці
        logFile.print(currentDiag.vibrationRms, 2);                                     // Запис значення середньоквадратичної вібрації
        logFile.print(",");                                                             // Друк коміркового роздільника між даними
        logFile.print(currentDiag.variance, 3);                                         // Запис величини дисперсії вібраційного навантаження
        logFile.print(",");                                                             // Друк коми між колонками звіту чорного ящика
        logFile.print(currentDiag.temperature, 1);                                      // Запис виміряної температури вузла двигуна
        logFile.print(",");                                                             // Друк коми розділення параметрів телеметрії
        logFile.print(currentDiag.bearingWearIndex, 1);                                 // Запис розрахованого індексу зносу підшипника
        logFile.print(",");                                                             // Друк коми перед залишковим ресурсом обладнання
        logFile.print(currentDiag.remainingUsefulLife, 1);                              // Запис залишкового ресурсу експлуатації агрегата
        logFile.print(",");                                                             // Друк коми перед текстовим статусом вузла
        logFile.print(currentDiag.stateLabel);                                          // Запис класифікованого робочого стану
        logFile.print(",");                                                             // Друк коми перед прапорцем аварійного стану
        logFile.println(currentDiag.emergencyAlert ? "EMERGENCY_ALARM" : "NORMAL");     // Запис аварійної мітки або підтвердження норми
        logFile.flush();                                                                // Примусовий запис буфера даних на фізичний носій
        logFile.close();                                                                // Закриття дескриптора файлу після успішного запису
        sdRecordCount++;                                                                // Інкремент лічильника успішно збережених звітів
        if (forceEmergency) {                                                           // Перевірка необхідності екстреної фіксації у терміналі
            logEvent("STORAGE", "Emergency crash mark written to black box");           // Сповіщення про фіксацію аварійної мітки у лозі
        }                                                                               // Завершення перевірки екстреного запису
    } else {                                                                            // Обробка помилки доступу до файлу журналу
        logEvent("STORAGE", "Failed to open log file for write");                       // Реєстрація збою відкриття файлу для запису
    }                                                                                   // Завершення перевірки дескриптора файлу
}                                                                                       // Завершення функції збереження чорного ящика

void dumpLogFile() {                                                                    // Виведення вмісту чорного ящика у термінал
    if (!isSdReady) {                                                                   // Перевірка готовності накопичувача пам'яті
        logEvent("STORAGE", "MicroSD card offline");                                    // Повідомлення про відсутність накопичувача
        return;                                                                         // Вихід за відсутності накопичувача пам'яті
    }                                                                                   // Завершення перевірки статусу накопичувача
    File logFile = SD.open("/generator_log.csv", FILE_READ);                            // Відкриття файлу чорного ящика для читання
    if (logFile) {                                                                      // Перевірка успішного відкриття файлу звіту
        logEvent("STORAGE", "Dumping black box log to terminal");                       // Реєстрація старту вивантаження журналу
        Serial.println();                                                               // Друк порожнього рядка перед виводом даних
        while (logFile.available()) {                                                   // Цикл зчитування всіх наявних байтів файлу
            Serial.write(logFile.read());                                               // Виведення чергового символу прямо у консоль
        }                                                                               // Завершення циклу читання файлу накопичувача
        logFile.close();                                                                // Закриття файлу після завершення читання
        Serial.println();                                                               // Відступ порожнім рядком після таблиці
        logEvent("STORAGE", "Black box dump completed");                                // Підтвердження успішного вивантаження даних
    } else {                                                                            // Обробка помилки доступу до файлу журналу
        logEvent("STORAGE", "Failed to open log file for read");                        // Реєстрація збою відкриття файлу для читання
    }                                                                                   // Завершення аналізу відкриття файлу даних
}                                                                                       // Завершення функції вивантаження чорного ящика

void sendCloudTelemetry() {                                                             // Передача діагностичних даних у хмару ThingSpeak
    if (WiFi.status() != WL_CONNECTED) return;                                          // Скасування відправки за відсутності мережевого зв'язку
    HTTPClient http;                                                                    // Створення об'єкта клієнта протоколу передачі даних
    String url = String(TS_SERVER_URL) + "?api_key=" + TS_API_KEY;                      // Формування базової адреси сервера з ключем доступу
    url += "&field1=" + String(currentDiag.vibrationRms, 2);                            // Додавання першого поля з вібраційним фоном
    url += "&field2=" + String(currentDiag.variance, 3);                                // Додавання другого поля з дисперсією коливань
    url += "&field3=" + String(currentDiag.temperature, 1);                             // Додавання третього поля з температурою металу
    url += "&field4=" + String(currentDiag.bearingWearIndex, 1);                        // Додавання четвертого поля зі зносом підшипника
    url += "&field5=" + String(currentDiag.remainingUsefulLife, 1);                     // Додавання п'ятого поля із залишковим ресурсом
    url += "&field6=" + String((int)currentDiag.healthState);                           // Додавання шостого поля з числовим кодом статусу
    http.begin(url);                                                                    // Ініціалізація мережевого запиту за сформованою адресою
    int httpCode = http.GET();                                                          // Виконання мережевого запиту передачі даних
    lastHttpCode = httpCode;                                                            // Збереження коду відповіді для перегляду диспетчером
    if (httpCode == HTTP_CODE_OK) {                                                     // Перевірка успішного отримання відповіді сервером
        logEvent("CLOUD", "Telemetry packet sent to ThingSpeak successfully");          // Реєстрація успішної доставки даних у хмару
    } else {                                                                            // Обробка мережевої помилки зв'язку з хмарою
        String errStr = String("ThingSpeak delivery error: ") + String(httpCode);       // Формування тексту з числовим кодом помилки
        logEvent("CLOUD", errStr.c_str());                                              // Реєстрація збою надсилання даних у системному лозі
    }                                                                                   // Завершення аналізу результату мережевого запиту
    http.end();                                                                         // Звільнення мережевих ресурсів після завершення передачі
}                                                                                       // Завершення функції відправки хмарної телеметрії

void handleAlarmActuators() {                                                           // Керування світловою індикацією та аварійною сиреною
    if (currentDiag.healthState == HEALTH_CRITICAL) {                                   // Умова активації невідкладного аварійного режиму
        digitalWrite(PIN_LED, HIGH);                                                    // Постійне увімкнення червоного аварійного індикатора
        if (millis() - lastSirenToggleTime >= 250) {                                    // Перевірка інтервалу чергування тональностей сирени
            lastSirenToggleTime = millis();                                             // Оновлення часової мітки зміни звукового тону
            sirenToggleState = !sirenToggleState;                                       // Інверсія прапорця чергування висоти звучання
            if (sirenToggleState) {                                                     // Умова переходу на вищу частоту аварійної сирени
                setBuzzerTone(2400);                                                    // Генерація звукової хвилі високого аварійного тону
            } else {                                                                    // Умова переходу на нижчу частоту аварійної сирени
                setBuzzerTone(1700);                                                    // Генерація звукової хвилі низького аварійного тону
            }                                                                           // Завершення вибору висоти звукового сигналу тривоги
        }                                                                               // Завершення інтервальної модуляції звукової сирени
        if (!lastEmergencyState) {                                                      // Фіксація моменту переходу у критичний аварійний стан
            lastEmergencyState = true;                                                  // Збереження активного статусу критичної небезпеки
            logEvent("ALARM", "CRITICAL OVERLOAD: emergency siren active");             // Реєстрація активації сирени у журналі подій
            saveBlackBoxLog(true);                                                      // Негайний позачерговий аварійний запис на карту пам'яті
        }                                                                               // Завершення фіксації моменту виникнення тривоги
    } else if (currentDiag.healthState == HEALTH_WARNING) {                             // Умова фіксації підвищеного зносу обладнання
        setBuzzerTone(0);                                                               // Вимкнення звукового сигналу у режимі попередження
        lastEmergencyState = false;                                                     // Скидання прапорця невідкладної аварійної ситуації
        if (millis() - lastBlinkTime >= INTERVAL_BLINK_WARN_MS) {                       // Перевірка інтервалу миготіння світлодіода уваги
            lastBlinkTime = millis();                                                   // Оновлення часової мітки зміни фази миготіння
            ledBlinkState = !ledBlinkState;                                             // Інверсія логічного стану світлового випромінювача
            digitalWrite(PIN_LED, ledBlinkState ? HIGH : LOW);                          // Подача сигналу на світлодіод попередження
        }                                                                               // Завершення інтервального перемикання світлодіода
    } else {                                                                            // Умова безпечної штатної роботи енергетичного вузла
        setBuzzerTone(0);                                                               // Вимкнення звукової сирени у безпечному робочому стані
        digitalWrite(PIN_LED, LOW);                                                     // Вимкнення червоного світлодіода у безпечному режимі
        lastEmergencyState = false;                                                     // Підтвердження відсутності аварійного перевантаження
    }                                                                                   // Завершення керування силовими актуаторами системи
}                                                                                       // Завершення функції керування сигналізацією захисту

void setup() {                                                                          // Початкова ініціалізація апаратних вузлів та зв'язку
    Serial.begin(115200);                                                               // Ініціалізація термінального інтерфейсу нагляду
    delay(1000);                                                                        // Пауза стабілізації та підключення термінала монітора
    Serial.println();                                                                   // Відступ порожнім рядком перед виводом головного банера
    Serial.println("=================================================");                // Верхня горизонтальна рамка вітального банера
    Serial.println("Industrial Machinery Vibration Diagnostic Monitor");                // Назва інженерного проєкту моніторингу обладнання
    Serial.println("=================================================");                // Нижня горизонтальна рамка вітального банера
    Serial.println();                                                                   // Відступ порожнім рядком після рамки проєкту
    Serial.flush();                                                                     // Примусове проштовхування символів заголовка у термінал
    pinMode(PIN_LED, OUTPUT);                                                           // Налаштування лінії червоного світлодіода на вихід
    digitalWrite(PIN_LED, LOW);                                                         // Початковий низький рівень навантажувального індикатора
    initBuzzerHardware();                                                               // Запуск апаратного каналу керування звуковою сиреною
    Wire.begin(21, 22);                                                                 // Ініціалізація шини зв'язку на визначених лініях
    initDisplayModule();                                                                // Запуск графічного дисплея панелі моніторингу
    initSensors();                                                                      // Підключення та первинне налаштування датчика вібрацій
    initStorage();                                                                      // Підготовка карти пам'яті для автономного чорного ящика
    initNetwork();                                                                      // Встановлення бездротового з'єднання для телеметрії
    logEvent("SYSTEM", "Machinery health monitor ready");                               // Підтвердження завершення повної ініціалізації приладу
    currentDiag = aiEngine.analyze();                                                   // Первинне обчислення стану вузла перед початком циклу
    lastTableTime = millis();                                                           // Фіксація часу старту для першого періодичного звіту
}                                                                                       // Завершення процедури початкового налаштування

void loop() {                                                                           // Головний неблокуючий цикл керування та аналітики
    unsigned long currentMillis = millis();                                             // Отримання поточного значення системного часу
    if (currentMillis - lastSampleTime >= INTERVAL_SAMPLE_MS) {                         // Перевірка настання інтервалу опитування датчика
        lastSampleTime = currentMillis;                                                 // Оновлення часової мітки зняття показань сенсора
        if (isMpuReady) {                                                               // Перевірка працездатності вимірювального модуля
            sensors_event_t a, g, temp;                                                 // Оголошення структур даних вимірювальних каналів
            mpu.getEvent(&a, &g, &temp);                                                // Одночасне зчитування прискорень та поточної температури
            float noiseX = ((float)random(-10, 11)) * 0.008f;                           // Генерація мікрошуму першої осі вібрацій двигуна
            float noiseY = ((float)random(-10, 11)) * 0.008f;                           // Генерація мікрошуму другої осі коливань підшипника
            float noiseZ = ((float)random(-10, 11)) * 0.008f;                           // Генерація мікрошуму вертикальної осі механізму
            currentAx = a.acceleration.x + noiseX;                                      // Додавання фонового тремтіння першої просторової осі
            currentAy = a.acceleration.y + noiseY;                                      // Додавання фонового тремтіння другої просторової осі
            currentAz = a.acceleration.z + noiseZ;                                      // Додавання фонового тремтіння вертикальної осі сили
            currentTemp = temp.temperature;                                             // Фіксація температури корпусу працюючого вузла
            aiEngine.addSample(currentAx, currentAy, currentAz, currentTemp);           // Передача виміряних значень у процесор штучного інтелекту
        } else {                                                                        // Імітація тестових параметрів при збої сенсора
            currentAx = 0.05f;                                                          // Базовий рівень фонових коливань першої осі
            currentAy = 0.05f;                                                          // Базовий рівень фонових коливань другої осі
            currentAz = 9.81f;                                                          // Базове земне тяжіння для компенсації гравітації
            currentTemp = 42.0f;                                                        // Штатна температура вузла при тестовому режимі
            aiEngine.addSample(currentAx, currentAy, currentAz, currentTemp);           // Додавання базових тестових даних у буфер модуля ШІ
        }                                                                               // Завершення опрацювання вимірювального кроку
        currentDiag = aiEngine.analyze();                                               // Запуск математичного алгоритму інтелектуального оцінювання
    }                                                                                   // Завершення інтервального вимірювання вібрацій
    handleAlarmActuators();                                                             // Безперервне керування індикатором та звуковою тривогою
    if (Serial.available() > 0) {                                                       // Перевірка наявності вхідних символів у черзі
        String cmd = Serial.readStringUntil('\n');                                      // Зчитування надісланого текстового повідомлення
        cmd.trim();                                                                     // Очищення команди від символів переходу рядка
        if (cmd.equalsIgnoreCase("dump") || cmd.equalsIgnoreCase("log")) {              // Порівняння команди із ключовими словами запиту
            dumpLogFile();                                                              // Запуск вивантаження журналу чорного ящика
        }                                                                               // Завершення перевірки ключового слова запиту
    }                                                                                   // Завершення обробки команд послідовного порту
    if (currentMillis - lastDisplayTime >= INTERVAL_DISPLAY_MS) {                       // Перевірка настання моменту оновлення графічного екрана
        lastDisplayTime = currentMillis;                                                // Оновлення часової мітки виводу інтерфейсу приладу
        updateDisplay();                                                                // Відмальовування актуальних інженерних шкал на екрані
    }                                                                                   // Завершення таймера оновлення зображення на дисплеї
    if (currentMillis - lastSdLogTime >= INTERVAL_SD_LOG_MS) {                          // Перевірка періоду запису журналу чорного ящика
        lastSdLogTime = currentMillis;                                                  // Оновлення часової мітки збереження даних на картку
        saveBlackBoxLog(false);                                                         // Плановий циклічний запис телеметрії на накопичувач
    }                                                                                   // Завершення таймера збереження журналу чорного ящика
    if (currentMillis - lastCloudTime >= INTERVAL_THINGSPEAK_MS) {                      // Перевірка інтервалу відправки телеметрії в хмару
        lastCloudTime = currentMillis;                                                  // Оновлення часової мітки передачі пакета в інтернет
        sendCloudTelemetry();                                                           // Відправка пакетного звіту на хмарну платформу моніторингу
    }                                                                                   // Завершення таймера зв'язку з хмарним сервісом
    if (currentMillis - lastTableTime >= INTERVAL_SERIAL_TABLE_MS) {                    // Перевірка періоду друку таблиці телеметрії у консоль
        lastTableTime = currentMillis;                                                  // Оновлення часової мітки виводу таблиці параметрів
        printDataTable();                                                               // Формування та вивід структурованої таблиці замірів
    }                                                                                   // Завершення таймера виводу діагностичної таблиці
}                                                                                       // Завершення головного циклу функціонування системи