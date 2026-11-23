#include "Updater.h"
#include "Settings.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QThreadPool>
#include <QUrl>
#include <QVersionNumber>

#include <atomic>
#include <thread>
#include <windows.h>
#include <shellapi.h>
#include <urlmon.h>

namespace
{
const char* kRepo = "NKF-XXX/StartMenu";

QString stateFile()
{
    const QString d = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(d);
    return d + QStringLiteral("/update.json");
}

QJsonObject readState()
{
    QFile f(stateFile());
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}

void writeState(const QJsonObject& o)
{
    QSaveFile f(stateFile());
    if (f.open(QIODevice::WriteOnly))
    {
        f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
        f.commit();
    }
}

// Скачать файл (urlmon — без отдельных сетевых библиотек Qt). Путь к временному файлу или пусто.
bool download(const QString& url, const QString& to)
{
    const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const HRESULT hr = URLDownloadToFileW(nullptr, reinterpret_cast<LPCWSTR>(url.utf16()),
                                          reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(to).utf16()), 0, nullptr);
    if (SUCCEEDED(co))
        CoUninitialize();
    return SUCCEEDED(hr) && QFileInfo(to).size() > 0;
}

void onMain(std::function<void(bool)> done, bool ok)
{
    if (!done)
        return;
    QMetaObject::invokeMethod(QCoreApplication::instance(), [done, ok] { done(ok); }, Qt::QueuedConnection);
}

QVersionNumber parse(QString v)
{
    if (v.startsWith(QLatin1Char('v'), Qt::CaseInsensitive))
        v.remove(0, 1);
    return QVersionNumber::fromString(v);
}
}

namespace Updater
{
QString currentVersion()
{
    return QStringLiteral(APP_VERSION);
}

QString issuesUrl()
{
    return QStringLiteral("https://github.com/%1/issues/new/choose").arg(QLatin1String(kRepo));
}

std::optional<Info> available()
{
    // Файл меняется раз в сутки — перечитываем, только если он изменился
    static QDateTime seen;
    static std::optional<Info> cached;
    const QFileInfo fi(stateFile());
    if (!fi.exists())
        return std::nullopt;
    if (fi.lastModified() != seen)
    {
        seen = fi.lastModified();
        cached.reset();
        const QJsonObject o = readState();
        const QString v = o.value(QStringLiteral("version")).toString();
        if (!v.isEmpty() && parse(v) > parse(currentVersion()))
            cached = Info{v, o.value(QStringLiteral("page")).toString(), o.value(QStringLiteral("installer")).toString(),
                          o.value(QStringLiteral("sha256")).toString()};
    }
    return cached;
}

std::optional<Info> visible()
{
    auto info = available();
    if (info && !Settings::get().checkUpdates)
        return std::nullopt;
    if (info && info->version == Settings::get().hiddenUpdate)
        return std::nullopt;
    return info;
}

void check(bool force, std::function<void(bool)> done)
{
    // Вызывается при каждом открытии меню, поэтому в «не пора» должна быть почти бесплатной: время последней проверки
    // держим в памяти, файл читаем только когда по памяти уже пора, а сеть и поток трогаем лишь раз в 2 часа.
    static std::atomic<qint64> checkedAt{-1};
    static std::atomic<bool> running{false};
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    constexpr qint64 kEvery = 30 * 60;
    if (!force)
    {
        const qint64 mem = checkedAt.load();
        if (mem >= 0 && now - mem < kEvery)
            return; // совсем недавно проверяли — ни диска, ни сети
        const qint64 last = readState().value(QStringLiteral("checked")).toInteger(); // могла проверить другая копия (трей)
        checkedAt = last;
        if (now - last < kEvery || running.exchange(true))
            return;
    }
    // Отдельный поток с самым низким приоритетом: запрос не мешает ни меню, ни играм
    std::thread([done, now, force] {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_LOWEST);
        const QString tmp = QDir::tempPath() + QStringLiteral("/startmenu-release-%1.json").arg(now);
        // Метка времени в адресе — чтобы Windows не отдала старый ответ из кэша
        // Список, а не /releases/latest: пока выпусков нет, тот отвечает ошибкой 404
        const QString url = QStringLiteral("https://api.github.com/repos/%1/releases?per_page=10&t=%2").arg(QLatin1String(kRepo)).arg(now);
        const bool got = download(url, tmp);
        QJsonObject rel;
        if (got)
        {
            QFile f(tmp);
            if (f.open(QIODevice::ReadOnly))
                for (const auto& r : QJsonDocument::fromJson(f.readAll()).array())
                {
                    // Первый настоящий выпуск: без черновиков и тестовых версий
                    const QJsonObject ro = r.toObject();
                    if (!ro.value(QStringLiteral("draft")).toBool() && !ro.value(QStringLiteral("prerelease")).toBool())
                    {
                        rel = ro;
                        break;
                    }
                }
        }
        QFile::remove(tmp);
        const QString tag = rel.value(QStringLiteral("tag_name")).toString();
        QJsonObject state{{"checked", now}};
        if (!tag.isEmpty())
        {
            state["version"] = tag.startsWith(QLatin1Char('v'), Qt::CaseInsensitive) ? tag.mid(1) : tag;
            state["page"] = rel.value(QStringLiteral("html_url")).toString();
            for (const auto& a : rel.value(QStringLiteral("assets")).toArray())
            {
                const QJsonObject ao = a.toObject();
                if (ao.value(QStringLiteral("name")).toString().endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive))
                {
                    state["installer"] = ao.value(QStringLiteral("browser_download_url")).toString();
                    // GitHub сам считает SHA-256 каждого файла выпуска: «sha256:<hex>»
                    const QString digest = ao.value(QStringLiteral("digest")).toString();
                    if (digest.startsWith(QStringLiteral("sha256:")))
                        state["sha256"] = digest.mid(7).toLower();
                    break;
                }
            }
        }
        // Нет сети — время проверки в файл не записываем; в памяти откладываем повтор на 10 минут, чтобы не стучаться при каждом открытии
        checkedAt = got ? now : now - kEvery + 600;
        if (!force)
            running = false;
        if (got)
            writeState(state);
        onMain(done, got);
    }).detach();
}

bool userIsFree()
{
    QUERY_USER_NOTIFICATION_STATE st = QUNS_ACCEPTS_NOTIFICATIONS;
    return FAILED(SHQueryUserNotificationState(&st)) || st == QUNS_ACCEPTS_NOTIFICATIONS;
}

void install(const Info& info, std::function<void(bool)> done, bool quiet)
{
    const QString page = info.page;
    // Ставим только установщик из выпусков нашего репозитория и только с известной контрольной суммой
    static const QString kReleases = QStringLiteral("https://github.com/NKF-XXX/StartMenu/releases/download/");
    if (info.installer.isEmpty() || !info.installer.startsWith(kReleases) || info.sha256.size() != 64)
    {
        QDesktopServices::openUrl(QUrl(page));
        onMain(done, false);
        return;
    }
    const QString url = info.installer, version = info.version, sha = info.sha256;
    QThreadPool::globalInstance()->start([url, version, sha, page, done, quiet] {
        // Своя папка в профиле, а не общий %TEMP%: туда никто посторонний файл не подложит
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/StartMenu/update");
        QDir().mkpath(dir);
        const QString exe = dir + QStringLiteral("/StartMenu-%1-setup.exe").arg(version);
        QFile::remove(exe);
        auto verified = [&exe, &sha] {
            QFile f(exe);
            if (!f.open(QIODevice::ReadOnly))
                return false;
            QCryptographicHash h(QCryptographicHash::Sha256);
            if (!h.addData(&f))
                return false;
            f.close();
            if (QString::fromLatin1(h.result().toHex()) == sha)
                return true;
            QFile::remove(exe); // файл не тот, что выложен в выпуске, — не запускаем
            return false;
        };
        // Установщик читает STARTMENU_QUIET из окружения — без окон и сообщений; запущенный процесс унаследует переменную
        if (quiet)
            SetEnvironmentVariableW(L"STARTMENU_QUIET", L"1");
        const bool ok = download(url, exe) && verified()
                        && reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(exe).utf16()),
                                                                   nullptr, nullptr, SW_SHOWNORMAL)) > 32;
        if (quiet)
            SetEnvironmentVariableW(L"STARTMENU_QUIET", nullptr);
        if (!ok && !quiet) // молчаливое обновление не открывает браузер — просто попробует позже
            QMetaObject::invokeMethod(QCoreApplication::instance(), [page] { QDesktopServices::openUrl(QUrl(page)); },
                                      Qt::QueuedConnection);
        onMain(done, ok);
    });
}
}
