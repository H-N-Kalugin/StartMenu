#pragma once
#include <QColor>
#include <QJsonObject>
#include <QString>

// Настройки внешнего вида и поведения. Файл: %APPDATA%/StartMenu/settings.json
// Окно настроек (StartMenu.exe --settings) меняет файл и шлёт сигнал открытому меню и процессу трея.
struct Settings
{
    enum class Effect { None, Acrylic, Blur };       // фон: сплошной / матовое стекло Windows / своё размытие
    enum class Position { Center, Left };
    enum class Animation { None, Fade, Slide };

    int skin = 0;                    // стиль оформления (Skin::Kind)
    int theme = 2;                   // тема: 0 — тёмная, 1 — светлая, 2 — как в Windows
    QString language = QStringLiteral("auto"); // язык интерфейса: «auto» — как в Windows, или код («en», «de»…)
    int preset = 1;                  // какой пресет выбран (0 — «Свои настройки»)

    // Фон
    QColor background{20, 20, 22};
    int opacity = 96;                // %, непрозрачность фона панели
    Effect effect = Effect::None;
    int blurRadius = 40;             // сила своего размытия (Effect::Blur)
    int radius = 12;                 // скругление углов окна
    QColor accent{76, 141, 255};
    bool systemAccent = false;       // брать акцентный цвет из Windows
    QColor textColor{237, 239, 244};
    QColor groupColor{255, 255, 255};
    int groupOpacity = 6;            // %, фон групп «с фоном»
    int tileOpacity = 6;             // %, фон плиток в «чистых» группах
    bool border = true;              // тонкая рамка по краю окна
    QString avatarPath;              // своя картинка профиля; пусто — из учётной записи Windows

    // Плитки
    int cell = 46;                   // размер ячейки, px (средняя плитка = 2 ячейки)
    bool showLabels = true;
    bool adaptive = true;            // плитки и группы подстраиваются под размер окна
    int fontPx = 12;

    // Окно
    int width = 1400;
    int height = 520;
    Position position = Position::Center;
    int bottomGap = 12;
    Animation animation = Animation::Slide;
    int animationMs = 140;

    // Поведение
    bool winKey = true;              // одиночное нажатие Win открывает меню
    bool startButton = true;         // клик по кнопке «Пуск» на панели задач открывает меню
    bool closeOnLaunch = true;       // закрывать меню после запуска приложения
    bool standby = true;            // держать меню наготове (открывается мгновенно, +≈10 МБ в простое)
    bool reserveMemory = false;      // + закрепить его память в ОЗУ: мгновенно даже когда ОЗУ занята рендером
    int tileFill = 0;                // фон плитки из иконки: 0 — нет, 1 — цвет иконки, 2 — размытая иконка
    int tileFillOpacity = 85;        // %, непрозрачность этой заливки (100 — полностью)
    int mediaPlayer = 1;             // плеер: 0 — выключен, 1 — компактный в строке поиска, 2 — панель над меню
    int mediaArt = 0;                // обложка в панели: 0 — миниатюра, 1 — на весь фон
    int mediaArtOpacity = 45;        // %, яркость обложки-фона
    bool mediaWave = true;           // волна на ползунке, пока играет
    bool mediaGlow = true;           // подсветка снизу панели в ритм музыки
    QString linkBrowser;             // чем открывать ссылки на сайты: пусто — браузер по умолчанию, иначе путь к exe
    bool clipboardHistory = false;   // история буфера обмена (кнопка слева, точка — есть новое); включается в мастере
    bool systemMonitor = false;      // ЦП / ОЗУ / ГП / диск в нижней строке меню
    bool showHints = false;          // подсказки клавиш и счётчики групп/плиток
    bool checkUpdates = true;        // иногда (раз в пару часов) спрашивать GitHub о новой версии
    QString hiddenUpdate;            // версия, о которой человек попросил больше не напоминать
    QString lastSeenVersion;         // версия, для которой человек уже видел карточку «Что нового»
    int mediaColor = 0;              // цвет плеера (подсветка, полоса, кнопка): 0 — акцент стиля, 1 — из обложки

    // Вид каждого стиля отдельно: {"0": {...}, "1": {...}} — смена стиля не затирает настройки другого.
    mutable QJsonObject styles;

    static Settings& get();
    void load();
    void save() const;
    void apply() const;
    bool restoreStyle(int newSkin);  // переключить стиль; true — у него был сохранённый вид, он восстановлен
    void readLook(const QJsonObject& o);
    QJsonObject lookJson() const;              // переносит значения в Theme
    static QString filePath();
    static QColor windowsAccent();
    static bool windowsLight();      // в Windows включена светлая тема приложений
    bool wantsLight() const;         // какая тема должна быть сейчас
};
