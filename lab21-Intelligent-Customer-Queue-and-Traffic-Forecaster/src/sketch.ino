#include <Arduino.h>                                                                    // базова бібліотека функцій мікроконтролера
#include <Wire.h>                                                                       // апаратний інтерфейс двопровідної шини даних
#include <LiquidCrystal_I2C.h>                                                          // драйвер символьного дисплея клієнтського табло
#include <WiFi.h>                                                                       // модуль бездротового мережевого підключення
#include <WebServer.h>                                                                  // локальний веб сервер онлайн табло електронної черги
#include "driver/ledc.h"                                                                // апаратний генератор звукового сигналу виклику
#include "esp_log.h"                                                                    // модуль керування системними повідомленнями ядра
#include "ai_traffic_forecast.h"                                                        // модуль штучного інтелекту прогнозування черги

#define PIN_BTN_REGISTER 18                                                             // кнопка реєстрації нового клієнта та видачі талона
#define PIN_BTN_SERVE 19                                                                // кнопка виклику та обслуговування чергового клієнта
#define PIN_BUZZER 27                                                                   // лінія керування п єзоелектричним зумером дзвінка

const char* WIFI_SSID = "Wokwi-GUEST";                                                  // назва бездротової точки доступу симулятора
const char* WIFI_PASS = "";                                                             // пароль підключення до бездротової мережі

LiquidCrystal_I2C lcd(0x27, 16, 2);                                                     // об єкт керування символьним екраном термінала
WebServer server(80);                                                                   // об єкт локального веб сервера на стандартному порту
QueueTrafficAi aiQueue;                                                                 // екземпляр моделі штучного інтелекту прогнозу черги
QueueAiForecast currentAi;                                                              // структура поточних розрахунків прогнозу трафіку

int currentBuzzerFreq = 0;                                                              // поточна встановлена частота звукового зумера
unsigned long buzzerStopMs = 0;                                                         // момент автоматичного вимкнення звукового дзвінка
unsigned long lastDisplayMs = 0;                                                        // мітка часу оновлення інформації на дисплеї
unsigned long lastSerialTableMs = 0;                                                    // мітка часу виводу таблиці у термінал

void setBuzzerTone(int freq) {                                                          // безпечне керування частотою зумера через таймер
  if (freq != currentBuzzerFreq) {                                                      // перевірка зміни заданої частоти звучання
    currentBuzzerFreq = freq;                                                           // збереження нової активної частоти дзвінка
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

void updateLcdScreen() {                                                                // оновлення номерів талонів та прогнозу на дисплеї
  char line1[17];                                                                       // буфер першого рядка символьного екрана
  char line2[17];                                                                       // буфер другого рядка символьного екрана
  snprintf(line1, sizeof(line1), "NOW:A%02d LAST:A%02d",                                // шаблон рядка викликаного та останнього талона
           currentAi.currentServedTicket, currentAi.lastIssuedTicket);                  // підстановка номерів поточного і виданого талонів
  snprintf(line2, sizeof(line2), "Q:%2d WAIT:%4.1fm",                                   // шаблон рядка довжини черги і часу очікування
           currentAi.activeQueueCount, currentAi.estWaitMinutes);                       // підстановка кількості людей та хвилин прогнозу

  lcd.setCursor(0, 0);                                                                  // встановлення курсора на початок першого рядка
  lcd.print(line1);                                                                     // вивід номерів активного та виданого талонів
  lcd.print("  ");                                                                      // очищення залишкових символів першого рядка
  lcd.setCursor(0, 1);                                                                  // встановлення курсора на початок другого рядка
  lcd.print(line2);                                                                     // вивід розміру черги та прогнозу очікування
  lcd.print("  ");                                                                      // очищення залишкових символів другого рядка
}                                                                                       // завершення функції оновлення символьного екрана

void handleNewTicketAction() {                                                          // обробка реєстрації нового клієнта у черзі
  int ticketNum = aiQueue.registerNewClient();                                          // видача нового номера талона у моделі
  currentAi = aiQueue.evaluate();                                                       // перерахунок прогнозу очікування та трафіку
  updateLcdScreen();                                                                    // миттєве оновлення інформації на дисплеї табло
  setBuzzerTone(1600);                                                                  // подача короткого звукового сигналу друку талона
  buzzerStopMs = millis() + 60;                                                         // встановлення тривалості сигналу шістдесят мілісекунд
  char msg[64];                                                                         // буфер формування повідомлення про новий талон
  snprintf(msg, sizeof(msg), "Issued ticket A%02d | Wait forecast: %.1f min",           // шаблон повідомлення реєстрації клієнта
           ticketNum, currentAi.estWaitMinutes);                                        // підстановка номера талона і прогнозу хвилин
  logEvent("TICKET_NEW", msg);                                                          // запис події видачі талона у термінальний журнал
}                                                                                       // завершення функції реєстрації нового клієнта

void handleServeNextAction() {                                                          // обробка виклику наступного клієнта оператором
  bool called = aiQueue.callNextClient();                                               // спроба виклику чергового номера з черги
  currentAi = aiQueue.evaluate();                                                       // оновлення ковзного середнього та прогнозу черги
  updateLcdScreen();                                                                    // оновлення викликаного номера на дисплеї табло
  if (called) {                                                                         // перевірка успішного виклику клієнта до вікна
    setBuzzerTone(2200);                                                                // увімкнення дзвінкого тону запрошення клієнта
    buzzerStopMs = millis() + 140;                                                      // тривалість звукового дзвінка сто сорок мілісекунд
    char msg[64];                                                                       // буфер формування повідомлення виклику талона
    snprintf(msg, sizeof(msg), "Calling ticket A%02d | Avg service: %.0f s",            // шаблон повідомлення виклику клієнта до оператора
             currentAi.currentServedTicket, currentAi.avgServiceSec);                   // підстановка викликаного талона і середнього часу
    logEvent("QUEUE_CALL", msg);                                                        // запис події виклику клієнта у журнал
  } else {                                                                              // гілка натискання кнопки при порожній черзі
    setBuzzerTone(900);                                                                 // подача низького сигналу порожньої черги
    buzzerStopMs = millis() + 80;                                                       // тривалість попереджувального тону вісімдесят мілісекунд
    logEvent("QUEUE_IDLE", "No waiting clients in queue to call");                      // запис повідомлення про відсутність людей у черзі
  }                                                                                     // завершення перевірки результату виклику
}                                                                                       // завершення функції обслуговування чергового клієнта

void checkButtons() {                                                                   // неблокуюче опитування тактових кнопок термінала
  static unsigned long lastPressMs = 0;                                                 // мітка часу останнього натискання кнопки
  if (millis() - lastPressMs < 300) {                                                   // фільтрація брязкоту контактів триста мілісекунд
    return;                                                                             // вихід до завершення інтервалу стабілізації
  }                                                                                     // завершення перевірки інтервалу антибрязкоту

  if (digitalRead(PIN_BTN_REGISTER) == LOW) {                                           // перевірка натискання кнопки реєстрації клієнта
    lastPressMs = millis();                                                             // фіксація моменту натискання кнопки талона
    handleNewTicketAction();                                                            // виклик процедури друку нового віртуального талона
  } else if (digitalRead(PIN_BTN_SERVE) == LOW) {                                       // перевірка натискання кнопки виклику оператора
    lastPressMs = millis();                                                             // фіксація моменту натискання кнопки обслуговування
    handleServeNextAction();                                                            // виклик процедури запрошення наступного клієнта
  }                                                                                     // завершення перевірки станів тактових кнопок
}                                                                                       // завершення функції опитування клавіш термінала

void handleWebBoard() {                                                                 // формування веб сторінки онлайн табло номерів черги
  String html = "<!DOCTYPE html><html><head><meta charset='utf-8'>";                    // початок документа онлайн табло з кодуванням
  html += "<meta http-equiv='refresh' content='3'>";                                    // автоматичне оновлення веб табло кожні три секунди
  html += "<title>Smart Queue Board</title>";                                           // заголовок вкладки браузера онлайн табло
  html += "<style>body{font-family:Arial;background:#f2f5f8;text-align:center;}";       // базові стилі оформлення фону та шрифту сторінки
  html += ".box{background:#fff;max-width:480px;margin:20px auto;padding:20px;";        // стилізація центральної картки електронного табло
  html += "border-radius:12px;box-shadow:0 4px 12px rgba(0,0,0,0.1);}";                 // закруглення кутів та тінь контейнера табло
  html += ".num{font-size:42px;font-weight:bold;color:#1565c0;margin:10px 0;}";         // великий шрифт відображення викликаного номера
  html += ".digest{background:#e8f5e9;padding:10px;border-radius:8px;}";                // стиль блоку аналітичного дайджесту керівнику
  html += "</style></head><body><div class='box'>";                                     // відкриття основного контейнера тіла сторінки
  html += "<h2>Intelligent Customer Queue Terminal</h2>";                               // вивід назви системи керування чергою на табло
  html += "<p>Now Serving Ticket:</p><div class='num'>A";                               // підпис секції поточного викликаного талона
  if (currentAi.currentServedTicket < 10) {                                             // перевірка потреби ведучого нуля для номера
    html += "0";                                                                        // додавання ведучого нуля до однозначного номера
  }                                                                                     // завершення форматування викликаного номера
  html += String(currentAi.currentServedTicket) + "</div>";                             // вивід активного номера талона на екран браузера
  html += "<p>Last Issued: <b>A" + String(currentAi.lastIssuedTicket) + "</b>";         // вивід останнього зареєстрованого номера талона
  html += " | Waiting: <b>" + String(currentAi.activeQueueCount) + "</b></p>";          // вивід кількості людей які очікують у черзі
  html += "<p>AI Wait Time: <b>" + String(currentAi.estWaitMinutes, 1) + " min</b>";    // вивід розрахованого моделлю часу очікування
  html += " | Avg Service: <b>" + String(currentAi.avgServiceSec, 0) + " s</b></p>";    // вивід ковзного середнього часу обслуговування
  html += "<p>Traffic Load: <b>" + String(aiQueue.getLoadTitle(currentAi.loadLevel));   // вивід класифікованого рівня завантаженості залу
  html += "</b></p><div class='digest'>";                                               // відкриття контейнера аналітичного дайджесту
  html += String(currentAi.llmManagerDigest) + "</div>";                                // вивід тексту аналітичного дайджесту керівнику
  html += "</div></body></html>";                                                       // закриття розмітки веб сторінки онлайн табло
  server.send(200, "text/html", html);                                                  // відправка сформованої сторінки клієнту браузера
}                                                                                       // завершення обробника головної сторінки табло

void handleWebJson() {                                                                  // формування пакету телеметрії черги у форматі json
  char json[220];                                                                       // буфер для збереження текстового об єкта телеметрії
  const char* fmt = "{\"now\":%d,\"last\":%d,\"wait\":%d,\"min\":%.1f,\"avg\":%.1f}";   // шаблон полів поточного стану черги та прогнозу
  snprintf(json, sizeof(json), fmt, currentAi.currentServedTicket,                      // початок запису параметрів черги у буфер
           currentAi.lastIssuedTicket, currentAi.activeQueueCount,                      // підстановка номерів талонів і довжини черги
           currentAi.estWaitMinutes, currentAi.avgServiceSec);                          // підстановка прогнозу очікування і часу оператора
  server.send(200, "application/json", json);                                           // відправка json відповіді за мережевим запитом
}                                                                                       // завершення обробника json телеметрії сервера

void printDataTable() {                                                                 // вивід періодичної таблиці стану черги у консоль
  Serial.println();                                                                     // порожній рядок для відділення блоку таблиці
  Serial.println("Queue Analytics Telemetry Table:");                                   // заголовок діагностичної таблиці електронної черги
  Serial.print("Now Serving Ticket: A");                                                // вивід мітки поточного викликаного талона
  if (currentAi.currentServedTicket < 10) {                                             // перевірка формату однозначного номера виклику
    Serial.print("0");                                                                  // друк ведучого нуля перед номером виклику
  }                                                                                     // завершення вирівнювання номера виклику
  Serial.print(currentAi.currentServedTicket);                                          // вивід номера клієнта біля вікна оператора
  Serial.print(" / Last Issued: A");                                                    // вивід роздільника останнього виданого талона
  if (currentAi.lastIssuedTicket < 10) {                                                // перевірка формату однозначного виданого номера
    Serial.print("0");                                                                  // друк ведучого нуля перед виданим номером
  }                                                                                     // завершення вирівнювання виданого номера
  Serial.println(currentAi.lastIssuedTicket);                                           // вивід останнього зареєстрованого номера талона
  Serial.print("Clients Waiting in Queue: ");                                           // вивід мітки поточної кількості людей у черзі
  Serial.print(currentAi.activeQueueCount);                                             // вивід чисельності активної черги очікування
  Serial.print(" / Total Served: ");                                                    // вивід роздільника загальної кількості обслугованих
  Serial.println(currentAi.totalServedToday);                                           // вивід кількості обслугованих чеків за зміну
  Serial.print("Moving Avg Service Time: ");                                            // вивід мітки ковзного середнього часу оператора
  Serial.print(currentAi.avgServiceSec, 1);                                             // вивід середньої тривалості обслуговування у секундах
  Serial.println(" s");                                                                 // вивід одиниці вимірювання секунди
  Serial.print("AI Estimated Wait Time: ");                                             // вивід мітки прогнозованого часу очікування
  Serial.print(currentAi.estWaitMinutes, 1);                                            // вивід розрахованого часу очікування у хвилинах
  Serial.println(" min");                                                               // вивід одиниці вимірювання хвилини
  Serial.print("AI Traffic Forecast: ");                                                // вивід мітки прогнозованої інтенсивності потоку
  Serial.print(currentAi.arrivalRatePerHour, 1);                                        // вивід очікуваної кількості клієнтів за годину
  Serial.print(" clients/h | Load: ");                                                  // вивід роздільника класифікації навантаження
  Serial.println(aiQueue.getLoadTitle(currentAi.loadLevel));                            // вивід текстового рівня завантаженості сервісу
  Serial.println(currentAi.llmManagerDigest);                                           // вивід аналітичного дайджесту для керівника
  Serial.println();                                                                     // завершальний порожній рядок після таблиці
}                                                                                       // завершення функції виводу таблиці телеметрії

void setup() {                                                                          // початкове налаштування апаратних вузлів термінала
  Serial.begin(115200);                                                                 // ініціалізація послідовного порту для моніторингу
  delay(250);                                                                           // коротка пауза стабілізації живлення після старту
  Serial.println();                                                                     // порожній рядок перед стартовим банером
  Serial.println("===================");                                                // верхня рамка стартового інформаційного банера
  Serial.println("Intelligent Customer Queue and Traffic Forecaster");                  // назва проєкту електронної черги англійською мовою
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
    .gpio_num = PIN_BUZZER,                                                             // прив язка виводу звукового випромінювача дзвінка
    .speed_mode = LEDC_LOW_SPEED_MODE,                                                  // узгодження режиму швидкості з таймером
    .channel = LEDC_CHANNEL_0,                                                          // вибір нульового каналу модуляції
    .intr_type = LEDC_INTR_DISABLE,                                                     // вимкнення переривань генератора імпульсів
    .timer_sel = LEDC_TIMER_0,                                                          // підключення каналу до нульового таймера
    .duty = 0,                                                                          // початковий вимкнений стан без звуку
    .hpoint = 0                                                                         // нульовий зсув фази вихідного сигналу
  };                                                                                    // завершення структури конфігурації каналу
  ledc_channel_config(&ledc_channel);                                                   // ініціалізація апаратного каналу зумера

  pinMode(PIN_BTN_REGISTER, INPUT_PULLUP);                                              // увімкнення внутрішньої підтяжки кнопки реєстрації
  pinMode(PIN_BTN_SERVE, INPUT_PULLUP);                                                 // увімкнення внутрішньої підтяжки кнопки оператора
  aiQueue.init();                                                                       // початкова ініціалізація моделі прогнозування черги
  currentAi = aiQueue.evaluate();                                                       // первинний розрахунок стану порожньої черги

  Wire.begin(21, 22);                                                                   // запуск апаратної двопровідної шини дисплея
  lcd.init();                                                                           // ініціалізація контролера символьного дисплея
  lcd.backlight();                                                                      // увімкнення підсвічування символьного екрана
  lcd.setCursor(0, 0);                                                                  // переміщення курсора у початок першого рядка
  lcd.print("AI QUEUE SYSTEM");                                                         // вивід привітального заголовка на екран
  lcd.setCursor(0, 1);                                                                  // переміщення курсора у початок другого рядка
  lcd.print("INITIALIZING...");                                                         // вивід статусу початкового запуску термінала
  logEvent("SYSTEM", "Queue buttons buzzer and LCD initialized");                       // запис у журнал про готовність периферії

  WiFi.begin(WIFI_SSID, WIFI_PASS);                                                     // запуск підключення до бездротової мережі
  Serial.print("Connecting to WiFi");                                                   // вивід повідомлення про пошук мережі
  unsigned long wifiStart = millis();                                                   // фіксація часу початку підключення до мережі
  while (WiFi.status() != WL_CONNECTED && millis() - wifiStart < 5000) {                // очікування з єднання з лімітом п ять секунд
    delay(200);                                                                         // інтервал між перевірками статусу підключення
    Serial.print(".");                                                                  // друк індикатора прогресу у консоль
  }                                                                                     // завершення циклу очікування бездротової мережі
  Serial.println();                                                                     // перехід на новий рядок після підключення

  server.on("/", handleWebBoard);                                                       // реєстрація маршруту головної сторінки онлайн табло
  server.on("/json", handleWebJson);                                                    // реєстрація маршруту отримання телеметрії черги
  server.begin();                                                                       // запуск локального веб сервера онлайн табло
  if (WiFi.status() == WL_CONNECTED) {                                                  // перевірка успішного отримання мережевої адреси
    logEvent("NETWORK", "WiFi connected and local Web server started");                 // підтвердження запуску мережі та веб сервера
    Serial.print("IP Address: ");                                                       // вивід заголовка мережевої адреси
    Serial.println(WiFi.localIP());                                                     // вивід отриманої адреси мікроконтролера
    Serial.print("Web Queue Board URL: http://");                                       // вивід префікса посилання на онлайн табло
    Serial.print(WiFi.localIP());                                                       // вивід адреси веб пульта електронної черги
    Serial.println("/");                                                                // завершення рядка посилання на веб табло
  } else {                                                                              // обробка автономного режиму без мережі
    logEvent("NETWORK", "WiFi offline running local queue display");                    // повідомлення про перехід у локальний режим
  }                                                                                     // завершення перевірки бездротового зв язку

  updateLcdScreen();                                                                    // вивід початкового стану табло на дисплей
  logEvent("AI_QUEUE", currentAi.llmManagerDigest);                                     // вивід стартового дайджесту штучного інтелекту
  unsigned long bootDoneMs = millis();                                                  // фіксація моменту завершення стартової ініціалізації
  lastDisplayMs = bootDoneMs;                                                           // синхронізація таймера оновлення дисплея
  lastSerialTableMs = bootDoneMs;                                                       // синхронізація таймера першого виводу таблиці
}                                                                                       // завершення функції початкового налаштування

void loop() {                                                                           // головний безперервний цикл роботи термінала черги
  server.handleClient();                                                                // обслуговування запитів браузера до онлайн табло
  checkButtons();                                                                       // опитування кнопок реєстрації та виклику клієнтів

  if (currentBuzzerFreq > 0 && millis() >= buzzerStopMs) {                              // перевірка часу завершення звукового дзвінка
    setBuzzerTone(0);                                                                   // вимкнення зумера після відпрацювання сигналу
  }                                                                                     // завершення контролю тривалості звуку

  if (millis() - lastDisplayMs >= 500) {                                                // періодичне оновлення дисплея кожні пів секунди
    lastDisplayMs = millis();                                                           // оновлення мітки часу виводу на екран
    updateLcdScreen();                                                                  // відображення поточних номерів та часу очікування
  }                                                                                     // завершення блоку оновлення дисплея

  if (millis() - lastSerialTableMs >= 4000) {                                           // періодичний друк таблиці кожні чотири секунди
    lastSerialTableMs = millis();                                                       // оновлення мітки часу термінального звіту
    printDataTable();                                                                   // вивід структурованої таблиці черги у консоль
  }                                                                                     // завершення блоку термінальної таблиці
}                                                                                       // завершення головного циклу програми