#ifndef AI_SAFETY_H                                                                     // захист від повторного включення заголовка
#define AI_SAFETY_H                                                                     // оголошення макросу заголовка модуля

#include <Arduino.h>                                                                    // підключення базових типів платформи

enum RiskLevel {                                                                        // перелік рівнів детонаційної загрози
  RISK_SAFE,                                                                            // безпечний рівень без ризику вибуху
  RISK_CAUTION,                                                                         // рівень підвищеної уваги при наближенні
  RISK_WARNING,                                                                         // рівень високої загрози у робочій зоні
  RISK_CRITICAL                                                                         // критичний рівень небезпеки детонації
};                                                                                      // завершення переліку рівнів ризику

enum ObjectClassification {                                                             // перелік типів виявлених об'єктів
  OBJ_CLEAR,                                                                            // чистий безпечний простір попереду
  OBJ_UNKNOWN_CONTAINER,                                                                // невідомий підозрілий контейнер
  OBJ_SUSPICIOUS_UXO,                                                                   // нерозірваний вибухонебезпечний боєприпас
  OBJ_IMPROVISED_EXPLOSIVE                                                              // саморобний вибуховий пристрій
};                                                                                      // завершення переліку типів об'єктів

struct SafetyAssessment {                                                               // структура комплексної оцінки безпеки
  float distanceCm;                                                                     // дистанція до об'єкта у сантиметрах
  float approachSpeedCmS;                                                               // швидкість наближення у сантиметрах на секунду
  float riskIndex;                                                                      // розрахований індекс ризику детонації
  RiskLevel level;                                                                      // поточний рівень загрози безпеці
  ObjectClassification objectType;                                                      // класифікований тип виявленого предмета
  bool manipulatorLocked;                                                               // прапорець захисного блокування маніпулятора
  const char* sopProtocol;                                                              // код регламенту стандартних процедур дій
  const char* alertMessage;                                                             // текстове інформаційне повідомлення тривоги
};                                                                                      // завершення структури оцінки безпеки

class AISafetyEngine {                                                                  // клас бортового модуля безпеки
private:                                                                                // внутрішні змінні стану рушія
  float lastDistance;                                                                   // попередня зафіксована дистанція
  unsigned long lastTimestamp;                                                          // часова мітка попереднього вимірювання
  float safeDistanceThreshold;                                                          // поріг переходу в безпечну дистанцію
  float criticalDistanceThreshold;                                                      // поріг критичної дистанції зближення
  float emergencyStopDistance;                                                          // поріг екстреної аварійної зупинки
  SafetyAssessment currentAssessment;                                                   // збережений поточний стан оцінки

public:                                                                                 // публічний інтерфейс рушія безпеки
  AISafetyEngine() {                                                                    // конструктор ініціалізації параметрів
    lastDistance = 200.0f;                                                              // початкове значення попередньої дистанції
    lastTimestamp = 0;                                                                  // скидання початкової часової мітки
    safeDistanceThreshold = 50.0f;                                                      // встановлення порогу безпечної зони
    criticalDistanceThreshold = 20.0f;                                                  // встановлення порогу критичної зони
    emergencyStopDistance = 12.0f;                                                      // встановлення межі аварійного стопу
    currentAssessment.distanceCm = 200.0f;                                              // встановлення вихідної дистанції оцінки
    currentAssessment.approachSpeedCmS = 0.0f;                                          // нульова початкова швидкість наближення
    currentAssessment.riskIndex = 0.0f;                                                 // нульовий початковий індекс ризику
    currentAssessment.level = RISK_SAFE;                                                // безпечний вихідний рівень загрози
    currentAssessment.objectType = OBJ_CLEAR;                                           // відсутність перешкод на старті
    currentAssessment.manipulatorLocked = false;                                        // розблокований стан маніпулятора
    currentAssessment.sopProtocol = "SOP-13-NORM: PATROL ACTIVE";                       // базовий регламент патрулювання
    currentAssessment.alertMessage = "DISTANCE SAFE";                                   // повідомлення про безпечну відстань
  }                                                                                     // завершення конструктора рушія

  SafetyAssessment evaluate(float distanceCm, unsigned long currentMillis) {            // метод розрахунку поточної оцінки ризику
    if (distanceCm <= 0.0f || distanceCm > 400.0f) {                                    // перевірка виходу за діапазон далекоміра
      distanceCm = 400.0f;                                                              // обмеження максимальної дистанції сенсора
    }                                                                                   // завершення умови валідації відстані

    float dt = (currentMillis - lastTimestamp) / 1000.0f;                               // розрахунок часового кроку в секундах
    float speed = 0.0f;                                                                 // початкова швидкість наближення
    if (dt > 0.05f && lastTimestamp > 0) {                                              // перевірка мінімального часового інтервалу
      float deltaDist = lastDistance - distanceCm;                                      // обчислення зміни дистанції між замірами
      if (deltaDist > 0.0f) {                                                           // перевірка факту скорочення відстані
        speed = deltaDist / dt;                                                         // розрахунок миттєвої швидкості наближення
      }                                                                                 // завершення умови руху на зближення
    }                                                                                   // завершення блоку розрахунку швидкості
    lastDistance = distanceCm;                                                          // оновлення попередньої дистанції заміру
    lastTimestamp = currentMillis;                                                      // збереження поточної мітки часу

    float distFactor = 0.0f;                                                            // коефіцієнт небезпеки відстані
    if (distanceCm <= emergencyStopDistance) {                                          // перевірка досягнення аварійної дистанції
      distFactor = 1.0f;                                                                // максимальний фактор небезпеки відстані
    } else if (distanceCm <= criticalDistanceThreshold) {                               // перевірка входження у критичну зону
      distFactor = 0.75f + 0.25f * ((criticalDistanceThreshold - distanceCm) / (criticalDistanceThreshold - emergencyStopDistance));    // розрахунок коефіцієнта критичної зони
    } else if (distanceCm <= safeDistanceThreshold) {                                                                                   // перевірка входження у зону уваги
      distFactor = 0.25f + 0.50f * ((safeDistanceThreshold - distanceCm) / (safeDistanceThreshold - criticalDistanceThreshold));        // розрахунок коефіцієнта зони уваги
    } else {                                                                                                                            // обробка діапазону віддаленого наближення
      distFactor = (distanceCm < 100.0f) ? (0.25f * (100.0f - distanceCm) / 50.0f) : 0.0f;                                              // розрахунок коефіцієнта підходу
    }                                                                                                                                   // завершення розрахунку фактора відстані

    float speedFactor = (speed > 30.0f) ? 1.0f : (speed / 30.0f);                       // коефіцієнт швидкості наближення
    float rawRisk = (distFactor * 80.0f) + (speedFactor * 20.0f);                       // зважений розрахунок сумарного ризику
    if (rawRisk > 100.0f) rawRisk = 100.0f;                                             // обмеження максимального індексу
    if (rawRisk < 0.0f) rawRisk = 0.0f;                                                 // обмеження мінімального індексу

    currentAssessment.distanceCm = distanceCm;                                          // збереження актуальної дистанції в оцінку
    currentAssessment.approachSpeedCmS = speed;                                         // збереження швидкості наближення
    currentAssessment.riskIndex = rawRisk;                                              // збереження розрахованого індексу ризику

    if (rawRisk >= 75.0f || distanceCm <= criticalDistanceThreshold) {                  // умова переходу в критичний рівень загрози
      currentAssessment.level = RISK_CRITICAL;                                          // встановлення критичного рівня небезпеки
      currentAssessment.objectType = OBJ_IMPROVISED_EXPLOSIVE;                          // класифікація підозрілого фугасу
      currentAssessment.manipulatorLocked = true;                                       // активація блокування маніпулятора
      currentAssessment.sopProtocol = "SOP-13-C: EMERGENCY LOCKOUT";                    // регламент аварійного блокування дій
      currentAssessment.alertMessage = "DANGER: HIGH EXPLOSIVE RISK";                   // попередження про високий ризик вибуху
    } else if (rawRisk >= 50.0f || distanceCm <= 35.0f) {                               // умова переходу у рівень високої загрози
      currentAssessment.level = RISK_WARNING;                                           // встановлення рівня високої небезпеки
      currentAssessment.objectType = OBJ_SUSPICIOUS_UXO;                                // класифікація нерозірваного снаряда
      currentAssessment.manipulatorLocked = true;                                       // активація захисного блокування клішні
      currentAssessment.sopProtocol = "SOP-13-B: RESTRICTED SPEED SCAN";                // регламент руху на обмеженій швидкості
      currentAssessment.alertMessage = "WARNING: APPROACHING HAZARD";                   // сповіщення про небезпечне зближення
    } else if (rawRisk >= 25.0f || distanceCm <= safeDistanceThreshold) {               // умова переходу у рівень підвищеної уваги
      currentAssessment.level = RISK_CAUTION;                                           // встановлення рівня підвищеної уваги
      currentAssessment.objectType = OBJ_UNKNOWN_CONTAINER;                             // класифікація невідомого об'єкта
      currentAssessment.manipulatorLocked = false;                                      // дозвіл на роботу маніпулятора
      currentAssessment.sopProtocol = "SOP-13-A: CAUTION APPROACH";                     // регламент обережного наближення
      currentAssessment.alertMessage = "CAUTION: DETECTED OBJECT";                      // сповіщення про виявлення об'єкта
    } else {                                                                            // умова штатного безпечного простору
      currentAssessment.level = RISK_SAFE;                                              // встановлення безпечного статусу руху
      currentAssessment.objectType = OBJ_CLEAR;                                         // підтвердження чистого шляху платформи
      currentAssessment.manipulatorLocked = false;                                      // штатний дозвіл роботи маніпулятора
      currentAssessment.sopProtocol = "SOP-13-NORM: PATROL ACTIVE";                     // штатний регламент патрулювання
      currentAssessment.alertMessage = "CLEAR: PATH OPEN";                              // сповіщення про вільний простір
    }                                                                                   // завершення класифікації рівня загрози

    return currentAssessment;                                                           // повернення структури поточної оцінки
  }                                                                                     // завершення методу оцінки ситуації

  bool isManipulatorCommandAllowed(const String& cmd) const {                           // перевірка дозволу команд маніпулятора
    if (!currentAssessment.manipulatorLocked) {                                         // перевірка відсутності блокування
      return true;                                                                      // дозвіл виконання команди маніпулятора
    }                                                                                   // завершення перевірки штатного стану
    if (cmd == "lift_safe" || cmd == "stop") {                                          // перевірка виклику безпечних команд
      return true;                                                                      // дозвіл команд безпечного положення
    }                                                                                   // завершення перевірки винятків безпеки
    return false;                                                                       // заборона небезпечних рухів стріли й клішні
  }                                                                                     // завершення методу перевірки маніпулятора

  bool isChassisMovementAllowed(const String& cmd) const {                              // перевірка дозволу маневрів шасі
    if (currentAssessment.distanceCm <= emergencyStopDistance) {                        // перевірка знаходження в аварійній зоні
      if (cmd == "forward") {                                                           // перевірка спроби руху вперед на міну
        return false;                                                                   // заборона руху платформи вперед
      }                                                                                 // завершення блокування руху вперед
    }                                                                                   // завершення контролю аварійної зони
    return true;                                                                        // дозвіл інших маневрів платформи
  }                                                                                     // завершення методу контролю шасі

  const SafetyAssessment& getAssessment() const {                                       // метод отримання поточної оцінки
    return currentAssessment;                                                           // повернення посилання на структуру оцінки
  }                                                                                     // завершення методу повернення оцінки
};                                                                                      // завершення опису класу рушія безпеки

#endif                                                                                  // кінець блоку директиви заголовка