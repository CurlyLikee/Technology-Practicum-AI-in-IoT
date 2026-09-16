#ifndef AI_MODEL_H                                                                      // Захист від повторного включення заголовного файлу
#define AI_MODEL_H                                                                      // Визначення макросу заголовного файлу штучного інтелекту

#include <Arduino.h>                                                                    // Підключення базової бібліотеки середовища Ардуіно

struct AgroTelemetry {                                                                  // Структура для збереження вхідних показників сенсорів
  float airTemperature;                                                                 // Температура повітря у градусах Цельсія
  float airHumidity;                                                                    // Відносна вологість повітря у відсотках
  int soilMoistureCapacitive;                                                           // Показник місткісного сенсора вологості грунту у відсотках
  int soilMoistureResistive;                                                            // Показник резистивного сенсора вологості грунту у відсотках
  float soilPh;                                                                         // Рівень водневого показника кислотності грунту
};                                                                                      // Завершення опису структури вхідних телеметричних даних

struct AIPredictionResult {                                                             // Структура для збереження результатів роботи штучного інтелекту
  float effectiveMoisture;                                                              // Усереднена ефективна вологість грунту у відсотках
  float vpdKpa;                                                                         // Дефіцит тиску насиченої пари у кілопаскалях
  float predictedMoisture1h;                                                            // Прогнозована вологість грунту через одну годину у відсотках
  float dryingRatePerHour;                                                              // Швидкість висихання грунту у відсотках за годину
  int irrigationScore;                                                                  // Індекс потреби поливу за шкалою від нуля до ста
  int recommendedDurationSec;                                                           // Рекомендована тривалість роботи насоса у секундах
  const char* soilHealthState;                                                          // Текстовий вердикт щодо агрономічного стану грунту
  const char* urgentAction;                                                             // Рекомендована дія системи для оператора або автоматики
  bool wateringRecommended;                                                             // Прапорець доцільності увімкнення поливу на основі аналізу
};                                                                                      // Завершення опису структури вихідного прогнозу моделі

class AgroAIEngine {                                                                    // Клас інтелектуального аналізу та прогнозування стану агросистеми
public:                                                                                 // Секція публічних методів класу
  AIPredictionResult analyze(const AgroTelemetry& data);                                // Метод виконання повного циклу аналізу та формування прогнозу
  void printLLMDigest(const AgroTelemetry& data,                                        // Метод генерації розгорнутого аналітичного дайджесту
                      const AIPredictionResult& res);                                   // Продовження параметрів методу генерації дайджесту
private:                                                                                // Секція приватних розрахункових методів класу
  float calculateVPD(float temp, float humidity);                                       // Розрахунок дефіциту тиску водяної пари у повітрі
  float estimateSoilMoisture(int capacitive, int resistive);                            // Оцінка інтегральної вологості на основі двох датчиків
  int computeIrrigationScore(float moisture, float vpd,                                 // Розрахунок індексу потреби поливу на основі факторів
                             float ph);                                                 // Продовження списку аргументів розрахунку індексу поливу
};                                                                                      // Завершення оголошення класу інтелектуального рушія

inline float AgroAIEngine::calculateVPD(float temp,                                     // Реалізація методу обчислення дефіциту тиску пари
                                        float humidity) {                               // Продовження сигнатури методу розрахунку дефіциту пари
  float satVapor = 0.61078 * exp((17.27 * temp) /                                       // Обчислення тиску насиченої водяної пари
                                 (temp + 237.3));                                       // Завершення формули насиченої водяної пари Магнуса
  float actVapor = satVapor * (humidity / 100.0);                                       // Обчислення фактичного тиску водяної пари у повітрі
  float vpd = satVapor - actVapor;                                                      // Обчислення різниці тисків насиченої та фактичної пари
  return (vpd < 0.0) ? 0.0 : vpd;                                                       // Повернення невідємного значення дефіциту тиску пари
}                                                                                       // Завершення методу обчислення дефіциту водяної пари

inline float AgroAIEngine::estimateSoilMoisture(int capacitive,                         // Реалізація методу зваженої оцінки вологості грунту
                                                int resistive) {                        // Продовження списку параметрів оцінки вологості
  float capPart = (float)capacitive * 0.70;                                             // Ваговий внесок показника місткісного сенсора сімдесят відсотків
  float resPart = (float)resistive * 0.30;                                              // Ваговий внесок показника резистивного сенсора тридцять відсотків
  float combined = capPart + resPart;                                                   // Обчислення сумарної інтегральної вологості грунту
  if (combined < 0.0) return 0.0;                                                       // Захисне обмеження мінімального значення вологості нулем
  if (combined > 100.0) return 100.0;                                                   // Захисне обмеження максимального значення вологості сотнею
  return combined;                                                                      // Повернення розрахованої зваженої вологості грунту
}                                                                                       // Завершення методу оцінки інтегральної вологості грунту

inline int AgroAIEngine::computeIrrigationScore(float moisture,                         // Реалізація методу обчислення потреби зрошення
                                               float vpd,                               // Параметр дефіциту тиску пари у повітрі
                                               float ph) {                              // Параметр кислотності грунту для врахування стресу
  float moistureDeficit = (moisture < 50.0) ? (50.0 - moisture) : 0.0;                  // Визначення дефіциту вологи відносно цільового рівня
  float score = moistureDeficit * 1.6;                                                  // Базовий внесок дефіциту вологи у підсумковий бал
  if (vpd > 1.2) {                                                                      // Перевірка підвищеного випаровування вологи в атмосферу
    score += (vpd - 1.2) * 12.0;                                                        // Додатковий бал через інтенсивне підсихання грунту
  }                                                                                     // Завершення блоку врахування дефіциту тиску пари
  if (ph < 5.8 || ph > 7.5) {                                                           // Перевірка відхилення кислотності від оптимального діапазону
    score += 5.0;                                                                       // Коригування пріоритету поливу для зниження осмотичного стресу
  }                                                                                     // Завершення блоку врахування кислотності грунту
  if (score < 0.0) score = 0.0;                                                         // Обмеження підсумкового балу знизу нулем
  if (score > 100.0) score = 100.0;                                                     // Обмеження підсумкового балу зверху сотнею
  return (int)(score + 0.5);                                                            // Повернення округленого цілочисельного значення індексу
}                                                                                       // Завершення методу розрахунку індексу потреби зрошення

inline AIPredictionResult AgroAIEngine::analyze(const AgroTelemetry& data) {            // Реалізація методу комплексного інтелектуального аналізу
  AIPredictionResult res;                                                               // Створення обєкта для збереження вихідних даних аналізу
  res.effectiveMoisture = estimateSoilMoisture(data.soilMoistureCapacitive,             // Обчислення зваженого значення поточної вологості грунту
                                              data.soilMoistureResistive);              // Передача показника другого датчика у метод оцінки
  res.vpdKpa = calculateVPD(data.airTemperature, data.airHumidity);                     // Обчислення дефіциту тиску пари для оцінки транспірації
  float baseLoss = 0.9;                                                                 // Базова швидкість випаровування вологи за одну годину
  float vpdLossFactor = 1.0 + (res.vpdKpa * 0.65);                                      // Коефіцієнт прискорення підсихання грунту під дією атмосфери
  res.dryingRatePerHour = baseLoss * vpdLossFactor;                                     // Розрахунок прогнозованої швидкості втрати вологи за годину
  res.predictedMoisture1h = res.effectiveMoisture - res.dryingRatePerHour;              // Прогнозування залишкової вологості грунту через годину
  if (res.predictedMoisture1h < 0.0) {                                                  // Захисна перевірка на випадок відємного прогнозу вологості
    res.predictedMoisture1h = 0.0;                                                      // Приведення мінімального прогнозу до нуля відсотків
  }                                                                                     // Завершення блоку захисту нижньої межі вологості
  res.irrigationScore = computeIrrigationScore(res.effectiveMoisture,                   // Розрахунок інтелектуального індексу необхідності поливу
                                              res.vpdKpa, data.soilPh);                 // Передача параметрів мікроклімату та грунту для оцінки
  res.wateringRecommended = (res.irrigationScore >= 45 ||                               // Формування рішення про рекомендацію поливу рослин
                             res.effectiveMoisture < 30.0);                             // Додаткова умова критично низького рівня вологи грунту
  if (res.wateringRecommended) {                                                        // Умова потреби проведення штучного зволоження грунту
    float targetDeficit = 50.0 - res.effectiveMoisture;                                 // Розрахунок дефіциту вологості до оптимального значення
    int duration = (int)(targetDeficit * 0.75 + res.vpdKpa * 2.0);                      // Адаптивний розрахунок тривалості поливу у секундах
    if (duration < 6) duration = 6;                                                     // Мінімальна ефективна тривалість роботи насоса шість секунд
    if (duration > 30) duration = 30;                                                   // Максимальна безпечна тривалість роботи насоса тридцять секунд
    res.recommendedDurationSec = duration;                                              // Фіксація рекомендованої тривалості роботи помпи поливу
  } else {                                                                              // Гілка умови коли полив грунту зараз не потрібен
    res.recommendedDurationSec = 0;                                                     // Встановлення нульової рекомендованої тривалості зрошення
  }                                                                                     // Завершення блоку розрахунку тривалості роботи помпи
  if (data.soilPh < 5.5) {                                                              // Перевірка показника кислотності на сильне закислення
    res.soilHealthState = "Acidic soil, risk of phosphorus lockout";                    // Текстова діагностика грунту з підвищеною кислотністю
  } else if (data.soilPh <= 7.2) {                                                      // Перевірка показника на оптимальний нейтральний рівень
    res.soilHealthState = "Optimal neutral balance for plant nutrition";                // Текстова діагностика оптимального стану для кореневої системи
  } else {                                                                              // Гілка умови якщо грунт має лужну реакцію середовища
    res.soilHealthState = "Alkaline soil, risk of micronutrient deficiency";            // Текстова діагностика лужного стану грунтового субстрату
  }                                                                                     // Завершення блоку визначення стану кислотності грунту
  if (res.effectiveMoisture < 20.0) {                                                   // Перевірка на настання екстремальної посухи грунту
    res.urgentAction = "Emergency deep watering, wilting danger";                       // Рекомендація екстреного втручання для порятунку сходів
  } else if (res.wateringRecommended) {                                                 // Перевірка на звичайну потребу у поливі рослин
    res.urgentAction = "Scheduled irrigation based on drying forecast";                 // Рекомендація вчасного планового зволоження грунту
  } else if (res.effectiveMoisture > 75.0) {                                            // Перевірка грунту на наявність надлишкового зволоження
    res.urgentAction = "Excessive moisture, irrigation paused, fan cooling";            // Рекомендація зупинити подачу води та активувати обдув
  } else {                                                                              // Гілка умови комфортного стану вологості кореневої зони
    res.urgentAction = "Moisture within optimal range, irrigation not required";        // Підтвердження оптимального гідротермічного режиму посіву
  }                                                                                     // Завершення блоку формування фінальної рекомендації дії
  return res;                                                                           // Повернення заповненої структури з результатами аналізу
}                                                                                       // Завершення методу комплексного інтелектуального аналізу

inline void AgroAIEngine::printLLMDigest(const AgroTelemetry& data,                     // Реалізація методу виводу текстового дайджесту моделі
                                        const AIPredictionResult& res) {                // Продовження аргументів методу друку агрономічного дайджесту
  Serial.println();                                                                     // Вивід порожнього розділювального рядка перед звітом
  Serial.println("[LLM AGRO STATE DIGEST]");                                            // Заголовок блоку інтелектуального звіту англійською мовою
  Serial.println("1. MICROCLIMATE AND ATMOSPHERE:");                                    // Заголовок розділу атмосферних кліматичних показників
  Serial.print("   Air Temperature:        ");                                          // Вивід текстової мітки температури навколишнього повітря
  Serial.print(data.airTemperature, 1);                                                 // Друк числового значення поточної температури повітря
  Serial.println(" C");                                                                 // Друк одиниці вимірювання температури у градусах Цельсія
  Serial.print("   Air Humidity:           ");                                          // Вивід текстової мітки відносної вологості атмосфери
  Serial.print(data.airHumidity, 1);                                                    // Друк числового значення поточної вологості атмосфери
  Serial.println(" %");                                                                 // Друк знаку відсотка для відносної вологості повітря
  Serial.print("   Vapor Pressure Deficit: ");                                          // Вивід мітки дефіциту тиску насиченої водяної пари
  Serial.print(res.vpdKpa, 2);                                                          // Друк обчисленого значення дефіциту тиску водяної пари
  Serial.println(" kPa");                                                               // Друк одиниці вимірювання тиску у кілопаскалях
  Serial.println("2. SOIL CONDITION AND FORECAST:");                                    // Заголовок розділу параметрів грунтового середовища
  Serial.print("   Current Soil Moisture:  ");                                          // Вивід текстової мітки інтегральної вологості грунту
  Serial.print(res.effectiveMoisture, 1);                                               // Друк розрахованого значення поточної вологості грунту
  Serial.println(" %");                                                                 // Друк знаку відсотка для значення поточної вологості грунту
  Serial.print("   Soil Drying Rate:       ");                                          // Вивід мітки розрахованої швидкості втрати вологи грунтом
  Serial.print(res.dryingRatePerHour, 2);                                               // Друк швидкості висихання субстрату у відсотках за годину
  Serial.println(" % per hour");                                                        // Друк одиниці вимірювання динаміки втрати вологості
  Serial.print("   Predicted Moisture 1h:  ");                                          // Вивід мітки прогнозованої вологості грунту через годину
  Serial.print(res.predictedMoisture1h, 1);                                             // Друк прогнозованого значення залишкової вологості грунту
  Serial.println(" %");                                                                 // Друк знаку відсотка для прогнозованої вологості грунту
  Serial.print("   Soil pH Level:          ");                                          // Вивід текстової мітки водневого показника кислотності
  Serial.print(data.soilPh, 2);                                                         // Друк виміряного значення водневого показника кислотності
  Serial.println();                                                                     // Перехід на новий рядок після виводу числа кислотності
  Serial.print("   Substrate Health:       ");                                          // Вивід мітки якісної агрономічної оцінки стану грунту
  Serial.println(res.soilHealthState);                                                  // Друк вердикту штучного інтелекту щодо кислотності грунту
  Serial.println("3. AI ANALYTICS AND DECISION:");                                      // Заголовок розділу рекомендацій та керуючих рішень моделі
  Serial.print("   Irrigation Need Index:  ");                                          // Вивід текстової мітки індексу терміновості зволоження
  Serial.print(res.irrigationScore);                                                    // Друк обчисленого індексу потреби проведення поливу
  Serial.println(" of 100");                                                            // Друк шкали оцінювання терміновості поливу грунту
  Serial.print("   Irrigation Recommended: ");                                          // Вивід текстової мітки рішення щодо увімкнення поливу
  Serial.println(res.wateringRecommended ? "YES" : "NO");                               // Друк позитивного або негативного рішення щодо зрошення
  if (res.wateringRecommended) {                                                        // Умова виводу розрахованої тривалості зволоження грунту
    Serial.print("   Calculated Duration:    ");                                        // Вивід мітки розрахованого часу подачі поливної води
    Serial.print(res.recommendedDurationSec);                                           // Друк рекомендованої тривалості роботи поливної помпи
    Serial.println(" sec");                                                             // Друк одиниці вимірювання тривалості поливу у секундах
  }                                                                                     // Завершення блоку виводу рекомендованого часу роботи помпи
  Serial.print("   Action Plan:            ");                                          // Вивід мітки головної експертної рекомендації системи
  Serial.println(res.urgentAction);                                                     // Друк сформованого підсумкового висновку штучного інтелекту
  Serial.println();                                                                     // Вивід завершального порожнього рядка після дайджесту
}                                                                                       // Завершення реалізації методу друку дайджесту стану

#endif                                                                                  // Завершення блоку умовної компіляції заголовного файлу
