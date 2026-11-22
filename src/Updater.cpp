#include "Updater.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QThreadPool>
#include <QUrl>
#include <QVersionNumber>

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
            cached = Info{v, o.value(QStringLiteral("page")).toString(), o.value(QStringLiteral("installer")).toString()};
    }
    return cached;
}

void check(bool force, std::function<void(bool)> done)
{
    const qint64 last = readState().value(QStringLiteral("checked")).toInteger();
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    if (!force && now - last < 20 * 3600)
    {
        onMain(done, true);
        return;
    }
    QThreadPool::globalInstance()->start([done, now] {
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
                    break;
                }
            }
        }
        // Нет сети — время проверки не записываем, попробуем позже
        if (got)
            writeState(state);
        onMain(done, got);
    });
}

void install(const Info& info, std::function<void(bool)> done)
{
    const QString page = info.page;
    if (info.installer.isEmpty() || !info.installer.startsWith(QStringLiteral("https://github.com/")))
    {
        QDesktopServices::openUrl(QUrl(page));
        onMain(done, false);
        return;
    }
    const QString url = info.installer, version = info.version;
    QThreadPool::globalInstance()->start([url, version, page, done] {
        const QString exe = QDir::tempPath() + QStringLiteral("/StartMenu-%1-setup.exe").arg(version);
        QFile::remove(exe);
        const bool ok = download(url, exe)
                        && reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(exe).utf16()),
                                                                   nullptr, nullptr, SW_SHOWNORMAL)) > 32;
        if (!ok)
            QMetaObject::invokeMethod(QCoreApplication::instance(), [page] { QDesktopServices::openUrl(QUrl(page)); },
                                      Qt::QueuedConnection);
        onMain(done, ok);
    });
}
}
