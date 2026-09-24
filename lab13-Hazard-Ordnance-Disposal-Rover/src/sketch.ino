#include <WiFi.h>                                                                       // бібліотека бездротового зв'язку
#include <WebServer.h>                                                                  // бібліотека локального веб-сервера
#include <ESP32Servo.h>                                                                 // бібліотека керування сервоприводами
#include "ai_safety.h"                                                                  // модуль бортового штучного інтелекту

float readDistanceCm();                                                                 // прототип функції вимірювання дистанції
void logEvent(const char* cat, const char* msg);                                        // прототип функції реєстрації подій
void printTableRow(const char* name, const char* val, const char* unit);                // прототип рядка таблиці діагностики
void printDataTable(const SafetyAssessment& sa);                                        // прототип друку таблиці моніторингу
void chassisStop();                                                                     // прототип повної зупинки шасі
void chassisForward();                                                                  // прототип руху платформи вперед
void chassisBackward();                                                                 // прототип руху платформи назад
void chassisLeft();                                                                     // прототип розвороту платформи ліворуч
void chassisRight();                                                                    // прототип розвороту платформи праворуч
void manipulatorGripOpen();                                                             // прототип відкриття клішні
void manipulatorGripClose();                                                            // прототип закриття клішні
void manipulatorLiftUp();                                                               // прототип підйому стріли вгору
void manipulatorLiftDown();                                                             // прототип опускання стріли вниз
void manipulatorLiftSafe();                                                             // прототип безпечного положення стріли
void updateDangerIndicator(const SafetyAssessment& sa);                                 // прототип оптичної індикації загрози
void handleRoot();                                                                      // прототип обробника головної сторінки
void handleTelemetry();                                                                 // прототип обробника телеметрії
void handleCommand();                                                                   // прототип обробника команд керування
void handleFavicon();                                                                   // прототип обробника запиту іконки
void processSerialCommand(char ch);                                                     // прототип обробки команд терміналу

const char* ssid = "Wokwi-GUEST";                                                       // назва локальної мережі точки доступу
const char* password = "";                                                              // пароль бездротової мережі

const int pinTrig = 5;                                                                  // лінія запуску ультразвукового імпульсу
const int pinEcho = 18;                                                                 // лінія прийому луни ультразвуку
const int pinLedDanger = 21;                                                            // лінія оптичного індикатора небезпеки
const int pinServoGrip = 16;                                                            // лінія керування приводом клішні
const int pinServoLift = 4;                                                             // лінія керування приводом стріли
const int pinServoLeft = 2;                                                             // лінія керування лівим бортом шасі
const int pinServoRight = 15;                                                           // лінія керування правим бортом шасі

Servo servoGrip;                                                                        // об'єкт сервопривода клішні
Servo servoLift;                                                                        // об'єкт сервопривода стріли
Servo servoLeft;                                                                        // об'єкт сервопривода лівого колеса
Servo servoRight;                                                                       // об'єкт сервопривода правого колеса

AISafetyEngine aiEngine;                                                                // екземпляр бортового рушія безпеки
WebServer server(80);                                                                   // сервер обробки запитів на порті вісімдесят

int gripAngle = 0;                                                                      // поточний кут положення клішні
int liftAngle = 45;                                                                     // поточний кут положення стріли
String currentMotion = "STOP";                                                          // поточний стан руху платформи
float currentDistance = 200.0f;                                                         // поточна дистанція далекоміра
unsigned long lastSensorRead = 0;                                                       // час останнього опитування далекоміра
unsigned long lastLedBlink = 0;                                                         // час перемикання стану світлодіода
unsigned long lastTablePrint = 0;                                                       // час останнього друку таблиці монітора
bool ledState = false;                                                                  // логічний стан аварійного світлодіода
bool previousLockedState = false;                                                       // попередній статус блокування маніпулятора

const char htmlPage[] PROGMEM =                                                         // розмітка інтерактивного веб-інтерфейсу пульта
R"rawliteral(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Hazard Ordnance Disposal Rover</title>
<style>
*{box-sizing:border-box;margin:0;padding:0;font-family:'Segoe UI',Consolas,monospace}
body{background:#0d1117;color:#c9d1d9;padding:16px;display:flex;justify-content:center}
.wrapper{max-width:960px;width:100%}
header{background:#161b22;border:1px solid #30363d;border-radius:10px;padding:16px 20px;margin-bottom:16px;display:flex;justify-content:space-between;align-items:center;flex-wrap:wrap;gap:12px}
h1{font-size:20px;color:#58a6ff;text-transform:uppercase;letter-spacing:1px}
.badge{padding:6px 12px;border-radius:20px;font-size:12px;font-weight:700;letter-spacing:0.5px}
.badge-ok{background:#238636;color:#fff}
.badge-warn{background:#d29922;color:#000}
.badge-danger{background:#da3633;color:#fff;animation:pulse 1s infinite alternate}
@keyframes pulse{0%{opacity:1}100%{opacity:0.6}}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(280px,1fr));gap:16px}
.card{background:#161b22;border:1px solid #30363d;border-radius:10px;padding:16px}
.card-title{font-size:14px;color:#8b949e;text-transform:uppercase;margin-bottom:12px;border-bottom:1px solid #21262d;padding-bottom:6px}
.metric{display:flex;justify-content:space-between;align-items:center;margin-bottom:10px;font-size:14px}
.metric-val{font-size:18px;font-weight:700;color:#58a6ff}
.risk-bar-bg{width:100%;height:14px;background:#21262d;border-radius:7px;overflow:hidden;margin-top:6px}
.risk-bar-fill{height:100%;width:0%;background:#238636;transition:width .3s,background-color .3s}
.sop-box{background:#0d1117;border-left:4px solid #58a6ff;padding:10px;margin-top:10px;font-size:13px;color:#f0f6fc}
.ctrl-grid{display:grid;grid-template-columns:repeat(3,1fr);gap:8px;max-width:240px;margin:0 auto}
.btn{background:#21262d;border:1px solid #30363d;color:#c9d1d9;padding:14px;border-radius:8px;font-size:15px;font-weight:700;cursor:pointer;transition:all .15s}
.btn:hover{background:#30363d;color:#58a6ff;border-color:#58a6ff}
.btn:active{transform:scale(0.96)}
.btn-stop{background:#da3633;color:#fff;border-color:#f85149}
.btn-stop:hover{background:#b62324;color:#fff}
.manip-list{display:flex;flex-direction:column;gap:8px}
.btn-row{display:flex;gap:8px}
.btn-row .btn{flex:1}
.log-box{margin-top:12px;padding:8px;background:#0d1117;border-radius:6px;font-size:12px;color:#8b949e;min-height:24px}
</style>
</head>
<body>
<div class="wrapper">
<header>
<div>
<h1>Hazard Ordnance Disposal Rover</h1>
<p style="font-size:12px;color:#8b949e">EOD Platform with AI Safety Interlock Module</p>
</div>
<div id="aiBadge" class="badge badge-ok">AI: SAFE</div>
</header>
<div class="grid">
<div class="card">
<div class="card-title">AI Telemetry & Risk Assessment</div>
<div class="metric"><span>HC-SR04 Standoff Distance:</span><span id="distVal" class="metric-val">-- cm</span></div>
<div class="metric"><span>Approach Velocity:</span><span id="spdVal" class="metric-val">-- cm/s</span></div>
<div class="metric"><span>Detonation Risk Index:</span><span id="riskVal" class="metric-val">-- %</span></div>
<div class="risk-bar-bg"><div id="riskBar" class="risk-bar-fill"></div></div>
<div class="sop-box" id="sopVal">Initializing SOP Protocol...</div>
<div class="sop-box" id="alertVal" style="border-left-color:#da3633;margin-top:8px">Awaiting sensor telemetry...</div>
</div>
<div class="card">
<div class="card-title">Chassis Locomotion Control</div>
<div class="ctrl-grid">
<div></div>
<button class="btn" onclick="sendCmd('forward')">&#9650;<br>Forward</button>
<div></div>
<button class="btn" onclick="sendCmd('left')">&#9664;<br>Left</button>
<button class="btn btn-stop" onclick="sendCmd('stop')">&#9632;<br>Stop</button>
<button class="btn" onclick="sendCmd('right')">&#9654;<br>Right</button>
<div></div>
<button class="btn" onclick="sendCmd('backward')">&#9660;<br>Backward</button>
<div></div>
</div>
<div class="metric" style="margin-top:16px"><span>Current Motion:</span><span id="motionVal" class="metric-val">STOP</span></div>
</div>
<div class="card">
<div class="card-title">EOD Manipulator Control</div>
<div class="metric"><span>AI Safety Interlock:</span><span id="lockVal" class="metric-val">UNLOCKED</span></div>
<div class="manip-list">
<div class="btn-row">
<button class="btn" onclick="sendCmd('lift_up')">Arm Up</button>
<button class="btn" onclick="sendCmd('lift_down')">Arm Down</button>
</div>
<div class="btn-row">
<button class="btn" onclick="sendCmd('grip_open')">Gripper Open</button>
<button class="btn" onclick="sendCmd('grip_close')">Gripper Close</button>
</div>
<button class="btn" onclick="sendCmd('lift_safe')">Safe Standby</button>
</div>
<div class="metric" style="margin-top:12px"><span>Arm:</span><span id="liftVal" class="metric-val">45°</span><span>Gripper:</span><span id="gripVal" class="metric-val">0°</span></div>
<div id="cmdLog" class="log-box">Ready for operator command</div>
</div>
</div>
</div>
<script>
let busy = false;
const updateTelemetry = async () => {
if (busy) return;
busy = true;
try {
let r = await fetch('/telemetry');
if (r.ok) {
let d = await r.json();
document.getElementById('distVal').innerText = d.distance.toFixed(1) + ' cm';
document.getElementById('spdVal').innerText = d.speed.toFixed(1) + ' cm/s';
document.getElementById('riskVal').innerText = d.risk.toFixed(1) + ' %';
document.getElementById('motionVal').innerText = d.motion;
document.getElementById('liftVal').innerText = d.lift + '°';
document.getElementById('gripVal').innerText = d.grip + '°';
document.getElementById('sopVal').innerText = d.sop;
document.getElementById('alertVal').innerText = d.alert;
let bar = document.getElementById('riskBar');
bar.style.width = Math.min(100, Math.max(0, d.risk)) + '%';
let badge = document.getElementById('aiBadge');
let lock = document.getElementById('lockVal');
if (d.locked) {
lock.innerText = 'LOCKED BY AI';
lock.style.color = '#da3633';
} else {
lock.innerText = 'UNLOCKED';
lock.style.color = '#238636';
}
if (d.level === 3) {
badge.className = 'badge badge-danger';
badge.innerText = 'AI: CRITICAL RISK';
bar.style.backgroundColor = '#da3633';
} else if (d.level === 2) {
badge.className = 'badge badge-warn';
badge.innerText = 'AI: HAZARD ZONE';
bar.style.backgroundColor = '#d29922';
} else if (d.level === 1) {
badge.className = 'badge badge-warn';
badge.innerText = 'AI: APPROACH DETECTED';
bar.style.backgroundColor = '#e3b341';
} else {
badge.className = 'badge badge-ok';
badge.innerText = 'AI: PATH CLEAR';
bar.style.backgroundColor = '#238636';
}
}
} catch (e) {
} finally {
busy = false;
}
};
const sendCmd = async (c) => {
let log = document.getElementById('cmdLog');
log.innerText = 'Transmitting [' + c + ']...';
log.style.color = '#8b949e';
try {
let r = await fetch('/command?cmd=' + c);
let t = await r.text();
log.innerText = 'Command [' + c + ']: ' + t;
if (t.includes('BLOCKED')) {
log.style.color = '#da3633';
} else {
log.style.color = '#238636';
}
updateTelemetry();
} catch (e) {
log.innerText = 'Connection error: ' + e;
log.style.color = '#da3633';
}
};
setInterval(updateTelemetry, 1500);
setTimeout(updateTelemetry, 200);
document.addEventListener('keydown', e => {
if (e.repeat) return;
let k = e.key.toLowerCase();
if (k === 'w') sendCmd('forward');
else if (k === 's') sendCmd('backward');
else if (k === 'a') sendCmd('left');
else if (k === 'd') sendCmd('right');
else if (k === ' ') sendCmd('stop');
});
</script>
</body>
</html>)rawliteral";                                                                    // завершення блоку розмітки веб-інтерфейсу

float readDistanceCm() {                                                                // функція вимірювання поточної дистанції
  digitalWrite(pinTrig, LOW);                                                           // встановлення низького рівня на лінії
  delayMicroseconds(2);                                                                 // коротка стабілізаційна мікропауза
  digitalWrite(pinTrig, HIGH);                                                          // генерація ультразвукового імпульсу
  delayMicroseconds(10);                                                                // тривалість запускаючого імпульсу
  digitalWrite(pinTrig, LOW);                                                           // завершення випромінювання сигналу

  long duration = pulseIn(pinEcho, HIGH, 15000);                                        // зчитування тривалості відбитої луни
  if (duration <= 0) {                                                                  // перевірка відсутності прийнятого сигналу
    return 400.0f;                                                                      // повернення максимальної віддалі сенсора
  }                                                                                     // завершення обробки відсутності сигналу
  float dist = (duration * 0.0343f) / 2.0f;                                             // перерахунок часу луни у сантиметри
  if (dist < 2.0f) dist = 2.0f;                                                         // обмеження нижньої межі вимірювань
  if (dist > 400.0f) dist = 400.0f;                                                     // обмеження верхньої межі вимірювань
  return dist;                                                                          // повернення розрахованої відстані
}                                                                                       // завершення функції роботи далекоміра

void logEvent(const char* cat, const char* msg) {                                       // функція реєстрації подій системи
  unsigned long sec = millis() / 1000;                                                  // визначення системного часу в секундах
  char timeStr[16];                                                                     // буфер форматованого часового рядка
  snprintf(timeStr, sizeof(timeStr), "%02lu:%02lu:%02lu", (sec / 3600) % 24, (sec / 60) % 60, sec % 60); // генерація текстової мітки часу події
  char logLine[160];                                                                    // буфер повного тексту запису журналу
  snprintf(logLine, sizeof(logLine), "[%s] [%s] %s", timeStr, cat, msg);                // компонування фінального рядка журналу
  Serial.println(logLine);                                                              // виведення запису в послідовний порт
}                                                                                       // завершення процедури реєстрації події

void printTableRow(const char* name, const char* val, const char* unit) {               // функція друку рядка таблиці
  char rowBuffer[70];                                                                   // буфер моноширинного рядка виведення
  snprintf(rowBuffer, sizeof(rowBuffer), "| %-34s | %-10s | %-6s |", name, val, unit);  // форматування колонок таблиці діагностики
  Serial.println(rowBuffer);                                                            // друк відформатованого рядка даних
}                                                                                       // завершення процедури друку рядка

void printDataTable(const SafetyAssessment& sa) {                                       // функція виведення зведеної таблиці
  char valBuf[16];                                                                      // буфер перетворення числових даних
  Serial.println();                                                                     // порожній рядок перед початком таблиці
  Serial.println("+------------------------------------+------------+--------+");       // друк верхньої межі структури таблиці
  Serial.println("| Monitored Parameter                | Value      | Unit   |");       // друк заголовків стовпчиків вимірів
  Serial.println("+------------------------------------+------------+--------+");       // друк розділювача заголовків колонок
  snprintf(valBuf, sizeof(valBuf), "%.1f", sa.distanceCm);                              // форматування показника дистанції
  printTableRow("Standoff Distance HC-SR04", valBuf, "cm");                             // виведення дистанції з далекоміра
  snprintf(valBuf, sizeof(valBuf), "%.1f", sa.approachSpeedCmS);                        // форматування швидкості зближення
  printTableRow("Approach Velocity", valBuf, "cm/s");                                   // виведення миттєвої швидкості ходу
  snprintf(valBuf, sizeof(valBuf), "%.1f", sa.riskIndex);                               // форматування індексу загрози вибуху
  printTableRow("Detonation Risk Index", valBuf, "%");                                  // виведення розрахованого рівня ризику
  printTableRow("AI Threat Level", (sa.level == RISK_CRITICAL ? "CRITICAL" : (sa.level == RISK_WARNING ? "WARNING" : (sa.level == RISK_CAUTION ? "CAUTION" : "SAFE"))), "level");         // виведення статусу небезпеки безпеки
  printTableRow("Safety Interlock", sa.manipulatorLocked ? "LOCKED" : "UNLOCKED", "status");                                                                                              // виведення статусу блокування приводів
  printTableRow("LLM-SOP Protocol", (sa.level == RISK_CRITICAL ? "SOP-13-C" : (sa.level == RISK_WARNING ? "SOP-13-B" : (sa.level == RISK_CAUTION ? "SOP-13-A" : "SOP-NORM"))), "code");   // виведення коду операційних процедур
  printTableRow("Danger Zone LED", (sa.level == RISK_CRITICAL ? "SOLID ON" : (sa.level == RISK_WARNING ? "FAST BLK" : (sa.level == RISK_CAUTION ? "SLOW BLK" : "OFF"))), "state");        // виведення стану світлового сповіщення
  snprintf(valBuf, sizeof(valBuf), "%d", liftAngle);                                    // форматування кута нахилу стріли
  printTableRow("Manipulator Arm Lift", valBuf, "deg");                                 // виведення кута позиції стріли
  snprintf(valBuf, sizeof(valBuf), "%d", gripAngle);                                    // форматування кута розкриття клішні
  printTableRow("Manipulator Gripper Claw", valBuf, "deg");                             // виведення положення приводу клішні
  printTableRow("Chassis Motion State", currentMotion.c_str(), "state");                // виведення напрямку переміщення шасі
  Serial.println("+------------------------------------+------------+--------+");       // друк нижньої рамки меж таблиці
  Serial.println();                                                                     // завершальний порожній рядок виводу
}                                                                                       // завершення процедури виведення таблиці

void chassisStop() {                                                                    // функція повної зупинки приводів шасі
  servoLeft.write(90);                                                                  // нейтральне положення лівого приводу
  servoRight.write(90);                                                                 // нейтральне положення правого приводу
  currentMotion = "STOP";                                                               // фіксація зупиненого стану платформи
}                                                                                       // завершення процедури зупинки ходової

void chassisForward() {                                                                 // функція руху платформи вперед
  servoLeft.write(180);                                                                 // пряме обертання лівого сервопривода
  servoRight.write(0);                                                                  // реверсивне обертання правого приводу
  currentMotion = "FORWARD";                                                            // фіксація стану поступального руху
}                                                                                       // завершення команди руху вперед

void chassisBackward() {                                                                // функція руху платформи назад
  servoLeft.write(0);                                                                   // зворотне обертання лівого приводу
  servoRight.write(180);                                                                // пряме обертання правого приводу
  currentMotion = "BACKWARD";                                                           // фіксація стану руху машини назад
}                                                                                       // завершення команди руху назад

void chassisLeft() {                                                                    // функція розвороту платформи ліворуч
  servoLeft.write(0);                                                                   // синхронний зворотний хід лівого борту
  servoRight.write(0);                                                                  // синхронний зворотний хід правого борту
  currentMotion = "LEFT";                                                               // фіксація стану розвороту ліворуч
}                                                                                       // завершення команди розвороту ліворуч

void chassisRight() {                                                                   // функція розвороту платформи праворуч
  servoLeft.write(180);                                                                 // синхронний прямий хід лівого борту
  servoRight.write(180);                                                                // синхронний прямий хід правого борту
  currentMotion = "RIGHT";                                                              // фіксація стану розвороту праворуч
}                                                                                       // завершення команди розвороту праворуч

void manipulatorGripOpen() {                                                            // функція повного розкриття клішні
  gripAngle = 0;                                                                        // встановлення нульового кута клішні
  servoGrip.write(gripAngle);                                                           // позиціонування приводу розкриття
}                                                                                       // завершення процедури розкриття клішні

void manipulatorGripClose() {                                                           // функція стискання губок клішні
  gripAngle = 90;                                                                       // кут дев'яносто градусів для стискання
  servoGrip.write(gripAngle);                                                           // позиціонування приводу стискання
}                                                                                       // завершення процедури закриття клішні

void manipulatorLiftUp() {                                                              // функція підйому стріли маніпулятора
  liftAngle = 90;                                                                       // кут максимального підйому стріли
  servoLift.write(liftAngle);                                                           // позиціонування приводу стріли вгору
}                                                                                       // завершення процедури підйому стріли

void manipulatorLiftDown() {                                                            // функція опускання стріли маніпулятора
  liftAngle = 10;                                                                       // кут опускання для огляду предмета
  servoLift.write(liftAngle);                                                           // позиціонування приводу стріли вниз
}                                                                                       // завершення процедури опускання стріли

void manipulatorLiftSafe() {                                                            // функція безпечного відведення стріли
  liftAngle = 45;                                                                       // кут сорок п'ять градусів для захисту
  servoLift.write(liftAngle);                                                           // переведення стріли у захисний стан
}                                                                                       // завершення відведення стріли у безпеку

void updateDangerIndicator(const SafetyAssessment& sa) {                                // процедура оновлення стану індикатора
  if (sa.level == RISK_CRITICAL) {                                                      // перевірка настання критичного ризику
    digitalWrite(pinLedDanger, HIGH);                                                   // безперервне світіння аварійного діода
  } else if (sa.level == RISK_WARNING) {                                                // перевірка рівня підвищеної загрози
    if (millis() - lastLedBlink > 200) {                                                // інтервал швидкого стробування сигналу
      ledState = !ledState;                                                             // інвертування логічного стану діода
      digitalWrite(pinLedDanger, ledState ? HIGH : LOW);                                // перемикання сигналу світлодіода
      lastLedBlink = millis();                                                          // оновлення часу останнього перемикання
    }                                                                                   // завершення блоку швидкого блимання
  } else if (sa.level == RISK_CAUTION) {                                                // перевірка рівня попереднього наближення
    if (millis() - lastLedBlink > 600) {                                                // інтервал повільного блимання діода
      ledState = !ledState;                                                             // інвертування логічного стану індикатора
      digitalWrite(pinLedDanger, ledState ? HIGH : LOW);                                // перемикання сигналу світлодіода
      lastLedBlink = millis();                                                          // оновлення часу останнього перемикання
    }                                                                                   // завершення блоку повільного блимання
  } else {                                                                              // умова перебування у безпечній зоні
    digitalWrite(pinLedDanger, LOW);                                                    // вимкнення світлового індикатора тривоги
  }                                                                                     // завершення логіки керування індикацією
}                                                                                       // завершення процедури індикації загрози

void handleRoot() {                                                                     // обробник запиту кореневої веб-сторінки
  server.send_P(200, "text/html", htmlPage);                                            // відправка клієнту збереженої розмітки
}                                                                                       // завершення обробника головного маршруту

void handleTelemetry() {                                                                // обробник запиту пакета телеметрії
  const SafetyAssessment& sa = aiEngine.getAssessment();                                // отримання актуальної оцінки безпеки
  String json = "{";                                                                    // ініціалізація текстового буфера даних
  json += "\"distance\":" + String(sa.distanceCm, 1) + ",";                             // пакування дистанції у формат запису
  json += "\"speed\":" + String(sa.approachSpeedCmS, 1) + ",";                          // пакування швидкості у вихідний пакет
  json += "\"risk\":" + String(sa.riskIndex, 1) + ",";                                  // пакування індексу ризику вибуху
  json += "\"level\":" + String((int)sa.level) + ",";                                   // пакування числового коду загрози
  json += "\"locked\":" + String(sa.manipulatorLocked ? "true" : "false") + ",";        // пакування статусу блокування приводів
  json += "\"sop\":\"" + String(sa.sopProtocol) + "\",";                                // пакування текстового коду процедури
  json += "\"alert\":\"" + String(sa.alertMessage) + "\",";                             // пакування тексту повідомлення загрози
  json += "\"lift\":" + String(liftAngle) + ",";                                        // пакування поточного кута стріли
  json += "\"grip\":" + String(gripAngle) + ",";                                        // пакування поточного кута клішні
  json += "\"motion\":\"" + currentMotion + "\"";                                       // пакування поточного напрямку руху шасі
  json += "}";                                                                          // закриття текстового блоку структури даних
  server.send(200, "application/json", json);                                           // відправка сформованого пакета клієнту
}                                                                                       // завершення обробника каналу телеметрії

void handleCommand() {                                                                  // обробник надходження керуючих директив
  String cmd = server.arg("cmd");                                                       // вилучення значення параметра команди

  if (cmd == "forward") {                                                               // обробка надходження команди вперед
    if (aiEngine.isChassisMovementAllowed(cmd)) {                                       // перевірка дозволу руху від модуля ШІ
      chassisForward();                                                                 // виконання фізичного переміщення вперед
      logEvent("COMMAND", "Web command FORWARD executed -> Left=180, Right=0");         // фіксація події виконання руху вперед
      server.send(200, "text/plain", "OK");                                             // підтвердження успіху виконання команди
    } else {                                                                            // умова заборони руху через близьку міну
      chassisStop();                                                                    // зупинка платформи перед перешкодою
      logEvent("COMMAND", "Web command FORWARD REJECTED: Hazard proximity");            // фіксація відхилення небезпечної команди
      server.send(403, "text/plain", "BLOCKED: HAZARD TOO CLOSE");                      // відповідь про блокування дії оператора
    }                                                                                   // завершення обробки перевірки дозволу
    return;                                                                             // вихід із функції обробника запиту
  }                                                                                     // завершення блоку аналізу руху вперед
  if (cmd == "backward") {                                                              // обробка надходження команди назад
    chassisBackward();                                                                  // виконання фізичного переміщення назад
    logEvent("COMMAND", "Web command BACKWARD executed -> Left=0, Right=180");          // реєстрація події успішного руху назад
    server.send(200, "text/plain", "OK");                                               // відправка позитивної відповіді клієнту
    return;                                                                             // вихід із процедури обробки команди
  }                                                                                     // завершення обробки руху назад
  if (cmd == "left") {                                                                  // обробка директиви розвороту ліворуч
    chassisLeft();                                                                      // виконання танкового розвороту ліворуч
    logEvent("COMMAND", "Web command LEFT executed -> Left=0, Right=0");                // реєстрація події маневру ліворуч
    server.send(200, "text/plain", "OK");                                               // підтвердження успіху виконання розвороту
    return;                                                                             // вихід із процедури обробника команди
  }                                                                                     // завершення аналізу розвороту ліворуч
  if (cmd == "right") {                                                                 // обробка директиви розвороту праворуч
    chassisRight();                                                                     // виконання танкового розвороту праворуч
    logEvent("COMMAND", "Web command RIGHT executed -> Left=180, Right=180");           // реєстрація події маневру праворуч
    server.send(200, "text/plain", "OK");                                               // підтвердження успіху виконання розвороту
    return;                                                                             // вихід із процедури обробника команди
  }                                                                                     // завершення аналізу розвороту праворуч
  if (cmd == "stop") {                                                                  // обробка директиви аварійної зупинки
    chassisStop();                                                                      // переведення приводів у стан зупинки
    logEvent("COMMAND", "Web command STOP executed -> Left=90, Right=90");              // реєстрація події повної зупинки платформи
    server.send(200, "text/plain", "OK");                                               // відправка підтвердження зупинки оператору
    return;                                                                             // вихід із процедури обробника команди
  }                                                                                     // завершення обробки команди зупинки

  if (cmd.startsWith("lift") || cmd.startsWith("grip")) {                               // обробка команд керування маніпулятором
    if (!aiEngine.isManipulatorCommandAllowed(cmd)) {                                   // перевірка відсутності блокування ШІ
      logEvent("MANIPULATOR", "Command BLOCKED by AI safety interlock");                // реєстрація блокування дій маніпулятора
      server.send(403, "text/plain", "BLOCKED_BY_AI_SAFETY");                           // сповіщення оператора про захист від удару
      return;                                                                           // вихід із процедури блокованої команди
    }                                                                                   // завершення умови перевірки блокування
    if (cmd == "lift_up") {                                                             // обробка директиви підйому стріли
      manipulatorLiftUp();                                                              // позиціонування стріли на дев'яносто
      logEvent("MANIPULATOR", "Arm lift raised to 90 deg");                             // фіксація події успішного підйому стріли
    } else if (cmd == "lift_down") {                                                    // обробка директиви опускання стріли
      manipulatorLiftDown();                                                            // позиціонування стріли на десять градусів
      logEvent("MANIPULATOR", "Arm lift lowered to 10 deg");                            // фіксація події опускання стріли вниз
    } else if (cmd == "lift_safe") {                                                    // обробка запиту безпечного положення
      manipulatorLiftSafe();                                                            // позиціонування стріли на сорок п'ять
      logEvent("MANIPULATOR", "Arm lift set to safe standby 45 deg");                   // реєстрація повернення стріли у безпеку
    } else if (cmd == "grip_open") {                                                    // обробка директиви відкриття клішні
      manipulatorGripOpen();                                                            // розкриття губок захоплення на нуль
      logEvent("MANIPULATOR", "Claw opened to 0 deg");                                  // реєстрація успішного розкриття клішні
    } else if (cmd == "grip_close") {                                                   // обробка директиви закриття клішні
      manipulatorGripClose();                                                           // змикання губок захоплення на дев'яносто
      logEvent("MANIPULATOR", "Claw closed to 90 deg");                                 // реєстрація успішного закриття клішні
    }                                                                                   // завершення аналізу варіантів команд маніпулятора
    server.send(200, "text/plain", "OK");                                               // відправка підтвердження виконання команди
    return;                                                                             // вихід із функції обробника запиту
  }                                                                                     // завершення блоку команд маніпулятора

  server.send(400, "text/plain", "UNKNOWN_COMMAND");                                    // повернення помилки на невідомий запит
}                                                                                       // завершення процедури обробки команд

void handleFavicon() {                                                                  // обробник системного запиту браузерної іконки
  server.send(204);                                                                     // повернення статусу відсутності контенту
}                                                                                       // завершення процедури обслуговування іконки

void processSerialCommand(char ch) {                                                    // функція обробки літерних команд терміналу
  switch (ch) {                                                                         // аналіз отриманого символу керування
    case 'w':                                                                           // код клавіші переміщення платформи вперед
    case 'W':                                                                           // дублювання великої літери руху вперед
      if (aiEngine.isChassisMovementAllowed("forward")) {                               // перевірка дозволу руху назустріч об'єкту
        chassisForward();                                                               // запуск сервоприводів у напрямку вперед
        logEvent("COMMAND", "Serial FORWARD executed -> Left=180, Right=0");            // реєстрація консольної директиви вперед
      } else {                                                                          // умова заборони руху через близьку загрозу
        chassisStop();                                                                  // зупинка машини перед підозрілою міною
        logEvent("COMMAND", "Serial FORWARD REJECTED: Proximity hazard");               // фіксація блокування консольного руху вперед
      }                                                                                 // завершення контролю дозволу консольного ходу
      break;                                                                            // вихід із гілки обробки команди вперед
    case 's':                                                                           // код клавіші переміщення платформи назад
    case 'S':                                                                           // дублювання великої літери руху назад
      chassisBackward();                                                                // запуск сервоприводів у напрямку назад
      logEvent("COMMAND", "Serial BACKWARD executed -> Left=0, Right=180");             // реєстрація консольної директиви назад
      break;                                                                            // вихід із гілки обробки команди назад
    case 'a':                                                                           // код клавіші розвороту платформи ліворуч
    case 'A':                                                                           // дублювання великої літери розвороту ліворуч
      chassisLeft();                                                                    // запуск сервоприводів для розвороту вліво
      logEvent("COMMAND", "Serial LEFT executed -> Left=0, Right=0");                   // реєстрація консольної директиви ліворуч
      break;                                                                            // вихід із гілки обробки команди ліворуч
    case 'd':                                                                           // код клавіші розвороту платформи праворуч
    case 'D':                                                                           // дублювання великої літери розвороту праворуч
      chassisRight();                                                                   // запуск сервоприводів для розвороту вправо
      logEvent("COMMAND", "Serial RIGHT executed -> Left=180, Right=180");              // реєстрація консольної директиви праворуч
      break;                                                                            // вихід із гілки обробки команди праворуч
    case ' ':                                                                           // код клавіші пробілу для негайного стопу
    case 'x':                                                                           // код літери для повної зупинки платформи
    case 'X':                                                                           // дублювання великої літери повної зупинки
      chassisStop();                                                                    // вимкнення тяги ходових сервоприводів
      logEvent("COMMAND", "Serial STOP executed -> Left=90, Right=90");                 // реєстрація консольної команди повної зупинки
      break;                                                                            // вихід із гілки обробки команди зупинки
    case 'u':                                                                           // код клавіші для підйому стріли маніпулятора
    case 'U':                                                                           // дублювання великої літери підйому стріли
      if (aiEngine.isManipulatorCommandAllowed("lift_up")) {                            // перевірка дозволу підйому від рушія ШІ
        manipulatorLiftUp();                                                            // поворот приводу стріли у верхню точку
        logEvent("MANIPULATOR", "Serial arm lift raised to 90 deg");                    // реєстрація консольного підйому стріли
      } else {                                                                          // умова захисного блокування маніпулятора
        logEvent("MANIPULATOR", "Serial arm BLOCKED by AI safety interlock");           // фіксація блокування консольної дії стріли
      }                                                                                 // завершення обробки перевірки підйому
      break;                                                                            // вихід із гілки обробки підйому стріли
    case 'j':                                                                           // код клавіші для опускання стріли вниз
    case 'J':                                                                           // дублювання великої літери опускання стріли
      if (aiEngine.isManipulatorCommandAllowed("lift_down")) {                          // перевірка дозволу опускання від модуля ШІ
        manipulatorLiftDown();                                                          // поворот приводу стріли у нижню точку
        logEvent("MANIPULATOR", "Serial arm lift lowered to 10 deg");                   // реєстрація консольного опускання стріли
      } else {                                                                          // умова блокування опускання перед міною
        logEvent("MANIPULATOR", "Serial arm BLOCKED by AI safety interlock");           // фіксація блокування консольного опускання
      }                                                                                 // завершення обробки перевірки опускання
      break;                                                                            // вихід із гілки обробки опускання стріли
    case 'k':                                                                           // код клавіші повернення стріли у безпеку
    case 'K':                                                                           // дублювання великої літери безпечного положення
      manipulatorLiftSafe();                                                            // поворот приводу стріли на сорок п'ять
      logEvent("MANIPULATOR", "Serial arm set to safe position 45 deg");                // реєстрація переведення стріли у безпеку
      break;                                                                            // вихід із гілки команди безпечного стану
    case 'o':                                                                           // код клавіші розкриття клішні маніпулятора
    case 'O':                                                                           // дублювання великої літери розкриття клішні
      if (aiEngine.isManipulatorCommandAllowed("grip_open")) {                          // перевірка відсутності блокування розкриття
        manipulatorGripOpen();                                                          // поворот приводу клішні у відкритий стан
        logEvent("MANIPULATOR", "Serial claw opened to 0 deg");                         // реєстрація консольного розкриття клішні
      } else {                                                                          // умова блокування дій клішні модулем ШІ
        logEvent("MANIPULATOR", "Serial claw BLOCKED by AI safety interlock");          // фіксація блокування консольної дії клішні
      }                                                                                 // завершення перевірки дозволу розкриття
      break;                                                                            // вихід із гілки обробки розкриття клішні
    case 'c':                                                                           // код клавіші для змикання губок клішні
    case 'C':                                                                           // дублювання великої літери закриття клішні
      if (aiEngine.isManipulatorCommandAllowed("grip_close")) {                         // перевірка дозволу стискання предметів
        manipulatorGripClose();                                                         // поворот приводу клішні у закритий стан
        logEvent("MANIPULATOR", "Serial claw closed to 90 deg");                        // реєстрація консольного закриття клішні
      } else {                                                                          // умова захисного блокування стискання
        logEvent("MANIPULATOR", "Serial claw BLOCKED by AI safety interlock");          // фіксація блокування консольного стискання
      }                                                                                 // завершення перевірки дозволу закриття
      break;                                                                            // вихід із гілки обробки закриття клішні
  }                                                                                     // завершення селектора символів терміналу
}                                                                                       // завершення процедури обробки команд консолі

void setup() {                                                                          // головна процедура ініціалізації системи
  Serial.begin(115200);                                                                 // ініціалізація послідовного порту зв'язку
  delay(300);                                                                           // стабілізаційна пауза після подачі живлення

  Serial.println();                                                                     // відступ рядка перед початком банера
  Serial.println("===================");                                                // верхня декоративна рамка назви проєкту
  Serial.println("Hazard Ordnance Disposal Rover");                                     // назва роботизованого саперного комплексу
  Serial.println("===================");                                                // нижня декоративна рамка назви проєкту
  Serial.println();                                                                     // порожній рядок після інформаційного банера

  pinMode(pinTrig, OUTPUT);                                                             // налаштування лінії запуску як цифровий вихід
  pinMode(pinEcho, INPUT);                                                              // налаштування лінії ехо-луни як цифровий вхід
  pinMode(pinLedDanger, OUTPUT);                                                        // налаштування лінії аварійного світлодіода
  digitalWrite(pinLedDanger, LOW);                                                      // початкове знеструмлення аварійного індикатора

  ESP32PWM::allocateTimer(0);                                                           // виділення нульового апаратного таймера ШІМ
  ESP32PWM::allocateTimer(1);                                                           // виділення першого апаратного таймера ШІМ
  ESP32PWM::allocateTimer(2);                                                           // виділення другого апаратного таймера ШІМ
  ESP32PWM::allocateTimer(3);                                                           // виділення третього апаратного таймера ШІМ

  servoGrip.setPeriodHertz(50);                                                         // встановлення частоти п'ятдесят герц приводу
  servoLift.setPeriodHertz(50);                                                         // встановлення частоти п'ятдесят герц стріли
  servoLeft.setPeriodHertz(50);                                                         // встановлення частоти ходового лівого приводу
  servoRight.setPeriodHertz(50);                                                        // встановлення частоти правого ходового приводу

  servoGrip.attach(pinServoGrip, 500, 2400);                                            // прив'язка лінії та імпульсів клішні
  servoLift.attach(pinServoLift, 500, 2400);                                            // прив'язка лінії та імпульсів стріли
  servoLeft.attach(pinServoLeft, 500, 2400);                                            // прив'язка лінії та імпульсів лівого борту
  servoRight.attach(pinServoRight, 500, 2400);                                          // прив'язка лінії та імпульсів правого борту

  manipulatorGripOpen();                                                                // переведення клішні у вихідний відкритий стан
  manipulatorLiftSafe();                                                                // встановлення стріли у транспортне положення
  chassisStop();                                                                        // початкове знерухомлення ходової частини

  logEvent("SYSTEM", "Hardware initialized");                                           // реєстрація успішного старту обладнання

  Serial.print("Connecting to WiFi: ");                                                 // повідомлення про початок з'єднання з мережею
  Serial.print(ssid);                                                                   // друк назви локальної точки доступу
  Serial.print(" ");                                                                    // друк розділювача перед прогресом з'єднання
  WiFi.mode(WIFI_STA);                                                                  // переведення модуля зв'язку у режим станції
  WiFi.begin(ssid, password, 6);                                                        // підключення до точки доступу на шостому каналі
  int attempts = 0;                                                                     // лічильник спроб отримання мережевої адреси
  while (WiFi.status() != WL_CONNECTED && attempts < 35) {                              // очікування встановлення мережевого зв'язку
    delay(150);                                                                         // пауза між опитуваннями статусу підключення
    Serial.print(".");                                                                  // виведення крапки індикації прогресу зв'язку
    attempts++;                                                                         // інкремент лічильника виконаних спроб
  }                                                                                     // завершення циклу встановлення з'єднання
  Serial.println();                                                                     // переведення рядка після індикації крапок

  if (WiFi.status() == WL_CONNECTED) {                                                  // перевірка успішного з'єднання з мережею
    logEvent("WIFI", "Connected successfully");                                         // реєстрація успішного отримання адреси
    Serial.print("[WIFI] ESP32 IP: ");                                                  // друк префікса мережевої адреси мікроконтролера
    Serial.println(WiFi.localIP());                                                     // виведення локальної мережевої адреси плати
    Serial.println("[WIFI] Web Console URL: http://localhost:8180");                    // виведення посилання на пульт керування
  } else {                                                                              // умова відсутності бездротового з'єднання
    logEvent("WIFI", "Offline mode active");                                            // реєстрація переходу у автономний режим
  }                                                                                     // завершення аналізу статусу мережі

  server.on("/", handleRoot);                                                           // реєстрація обробника кореневого веб-шляху
  server.on("/telemetry", handleTelemetry);                                             // реєстрація обробника потоку телеметрії
  server.on("/command", handleCommand);                                                 // реєстрація обробника директив керування
  server.on("/favicon.ico", handleFavicon);                                             // реєстрація кінцевої точки системної іконки
  server.enableCORS(true);                                                              // активація підтримки крос-доменних запитів
  server.begin();                                                                       // запуск локального веб-сервера на порті вісімдесят
  logEvent("HTTP", "Local Web Control Server listening on port 80");                    // фіксація готовності приймати з'єднання
  Serial.println();                                                                     // порожній рядок перед стартом основного циклу
}                                                                                       // завершення головної процедури ініціалізації

void loop() {                                                                           // основний безперервний цикл мікроконтролера
  server.handleClient();                                                                // обслуговування чергових клієнтських запитів

  while (Serial.available()) {                                                          // перевірка наявності символів у буфері порту
    char c = (char)Serial.read();                                                       // читання чергового байта команди з терміналу
    processSerialCommand(c);                                                            // передача символу на обробку рушію дій
  }                                                                                     // завершення обробки символів послідовного порту

  if (millis() - lastSensorRead >= 400) {                                               // перевірка періоду опитування далекоміра
    lastSensorRead = millis();                                                          // оновлення часу останнього вимірювання
    currentDistance = readDistanceCm();                                                 // вимірювання актуальної дистанції далекоміра
    SafetyAssessment sa = aiEngine.evaluate(currentDistance, lastSensorRead);           // розрахунок поточної оцінки ризику ШІ
    updateDangerIndicator(sa);                                                          // оновлення режиму роботи аварійного діода

    if (sa.manipulatorLocked != previousLockedState) {                                  // перевірка зміни статусу блокування маніпулятора
      previousLockedState = sa.manipulatorLocked;                                       // збереження нового статусу блокування приводів
      if (sa.manipulatorLocked) {                                                       // умова активації захисного стану блокування
        logEvent("AI_SAFETY", "Critical proximity hazard: Manipulator locked");         // реєстрація аварійного блокування маніпулятора
        manipulatorLiftSafe();                                                          // примусовий підйом стріли у безпечне положення
      } else {                                                                          // умова зняття захисного блокування маніпулятора
        logEvent("AI_SAFETY", "Hazard cleared: Manipulator unlocked");                  // реєстрація повернення дозволу на керування
      }                                                                                 // завершення обробки перемикання блокування
    }                                                                                   // завершення аналізу зміни статусу маніпулятора

    if (sa.level == RISK_CRITICAL && currentMotion == "FORWARD") {                      // перевірка небезпечного руху назустріч вибухівці
      chassisStop();                                                                    // аварійна зупинка платформи перед міною
      logEvent("SAFETY", "Emergency stop triggered: Critical hazard proximity");        // реєстрація спрацьовування екстреного захисту
    }                                                                                   // завершення обробки екстреної зупинки шасі
  }                                                                                     // завершення блоку періодичного аналізу безпеки

  if (millis() - lastTablePrint >= 1500) {                                              // перевірка інтервалу друку структурованої таблиці
    lastTablePrint = millis();                                                          // оновлення часу останнього виведення даних
    const SafetyAssessment& sa = aiEngine.getAssessment();                              // отримання свіжої оцінки безпеки від ШІ
    printDataTable(sa);                                                                 // друк зведеної таблиці телеметрії в термінал
  }                                                                                     // завершення блоку періодичної таблиці діагностики

  yield();                                                                              // передача кванту часу фоновим задачам ядра
}                                                                                       // завершення основного диспетчерського циклу