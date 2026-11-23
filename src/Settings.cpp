#include "Settings.h"
#include "Config.h"
#include "Skin.h"
#include "Theme.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#include <dwmapi.h>
#endif

namespace
{
    QColor colorFrom(const QJsonValue& v, const QColor& def)
    {
        const QColor c = QColor::fromString(v.toString());
        return c.isValid() ? c : def;
    }

    int pct(int percent) { return std::clamp(percent, 0, 100) * 255 / 100; }
}

Settings& Settings::get()
{
    static Settings s;
    return s;
}

QString Settings::filePath()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + "/settings.json";
}

void Settings::readLook(const QJsonObject& o)
{
    preset = o["preset"].toInt(o.isEmpty() ? 1 : 0); // файл без поля «preset» (старый) — это «Свои настройки»
    background = colorFrom(o["background"], background);
    opacity = o["opacity"].toInt(opacity);
    effect = Effect(std::clamp(o["effect"].toInt(int(effect)), 0, 2));
    blurRadius = o["blurRadius"].toInt(blurRadius);
    radius = o["radius"].toInt(radius);
    accent = colorFrom(o["accent"], accent);
    systemAccent = o["systemAccent"].toBool(systemAccent);
    textColor = colorFrom(o["textColor"], textColor);
    groupColor = colorFrom(o["groupColor"], groupColor);
    groupOpacity = o["groupOpacity"].toInt(groupOpacity);
    tileOpacity = o["tileOpacity"].toInt(tileOpacity);
    border = o["border"].toBool(border);
    cell = o["cell"].toInt(cell);
    showLabels = o["showLabels"].toBool(showLabels);
    adaptive = o["adaptive"].toBool(adaptive);
    fontPx = o["fontPx"].toInt(fontPx);
    animation = Animation(std::clamp(o["animation"].toInt(int(animation)), 0, 2));
    animationMs = o["animationMs"].toInt(animationMs);
    tileFill = std::clamp(o["tileFill"].toInt(o["tileIconFill"].toBool() ? 1 : 0), 0, 2); // старый флаг → «цвет иконки»
    tileFillOpacity = std::clamp(o["tileFillOpacity"].toInt(tileFillOpacity), 0, 100);
}

QJsonObject Settings::lookJson() const
{
    return QJsonObject{
        {"preset", preset},
        {"background", background.name()},   {"opacity", opacity},
        {"effect", int(effect)},             {"blurRadius", blurRadius},
        {"radius", radius},                  {"accent", accent.name()},
        {"systemAccent", systemAccent},      {"textColor", textColor.name()},
        {"groupColor", groupColor.name()},   {"groupOpacity", groupOpacity},
        {"tileOpacity", tileOpacity},        {"border", border},
        {"cell", cell},                      {"showLabels", showLabels},
        {"adaptive", adaptive},              {"fontPx", fontPx},
        {"animation", int(animation)},       {"animationMs", animationMs},
        {"tileFill", tileFill},              {"tileFillOpacity", tileFillOpacity},
    };
}

bool Settings::restoreStyle(int newSkin)
{
    const QJsonValue v = styles.value(QString::number(newSkin));
    skin = newSkin;
    if (!v.isObject())
        return false;
    readLook(v.toObject());
    return true;
}

void Settings::load()
{
    *this = Settings(); // всё, чего нет в файле, — по умолчанию
    bool ok = true;
    QJsonObject o = readJsonFile(filePath(), &ok);
    if (!ok)
    {
        bool bakOk = true;
        o = readJsonFile(filePath() + QStringLiteral(".bak"), &bakOk);
        loadFailed = !bakOk || o.isEmpty();
    }

    skin = std::clamp(o["skin"].toInt(skin), 0, 3);
    language = o["language"].toString(language);
    readLook(o);
    styles = o["styles"].toObject();
    avatarPath = o["avatarPath"].toString();
    width = o["width"].toInt(width);
    height = o["height"].toInt(height);
    position = Position(std::clamp(o["position"].toInt(int(position)), 0, 1));
    bottomGap = o["bottomGap"].toInt(bottomGap);
    winKey = o["winKey"].toBool(winKey);
    startButton = o["startButton"].toBool(startButton);
    closeOnLaunch = o["closeOnLaunch"].toBool(closeOnLaunch);
    standby = o["standby"].toBool(standby);
    reserveMemory = o["reserveMemory"].toBool(reserveMemory);
    mediaPlayer = std::clamp(o["mediaPlayer"].toInt(mediaPlayer), 0, 2);
    mediaArt = std::clamp(o["mediaArt"].toInt(mediaArt), 0, 1);
    mediaArtOpacity = std::clamp(o["mediaArtOpacity"].toInt(mediaArtOpacity), 10, 100);
    mediaWave = o["mediaWave"].toBool(mediaWave);
    mediaGlow = o["mediaGlow"].toBool(mediaGlow);
    mediaColor = std::clamp(o["mediaColor"].toInt(mediaColor), 0, 1);
    clipboardHistory = o["clipboardHistory"].toBool(clipboardHistory);
    linkBrowser = o["linkBrowser"].toString();
    systemMonitor = o["systemMonitor"].toBool(systemMonitor);
    theme = std::clamp(o["theme"].toInt(theme), 0, 2);
    showHints = o["showHints"].toBool(showHints);
    powerLock = o["powerLock"].toBool(powerLock);
    powerSignOut = o["powerSignOut"].toBool(powerSignOut);
    powerSleep = o["powerSleep"].toBool(powerSleep);
    powerHibernate = o["powerHibernate"].toBool(powerHibernate);
    powerRestart = o["powerRestart"].toBool(powerRestart);
    powerShutdown = o["powerShutdown"].toBool(powerShutdown);
    checkUpdates = o["checkUpdates"].toBool(checkUpdates);
    hiddenUpdate = o["hiddenUpdate"].toString();
    lastSeenVersion = o["lastSeenVersion"].toString();
    showWhatsNew = o["showWhatsNew"].toBool(true);
    autoUpdate = o["autoUpdate"].toBool(false);
    lastAutoUpdate = o["lastAutoUpdate"].toInteger();
    apply();
}

void Settings::save() const
{
    if (loadFailed)
        return; // настройки не прочитались — не затираем их значениями по умолчанию
    // Вид текущего стиля запоминаем отдельно — при возврате к этому стилю он восстановится.
    styles[QString::number(skin)] = lookJson();
    QJsonObject o = lookJson();
    o["skin"] = skin;
    o["language"] = language;
    o["styles"] = styles;
    o["avatarPath"] = avatarPath;
    o["width"] = width;
    o["height"] = height;
    o["position"] = int(position);
    o["bottomGap"] = bottomGap;
    o["winKey"] = winKey;
    o["startButton"] = startButton;
    o["closeOnLaunch"] = closeOnLaunch;
    o["standby"] = standby;
    o["reserveMemory"] = reserveMemory;
    o["mediaPlayer"] = mediaPlayer;
    o["mediaArt"] = mediaArt;
    o["mediaArtOpacity"] = mediaArtOpacity;
    o["mediaWave"] = mediaWave;
    o["mediaGlow"] = mediaGlow;
    o["mediaColor"] = mediaColor;
    o["clipboardHistory"] = clipboardHistory;
    o["linkBrowser"] = linkBrowser;
    o["systemMonitor"] = systemMonitor;
    o["showHints"] = showHints;
    o["powerLock"] = powerLock;
    o["powerSignOut"] = powerSignOut;
    o["powerSleep"] = powerSleep;
    o["powerHibernate"] = powerHibernate;
    o["powerRestart"] = powerRestart;
    o["powerShutdown"] = powerShutdown;
    o["theme"] = theme;
    o["checkUpdates"] = checkUpdates;
    o["hiddenUpdate"] = hiddenUpdate;
    o["lastSeenVersion"] = lastSeenVersion;
    o["showWhatsNew"] = showWhatsNew;
    o["autoUpdate"] = autoUpdate;
    o["lastAutoUpdate"] = lastAutoUpdate;
    const FileLock lock;
    keepBackup(filePath());
    QSaveFile f(filePath());
    if (f.open(QIODevice::WriteOnly))
    {
        f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
        f.commit();
    }
}

void Settings::apply() const
{
    // Стиль задаёт свои отступы, скругления и шрифты.
    Skin::select(skin);
    Theme::ContentInsetX = 0; // по умолчанию без отступа; стиль может задать свой
    Skin::current().applyMetrics();

    Theme::WindowWidth = std::clamp(width, 600, 4000);
    Theme::WindowHeight = std::clamp(height, 300, 2000);
    Theme::Radius = std::clamp(radius, 0, 32);
    Theme::BottomGap = std::clamp(bottomGap, 0, 200);

    Theme::Cell = std::clamp(cell, int(Theme::MinCell), int(Theme::MaxCell));
    Theme::IconSmall = Theme::Cell * 60 / 100;
    Theme::IconMedium = Theme::Cell * 104 / 100;
    Theme::IconLarge = Theme::Cell * 174 / 100;
    Theme::ShowLabels = showLabels;
    Theme::TileFill = tileFill;
    Theme::TileFillOpacity = tileFillOpacity;
    Theme::Adaptive = adaptive;
    Theme::TileFontPx = std::clamp(fontPx, 9, 20);

    // Светлая тема: тёмные цвета стиля заменяются светлыми того же оттенка (свои светлые цвета не трогаем)
    Theme::Light = wantsLight();
    auto luma = [](const QColor& c) { return 0.2126 * c.redF() + 0.7152 * c.greenF() + 0.0722 * c.blueF(); };
    QColor bg = background, txt = textColor, grp = groupColor, acc = systemAccent ? windowsAccent() : accent;
    if (Theme::Light)
    {
        if (luma(bg) < 0.5)
            bg = QColor::fromHslF(qMax(0.0f, bg.hslHueF()), qMin(0.22f, bg.hslSaturationF() * 1.5f), 0.955f);
        if (luma(txt) > 0.5)
            txt = QColor(0x1B, 0x1D, 0x23);
        if (luma(grp) > 0.5)
            grp = QColor(0, 0, 0);
        if (luma(acc) > 0.55) // слишком светлый акцент не читается на белом
            acc = acc.darker(135);
    }

    // Светлый фон делаем плотнее: полупрозрачный белый поверх тёмных обоев выглядит грязно-серым
    Theme::PanelBg = Theme::Rgba::from(bg, pct(Theme::Light ? opacity + (100 - opacity) * 6 / 10 : opacity));
    Theme::GroupBg = Theme::Rgba::from(grp, pct(groupOpacity));
    Theme::TileBg = Theme::Rgba::from(grp, pct(tileOpacity));
    Theme::TextColor = Theme::Rgba::from(txt, 255);
    Theme::Accent = Theme::Rgba::from(acc, 255);
    Theme::PanelBorder = Theme::Light ? Theme::Rgba{0, 0, 0, border ? 30 : 0} : Theme::Rgba{255, 255, 255, border ? 22 : 0};

    // Подсветка наведения/нажатия — от цвета текста, чтобы работало и на светлом фоне.
    Theme::TileHover = Theme::Rgba::from(txt, Theme::Light ? 22 : 30);
    Theme::TilePressed = Theme::Rgba::from(txt, Theme::Light ? 36 : 48);
    Theme::ChipBg = Theme::Rgba::from(txt, Theme::Light ? 26 : 40);
    Theme::ScrollHandle = Theme::Rgba::from(txt, 50);
    Theme::SubText = Theme::Light ? Theme::Rgba{0x5F, 0x64, 0x70, 255} : Theme::Rgba::from(txt.darker(150), 255);
}

bool Settings::windowsLight()
{
#ifdef _WIN32
    DWORD v = 0, size = sizeof(v);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &v, &size) == ERROR_SUCCESS)
        return v != 0;
#endif
    return false;
}

bool Settings::wantsLight() const
{
    return theme == 1 || (theme == 2 && windowsLight());
}

QColor Settings::windowsAccent()
{
#ifdef _WIN32
    DWORD color = 0;
    BOOL opaque = FALSE;
    if (SUCCEEDED(DwmGetColorizationColor(&color, &opaque)))
        return QColor((color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF);
#endif
    return QColor(76, 141, 255);
}
