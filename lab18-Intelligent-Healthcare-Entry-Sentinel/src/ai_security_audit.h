#ifndef AI_SECURITY_AUDIT_H                                                             // захист від повторного підключення заголовка
#define AI_SECURITY_AUDIT_H                                                             // визначення макросу заголовкового файлу

#include <Arduino.h>                                                                    // базові типи та функції мікроконтролера

enum CardRole {                                                                         // перелік категорій доступу користувачів
  ROLE_UNKNOWN = 0,                                                                     // невідомий ідентифікатор без прав
  ROLE_STAFF_NURSE = 1,                                                                 // медичний персонал денної зміни
  ROLE_CHIEF_DOCTOR = 2                                                                 // черговий лікар з цілодобовим доступом
};                                                                                      // завершення переліку ролей

enum ShiftSchedule {                                                                    // перелік робочих змін медустанови
  SHIFT_DAY = 0,                                                                        // денна робоча зміна з семи до дев ятнадцяти
  SHIFT_NIGHT = 1                                                                       // нічне чергування з дев ятнадцяти до семи
};                                                                                      // завершення переліку змін

enum ThreatLevel {                                                                      // градація рівня загрози безпеці
  THREAT_NORMAL = 0,                                                                    // штатний безпечний рівень
  THREAT_LOW = 1,                                                                       // низький рівень застереження
  THREAT_ELEVATED = 2,                                                                  // підвищений ризик порушення правил
  THREAT_CRITICAL = 3                                                                   // критична загроза несанкціонованого злому
};                                                                                      // завершення градації загрози

enum AccessDecision {                                                                   // перелік рішень системи контролю доступу
  DECISION_GRANTED = 0,                                                                 // доступ дозволено ригель відкрито
  DECISION_DENIED_SHIFT = 1,                                                            // відмовлено через невідповідність зміні
  DECISION_DENIED_UNAUTHORIZED = 2,                                                     // відмовлено невідомий ідентифікатор
  DECISION_LOCKOUT = 3                                                                  // термінал заблоковано через серію порушень
};                                                                                      // завершення рішень доступу

struct SecurityAuditResult {                                                            // структура результату інтелектуального аудиту
  AccessDecision decision;                                                              // підсумкове рішення щодо пропуску
  ThreatLevel threat;                                                                   // визначений ступінь небезпеки події
  float riskScore;                                                                      // розрахований індекс ризику від нуля до ста
  int consecutiveFailures;                                                              // кількість послідовних хибних спроб
  const char* auditLog;                                                                 // текстовий звіт аудиту політики безпеки
};                                                                                      // завершення структури результату

class AISecurityAudit {                                                                 // клас нейромережевого аудиту безпеки
private:                                                                                // приватні змінні внутрішнього стану
  int failureCount;                                                                     // лічильник поспіль зафіксованих помилок
  unsigned long lastAttemptTime;                                                        // час попередньої спроби у мілісекундах
  bool lockoutActive;                                                                   // прапорець блокування системи
  unsigned long lockoutStartTime;                                                       // момент початку блокування термінала

public:                                                                                 // публічні методи взаємодії
  AISecurityAudit() {                                                                   // конструктор класу аудиту
    failureCount = 0;                                                                   // скидання лічильника відхилень
    lastAttemptTime = 0;                                                                // початкова ініціалізація мітки часу
    lockoutActive = false;                                                              // початковий стан без блокування
    lockoutStartTime = 0;                                                               // скидання таймера блокування
  }                                                                                     // завершення конструктора

  void init() {                                                                         // метод ініціалізації модуля аудиту
    failureCount = 0;                                                                   // обнулення невдалих спроб
    lockoutActive = false;                                                              // деактивація блокування
    lastAttemptTime = millis();                                                         // фіксація початкового часу запуску
  }                                                                                     // завершення методу ініціалізації

  bool isLockedOut() {                                                                  // перевірка активності блокування системи
    if (lockoutActive) {                                                                // перевірка чи активний режим тривоги
      if (millis() - lockoutStartTime >= 15000) {                                       // перевірка спливання п ятнадцяти секунд
        lockoutActive = false;                                                          // автоматичне зняття блокування
        failureCount = 0;                                                               // скидання серії помилок після тайму
        return false;                                                                   // повернення стану готовності
      }                                                                                 // завершення перевірки інтервалу
      return true;                                                                      // підтвердження активного блокування
    }                                                                                   // завершення умови перевірки
    return false;                                                                       // підтвердження штатного режиму
  }                                                                                     // завершення перевірки блокування

  SecurityAuditResult evaluateAccess(CardRole role, ShiftSchedule shift) {              // оцінка доступу алгоритмом аудиту
    SecurityAuditResult result;                                                         // об єкт структури звіту
    unsigned long now = millis();                                                       // фіксація поточного часу мікроконтролера
    unsigned long delta = now - lastAttemptTime;                                        // інтервал між послідовними прикладаннями
    lastAttemptTime = now;                                                              // оновлення часу останньої спроби

    if (isLockedOut()) {                                                                // обробка спроби під час блокування
      result.decision = DECISION_LOCKOUT;                                               // фіксація стану блокування ригеля
      result.threat = THREAT_CRITICAL;                                                  // максимальний критичний рівень загрози
      result.riskScore = 100.0f;                                                        // максимальний показник небезпеки
      result.consecutiveFailures = failureCount;                                        // збереження лічильника помилок
      result.auditLog = "Terminal locked. High breach threat.";                         // повідомлення про активне блокування
      return result;                                                                    // повернення результату блокування
    }                                                                                   // завершення обробки блокування

    float baseRisk = 0.0f;                                                              // базовий ризик за посадовою роллю
    if (role == ROLE_CHIEF_DOCTOR) {                                                    // перевірка повноважень чергового лікаря
      baseRisk = 5.0f;                                                                  // мінімальний ризик для лікаря
    } else if (role == ROLE_STAFF_NURSE) {                                              // перевірка повноважень медперсоналу
      baseRisk = 15.0f;                                                                 // помірний ризик для персоналу
    } else {                                                                            // обробка невідомого відвідувача
      baseRisk = 75.0f;                                                                 // високий початковий ризик сторонньої карти
    }                                                                                   // завершення оцінки ролі

    float shiftPenalty = 0.0f;                                                          // штрафний бал відповідності зміні
    if (shift == SHIFT_NIGHT) {                                                         // перевірка умов нічного чергування
      if (role == ROLE_CHIEF_DOCTOR) {                                                  // перевірка лікаря у нічний час
        shiftPenalty = 5.0f;                                                            // допустимий прохід чергового лікаря
      } else if (role == ROLE_STAFF_NURSE) {                                            // перевірка персоналу вночі
        shiftPenalty = 60.0f;                                                           // штраф за прохід персоналу поза зміною
      } else {                                                                          // перевірка сторонніх осіб уночі
        shiftPenalty = 20.0f;                                                           // максимальне посилення підозри вночі
      }                                                                                 // завершення нічних умов
    } else {                                                                            // перевірка умов денної зміни
      if (role == ROLE_UNKNOWN) {                                                       // перевірка стороннього вдень
        shiftPenalty = 10.0f;                                                           // помірне посилення підозри вдень
      }                                                                                 // завершення денних умов
    }                                                                                   // завершення перевірки зміни

    float frequencyPenalty = 0.0f;                                                      // штрафний бал підозрілої частоти
    if (delta < 2000) {                                                                 // детекція занадто швидких повторних спроб
      frequencyPenalty = 20.0f;                                                         // штраф за спробу підбору або спам кодами
    }                                                                                   // завершення оцінки частоти

    float streakPenalty = (float)failureCount * 15.0f;                                  // штраф за накопичені послідовні відмови
    float totalRisk = baseRisk + shiftPenalty + frequencyPenalty + streakPenalty;       // обчислення сумарного індексу загрози
    if (totalRisk > 100.0f) {                                                           // обмеження максимуму ста відсотків
      totalRisk = 100.0f;                                                               // нормування верхньої межі ризику
    }                                                                                   // завершення нормування ризику

    if (role == ROLE_UNKNOWN) {                                                         // прийняття рішення для невідомої картки
      failureCount++;                                                                   // збільшення лічильника порушень
      result.decision = DECISION_DENIED_UNAUTHORIZED;                                   // відмова у доступі невідомому ключу
      result.threat = (totalRisk >= 85.0f) ? THREAT_CRITICAL : THREAT_ELEVATED;         // визначення ступеня загрози карти
      result.auditLog = "Denied. Unauthorized RFID identifier.";                        // повідомлення про заборону проходу
    } else if (shift == SHIFT_NIGHT && role == ROLE_STAFF_NURSE) {                      // перевірка політики нічного обмеження
      failureCount++;                                                                   // інкремент лічильника відхилень
      result.decision = DECISION_DENIED_SHIFT;                                          // відмова через заборону нічного входу
      result.threat = THREAT_ELEVATED;                                                  // підвищений ступінь загрози
      result.auditLog = "Denied. Staff off-shift night access.";                        // повідомлення про порушення графіка
    } else {                                                                            // штатний випадок дозволеного проходу
      failureCount = 0;                                                                 // скидання лічильника відмов
      result.decision = DECISION_GRANTED;                                               // дозвіл відмикання ригеля замка
      result.threat = (totalRisk > 25.0f) ? THREAT_LOW : THREAT_NORMAL;                 // встановлення безпечного рівня загрози
      result.auditLog = "Access granted. Policy check verified.";                       // повідомлення про успішний прохід
    }                                                                                   // завершення умовного аналізу

    if (failureCount >= 3) {                                                            // перевірка порогу критичної серії
      lockoutActive = true;                                                             // ввімкнення блокування термінала
      lockoutStartTime = millis();                                                      // фіксація моменту початку блокування
      result.decision = DECISION_LOCKOUT;                                               // зміна рішення на повне блокування
      result.threat = THREAT_CRITICAL;                                                  // перехід у стан критичної загрози
      totalRisk = 100.0f;                                                               // встановлення стовідсоткового ризику
      result.auditLog = "Breach alert. 3 failures cause lockout.";                      // повідомлення про спрацювання захисту
    }                                                                                   // завершення перевірки блокування

    result.riskScore = totalRisk;                                                       // запис підсумкового бала ризику
    result.consecutiveFailures = failureCount;                                          // запис поточної серії відмов
    return result;                                                                      // повернення результату аудиту
  }                                                                                     // завершення методу оцінки доступу
};                                                                                      // завершення опису класу аудиту

#endif                                                                                  // завершення блоку умовної компіляції