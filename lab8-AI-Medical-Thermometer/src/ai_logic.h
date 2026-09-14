#ifndef AI_LOGIC_H                                              // Перевірка чи не підключено заголовок раніше
#define AI_LOGIC_H                                              // Визначення макросу для запобігання повторному підключенню
#include <Arduino.h>                                            // Бібліотека для роботи з базовими типами мікроконтролера
#include <math.h>                                               // Бібліотека для математичних розрахунків

enum PatientAIState                                             // Оголошення переліку можливих станів пацієнта
{                                                               // Оголошення типу переліку станів пацієнта
    AI_STATE_NORMAL = 0,                                        // Стан нормальної температури тіла
    AI_STATE_SUBFEBRILE = 1,                                    // Стан субфебрильної температури
    AI_STATE_HYPERTHERMIA = 2,                                  // Стан лихоманки та підвищеної температури
    AI_STATE_HYPOTHERMIA = 3,                                   // Стан переохолодження організму
    AI_STATE_ARTIFACT = 4                                       // Стан технічного артефакту або перешкоди сенсора
};                                                              // Завершення переліку станів

inline const char *getAIStateString(PatientAIState state)       // Оголошення функції отримання рядкової назви стану
{                                                               // Функція отримання назви стану
    switch (state)                                              // Вибір текстового позначення за кодом стану
    {                                                           // Вибір назви відповідно до стану
    case AI_STATE_NORMAL:                                       // Варіант нормального стану пацієнта
        return "NORMAL";                                        // Повернення назви нормального стану
    case AI_STATE_SUBFEBRILE:                                   // Варіант субфебрильного стану пацієнта
        return "SUBFEBR";                                       // Повернення назви субфебрильного стану
    case AI_STATE_HYPERTHERMIA:                                 // Варіант гіпертермічного стану пацієнта
        return "HYPERTH";                                       // Повернення назви стану гіпертермії
    case AI_STATE_HYPOTHERMIA:                                  // Варіант гіпотермічного стану пацієнта
        return "HYPOTH";                                        // Повернення назви стану гіпотермії
    case AI_STATE_ARTIFACT:                                     // Варіант артефакту або апаратного збою
        return "ARTIFACT";                                      // Повернення назви стану артефакту
    default:                                                    // Варіант нерозпізнаного стану за замовчуванням
        return "UNKNOWN";                                       // Повернення назви невідомого стану
    }                                                           // Завершення блоку перевірки
}                                                               // Завершення функції отримання назви

inline const char *getAIStateShort(PatientAIState state)        // Оголошення функції отримання короткого імені стану
{                                                               // Функція отримання короткої назви стану
    switch (state)                                              // Вибір скороченого позначення за кодом стану
    {                                                           // Вибір короткої назви за станом
    case AI_STATE_NORMAL:                                       // Коротке позначення нормального стану
        return "NORM";                                          // Скорочення для нормального стану
    case AI_STATE_SUBFEBRILE:                                   // Коротке позначення субфебрильного стану
        return "SUBF";                                          // Скорочення для субфебрильного стану
    case AI_STATE_HYPERTHERMIA:                                 // Коротке позначення гіпертермічного стану
        return "FEVR";                                          // Скорочення для стану лихоманки
    case AI_STATE_HYPOTHERMIA:                                  // Коротке позначення гіпотермічного стану
        return "HYPO";                                          // Скорочення для стану переохолодження
    case AI_STATE_ARTIFACT:                                     // Коротке позначення стану артефакту сенсора
        return "ARTF";                                          // Скорочення для стану артефакту
    default:                                                    // Скорочення для невизначеного стану
        return "UNKN";                                          // Скорочення для невідомого стану
    }                                                           // Завершення блоку вибору
}                                                               // Завершення функції короткої назви

class AIAnomalyDetector                                         // Оголошення класу детекції аномалій температури
{                                                               // Оголошення класу детектора аномалій
public:                                                         // Секція загальнодоступних членів класу
    static const int WINDOW_SIZE = 15;                          // Кількість елементів у ковзному вікні вимірювань
    static constexpr float PHYSIOLOGICAL_MAX_RATE = 0.6f;       // Гранична фізіологічна швидкість зміни температури за секунду
    static constexpr float Z_SCORE_THRESHOLD = 2.3f;            // Статистичний поріг для виявлення відхилення

    AIAnomalyDetector()                                         // Конструктор класу аналізатора вимірювань
    {                                                           // Конструктор об'єкта детектора
        reset();                                                // Виклик функції початкового скидання параметрів
    }                                                           // Завершення конструктора

    void reset()                                                // Метод початкового скидання внутрішніх буферів
    {                                                           // Функція скидання параметрів детектора
        for (int i = 0; i < WINDOW_SIZE; i++)                   // Цикл заповнення ковзного вікна базовими значеннями
        {                                                       // Цикл заповнення масиву початковими значеннями
            window[i] = 36.6f;                                  // Встановлення нормальної температури для кожного елемента
        }                                                       // Завершення циклу ініціалізації
        windowIndex = 0;                                        // Початковий індекс для запису у вікно
        isWindowFilled = false;                                 // Прапорець заповнення вікна як не заповнений
        lastValidTemp = 36.6f;                                  // Початкове значення останньої достовірної температури
        prevRawTemp = 36.6f;                                    // Початкове значення попереднього вимірювання
        lastTimestamp = 0;                                      // Час попереднього вимірювання встановлюється в нуль
        anomalyScore = 0.0f;                                    // Початковий показник аномальності
        currentState = AI_STATE_NORMAL;                         // Початковий стан системи як нормальний
        rollingMean = 36.6f;                                    // Початкове середнє значення вибірки
        rollingStdDev = 0.1f;                                   // Початкове стандартне відхилення вибірки
        filteredTemp = 36.6f;                                   // Початкове значення відфільтрованої температури
        isAnomalyConfirmed = false;                             // Початковий стан підтвердження аномалії
    }                                                           // Завершення функції скидання

    float processSample(float rawTemp, unsigned long timestampMs)                               // Головний метод фільтрації та оцінки чергового виміру
    {                                                                                           // Функція обробки нового вимірювання
        float dt = (lastTimestamp > 0) ? (timestampMs - lastTimestamp) / 1000.0f : 1.0f;        // Розрахунок інтервалу часу між вимірами в секундах
        if (dt <= 0.0f)                                                                         // Захист від нульового чи від'ємного кроку часу
            dt = 1.0f;                                                                          // Захист від нульового чи від'ємного інтервалу часу
        lastTimestamp = timestampMs;                                                            // Збереження поточного часу як попереднього
        float deltaTemp = fabsf(rawTemp - prevRawTemp);                                         // Обчислення абсолютної різниці між вимірами
        float rateOfChange = deltaTemp / dt;                                                    // Обчислення швидкості зміни температури
        prevRawTemp = rawTemp;                                                                  // Оновлення значення попереднього вимірювання
        bool isPhysiologicalArtifact = (rateOfChange > PHYSIOLOGICAL_MAX_RATE);                 // Визначення перевищення граничної швидкості
        calculateStatistics();                                                                  // Розрахунок середнього значення та стандартного відхилення
        float zScore = 0.0f;                                                                    // Початкове значення статистичного відхилення
        if (rollingStdDev > 0.001f)                                                             // Перевірка значущості середньоквадратичного відхилення
        {                                                                                       // Перевірка на ненульове відхилення
            zScore = fabsf(rawTemp - rollingMean) / rollingStdDev;                              // Обчислення статистичного показника відхилення
        }                                                                                       // Завершення перевірки відхилення
        float statAnomaly = (zScore > Z_SCORE_THRESHOLD) ? fminf((zScore - Z_SCORE_THRESHOLD) / 2.0f + 0.5f, 1.0f) : (zScore / (2.0f * Z_SCORE_THRESHOLD));     // Обчислення ступеня статистичної аномальності
        float clinicalRisk = 0.0f;                                                                                                                              // Початкове значення медичного ризику
        if (rawTemp >= 38.0f)                                                   // Перевірка перевищення порогу підвищеної температури
        {                                                                       // Перевірка на високу температуру
            clinicalRisk = fminf((rawTemp - 37.5f) / 2.5f, 1.0f);               // Розрахунок ступеня ризику лихоманки
        }                                                                       // Завершення гілки оцінки високої температури
        else if (rawTemp <= 35.5f)                                              // Перевірка наявності небезпечно низької температури
        {                                                                       // Перевірка на низьку температуру
            clinicalRisk = fminf((36.0f - rawTemp) / 2.0f, 1.0f);               // Розрахунок ступеня ризику переохолодження
        }                                                                       // Завершення блоку оцінки ризику
        if (isPhysiologicalArtifact)                                            // Перевірка умови виявлення фізіологічного артефакту
        {                                                                       // Умова фіксації артефакту сенсора
            currentState = AI_STATE_ARTIFACT;                                   // Встановлення стану артефакту
            anomalyScore = 0.95f;                                               // Призначення високої оцінки технічної аномалії
            filteredTemp = 0.85f * lastValidTemp + 0.15f * rollingMean;         // Згладжування значення без урахування стрибка
        }                                                                       // Завершення блоку реакції на технічний артефакт
        else                                                                    // Обробка достовірних показників сенсора
        {                                                                       // Блок дій за відсутності артефакту
            window[windowIndex] = rawTemp;                                      // Запис нового достовірного виміру у вікно
            windowIndex = (windowIndex + 1) % WINDOW_SIZE;                      // Переміщення індексу вікна за модулем
            if (windowIndex == 0)                                               // Перевірка завершення першого повного циклу вікна
                isWindowFilled = true;                                          // Фіксація повного заповнення масиву
            lastValidTemp = rawTemp;                                            // Оновлення останньої достовірної температури
            float alpha = (zScore > Z_SCORE_THRESHOLD) ? 0.3f : 0.7f;           // Вибір коефіцієнта згладжування
            filteredTemp = alpha * rawTemp + (1.0f - alpha) * filteredTemp;     // Розрахунок згладженого значення температури
            anomalyScore = 0.4f * statAnomaly + 0.6f * clinicalRisk;            // Поєднання статистичного та клінічного показників
            if (anomalyScore > 1.0f)                                            // Обмеження значення аномальності верхньою межею
                anomalyScore = 1.0f;                                            // Обмеження максимального показника одиницею
            if (filteredTemp > 38.0f)                                           // Класифікація стану лихоманки або гіпертермії
            {                                                                   // Перевірка на перевищення порогу лихоманки
                currentState = AI_STATE_HYPERTHERMIA;                           // Встановлення стану гіпертермії
            }                                                                   // Завершення умови визначення гіпертермії
            else if (filteredTemp >= 37.3f)                                     // Класифікація субфебрильного підвищення температури
            {                                                                   // Перевірка на субфебрильний діапазон
                currentState = AI_STATE_SUBFEBRILE;                             // Встановлення субфебрильного стану
            }                                                                   // Завершення умови визначення субфебрилітету
            else if (filteredTemp < 35.5f)                                      // Класифікація стану переохолодження або гіпотермії
            {                                                                   // Перевірка на знижену температуру
                currentState = AI_STATE_HYPOTHERMIA;                            // Встановлення стану гіпотермії
            }                                                                   // Завершення умови визначення гіпотермії
            else                                                                // Класифікація нормальної температури пацієнта
            {                                                                                   // Діапазон нормальної температури
                currentState = AI_STATE_NORMAL;                                                 // Встановлення нормального стану
            }                                                                                   // Завершення класифікації
        }                                                                                       // Завершення гілки валідних даних
        isAnomalyConfirmed = (anomalyScore >= 0.65f && currentState != AI_STATE_ARTIFACT);      // Фіксація високої ймовірності справжньої патології
        return filteredTemp;                                                                    // Повернення очищеного значення температури
    }                                                                                           // Завершення методу обробки вимірювання

    bool shouldTriggerActuator() const                                                          // Оцінка потреби спрацювання охолоджувального пристрою
    {                                                                                           // Метод визначення потреби вмикання актуатора
        if (currentState == AI_STATE_ARTIFACT)                                                  // Блокування включення при виявленні стрибка артефакту
        {                                                                                       // Перевірка наявності артефакту
            return false;                                                                       // Заборона спрацювання під час артефакту
        }                                                                                       // Завершення блоку перевірки
        return (filteredTemp >= 37.5f || currentState == AI_STATE_HYPERTHERMIA);                // Дозвіл на увімкнення охолодження при підтвердженій лихоманці
    }                                                                                           // Завершення методу керування актуатором

    float getAnomalyScore() const { return anomalyScore; }                                      // Отримання числового значення аномальності
    float getAnomalyScorePercent() const { return anomalyScore * 100.0f; }                      // Отримання значення аномальності у відсотках
    PatientAIState getState() const { return currentState; }                                    // Отримання поточного стану пацієнта
    const char *getStateStr() const { return getAIStateString(currentState); }                  // Отримання текстового рядка поточного стану
    const char *getStateShortStr() const { return getAIStateShort(currentState); }              // Отримання короткого рядка поточного стану
    float getFilteredTemp() const { return filteredTemp; }                                      // Отримання значення відфільтрованої температури
    float getRollingMean() const { return rollingMean; }                                        // Отримання середнього значення по вікну
    float getRollingStdDev() const { return rollingStdDev; }                                    // Отримання стандартного відхилення по вікну
    bool isAlertActive() const { return isAnomalyConfirmed; }                                   // Отримання статусу активної клінічної тривоги

private:                                                                                        // Секція приватних членів класу
    float window[WINDOW_SIZE];                                                                  // Масив для збереження вибірки значень вікна
    int windowIndex;                                                                            // Поточна позиція запису в масиві
    bool isWindowFilled;                                                                        // Ознака повного заповнення вибірки вікна
    float lastValidTemp;                                                                        // Збережене попереднє достовірне значення
    float prevRawTemp;                                                                          // Збережене попереднє сире значення
    unsigned long lastTimestamp;                                                                // Часова мітка попереднього вимірювання
    float rollingMean;                                                                          // Поточне ковзне середнє значення
    float rollingStdDev;                                                                        // Поточне середньоквадратичне відхилення
    float filteredTemp;                                                                         // Поточна розрахована температура
    float anomalyScore;                                                                         // Поточна інтегральна оцінка аномальності
    PatientAIState currentState;                                                                // Поточний клінічний стан у системі
    bool isAnomalyConfirmed;                                                                    // Прапорець підтвердженої тривоги

    void calculateStatistics()                                                                  // Внутрішній метод розрахунку параметрів розподілу
    {                                                                                           // Функція розрахунку статистичних параметрів вікна
        int count = isWindowFilled ? WINDOW_SIZE : (windowIndex == 0 ? 1 : windowIndex);        // Визначення кількості доступних зразків для аналізу
        float sum = 0.0f;                                                                       // Змінна накопичення суми вимірювань
        for (int i = 0; i < count; i++)                                                         // Цикл підсумовування значень поточного вікна
        {                                                                                       // Цикл додавання значень вибірки
            sum += window[i];                                                                   // Додавання чергового значення до загальної суми
        }                                                                                       // Завершення циклу сумування
        rollingMean = sum / count;                                                              // Обчислення середнього арифметичного значення
        float varianceSum = 0.0f;                                                               // Змінна накопичення суми квадратів різниць
        for (int i = 0; i < count; i++)                                                         // Цикл накопичення квадратів відхилень вибірки
        {                                                                                       // Цикл обчислення квадратичних відхилень
            float diff = window[i] - rollingMean;                                               // Розрахунок різниці між елементом і середнім
            varianceSum += diff * diff;                                                         // Додавання квадрата різниці до суми дисперсії
        }                                                                                       // Завершення циклу розрахунку дисперсії
        rollingStdDev = sqrtf(varianceSum / count);                                             // Обчислення квадратного кореня з дисперсії
        if (rollingStdDev < 0.05f)                                                              // Обмеження мінімального порогу дисперсії від нуля
            rollingStdDev = 0.05f;                                                              // Встановлення мінімального порогу для запобігання діленню на нуль
    }                                                                                           // Завершення функції розрахунку статистики
};                                                                                              // Завершення оголошення класу детектора

inline const char *getLLMConsentTemplate()                                                                      // Оголошення функції шаблону інформованої згоди пацієнта
{                                                                                                               // Функція повернення шаблону інформованої згоди користувача
    return                                                                                                      // Повернення тексту інформованої згоди
        "INFORMED CONSENT FOR AI/LLM PROCESSING OF BIOSENSOR DATA\r\n"                                          // Заголовок документа інформованої згоди пацієнта
        "1. PURPOSE OF SYSTEM:\r\n"                                                                             // Перший розділ про призначення системи
        "   This device monitors body temperature using DS18B20 biosensor and applies\r\n"                      // Опис моніторингу температури та біосенсора
        "   AI Anomaly Detection and LLM analysis for educational and research purposes.\r\n\r\n"               // Опис навчального призначення аналітики
        "2. DATA PROCESSING:\r\n"                                                                               // Другий розділ про збір та обробку даних
        "   - Data: Dynamic temperature series, dT/dt rate, Z-score, timestamps.\r\n"                           // Перелік параметрів що збираються сенсором
        "   - AI Methods: Edge anomaly filtering and telemetry reporting to LLM API.\r\n\r\n"                   // Опис методів обробки даних алгоритмами
        "3. HUMAN-IN-THE-LOOP PRINCIPLE:\r\n"                                                                   // Третій розділ про нагляд медичного фахівця
        "   AI functions solely as a clinical decision support system. It does not provide\r\n"                 // Зазначення допоміжного характеру висновків алгоритму
        "   final diagnosis or prescriptions. Decisions must be verified by a medical professional.\r\n\r\n"    // Вимога обов'язкової перевірки лікарем
        "4. PRIVACY & COMPLIANCE (GDPR / EU AI ACT):\r\n"                                                       // Четвертий розділ про захист персональних даних
        "   - All telemetry transmitted to LLM is pseudonymized.\r\n"                                           // Підтвердження знеособлення телеметричних пакетів
        "   - Data is not used for training public models.\r\n"                                                 // Заборона використання даних пацієнта для донавчання
        "   - User may revoke consent at any time.\r\n\r\n"                                                     // Право користувача на відкликання згоди
        "5. CONFIRMATION:\r\n"                                                                                  // П'ятий розділ підтвердження надання згоди
        "   [X] Consent granted for automated biosensor analysis.\r\n";                                         // Підтвердження користувачем згоди на аналіз
}                                                                                                               // Завершення функції повернення шаблону

inline String generateLLMClinicalPrompt(float currentTemp, float rawTemp, float anomalyScore, PatientAIState state, unsigned long count)    // Функція підготовки клінічного промпта з даними біосенсора
{                                                                                           // Функція генерації клінічного промпта
    String prompt = "";                                                                     // Створення порожнього об'єкта рядка для формування запиту
    prompt += "=== LLM CLINICAL DECISION SUPPORT PROMPT ===\r\n";                           // Додавання заголовка структурованого промпта
    prompt += "Role: Clinical AI engineer and HealthTech consultant.\r\n";                  // Визначення ролі експерта для мовної моделі
    prompt += "Task: Analyze biosensor telemetry and provide clinical summary.\r\n\r\n";    // Формулювання завдання для моделі
    prompt += "[TELEMETRY DATA]:\r\n";                                                      // Секція вхідних телеметричних даних пацієнта
    prompt += "- Sample count: #" + String(count) + "\r\n";                                 // Додавання номера вимірювання до тексту
    prompt += "- Filtered temperature: " + String(currentTemp, 2) + " C\r\n";               // Додавання значення відфільтрованої температури
    prompt += "- Raw biosensor signal: " + String(rawTemp, 2) + " C\r\n";                   // Додавання первинного сигналу біосенсора
    prompt += "- AI Anomaly Score: " + String(anomalyScore * 100.0f, 1) + " %\r\n";         // Додавання розрахованого показника аномальності
    prompt += "- AI State: " + String(getAIStateString(state)) + "\r\n";                    // Додавання текстового опису клінічного стану
    prompt += "- Consent status: Verified\r\n\r\n";                                         // Підтвердження верифікації згоди на обробку
    prompt += "[REQUIREMENTS]:\r\n";                                                        // Секція вимог до структури відповіді
    prompt += "1. Clinical interpretation.\r\n";                                            // Вимога клінічної інтерпретації показників
    prompt += "2. Noise/artifact analysis.\r\n";                                            // Вимога аналізу наявності шумів та перешкод
    prompt += "3. Actuator recommendations.\r\n";                                           // Вимога надання рекомендацій для актуатора
    prompt += "4. Human-in-the-loop reminder.\r\n";                                         // Вимога нагадування про відповідальність лікаря
    return prompt;                                                                          // Повернення сформованого тексту клінічного запиту
}                                                                                           // Завершення функції генерації клінічного промпта

#endif                                                                                      // Завершення директиви умовного включення заголовка