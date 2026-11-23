#include "SettingsWindow.h"
#include "Lang.h"
#include "IconCache.h"
#include "Skin.h"
#include "PinSync.h"
#include "Shell.h"
#include "Theme.h"
#include "Backups.h"
#include "Updater.h"

#include <QCheckBox>
#include "SegSwitch.h"
#include <QDesktopServices>
#include <QUrl>
#include <QCloseEvent>
#include <QColorDialog>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QProcess>
#include <QSignalBlocker>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QScreen>
#include <QSettings>
#include <QStandardPaths>
#include <QSlider>
#include <QSpinBox>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

#ifdef _WIN32
#include <windows.h>
#endif

namespace
{
    const char* kRunKey = "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run";

    void signalEvent(const wchar_t* name)
    {
#ifdef _WIN32
        if (HANDLE h = OpenEventW(EVENT_MODIFY_STATE, FALSE, name))
        {
            SetEvent(h);
            CloseHandle(h);
        }
#else
        Q_UNUSED(name)
#endif
    }

    QString swatchStyle(const QColor& c)
    {
        return QStringLiteral("QPushButton { background: %1; border: 1px solid rgba(255,255,255,60); border-radius: 6px; "
                              "min-width: 56px; min-height: 22px; } QPushButton:hover { border-color: rgba(255,255,255,140); }")
            .arg(c.name());
    }
}

SettingsWindow::SettingsWindow()
{
    setWindowTitle(TXT("Настройки меню «Пуск»"));
    setAttribute(Qt::WA_DeleteOnClose);
    Settings::get().load();

    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(120);
    // Сохранение и сигнал меню — одним разом после паузы, а не на каждый шаг ползунка
    connect(m_debounce, &QTimer::timeout, this, [this] {
        Settings::get().save();
        notify();
    });

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(12);

    // Стиль оформления и пресет
    auto* top = new QGridLayout;
    top->setHorizontalSpacing(12);
    top->addWidget(new QLabel(TXT("Стиль:")), 0, 0);
    auto* skinBox = new QComboBox;
    skinBox->addItems(Skin::names());
    top->addWidget(skinBox, 0, 1);
    top->addWidget(new QLabel(TXT("Пресет:")), 1, 0);
    m_presetBox = new QComboBox;
    m_presetBox->addItems({TXT("Свои настройки"), TXT("Стандартный для стиля"),
                           TXT("Матовое стекло Windows"), TXT("Сильное размытие"),
                           TXT("Светлый"), TXT("Компактный")});
    top->addWidget(m_presetBox, 1, 1);
    // Язык интерфейса — окно сразу пересоздаётся на новом языке
    top->addWidget(new QLabel(TXT("Язык:")), 2, 0);
    auto* langBox = new QComboBox;
    langBox->addItem(TXT("Как в Windows"), QStringLiteral("auto"));
    for (const Lang::Info& l : Lang::available())
        langBox->addItem(l.native, l.code);
    top->addWidget(langBox, 2, 1);
    m_refreshers.append([langBox] { langBox->setCurrentIndex(qMax(0, langBox->findData(Settings::get().language))); });
    connect(langBox, &QComboBox::activated, this, [this, langBox](int i) {
        Settings::get().language = langBox->itemData(i).toString();
        Settings::get().save();
        Lang::load(Settings::get().language);
        notify();
        emit languageChanged();
    });
    top->setColumnStretch(1, 1);
    root->addLayout(top);
    m_refreshers.append([skinBox, this] {
        skinBox->setCurrentIndex(Settings::get().skin);
        m_presetBox->setCurrentIndex(std::clamp(Settings::get().preset, 0, 5));
    });
    // У каждого стиля свой сохранённый вид. Стиль, который ещё не настраивали, — с его стандартными цветами.
    connect(skinBox, &QComboBox::activated, this, [this](int i) {
        Settings& s = Settings::get();
        if (i == s.skin)
            return;
        s.save(); // запомнить вид стиля, с которого уходим
        if (!s.restoreStyle(i))
        {
            applyPreset(1);
            return;
        }
        s.apply();
        m_inPreset = true; // пресет не сбрасываем — он тоже восстановлен из стиля
        loadIntoUi();
        changed();
        m_inPreset = false;
    });
    connect(m_presetBox, &QComboBox::activated, this, [this](int i) {
        if (i > 0)
            applyPreset(i);
        else
            m_presetBox->setCurrentIndex(Settings::get().preset); // «Свои настройки» выбираются сами, правкой
    });

    auto* tabs = new QTabWidget;
    tabs->addTab(buildLookTab(), TXT("Фон и цвета"));
    tabs->addTab(buildTilesTab(), TXT("Плитки и окно"));
    tabs->addTab(buildBehaviorTab(), TXT("Поведение"));
    tabs->addTab(buildDataTab(), TXT("Копии и обновления"));
    root->addWidget(tabs, 1);

    auto* buttons = new QHBoxLayout;
    auto* preview = new QPushButton(TXT("Открыть предпросмотр"));
    auto* reset = new QPushButton(TXT("Сбросить всё"));
    auto* closeBtn = new QPushButton(TXT("Закрыть"));
    buttons->addWidget(preview);
    buttons->addStretch();
    buttons->addWidget(reset);
    buttons->addWidget(closeBtn);
    root->addLayout(buttons);
    connect(preview, &QPushButton::clicked, this, &SettingsWindow::openPreview);
    connect(reset, &QPushButton::clicked, this, [this] {
        if (QMessageBox::question(this, TXT("Сбросить всё"), TXT("Вернуть стандартный вид этого стиля? Поведение и размер окна не изменятся."))
            == QMessageBox::Yes)
            applyPreset(1);
    });
    connect(closeBtn, &QPushButton::clicked, this, &QWidget::close);

    setStyleSheet(QStringLiteral(R"(
        QWidget { background: #1C1D21; color: #E6E8EE; font-size: 13px; }
        QTabWidget::pane { border: 1px solid #33353C; border-radius: 8px; top: -1px; }
        QTabBar::tab { background: transparent; padding: 8px 14px; border: none; color: #9AA0AE; }
        QTabBar::tab:selected { color: #FFFFFF; border-bottom: 2px solid #4C8DFF; }
        QPushButton { background: #2A2C33; border: 1px solid #3A3D45; border-radius: 6px; padding: 6px 14px; }
        QPushButton:hover { background: #33363E; }
        QComboBox, QSpinBox { background: #2A2C33; border: 1px solid #3A3D45; border-radius: 6px; padding: 4px 8px; }
        QComboBox QAbstractItemView { background: #2A2C33; selection-background-color: #4C8DFF; }
        QSlider::groove:horizontal { height: 4px; background: #3A3D45; border-radius: 2px; }
        QSlider::sub-page:horizontal { background: #4C8DFF; border-radius: 2px; }
        QSlider::handle:horizontal { background: #FFFFFF; width: 14px; margin: -6px 0; border-radius: 7px; }
        QCheckBox::indicator { width: 16px; height: 16px; border: 1px solid #5A5E68; border-radius: 4px; background: #2A2C33; }
        QCheckBox::indicator:checked { background: #4C8DFF; border-color: #4C8DFF; image: url(:/res/check.png); }
        QLabel#section { color: #9AA0AE; font-weight: bold; padding-top: 6px; }
        QLabel:disabled, QCheckBox:disabled, QComboBox:disabled, QSpinBox:disabled { color: #5F636D; }
        QSlider::sub-page:horizontal:disabled { background: #454955; }
        QSlider::handle:horizontal:disabled { background: #6A6E78; }
        QCheckBox::indicator:checked:disabled { background: #3A4560; border-color: #3A4560; }
    )"));

    resize(640, 560);
    // Ставим окно у верхнего края экрана, чтобы оно не перекрывало меню предпросмотра снизу.
    if (QScreen* s = QGuiApplication::primaryScreen())
    {
        const QRect r = s->availableGeometry();
        move(r.center().x() - width() / 2, r.top() + 40);
    }
    loadIntoUi();
    WinShell::setDarkTitleBar(this);
}

// ---------- конструкторы контролов ----------

QPushButton* SettingsWindow::colorButton(QColor Settings::* field)
{
    auto* b = new QPushButton;
    b->setCursor(Qt::PointingHandCursor);
    b->setFixedSize(64, 26);
    m_refreshers.append([b, field] { b->setStyleSheet(swatchStyle(Settings::get().*field)); });
    connect(b, &QPushButton::clicked, this, [this, b, field] {
        const QColor c = QColorDialog::getColor(Settings::get().*field, this, TXT("Цвет"));
        if (!c.isValid())
            return;
        Settings::get().*field = c;
        b->setStyleSheet(swatchStyle(c));
        changed();
    });
    return b;
}

QSlider* SettingsWindow::slider(int Settings::* field, int min, int max, const QString& suffix, QWidget** valueLabel)
{
    auto* s = new QSlider(Qt::Horizontal);
    s->setRange(min, max);
    auto* label = new QLabel;
    label->setMinimumWidth(52);
    label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    if (valueLabel)
        *valueLabel = label;
    m_refreshers.append([s, label, field, suffix] {
        s->setValue(Settings::get().*field);
        label->setText(QString::number(Settings::get().*field) + suffix);
    });
    connect(s, &QSlider::valueChanged, this, [this, label, field, suffix](int v) {
        label->setText(QString::number(v) + suffix);
        if (m_loading)
            return;
        Settings::get().*field = v;
        changed();
    });
    return s;
}

QSpinBox* SettingsWindow::spin(int Settings::* field, int min, int max, const QString& suffix)
{
    auto* s = new QSpinBox;
    s->setRange(min, max);
    s->setSuffix(suffix);
    s->setSingleStep(max - min > 200 ? 10 : 1);
    m_refreshers.append([s, field] { s->setValue(Settings::get().*field); });
    connect(s, &QSpinBox::valueChanged, this, [this, field](int v) {
        if (m_loading)
            return;
        Settings::get().*field = v;
        changed();
    });
    return s;
}

QCheckBox* SettingsWindow::check(const QString& text, bool Settings::* field)
{
    auto* c = new SegSwitch(text);
    m_refreshers.append([c, field] { c->setChecked(Settings::get().*field); });
    connect(c, &QCheckBox::toggled, this, [this, field](bool on) {
        if (m_loading)
            return;
        Settings::get().*field = on;
        changed();
    });
    return c;
}

// Слайдер с подписью значения справа — одной строкой для QFormLayout.
static QWidget* withValue(QSlider* s, QWidget* label)
{
    auto* w = new QWidget;
    auto* h = new QHBoxLayout(w);
    h->setContentsMargins(0, 0, 0, 0);
    h->addWidget(s, 1);
    if (label)
        h->addWidget(label);
    return w;
}

static QLabel* section(const QString& text)
{
    auto* l = new QLabel(text);
    l->setObjectName("section");
    return l;
}

// ---------- вкладки ----------

QWidget* SettingsWindow::buildLookTab()
{
    auto* page = new QWidget;
    auto* f = new QFormLayout(page);
    f->setLabelAlignment(Qt::AlignLeft);
    f->setHorizontalSpacing(20);

    f->addRow(section(TXT("Фон меню")));
    auto* theme = new QComboBox;
    theme->addItems({TXT("Тёмная"), TXT("Светлая"), TXT("Как в Windows")});
    m_refreshers.append([theme] { theme->setCurrentIndex(Settings::get().theme); });
    connect(theme, &QComboBox::currentIndexChanged, this, [this](int i) {
        if (m_loading)
            return;
        Settings::get().theme = i;
        changed();
    });
    f->addRow(TXT("Тема"), theme);
    f->addRow(TXT("Цвет фона"), colorButton(&Settings::background));
    QWidget* lbl = nullptr;
    QSlider* s = slider(&Settings::opacity, 0, 100, QStringLiteral(" %"), &lbl);
    f->addRow(TXT("Непрозрачность"), withValue(s, lbl));

    m_effect = new QComboBox;
    m_effect->addItems({TXT("Без эффекта"), TXT("Матовое стекло Windows (живое)"),
                        TXT("Своё размытие (настраиваемое)")});
    m_refreshers.append([this] {
        m_effect->setCurrentIndex(int(Settings::get().effect));
        m_blurSlider->setEnabled(Settings::get().effect == Settings::Effect::Blur);
    });
    connect(m_effect, &QComboBox::currentIndexChanged, this, [this](int i) {
        if (m_loading)
            return;
        Settings::get().effect = Settings::Effect(i);
        m_blurSlider->setEnabled(i == int(Settings::Effect::Blur));
        changed();
    });
    f->addRow(TXT("Эффект"), m_effect);
    m_blurSlider = slider(&Settings::blurRadius, 4, 120, QStringLiteral(" px"), &lbl);
    f->addRow(TXT("Сила размытия"), withValue(m_blurSlider, lbl));
    s = slider(&Settings::radius, 0, 32, QStringLiteral(" px"), &lbl);
    f->addRow(TXT("Скругление углов"), withValue(s, lbl));
    f->addRow(QString(), check(TXT("Тонкая рамка по краю"), &Settings::border));

    f->addRow(section(TXT("Цвета")));
    auto* accentRow = new QWidget;
    auto* ah = new QHBoxLayout(accentRow);
    ah->setContentsMargins(0, 0, 0, 0);
    ah->addWidget(colorButton(&Settings::accent));
    ah->addWidget(check(TXT("как в Windows"), &Settings::systemAccent));
    ah->addStretch();
    f->addRow(TXT("Акцентный цвет"), accentRow);
    f->addRow(TXT("Цвет текста"), colorButton(&Settings::textColor));
    f->addRow(TXT("Цвет групп и плиток"), colorButton(&Settings::groupColor));
    s = slider(&Settings::groupOpacity, 0, 60, QStringLiteral(" %"), &lbl);
    f->addRow(TXT("Фон групп"), withValue(s, lbl));
    s = slider(&Settings::tileOpacity, 0, 60, QStringLiteral(" %"), &lbl);
    f->addRow(TXT("Фон плиток"), withValue(s, lbl));
    return page;
}

QWidget* SettingsWindow::buildTilesTab()
{
    auto* page = new QWidget;
    auto* f = new QFormLayout(page);
    f->setHorizontalSpacing(20);
    QWidget* lbl = nullptr;

    f->addRow(section(TXT("Плитки")));
    f->addRow(QString(), check(TXT("Адаптивная раскладка: плитки и группы подстраиваются под размер окна"),
                               &Settings::adaptive));
    QSlider* s = slider(&Settings::cell, 32, 80, QStringLiteral(" px"), &lbl);
    QWidget* cellRow = withValue(s, lbl);
    f->addRow(TXT("Размер ячейки (без адаптивной)"), cellRow);
    m_enablers.append([cellRow] { cellRow->setEnabled(!Settings::get().adaptive); });
    // Общий переключатель подписей главнее частных: включили — видны у всех, выключили — скрыты у всех.
    auto* labels = new SegSwitch(TXT("Подписи на плитках (у всех групп и плиток)"));
    m_refreshers.append([labels] { labels->setChecked(Settings::get().showLabels); });
    connect(labels, &QCheckBox::toggled, this, [this](bool on) {
        if (m_loading)
            return;
        Config& c = Config::instance();
        {
            const FileLock lock;
            c.load();
            c.resetLabelOverrides();
            c.save();
        }
        if (HANDLE ev = OpenEventW(EVENT_MODIFY_STATE, FALSE, L"Local\\StartMenu.App.Items"))
        {
            SetEvent(ev); // открытое меню перечитает плитки, а не затрёт сброс своей копией
            CloseHandle(ev);
        }
        Settings::get().showLabels = on;
        changed();
    });
    f->addRow(QString(), labels);
    f->addRow(TXT("Шрифт подписей"), spin(&Settings::fontPx, 9, 20, QStringLiteral(" px")));
    f->addRow(section(TXT("Заливка плиток цветом иконки")));
    auto* fill = new QComboBox;
    fill->addItems({TXT("Выключена — обычный фон плитки"),
                    TXT("Цвет иконки — однотонная, цвет подбирается по подложке иконки"),
                    TXT("Размытая иконка — мягкое пятно её цветов")});
    QSlider* fillOp = slider(&Settings::tileFillOpacity, 10, 100, QStringLiteral(" %"), &lbl);
    m_refreshers.append([fill, fillOp] {
        fill->setCurrentIndex(Settings::get().tileFill);
        fillOp->setEnabled(Settings::get().tileFill > 0);
    });
    connect(fill, &QComboBox::currentIndexChanged, this, [this, fillOp](int i) {
        if (m_loading)
            return;
        Settings::get().tileFill = i;
        fillOp->setEnabled(i > 0);
        changed();
    });
    f->addRow(TXT("Заливка"), fill);
    f->addRow(TXT("Непрозрачность заливки"), withValue(fillOp, lbl));

    f->addRow(section(TXT("Плеер (что играет в Windows: Spotify, браузер…)")));
    auto* media = new QComboBox;
    media->addItems({TXT("Не показывать"), TXT("Компактный — в строке поиска"),
                     TXT("Большой — отдельной панелью над меню")});
    m_refreshers.append([media] { media->setCurrentIndex(Settings::get().mediaPlayer); });
    connect(media, &QComboBox::currentIndexChanged, this, [this](int i) {
        if (m_loading)
            return;
        Settings::get().mediaPlayer = i;
        changed();
    });
    f->addRow(TXT("Плеер"), media);
    QList<QWidget*> playerOnly; // всё ниже имеет смысл, только если плеер включён
    auto* art = new QComboBox;
    art->addItems({TXT("Миниатюрой слева"), TXT("На весь фон панели")});
    m_refreshers.append([art] { art->setCurrentIndex(Settings::get().mediaArt); });
    connect(art, &QComboBox::currentIndexChanged, this, [this](int i) {
        if (m_loading)
            return;
        Settings::get().mediaArt = i;
        changed();
    });
    f->addRow(TXT("Обложка в большом плеере"), art);
    playerOnly.append(art);
    auto* mcolor = new QComboBox;
    mcolor->addItems({TXT("Акцентный цвет стиля"), TXT("Из обложки трека")});
    mcolor->setToolTip(TXT("Подсветка в ритм музыки, полоса времени и кнопка «играть»"));
    m_refreshers.append([mcolor] { mcolor->setCurrentIndex(Settings::get().mediaColor); });
    connect(mcolor, &QComboBox::currentIndexChanged, this, [this](int i) {
        if (m_loading)
            return;
        Settings::get().mediaColor = i;
        changed();
    });
    f->addRow(TXT("Цвет плеера"), mcolor);
    playerOnly.append(mcolor);
    QSlider* artOp = slider(&Settings::mediaArtOpacity, 10, 100, QStringLiteral(" %"), &lbl);
    QWidget* artOpRow = withValue(artOp, lbl);
    f->addRow(TXT("Яркость обложки-фона"), artOpRow);
    QCheckBox* wave = check(TXT("Волна на ползунке, пока играет (по громкости звука)"), &Settings::mediaWave);
    QCheckBox* glow = check(TXT("Подсветка снизу в ритм музыки (цвета обложки)"), &Settings::mediaGlow);
    f->addRow(QString(), wave);
    f->addRow(QString(), glow);
    playerOnly << wave << glow;
    m_enablers.append([playerOnly, artOpRow, art, wave, glow] {
        const Settings& st = Settings::get();
        for (QWidget* w : playerOnly)
            w->setEnabled(st.mediaPlayer > 0);
        // обложка, волна и подсветка — только у большого плеера; яркость фона — только при обложке на весь фон
        art->setEnabled(st.mediaPlayer == 2);
        wave->setEnabled(st.mediaPlayer == 2);
        glow->setEnabled(st.mediaPlayer == 2);
        artOpRow->setEnabled(st.mediaPlayer == 2 && st.mediaArt == 1);
    });

    f->addRow(section(TXT("Окно")));
    f->addRow(TXT("Ширина"), spin(&Settings::width, 600, 4000, QStringLiteral(" px")));
    f->addRow(TXT("Высота"), spin(&Settings::height, 300, 2000, QStringLiteral(" px")));

    auto* pos = new QComboBox;
    pos->addItems({TXT("По центру"), TXT("Слева")});
    m_refreshers.append([pos] { pos->setCurrentIndex(int(Settings::get().position)); });
    connect(pos, &QComboBox::currentIndexChanged, this, [this](int i) {
        if (m_loading)
            return;
        Settings::get().position = Settings::Position(i);
        changed();
    });
    f->addRow(TXT("Положение"), pos);
    f->addRow(TXT("Отступ от панели задач"), spin(&Settings::bottomGap, 0, 200, QStringLiteral(" px")));

    auto* anim = new QComboBox;
    anim->addItems({TXT("Без анимации"), TXT("Плавное появление"), TXT("Выезд снизу")});
    m_refreshers.append([anim] { anim->setCurrentIndex(int(Settings::get().animation)); });
    connect(anim, &QComboBox::currentIndexChanged, this, [this](int i) {
        if (m_loading)
            return;
        Settings::get().animation = Settings::Animation(i);
        changed();
    });
    f->addRow(TXT("Анимация открытия"), anim);
    f->addRow(TXT("Длительность"), spin(&Settings::animationMs, 0, 600, TXT(" мс")));
    return page;
}

QWidget* SettingsWindow::buildBehaviorTab()
{
    auto* page = new QWidget;
    auto* v = new QVBoxLayout(page);
    v->addWidget(check(TXT("Клавиша Win открывает это меню"), &Settings::winKey));
    v->addWidget(check(TXT("Кнопка «Пуск» на панели задач открывает это меню"), &Settings::startButton));
    v->addWidget(check(TXT("Закрывать меню после запуска приложения"), &Settings::closeOnLaunch));
    auto* standby = check(TXT("Держать меню наготове — открывается мгновенно (+≈20 МБ памяти в простое)"),
                          &Settings::standby);
    standby->setToolTip(TXT("Меню заранее запускается в фоне и по Win просто показывается."));
    v->addWidget(standby);
    auto* reserve = check(TXT("Резервировать память под меню — открывается мгновенно, даже когда память занята тяжёлыми программами, ≈100 МБ"),
                          &Settings::reserveMemory);
    // Резервирование включает и «наготове» — показываем это честно: галочка стоит и не снимается
    m_enablers.append([standby] {
        const bool reserved = Settings::get().reserveMemory;
        const QSignalBlocker block(standby);
        standby->setChecked(reserved || Settings::get().standby);
        standby->setEnabled(!reserved);
    });
    reserve->setToolTip(TXT("Меню держится наготове, и Windows запрещено выгружать его память на диск."));
    v->addWidget(reserve);

    auto* autostart = new SegSwitch(TXT("Запускать вместе с Windows"));
    autostart->setChecked(QSettings(kRunKey, QSettings::NativeFormat).contains("StartMenu"));
    connect(autostart, &QCheckBox::toggled, this, [](bool on) {
        QSettings reg(kRunKey, QSettings::NativeFormat);
        if (on)
            reg.setValue("StartMenu", QStringLiteral("\"%1\"").arg(QDir::toNativeSeparators(QCoreApplication::applicationFilePath())));
        else
            reg.remove("StartMenu");
    });
    v->addWidget(autostart);

    // Дополнительные функции меню
    auto* clip = check(TXT("История буфера обмена — кнопка слева, точка на ней, когда скопировано новое"),
                       &Settings::clipboardHistory);
    clip->setToolTip(TXT("Пароли из менеджеров паролей не сохраняются. История — в %LOCALAPPDATA%\\StartMenu\\clipboard."));
    v->addWidget(clip);
    v->addWidget(check(TXT("Монитор системы внизу меню (ЦП, память, видеокарта, диск)"), &Settings::systemMonitor));
    v->addWidget(check(TXT("Подсказки клавиш и счётчики групп и плиток"), &Settings::showHints));

    // Что показывать в меню кнопки питания
    v->addSpacing(8);
    auto* powerTitle = new QLabel(TXT("Меню кнопки питания"));
    powerTitle->setStyleSheet(QStringLiteral("font-weight: 600;"));
    v->addWidget(powerTitle);
    v->addWidget(check(TXT("Блокировка"), &Settings::powerLock));
    v->addWidget(check(TXT("Выход из системы"), &Settings::powerSignOut));
    v->addWidget(check(TXT("Спящий режим"), &Settings::powerSleep));
    auto* hib = check(TXT("Гибернация"), &Settings::powerHibernate);
    if (!WinShell::hibernateAvailable())
    {
        hib->setEnabled(false);
        hib->setToolTip(TXT("Гибернация выключена в Windows"));
    }
    v->addWidget(hib);
    v->addWidget(check(TXT("Перезагрузка"), &Settings::powerRestart));
    v->addWidget(check(TXT("Завершение работы"), &Settings::powerShutdown));
    v->addSpacing(8);

    // Чем открывать ссылки на сайты, закреплённые плитками
    auto* browserRow = new QHBoxLayout;
    browserRow->addWidget(new QLabel(TXT("Ссылки на сайты открывать в:")));
    auto* browser = new QComboBox;
    auto fillBrowsers = [browser] {
        browser->clear();
        browser->addItem(TXT("Браузер по умолчанию"), QString());
        for (const auto& b : WinShell::installedBrowsers())
            browser->addItem(b.first, b.second);
        const QString cur = Settings::get().linkBrowser;
        if (!cur.isEmpty() && browser->findData(cur) < 0)
            browser->addItem(QFileInfo(cur).completeBaseName(), cur);
        browser->addItem(TXT("Другой…"), QStringLiteral("?"));
        browser->setCurrentIndex(qMax(0, browser->findData(cur)));
    };
    fillBrowsers();
    m_refreshers.append(fillBrowsers);
    connect(browser, &QComboBox::activated, this, [this, browser, fillBrowsers](int i) {
        QString exe = browser->itemData(i).toString();
        if (exe == QStringLiteral("?"))
        {
            exe = QFileDialog::getOpenFileName(this, TXT("Браузер"), QStringLiteral("C:/Program Files"), QStringLiteral("*.exe"));
            if (exe.isEmpty())
            {
                fillBrowsers();
                return;
            }
        }
        Settings::get().linkBrowser = QDir::toNativeSeparators(exe);
        fillBrowsers();
        changed();
    });
    browserRow->addWidget(browser, 1);
    v->addLayout(browserRow);
    v->addSpacing(8);

    auto* folder = new QPushButton(TXT("Открыть папку с настройками"));
    connect(folder, &QPushButton::clicked, this, [] {
        WinShell::open(QFileInfo(Settings::filePath()).absolutePath());
    });
    auto* verb = new SegSwitch(TXT("Пункт «Закрепить в StartMenu» в контекстном меню Проводника"));
    verb->setChecked(PinSync::explorerVerbInstalled());
    verb->setToolTip(TXT("В Windows 11 он в «Показать дополнительные параметры» (Shift+F10)"));
    connect(verb, &QCheckBox::toggled, this, [](bool on) { PinSync::setExplorerVerb(on); });
    v->addWidget(verb);
    auto* pinHint = new QLabel(TXT("Ярлыки, закреплённые через «Закрепить на начальном экране», появляются в группе «Закреплённые» сами."));
    pinHint->setWordWrap(true);
    pinHint->setStyleSheet(QStringLiteral("color: #9AA0AE;"));
    v->addWidget(pinHint);

    auto* clearIcons = new QPushButton(TXT("Очистить кэш иконок"));
    connect(clearIcons, &QPushButton::clicked, this, [clearIcons] {
        IconCache::clearDiskCache();
        clearIcons->setText(TXT("Кэш иконок очищен"));
    });
    v->addSpacing(12);
    v->addWidget(folder, 0, Qt::AlignLeft);
    v->addWidget(clearIcons, 0, Qt::AlignLeft);
    auto* hint = new QLabel(TXT("Группы и плитки — в items.json, внешний вид — в settings.json.\n"
                                           "Родной «Пуск» Windows по-прежнему открывается по Ctrl+Esc."));
    hint->setStyleSheet(QStringLiteral("color: #9AA0AE;"));
    v->addWidget(hint);
    v->addStretch();
    return page;
}

QWidget* SettingsWindow::buildDataTab()
{
    auto* page = new QWidget;
    auto* v = new QVBoxLayout(page);
    auto grey = [](const QString& text) {
        auto* l = new QLabel(text);
        l->setWordWrap(true);
        l->setStyleSheet(QStringLiteral("color: #9AA0AE;"));
        return l;
    };

    // ---- Резервные копии ----
    v->addWidget(section(TXT("Резервные копии меню")));
    v->addWidget(grey(TXT("Раз в день группы и плитки сохраняются сами. Хранятся последние 7 копий.")));
    auto* restoreRow = new QHBoxLayout;
    auto* copies = new QComboBox;
    auto fillCopies = [copies] {
        copies->clear();
        const QLocale loc = Lang::locale();
        for (const Backups::Entry& e : Backups::list())
            copies->addItem(loc.toString(e.time, QLocale::ShortFormat), e.file);
        if (!copies->count())
            copies->addItem(TXT("Копий пока нет — появятся завтра"), QString());
    };
    fillCopies();
    auto* restoreBtn = new QPushButton(TXT("Восстановить"));
    restoreRow->addWidget(copies, 1);
    restoreRow->addWidget(restoreBtn);
    v->addLayout(restoreRow);

    auto restoreFrom = [this, fillCopies](const QString& file) {
        if (file.isEmpty())
            return;
        if (!Backups::isValidMenuFile(file))
        {
            QMessageBox::warning(this, TXT("Резервные копии меню"), TXT("Это не файл меню StartMenu."));
            return;
        }
        if (QMessageBox::question(this, TXT("Резервные копии меню"),
                                  TXT("Заменить текущие группы и плитки? Текущее меню тоже сохранится в копиях."))
            != QMessageBox::Yes)
            return;
        if (Backups::restore(file))
            QMessageBox::information(this, TXT("Резервные копии меню"), TXT("Меню восстановлено."));
        else
            QMessageBox::warning(this, TXT("Резервные копии меню"), TXT("Не удалось восстановить меню."));
        fillCopies();
    };
    connect(restoreBtn, &QPushButton::clicked, this, [copies, restoreFrom] { restoreFrom(copies->currentData().toString()); });

    auto* fileRow = new QHBoxLayout;
    auto* exportBtn = new QPushButton(TXT("Сохранить меню в файл…"));
    auto* importBtn = new QPushButton(TXT("Загрузить из файла…"));
    connect(exportBtn, &QPushButton::clicked, this, [this] {
        const QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
        const QString file = QFileDialog::getSaveFileName(this, TXT("Сохранить меню в файл…"), docs + QStringLiteral("/StartMenu-menu.json"),
                                                          QStringLiteral("JSON (*.json)"));
        if (!file.isEmpty() && !Backups::exportTo(file))
            QMessageBox::warning(this, TXT("Резервные копии меню"), TXT("Не удалось сохранить файл."));
    });
    connect(importBtn, &QPushButton::clicked, this, [this, restoreFrom] {
        const QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
        restoreFrom(QFileDialog::getOpenFileName(this, TXT("Загрузить из файла…"), docs, QStringLiteral("JSON (*.json)")));
    });
    fileRow->addWidget(exportBtn);
    fileRow->addWidget(importBtn);
    fileRow->addStretch();
    v->addLayout(fileRow);
    v->addWidget(grey(TXT("Файлом меню можно поделиться или перенести его на другой компьютер.")));
    v->addSpacing(14);

    // ---- Обновления ----
    v->addWidget(section(TXT("Обновления")));
    auto* updCheck = check(TXT("Проверять обновления автоматически"), &Settings::checkUpdates);
    updCheck->setToolTip(TXT("Меню спрашивает у GitHub номер последней версии. Никакие данные о вас не отправляются."));
    v->addWidget(updCheck);
    auto* autoInst = check(TXT("Устанавливать обновления автоматически (раз в сутки, не во время игры)"), &Settings::autoUpdate);
    autoInst->setToolTip(TXT("Если вышла новая версия, программа сама скачает и поставит её; меню перезапустится на пару секунд. "
                             "Не делает этого, пока вы в игре на весь экран или в презентации."));
    v->addWidget(autoInst);
    m_enablers.append([autoInst] { autoInst->setEnabled(Settings::get().checkUpdates); });
    v->addWidget(check(TXT("Показывать «Что нового» после обновления"), &Settings::showWhatsNew));
    auto* updRow = new QHBoxLayout;
    auto* updState = new QLabel;
    auto* checkNow = new QPushButton(TXT("Проверить сейчас"));
    auto* installBtn = new QPushButton;
    installBtn->setObjectName(QStringLiteral("primary"));
    updRow->addWidget(updState, 1);
    updRow->addWidget(checkNow);
    updRow->addWidget(installBtn);
    v->addLayout(updRow);
    auto showState = [updState, installBtn] {
        const auto info = Updater::available();
        updState->setText(info ? TXT("Доступна версия %1 (у вас %2)").arg(info->version, Updater::currentVersion())
                               : TXT("Версия %1 — последняя").arg(Updater::currentVersion()));
        installBtn->setVisible(info.has_value());
        if (info)
            installBtn->setText(TXT("Обновить до %1").arg(info->version));
    };
    showState();
    connect(checkNow, &QPushButton::clicked, this, [this, checkNow, updState, showState] {
        checkNow->setEnabled(false);
        updState->setText(TXT("Проверяю…"));
        QPointer<QPushButton> guard(checkNow);
        Updater::check(true, [guard, updState, showState](bool ok) {
            if (!guard)
                return;
            guard->setEnabled(true);
            showState();
            if (!ok)
                updState->setText(TXT("Не удалось связаться с GitHub. Проверьте интернет."));
        });
    });
    connect(installBtn, &QPushButton::clicked, this, [installBtn, updState] {
        if (const auto info = Updater::available())
        {
            installBtn->setEnabled(false);
            updState->setText(TXT("Скачиваю установщик…"));
            QPointer<QPushButton> guard(installBtn);
            Updater::install(*info, [guard, updState](bool ok) {
                if (!guard)
                    return;
                guard->setEnabled(true);
                updState->setText(ok ? TXT("Установщик запущен.") : TXT("Открыл страницу загрузки в браузере."));
            });
        }
    });
    v->addSpacing(14);

    // ---- Обратная связь ----
    v->addWidget(section(TXT("Обратная связь")));
    v->addWidget(grey(TXT("Нашли ошибку или есть идея? Напишите — так меню станет лучше.")));
    auto* report = new QPushButton(TXT("Сообщить об ошибке или идея"));
    connect(report, &QPushButton::clicked, this, [] { QDesktopServices::openUrl(QUrl(Updater::issuesUrl())); });
    v->addWidget(report, 0, Qt::AlignLeft);
    v->addStretch();
    return page;
}

// ---------- логика ----------

void SettingsWindow::reloadFromDisk()
{
    Settings::get().load();
    loadIntoUi();
}

void SettingsWindow::loadIntoUi()
{
    m_loading = true;
    for (const auto& r : m_refreshers)
        r();
    for (const auto& e : m_enablers)
        e();
    m_loading = false;
}

void SettingsWindow::changed()
{
    // Любая ручная правка — это уже «Свои настройки».
    if (!m_inPreset && Settings::get().preset != 0)
    {
        Settings::get().preset = 0;
        m_presetBox->setCurrentIndex(0);
    }
    for (const auto& e : m_enablers)
        e(); // зависимые настройки — включить/выключить
    m_debounce->start(); // сохранится через 120 мс после последнего изменения
}

void SettingsWindow::notify()
{
    signalEvent(L"Local\\StartMenu.App.MenuReload");
    signalEvent(L"Local\\StartMenu.App.HostReload");
}

void SettingsWindow::applyPreset(int index)
{
    Settings& s = Settings::get();
    // Поведение и размер окна пресетом не трогаем.
    const bool winKey = s.winKey, closeOnLaunch = s.closeOnLaunch, startButton = s.startButton;
    const QString avatar = s.avatarPath;
    const int width = s.width, height = s.height, skin = s.skin;
    const bool standby = s.standby, reserve = s.reserveMemory;
    const QString language = s.language;
    const int mediaPlayer = s.mediaPlayer, mediaArt = s.mediaArt, mediaArtOpacity = s.mediaArtOpacity;
    const bool mediaWave = s.mediaWave, mediaGlow = s.mediaGlow;
    const int mediaColor = s.mediaColor;
    const bool clipboardHistory = s.clipboardHistory, systemMonitor = s.systemMonitor;
    const bool showHints = s.showHints, checkUpdates = s.checkUpdates;
    const QString hiddenUpdate = s.hiddenUpdate, lastSeenVersion = s.lastSeenVersion;
    const bool showWhatsNew = s.showWhatsNew, autoUpdate = s.autoUpdate;
    const qint64 lastAutoUpdate = s.lastAutoUpdate;
    const int theme = s.theme;
    const QString linkBrowser = s.linkBrowser;
    const Settings::Position position = s.position;
    const int bottomGap = s.bottomGap;
    const QJsonObject styles = s.styles; // виды других стилей пресет не трогает
    s = Settings();
    s.styles = styles;
    s.standby = standby;
    s.reserveMemory = reserve;
    s.mediaPlayer = mediaPlayer;
    s.language = language;
    s.mediaArt = mediaArt;
    s.mediaArtOpacity = mediaArtOpacity;
    s.mediaWave = mediaWave;
    s.mediaGlow = mediaGlow;
    s.mediaColor = mediaColor;
    s.clipboardHistory = clipboardHistory;
    s.linkBrowser = linkBrowser;
    s.systemMonitor = systemMonitor;
    s.showHints = showHints;
    s.theme = theme;
    s.checkUpdates = checkUpdates;
    s.hiddenUpdate = hiddenUpdate;
    s.lastSeenVersion = lastSeenVersion;
    s.showWhatsNew = showWhatsNew;
    s.autoUpdate = autoUpdate;
    s.lastAutoUpdate = lastAutoUpdate;
    s.position = position;
    s.bottomGap = bottomGap;
    s.skin = skin;
    Skin::select(skin);
    Skin::current().preset(s); // цвета и фон «как задумано» в выбранном стиле
    switch (index)
    {
    case 2: // стекло Windows
        s.effect = Settings::Effect::Acrylic;
        s.opacity = qMin(s.opacity, 60);
        break;
    case 3: // сильное своё размытие
        s.effect = Settings::Effect::Blur;
        s.blurRadius = 80;
        s.opacity = 60;
        break;
    case 4: // светлый — светлая тема с цветами стиля
        s.theme = 1;
        break;
    case 5: // компактный
        s.cell = 38;
        s.fontPx = 11;
        s.height = 440;
        s.radius = 8;
        break;
    default:
        break;
    }
    s.winKey = winKey;
    s.closeOnLaunch = closeOnLaunch;
    s.startButton = startButton;
    s.avatarPath = avatar;
    if (index != 5) // у «Компактного» своя высота
    {
        s.width = width;
        s.height = height;
    }
    s.preset = index;
    s.apply();
    m_inPreset = true;
    loadIntoUi();
    changed();
    m_inPreset = false;
}

void SettingsWindow::openPreview()
{
    m_previewStarted = true;
#ifdef _WIN32
    if (HANDLE m = OpenMutexW(SYNCHRONIZE, FALSE, L"Local\\StartMenu.App.Menu"))
    {
        CloseHandle(m);
        signalEvent(L"Local\\StartMenu.App.Show"); // меню уже запущено (наготове) — показать
        return;
    }
#endif
    WinShell::startSelf(QStringList{QStringLiteral("--menu"), QStringLiteral("--preview")});
}

void SettingsWindow::closeEvent(QCloseEvent* e)
{
    if (m_debounce->isActive())
    {
        m_debounce->stop();
        Settings::get().save();
        notify();
    }
    if (m_previewStarted)
        signalEvent(L"Local\\StartMenu.App.Close");
    QWidget::closeEvent(e);
}
