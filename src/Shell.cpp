#include "Shell.h"
#include "Settings.h"
#include "Lang.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QDateTime>
#include <QDir>
#include <QStandardPaths>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QProcess>
#include <QSettings>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#include <sddl.h>
#include <shellapi.h>
#include <psapi.h>
#include <shlwapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <knownfolders.h>
#include <shlguid.h>
#endif

namespace
{
#ifdef _WIN32
    const wchar_t* wstr(const QString& s) { return reinterpret_cast<const wchar_t*>(s.utf16()); }

    // HBITMAP от Shell — 32-битный DIB с премультиплицированной альфой, строки часто идут снизу вверх.
    // QImage::fromHBITMAP это читает неверно (иконки вверх ногами и на чёрном фоне), поэтому копируем сами.
    QImage imageFromShellBitmap(HBITMAP hbm)
    {
        DIBSECTION ds = {};
        if (GetObjectW(hbm, sizeof(ds), &ds) != sizeof(ds) || ds.dsBm.bmBitsPixel != 32 || !ds.dsBm.bmBits)
            return QImage::fromHBITMAP(hbm);

        const int w = ds.dsBm.bmWidth;
        const int h = ds.dsBm.bmHeight;
        const bool bottomUp = ds.dsBmih.biHeight > 0;
        // Иконки приложений из Store приходят с обычной (непремультиплицированной) альфой, хотя Shell обещает обратное:
        // цвет в краевых пикселях ярче их прозрачности. Если читать их как премультиплицированные, по краю иконки
        // появляется цветная «бахрома». Проверяем пиксели и выбираем верный формат.
        bool straightAlpha = false;
        {
            const auto* base = static_cast<const uchar*>(ds.dsBm.bmBits);
            for (int y = 0; y < h && !straightAlpha; ++y)
            {
                const auto* line = reinterpret_cast<const QRgb*>(base + qsizetype(y) * ds.dsBm.bmWidthBytes);
                for (int x = 0; x < w; ++x)
                {
                    const QRgb c = line[x];
                    const int a = qAlpha(c);
                    if (qRed(c) > a || qGreen(c) > a || qBlue(c) > a)
                    {
                        straightAlpha = true;
                        break;
                    }
                }
            }
        }
        QImage img(static_cast<const uchar*>(ds.dsBm.bmBits), w, h, ds.dsBm.bmWidthBytes,
                   straightAlpha ? QImage::Format_ARGB32 : QImage::Format_ARGB32_Premultiplied);
        img = bottomUp ? img.mirrored(false, true) : img.copy(); // copy — отвязываемся от памяти HBITMAP
        if (straightAlpha)
            img = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);

        // Старые иконки бывают совсем без альфы — тогда считаем картинку непрозрачной.
        bool hasAlpha = false;
        for (int y = 0; y < h && !hasAlpha; ++y)
        {
            const auto* line = reinterpret_cast<const QRgb*>(img.constScanLine(y));
            for (int x = 0; x < w; ++x)
                if (qAlpha(line[x]) != 0) { hasAlpha = true; break; }
        }
        if (!hasAlpha)
            img = img.convertToFormat(QImage::Format_RGB32);
        return img;
    }

    QString currentUserSid()
    {
        QString out;
        HANDLE token = nullptr;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
            return out;
        DWORD size = 0;
        GetTokenInformation(token, TokenUser, nullptr, 0, &size);
        QByteArray buf(int(size), 0);
        if (GetTokenInformation(token, TokenUser, buf.data(), size, &size))
        {
            LPWSTR sid = nullptr;
            if (ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buf.data())->User.Sid, &sid))
            {
                out = QString::fromWCharArray(sid);
                LocalFree(sid);
            }
        }
        CloseHandle(token);
        return out;
    }
#endif
}

namespace WinShell
{
bool isLocalFile(const QString& path)
{
    return !path.startsWith(QStringLiteral("shell:"), Qt::CaseInsensitive) && QFileInfo::exists(path);
}

void launch(const Item& item, bool asAdmin)
{
    // Псевдо-адрес калькулятора из поиска: «запуск» = скопировать результат.
    if (item.path.startsWith(QStringLiteral("calc:")))
    {
        QGuiApplication::clipboard()->setText(item.path.mid(5));
        return;
    }
#ifdef _WIN32
    // Слэши меняем только у путей к файлам: у «https://», «search-ms:», «ms-settings:» и «shell:» их трогать нельзя.
    const bool shellPath = item.path.startsWith(QStringLiteral("shell:"), Qt::CaseInsensitive)
                           || item.path.contains(QStringLiteral("://")) || item.path.startsWith(QStringLiteral("search-ms:"))
                           || item.path.startsWith(QStringLiteral("ms-settings:"));
    const QString path = shellPath ? item.path : QDir::toNativeSeparators(item.path);
    const QFileInfo fi(item.path);
    // Для .lnk рабочую папку берёт сам ярлык; для exe — папка exe.
    const bool isLink = path.endsWith(QStringLiteral(".lnk"), Qt::CaseInsensitive);
    const QString dir = (!shellPath && !isLink && fi.isFile()) ? QDir::toNativeSeparators(fi.absolutePath()) : QString();
    // Ссылка на сайт и в настройках выбран свой браузер — открываем ею
    const QString browser = Settings::get().linkBrowser;
    if (Config::isWebLink(item.path) && !browser.isEmpty() && QFileInfo::exists(browser))
    {
        ShellExecuteW(nullptr, nullptr, wstr(browser), wstr(QLatin1Char('"') + item.path + QLatin1Char('"')), nullptr, SW_SHOWNORMAL);
        return;
    }
    // verb = nullptr — действие по умолчанию (для .uproject, .url и т.п. это не всегда "open").
    ShellExecuteW(nullptr, asAdmin ? L"runas" : nullptr, wstr(path),
                  item.args.isEmpty() ? nullptr : wstr(item.args),
                  dir.isEmpty() ? nullptr : wstr(dir), SW_SHOWNORMAL);
#else
    Q_UNUSED(item) Q_UNUSED(asAdmin)
#endif
}

void open(const QString& target)
{
#ifdef _WIN32
    ShellExecuteW(nullptr, nullptr, wstr(target), nullptr, nullptr, SW_SHOWNORMAL);
#else
    Q_UNUSED(target)
#endif
}

void showInFolder(const QString& path)
{
    QProcess::startDetached(QStringLiteral("explorer.exe"), {QStringLiteral("/select,"), QDir::toNativeSeparators(path)});
}

void openRunDialog()
{
    // CLSID штатного окна «Выполнить» (то же, что Win+R).
    QProcess::startDetached(QStringLiteral("explorer.exe"), {QStringLiteral("shell:::{2559a1f3-21d7-11d4-bdaf-00c04f60b9f0}")});
}

QPixmap loadIcon(const QString& path, int px)
{
#ifdef _WIN32
    const bool shellPath = path.startsWith(QStringLiteral("shell:"), Qt::CaseInsensitive);
    const QString native = shellPath ? path : QDir::toNativeSeparators(path);
    IShellItemImageFactory* factory = nullptr;
    if (FAILED(SHCreateItemFromParsingName(wstr(native), nullptr, IID_PPV_ARGS(&factory))))
        return {};
    QPixmap pm;
    HBITMAP hbm = nullptr;
    if (SUCCEEDED(factory->GetImage(SIZE{px, px}, SIIGBF_ICONONLY | SIIGBF_BIGGERSIZEOK, &hbm)) && hbm)
    {
        QImage img = imageFromShellBitmap(hbm);
        DeleteObject(hbm);
        if (!img.isNull())
        {
            if (img.width() != px || img.height() != px)
                img = img.scaled(px, px, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            pm = QPixmap::fromImage(std::move(img));
        }
    }
    factory->Release();
    return pm;
#else
    Q_UNUSED(path) Q_UNUSED(px)
    return {};
#endif
}

QList<Item> allApps()
{
    QList<Item> out;
#ifdef _WIN32
    // shell:AppsFolder — тот же список, что во «Всех приложениях» Windows, вместе с приложениями из Store.
    IShellItem* folder = nullptr;
    if (FAILED(SHGetKnownFolderItem(FOLDERID_AppsFolder, KF_FLAG_DEFAULT, nullptr, IID_PPV_ARGS(&folder))))
        return out;
    IEnumShellItems* items = nullptr;
    if (SUCCEEDED(folder->BindToHandler(nullptr, BHID_EnumItems, IID_PPV_ARGS(&items))))
    {
        IShellItem* it = nullptr;
        while (items->Next(1, &it, nullptr) == S_OK)
        {
            LPWSTR name = nullptr;
            LPWSTR id = nullptr;
            if (SUCCEEDED(it->GetDisplayName(SIGDN_NORMALDISPLAY, &name))
                && SUCCEEDED(it->GetDisplayName(SIGDN_PARENTRELATIVEPARSING, &id)))
            {
                const QString n = QString::fromWCharArray(name);
                if (!n.contains(QStringLiteral("uninstall"), Qt::CaseInsensitive)
                    && !n.contains(QStringLiteral("удал"), Qt::CaseInsensitive))
                    out.append({n, QStringLiteral("shell:AppsFolder\\") + QString::fromWCharArray(id), {}});
            }
            CoTaskMemFree(name);
            CoTaskMemFree(id);
            it->Release();
        }
        items->Release();
    }
    folder->Release();
    std::sort(out.begin(), out.end(), [](const Item& a, const Item& b) {
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });
#endif
    return out;
}

QList<QPair<QString, QString>> installedBrowsers()
{
    // Браузеры регистрируются в Clients\StartMenuInternet (для всех пользователей и для текущего)
    QList<QPair<QString, QString>> out;
    for (const char* root : {"HKEY_LOCAL_MACHINE\\SOFTWARE\\Clients\\StartMenuInternet",
                             "HKEY_CURRENT_USER\\SOFTWARE\\Clients\\StartMenuInternet"})
    {
        QSettings reg(QString::fromLatin1(root), QSettings::NativeFormat);
        for (const QString& key : reg.childGroups())
        {
            const QString name = reg.value(key + QStringLiteral("/Default")).toString();
            QString cmd = reg.value(key + QStringLiteral("/shell/open/command/Default")).toString().trimmed();
            // «"C:\...\chrome.exe" --flag» → путь к exe
            QString exe = cmd.startsWith(QLatin1Char('"')) ? cmd.section(QLatin1Char('"'), 1, 1) : cmd.section(QLatin1Char(' '), 0, 0);
            if (name.isEmpty() || exe.isEmpty() || !QFileInfo::exists(exe))
                continue;
            exe = QDir::toNativeSeparators(exe);
            bool dup = false;
            for (const auto& b : out)
                dup = dup || b.second.compare(exe, Qt::CaseInsensitive) == 0;
            if (!dup)
                out.append({name, exe});
        }
    }
    return out;
}

QString defaultBrowserExe()
{
#ifdef _WIN32
    wchar_t buf[MAX_PATH] = {};
    DWORD size = MAX_PATH;
    if (SUCCEEDED(AssocQueryStringW(ASSOCF_NONE, ASSOCSTR_EXECUTABLE, L"http", L"open", buf, &size)))
        return QString::fromWCharArray(buf);
#endif
    return {};
}

QString userPicturePath()
{
#ifdef _WIN32
    const QString sid = currentUserSid();
    if (sid.isEmpty())
        return {};
    QSettings reg(QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\AccountPicture\\Users\\") + sid,
                  QSettings::NativeFormat);
    for (const char* key : {"Image96", "Image192", "Image64", "Image448"})
    {
        const QString p = reg.value(QLatin1String(key)).toString();
        if (!p.isEmpty() && QFileInfo::exists(p))
            return p;
    }
#endif
    return {};
}

void setAcrylic(QWidget* window, bool enabled, const QColor& tint)
{
#ifdef _WIN32
    // SetWindowCompositionAttribute — недокументированная, но стабильная функция user32,
    // ей пользуются почти все «стеклянные» программы. Ищем её динамически, чтобы не зависеть от SDK.
    struct AccentPolicy { int state; int flags; DWORD gradient; int animation; };
    struct CompositionData { int attribute; void* data; SIZE_T size; };
    using Fn = BOOL(WINAPI*)(HWND, CompositionData*);
    static const auto fn = reinterpret_cast<Fn>(
        reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetWindowCompositionAttribute")));
    if (!fn)
        return;
    // Цвет подложки в формате AABBGGRR. Совсем прозрачная (0) ломает эффект — минимум 1.
    const DWORD abgr = (DWORD(qMax(1, tint.alpha())) << 24) | (DWORD(tint.blue()) << 16)
                       | (DWORD(tint.green()) << 8) | DWORD(tint.red());
    AccentPolicy accent{enabled ? 4 /*ACRYLICBLURBEHIND*/ : 0 /*DISABLED*/, 2, abgr, 0};
    CompositionData data{19 /*WCA_ACCENT_POLICY*/, &accent, sizeof(accent)};
    fn(reinterpret_cast<HWND>(window->winId()), &data);
#else
    Q_UNUSED(window) Q_UNUSED(enabled) Q_UNUSED(tint)
#endif
}

void setRoundCorners(QWidget* window, bool round)
{
#ifdef _WIN32
    // Windows 11: системное скругление углов окна (DWMWA_WINDOW_CORNER_PREFERENCE = 33).
    const int pref = round ? 2 /*DWMWCP_ROUND*/ : 1 /*DWMWCP_DONOTROUND*/;
    using Fn = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
    static const auto fn = reinterpret_cast<Fn>(
        reinterpret_cast<void*>(GetProcAddress(LoadLibraryW(L"dwmapi.dll"), "DwmSetWindowAttribute")));
    if (fn)
        fn(reinterpret_cast<HWND>(window->winId()), 33, &pref, sizeof(pref));
#else
    Q_UNUSED(window) Q_UNUSED(round)
#endif
}

void setCornerRadius(QWidget* window, int px)
{
#ifdef _WIN32
    const int pref = px == 0 ? 1 /*DONOTROUND*/ : px <= 6 ? 3 /*ROUNDSMALL*/ : 2 /*ROUND*/;
    using Fn = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
    static const auto fn = reinterpret_cast<Fn>(
        reinterpret_cast<void*>(GetProcAddress(LoadLibraryW(L"dwmapi.dll"), "DwmSetWindowAttribute")));
    if (fn)
        fn(reinterpret_cast<HWND>(window->winId()), 33, &pref, sizeof(pref));
#else
    Q_UNUSED(window) Q_UNUSED(px)
#endif
}

void setDarkTitleBar(QWidget* window)
{
#ifdef _WIN32
    // DWMWA_USE_IMMERSIVE_DARK_MODE = 20 — тёмный заголовок окна, как у системных приложений.
    const BOOL dark = TRUE;
    using Fn = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
    static const auto fn = reinterpret_cast<Fn>(
        reinterpret_cast<void*>(GetProcAddress(LoadLibraryW(L"dwmapi.dll"), "DwmSetWindowAttribute")));
    if (fn)
        fn(reinterpret_cast<HWND>(window->winId()), 20, &dark, sizeof(dark));
#else
    Q_UNUSED(window)
#endif
}

// Где на самом деле лежит то, что запускает плитка. Для строки состояния при наведении.
QString locationOf(const Item& item)
{
    static QHash<QString, QString> cache; // ярлыки разбираем один раз
    const QString p = item.path;
    if (p.isEmpty())
        return {};
    if (const auto it = cache.constFind(p); it != cache.constEnd())
        return *it;

    QString out = QDir::toNativeSeparators(p);
    static const QString apps = QStringLiteral("shell:AppsFolder\\");
    if (p.startsWith(apps, Qt::CaseInsensitive))
    {
        QString id = p.mid(apps.size());
        if (id.contains(QLatin1Char('!')))
            out = TXT("Приложение Microsoft Store · ") + id.section(QLatin1Char('_'), 0, 0);
        else if (id.startsWith(QLatin1Char('{')) && id.contains(QLatin1Char('}')))
        {
            // «{GUID известной папки}\Adobe\…» → «C:\Program Files\Adobe\…»
            GUID g{};
            const QString guid = id.left(id.indexOf(QLatin1Char('}')) + 1);
            PWSTR base = nullptr;
            if (SUCCEEDED(CLSIDFromString(reinterpret_cast<LPCOLESTR>(guid.utf16()), &g)) &&
                SUCCEEDED(SHGetKnownFolderPath(g, 0, nullptr, &base)))
                out = QString::fromWCharArray(base) + QDir::toNativeSeparators(id.mid(guid.size()));
            CoTaskMemFree(base);
        }
        else
            out = id;
    }
    else if (p.endsWith(QStringLiteral(".lnk"), Qt::CaseInsensitive))
    {
        // Ярлык — показываем, куда он ведёт.
        IShellLinkW* link = nullptr;
        if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link))))
        {
            IPersistFile* file = nullptr;
            if (SUCCEEDED(link->QueryInterface(IID_PPV_ARGS(&file))))
            {
                if (SUCCEEDED(file->Load(reinterpret_cast<LPCOLESTR>(QDir::toNativeSeparators(p).utf16()), STGM_READ)))
                {
                    wchar_t target[MAX_PATH] = {};
                    if (SUCCEEDED(link->GetPath(target, MAX_PATH, nullptr, SLGP_RAWPATH)) && target[0])
                    {
                        wchar_t expanded[MAX_PATH] = {};
                        ExpandEnvironmentStringsW(target, expanded, MAX_PATH);
                        out = QString::fromWCharArray(expanded[0] ? expanded : target);
                    }
                }
                file->Release();
            }
            link->Release();
        }
    }
    cache.insert(p, out);
    return out;
}

bool raiseTaskbar(const QRect& onScreen)
{
    // Над игрой на весь экран Windows прячет панель задач, а родной «Пуск» её показывает.
    // Поднять панель «поверх всех» мало — Проводник сразу возвращает её назад. Работает другое:
    // на миг сделать активной саму панель (как будто по ней кликнули) — Проводник её показывает,
    // а потом фокус забирает меню. Делаем это, только если активное окно занимает весь этот монитор.
    HWND fg = GetForegroundWindow();
    if (!fg)
        return false;
    RECT fr;
    if (!GetWindowRect(fg, &fr))
        return false;
    const RECT screen{onScreen.left(), onScreen.top(), onScreen.right() + 1, onScreen.bottom() + 1};
    const bool fullscreen = fr.left <= screen.left && fr.top <= screen.top && fr.right >= screen.right && fr.bottom >= screen.bottom;
    if (!fullscreen)
        return false;
    // Панель задач этого монитора: основная или дополнительная
    struct Ctx { RECT screen; HWND found; } ctx{screen, nullptr};
    EnumWindows([](HWND w, LPARAM lp) -> BOOL {
        wchar_t cls[64] = {};
        GetClassNameW(w, cls, 64);
        if (wcscmp(cls, L"Shell_TrayWnd") != 0 && wcscmp(cls, L"Shell_SecondaryTrayWnd") != 0)
            return TRUE;
        auto* c = reinterpret_cast<Ctx*>(lp);
        RECT r, x;
        if (GetWindowRect(w, &r) && IntersectRect(&x, &r, &c->screen))
        {
            c->found = w;
            return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&ctx));
    if (!ctx.found || ctx.found == fg)
        return false;
    const DWORD fgThread = GetWindowThreadProcessId(fg, nullptr);
    const DWORD me = GetCurrentThreadId();
    const bool attach = fgThread && fgThread != me && AttachThreadInput(me, fgThread, TRUE);
    SetForegroundWindow(ctx.found);
    if (attach)
        AttachThreadInput(me, fgThread, FALSE);
    return true;
}

void keepTaskbarAbove(const QRect& onScreen)
{
    // Меню уже активно — ставим панель задач этого монитора поверх игры (без фокуса)
    const RECT screen{onScreen.left(), onScreen.top(), onScreen.right() + 1, onScreen.bottom() + 1};
    EnumWindows([](HWND w, LPARAM lp) -> BOOL {
        wchar_t cls[64] = {};
        GetClassNameW(w, cls, 64);
        if (wcscmp(cls, L"Shell_TrayWnd") != 0 && wcscmp(cls, L"Shell_SecondaryTrayWnd") != 0)
            return TRUE;
        RECT r, x;
        if (GetWindowRect(w, &r) && IntersectRect(&x, &r, reinterpret_cast<const RECT*>(lp)))
        {
            ShowWindow(w, SW_SHOWNA);
            SetWindowPos(w, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&screen));
}

void startSelf(const QString& arg)
{
    startSelf(QStringList{arg});
}

void startSelf(const QStringList& args)
{
    // Свой CreateProcess вместо QProcess: флаг STARTF_FORCEOFFFEEDBACK убирает «крутящийся» курсор,
    // который Windows иначе показывает, пока у нового процесса не появится окно (у меню «наготове» его нет).
    const std::wstring exe = QDir::toNativeSeparators(QCoreApplication::applicationFilePath()).toStdWString();
    std::wstring cmd = L"\"" + exe + L"\"";
    for (const QString& a : args)
        cmd += L" \"" + a.toStdWString() + L"\"";
    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_FORCEOFFFEEDBACK;
    PROCESS_INFORMATION pi = {};
    if (CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi))
    {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
}

void log(const QString& line)
{
    const QString file = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/menu.log");
    QDir().mkpath(QFileInfo(file).absolutePath());
    QFile f(file);
    if (f.size() > 64 * 1024 && f.open(QIODevice::ReadWrite))
    {
        // Журнал разросся — оставляем последнюю половину.
        QByteArray tail = f.readAll().right(32 * 1024);
        tail = tail.mid(tail.indexOf('\n') + 1); // с начала строки — чтобы не разрезать букву
        f.resize(0);
        f.write(tail);
        f.close();
    }
    if (f.open(QIODevice::Append | QIODevice::Text))
        f.write((QDateTime::currentDateTime().toString(QStringLiteral("dd.MM HH:mm:ss.zzz ")) + line + QLatin1Char('\n')).toUtf8());
}

QString describeWindow(quintptr hwnd)
{
#ifdef _WIN32
    HWND h = reinterpret_cast<HWND>(hwnd);
    if (!h)
        return QStringLiteral("(нет окна)");
    wchar_t cls[128] = {};
    GetClassNameW(h, cls, 128);
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    QString exe = QStringLiteral("?");
    if (HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid))
    {
        wchar_t buf[MAX_PATH] = {};
        DWORD size = MAX_PATH;
        if (QueryFullProcessImageNameW(proc, 0, buf, &size))
            exe = QFileInfo(QString::fromWCharArray(buf, int(size))).fileName();
        CloseHandle(proc);
    }
    return exe + QStringLiteral(" · ") + QString::fromWCharArray(cls);
#else
    Q_UNUSED(hwnd)
    return {};
#endif
}

bool isPassiveWindow(quintptr hwnd)
{
#ifdef _WIN32
    HWND h = reinterpret_cast<HWND>(hwnd);
    if (!h)
        return true; // фокус «ни у кого» — это не переключение пользователем
    const LONG_PTR ex = GetWindowLongPtrW(h, GWL_EXSTYLE);
    // Окна «без активации», служебные и прозрачные для мыши оверлеи (запись экрана, счётчики FPS и т.п.).
    return (ex & WS_EX_NOACTIVATE) || ((ex & WS_EX_TOOLWINDOW) && !IsWindowEnabled(h))
           || ((ex & WS_EX_TRANSPARENT) && (ex & WS_EX_LAYERED));
#else
    Q_UNUSED(hwnd)
    return false;
#endif
}

bool mouseButtonDown()
{
#ifdef _WIN32
    return (GetAsyncKeyState(VK_LBUTTON) | GetAsyncKeyState(VK_RBUTTON) | GetAsyncKeyState(VK_MBUTTON)) & 0x8000;
#else
    return false;
#endif
}

bool switchKeysDown()
{
#ifdef _WIN32
    return (GetAsyncKeyState(VK_MENU) | GetAsyncKeyState(VK_LWIN) | GetAsyncKeyState(VK_RWIN)) & 0x8000;
#else
    return false;
#endif
}

static QString& perfLine()
{
    static QString s;
    return s;
}

void perfMark(const char* stage)
{
#ifdef _WIN32
    static const bool on = !qEnvironmentVariableIsEmpty("STARTMENU_PERF");
    if (!on)
        return;
    FILETIME created, exited, kernel, user, now;
    GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user);
    GetSystemTimeAsFileTime(&now);
    auto ticks = [](const FILETIME& f) { return (quint64(f.dwHighDateTime) << 32) | f.dwLowDateTime; };
    perfLine() += QStringLiteral("%1=%2 ").arg(QLatin1String(stage)).arg((ticks(now) - ticks(created)) / 10000);
#else
    Q_UNUSED(stage)
#endif
}

void perfFlush(const QString& extra)
{
    const QString log = qEnvironmentVariable("STARTMENU_PERF");
    if (log.isEmpty() || perfLine().isEmpty())
        return;
    QFile f(log);
    if (f.open(QIODevice::Append | QIODevice::Text))
        f.write((perfLine() + extra + QLatin1Char('\n')).toUtf8());
    perfLine().clear();
}

bool lockWorkingSet(int headroomMb)
{
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS pmc = {};
    pmc.cb = sizeof(pmc);
    if (!GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
        return false;
    const SIZE_T minWs = pmc.WorkingSetSize + SIZE_T(headroomMb) * 1024 * 1024;
    const SIZE_T maxWs = minWs + SIZE_T(512) * 1024 * 1024;
    return SetProcessWorkingSetSizeEx(GetCurrentProcess(), minWs, maxWs,
                                      QUOTA_LIMITS_HARDWS_MIN_ENABLE | QUOTA_LIMITS_HARDWS_MAX_DISABLE) != FALSE;
#else
    Q_UNUSED(headroomMb)
    return false;
#endif
}

void forceForeground(QWidget* window)
{
#ifdef _WIN32
    // Windows не даёт фоновому процессу просто так забрать фокус.
    // Временно подключаемся к очереди ввода текущего активного окна — тогда можно.
    HWND hwnd = reinterpret_cast<HWND>(window->winId());
    HWND fg = GetForegroundWindow();
    const DWORD fgThread = fg ? GetWindowThreadProcessId(fg, nullptr) : 0;
    const DWORD me = GetCurrentThreadId();
    const bool attach = fgThread && fgThread != me && AttachThreadInput(me, fgThread, TRUE);
    SetForegroundWindow(hwnd);
    BringWindowToTop(hwnd);
    SetFocus(hwnd);
    if (attach)
        AttachThreadInput(me, fgThread, FALSE);
#else
    Q_UNUSED(window)
#endif
}

quintptr foregroundWindow()
{
#ifdef _WIN32
    return reinterpret_cast<quintptr>(GetForegroundWindow());
#else
    return 0;
#endif
}

bool foregroundIsOurs()
{
#ifdef _WIN32
    HWND fg = GetForegroundWindow();
    if (!fg)
        return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    if (pid == GetCurrentProcessId())
        return true;
    // Окно другого нашего процесса (настройки) — тоже «своё»: предпросмотр не должен от него закрываться.
    bool ours = false;
    if (HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid))
    {
        wchar_t buf[MAX_PATH] = {};
        DWORD size = MAX_PATH;
        if (QueryFullProcessImageNameW(proc, 0, buf, &size))
            ours = QString::fromWCharArray(buf, int(size))
                       .compare(QDir::toNativeSeparators(QCoreApplication::applicationFilePath()), Qt::CaseInsensitive) == 0;
        CloseHandle(proc);
    }
    return ours;
#else
    return true;
#endif
}
}
