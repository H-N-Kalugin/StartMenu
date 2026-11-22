#include <QAction>
#include <functional>
#include <mutex>
#include "Lang.h"
#include <QClipboard>
#include <QApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QPixmapCache>
#include <QScreen>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QThreadPool>
#include <QTabWidget>
#include <QUrl>
#include <QWinEventNotifier>

#include "AppIcon.h"
#include "ClipboardStore.h"
#include "Config.h"
#include "Demo.h"
#include "PinSync.h"
#include "KeyHook.h"
#include "Lang.h"
#include "Settings.h"
#include "Skin.h"
#include "SettingsWindow.h"
#include "Shell.h"
#include "StartWindow.h"
#include "Theme.h"
#include "WhatsNew.h"
#include "Welcome.h"
#include "MediaSession.h"
#include "Workspace.h"
#include "IconCache.h"
#include "Updater.h"

#include <windows.h>
#include <malloc.h>

// Два режима одного exe:
//   StartMenu.exe         — процесс трея: только хук Win и иконка в трее, живёт всегда, весит ~4 МБ;
//   StartMenu.exe --menu  — процесс окна меню: запускается по Win и завершается при закрытии;
//   StartMenu.exe --settings — окно настроек (тоже отдельный процесс).
namespace
{
    const char* kRunKey = "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    const wchar_t* kHostMutex = L"Local\\StartMenu.App";
    const wchar_t* kMenuMutex = L"Local\\StartMenu.App.Menu";
    const wchar_t* kCloseEvent = L"Local\\StartMenu.App.Close";
    const wchar_t* kMenuReloadEvent = L"Local\\StartMenu.App.MenuReload";
    const wchar_t* kHostReloadEvent = L"Local\\StartMenu.App.HostReload";
    const wchar_t* kSettingsMutex = L"Local\\StartMenu.App.Settings";
    const wchar_t* kShowEvent = L"Local\\StartMenu.App.Show";
    const wchar_t* kClipboardEvent = L"Local\\StartMenu.App.Clipboard"; // трей записал новое в историю буфера
    const wchar_t* kQuitEvent = L"Local\\StartMenu.App.Quit";           // меню «наготове» — завершиться совсем
    const wchar_t* kItemsEvent = L"Local\\StartMenu.App.Items";         // плитки изменили снаружи (Проводник, настройки)

    void setupApp(QApplication& app)
    {
        app.setApplicationName("StartMenu");
        QApplication::setStyle(QStringLiteral("Fusion"));
        app.setWindowIcon(AppIcon::icon()); // своя иконка у всех окон (настройки, диалоги)
    }

    // ---------- процесс окна меню ----------

    int runMenu(int argc, char* argv[])
    {
        CreateMutexW(nullptr, TRUE, kMenuMutex);
        if (GetLastError() == ERROR_ALREADY_EXISTS)
            return 0;

        WinShell::perfMark("start");
        QApplication app(argc, argv);
        setupApp(app);
        WinShell::perfMark("qapp");
        Settings::get().load();
        Lang::load(Settings::get().language);
        WinShell::perfMark("settings");
        Config::instance().load();
        WinShell::perfMark("items");
        WinShell::perfMark("config");

        const bool standbyMode = app.arguments().contains(QStringLiteral("--standby"));
        auto* window = new StartWindow(app.arguments().contains(QStringLiteral("--preview")), standbyMode);
        WinShell::perfMark("window");
        QObject::connect(window, &QObject::destroyed, &app, &QCoreApplication::quit);

        // Процесс трея просит закрыться (повторное нажатие Win) через именованное событие,
        // окно настроек — перечитать настройки. CreateEvent откроет событие, если оно уже есть.
        HANDLE closeEvent = CreateEventW(nullptr, FALSE, FALSE, kCloseEvent);
        HANDLE reloadEvent = CreateEventW(nullptr, FALSE, FALSE, kMenuReloadEvent);
        QWinEventNotifier closeNotifier(closeEvent);
        QWinEventNotifier reloadNotifier(reloadEvent);
        QObject::connect(&closeNotifier, &QWinEventNotifier::activated, window, &StartWindow::closeFromHost);
        QObject::connect(&reloadNotifier, &QWinEventNotifier::activated, window, &StartWindow::reloadSettings);
        HANDLE clipEvent = CreateEventW(nullptr, FALSE, FALSE, kClipboardEvent);
        QWinEventNotifier clipNotifier(clipEvent);
        QObject::connect(&clipNotifier, &QWinEventNotifier::activated, window, &StartWindow::clipboardChanged);
        // Трей просит завершиться совсем (выключили «наготове», выход из трея)
        HANDLE quitEvent = CreateEventW(nullptr, TRUE, FALSE, kQuitEvent);
        ResetEvent(quitEvent);
        QWinEventNotifier quitNotifier(quitEvent);
        QObject::connect(&quitNotifier, &QWinEventNotifier::activated, &app, &QCoreApplication::quit);
        // Плитки поменяли снаружи (закрепили из Проводника, сбросили подписи в настройках) — перечитать
        HANDLE itemsEvent = CreateEventW(nullptr, FALSE, FALSE, kItemsEvent);
        QWinEventNotifier itemsNotifier(itemsEvent);
        QObject::connect(&itemsNotifier, &QWinEventNotifier::activated, window, &StartWindow::itemsChangedOutside);

        // Режим «наготове»: подготовиться и ждать сигнала «показать» от трея.
        const bool standby = standbyMode;
        HANDLE showEvent = CreateEventW(nullptr, FALSE, FALSE, kShowEvent);
        QWinEventNotifier showNotifier(showEvent);
        auto afterShown = [window] {
            // Закреплённое через Windows и список приложений — уже после первого кадра, чтобы не задерживать меню.
            QTimer::singleShot(30, window, [window] {
                if (PinSync::syncFromWindows())
                {
                    Config::instance().save();
                    window->reloadTiles();
                }
                PinSync::syncStartLayoutAsync(window, [window] { window->reloadTiles(); });
                window->prefetchApps();
            });
        };
        QObject::connect(&showNotifier, &QWinEventNotifier::activated, window, [window, afterShown] {
            const bool wasVisible = window->isVisible();
            window->showFromStandby();
            if (!wasVisible)
                afterShown();
        });
        if (standby)
        {
            window->prepareHidden();
            if (Settings::get().reserveMemory)
            {
                // Всё нужное для показа уже в памяти — запрещаем Windows её выгружать.
                const bool ok = WinShell::lockWorkingSet(24);
                WinShell::log(ok ? QStringLiteral("меню наготове, память закреплена")
                                 : TXT("меню наготове, закрепить память не удалось"));
            }
        }
        else
        {
            window->popup();
            afterShown();
        }
        WinShell::perfMark("shown");
        const QStringList args = app.arguments();
        const int qi = int(args.indexOf(QStringLiteral("--query")));
        if (qi >= 0 && qi + 1 < args.size())
            window->setQuery(args[qi + 1]);
        const int rc = app.exec();
        CloseHandle(quitEvent);
        CloseHandle(itemsEvent);
        CloseHandle(closeEvent);
        CloseHandle(reloadEvent);
        CloseHandle(showEvent);
        return rc;
    }

    // ---------- окно настроек ----------

    int runSettings(int argc, char* argv[])
    {
        CreateMutexW(nullptr, TRUE, kSettingsMutex);
        if (GetLastError() == ERROR_ALREADY_EXISTS)
            return 0;
        QApplication app(argc, argv);
        setupApp(app);
        Settings::get().load();
        Lang::load(Settings::get().language);
        HANDLE reloadEvent = CreateEventW(nullptr, FALSE, FALSE, L"Local\\StartMenu.App.SettingsReload");
        // Окно создаётся заново при смене языка (все подписи строятся при создании)
        std::function<void(const QPoint*)> open = [&](const QPoint* pos) {
            auto* w = new SettingsWindow;
            w->setAttribute(Qt::WA_DeleteOnClose);
            auto* notifier = new QWinEventNotifier(reloadEvent, w);
            QObject::connect(notifier, &QWinEventNotifier::activated, w, &SettingsWindow::reloadFromDisk);
            QObject::connect(w, &SettingsWindow::languageChanged, w, [w, &open] {
                const QPoint p = w->pos();
                QTimer::singleShot(0, [p, &open] { open(&p); });
                w->hide();
                w->deleteLater();
            });
            if (pos)
                w->move(*pos);
            w->show();
            WinShell::forceForeground(w);
        };
        open(nullptr);
        return app.exec();
    }

    // ---------- процесс трея ----------


    bool g_hostReserved = false;

    // Трей с резервом: его страницы (включая код перехвата клавиш) не выгружаются — Win не тормозит при нехватке ОЗУ.
    void reserveHostMemory(bool on)
    {
        if (on && !g_hostReserved)
            g_hostReserved = WinShell::lockWorkingSet(4);
        else if (!on && g_hostReserved)
        {
            SetProcessWorkingSetSizeEx(GetCurrentProcess(), SIZE_T(-1), SIZE_T(-1), QUOTA_LIMITS_HARDWS_MIN_DISABLE);
            g_hostReserved = false;
        }
    }

    void trimMemory()
    {
        if (g_hostReserved)
            return; // память закреплена — не выгружаем
        QPixmapCache::clear();
        _heapmin();
        SetProcessWorkingSetSize(GetCurrentProcess(), static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1));
    }

    quint64 fileTimeTicks(const FILETIME& f)
    {
        return (quint64(f.dwHighDateTime) << 32) | f.dwLowDateTime;
    }

    class MenuProcess
    {
    public:
        MenuProcess()
        {
            m_closeEvent = CreateEventW(nullptr, FALSE, FALSE, kCloseEvent);
            m_showEvent = CreateEventW(nullptr, FALSE, FALSE, kShowEvent);
            m_quitEvent = CreateEventW(nullptr, TRUE, FALSE, kQuitEvent);
        }
        std::function<void()> onGaveUp; // меню «наготове» падает снова и снова — сообщить пользователю
        ~MenuProcess()
        {
            if (m_quitEvent)
            {
                SetEvent(m_quitEvent); // трей закрывается — меню «наготове» тоже
                CloseHandle(m_quitEvent);
            }
            delete m_exitNotifier;
            if (m_process)
                CloseHandle(m_process);
            if (m_closeEvent)
                CloseHandle(m_closeEvent);
            if (m_showEvent)
                CloseHandle(m_showEvent);
        }

        // Режим «наготове»: процесс меню запущен заранее и ждёт сигнала «показать».
        void setStandby(bool on)
        {
            m_standbyEnabled = on;
            if (on)
                m_failures = 0;
            if (on && !running())
                QTimer::singleShot(300, [this] { spawn(true); });
            else if (!on && running() && m_isStandby)
                SetEvent(m_quitEvent); // выключили — заранее запущенное меню не нужно
        }

        void toggle()
        {
            if (running() && m_isStandby)
            {
                // Меню «наготове» живёт постоянно: событие «показать» само открывает или прячет его.
                // Разрешаем ему стать активным окном (иначе Windows может не отдать ему фокус).
                AllowSetForegroundWindow(GetProcessId(m_process));
                SetEvent(m_showEvent);
                return;
            }
            if (running())
            {
                // Просили закрыться больше 2 с назад, а процесс всё жив — завис. Снимаем и открываем заново.
                if (m_closeAsked.isValid() && m_closeAsked.elapsed() > 2000)
                {
                    TerminateProcess(m_process, 1);
                    WaitForSingleObject(m_process, 500);
                }
                else
                {
                    SetEvent(m_closeEvent);
                    m_closeAsked.start();
                    return;
                }
            }
            else if (menuOpenElsewhere())
            {
                // Меню открыто не нами (предпросмотр из окна настроек) — Win его закрывает.
                SetEvent(m_closeEvent);
                return;
            }
            else if (msSinceExit() < 300)
            {
                // Клик по трею сначала уводит фокус (меню закрывается), потом приходит щелчок — не открываем заново.
                return;
            }
            spawn(false);
        }

        void spawn(bool standby)
        {
            if (running())
                return;
            delete m_exitNotifier;
            m_exitNotifier = nullptr;
            if (m_process)
            {
                CloseHandle(m_process);
                m_process = nullptr;
            }
            m_closeAsked.invalidate();
            ResetEvent(m_closeEvent);
            ResetEvent(m_showEvent);
            ResetEvent(m_quitEvent);
            m_shown = !standby;
            m_isStandby = standby;

            const std::wstring exe = QDir::toNativeSeparators(QCoreApplication::applicationFilePath()).toStdWString();
            std::wstring cmd = L"\"" + exe + (standby ? L"\" --menu --standby" : L"\" --menu");
            STARTUPINFOW si = {};
            si.cb = sizeof(si);
            si.dwFlags = STARTF_FORCEOFFFEEDBACK; // без «крутящегося» курсора: у меню наготове окна нет
            PROCESS_INFORMATION pi = {};
            if (CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi))
            {
                CloseHandle(pi.hThread);
                m_process = pi.hProcess;
                // Меню закрылось — один раз отдаём системе память трея (раньше это делал таймер раз в минуту,
                // но выталкивать код хука из памяти по таймеру опасно: при нехватке ОЗУ ввод начинает тормозить).
                m_exitNotifier = new QWinEventNotifier(m_process);
                QObject::connect(m_exitNotifier, &QWinEventNotifier::activated, [this] {
                    m_exitNotifier->setEnabled(false);
                    QTimer::singleShot(500, trimMemory);
                    // Упало вскоре после запуска? Тогда перезапускаем с паузой, а после трёх раз подряд — сдаёмся,
                    // иначе сломанное меню перезапускалось бы каждую секунду.
                    DWORD code = 0;
                    GetExitCodeProcess(m_process, &code);
                    FILETIME created, exited, kernel, user;
                    qint64 lived = LLONG_MAX;
                    if (GetProcessTimes(m_process, &created, &exited, &kernel, &user))
                        lived = qint64((fileTimeTicks(exited) - fileTimeTicks(created)) / 10000);
                    m_failures = (code != 0 && lived < 5000) ? m_failures + 1 : 0;
                    if (m_failures >= 3)
                    {
                        WinShell::log(QStringLiteral("меню наготове падает при запуске — перезапуск остановлен"));
                        m_standbyEnabled = false;
                        if (onGaveUp)
                            onGaveUp();
                        return;
                    }
                    if (m_standbyEnabled)
                        QTimer::singleShot(300 << m_failures, [this] { spawn(true); });
                });
            }
        }

        void closeMenu()
        {
            if (running())
                SetEvent(m_closeEvent);
        }

    private:
        bool running() const { return m_process && WaitForSingleObject(m_process, 0) == WAIT_TIMEOUT; }

        static bool menuOpenElsewhere()
        {
            // Процесс меню держит именованный мьютекс; если он есть — меню уже открыто.
            HANDLE m = OpenMutexW(SYNCHRONIZE, FALSE, kMenuMutex);
            if (!m)
                return false;
            CloseHandle(m);
            return true;
        }

        qint64 msSinceExit() const
        {
            FILETIME created, exited, kernel, user, now;
            if (!m_process || !GetProcessTimes(m_process, &created, &exited, &kernel, &user))
                return LLONG_MAX;
            GetSystemTimeAsFileTime(&now);
            return qint64((fileTimeTicks(now) - fileTimeTicks(exited)) / 10000);
        }

        HANDLE m_process = nullptr;
        HANDLE m_quitEvent = nullptr;
        bool m_isStandby = false;
        int m_failures = 0;
        HANDLE m_closeEvent = nullptr;
        QWinEventNotifier* m_exitNotifier = nullptr;
        QElapsedTimer m_closeAsked;
        HANDLE m_showEvent = nullptr;
        bool m_shown = true;           // меню на экране (а не ждёт наготове)
        bool m_standbyEnabled = false;
    };

    int runHost(int argc, char* argv[])
    {
        // Один экземпляр. Мьютекс освобождается системой при выходе, даже после падения.
        CreateMutexW(nullptr, TRUE, kHostMutex);
        if (GetLastError() == ERROR_ALREADY_EXISTS)
            return 0;

        QApplication app(argc, argv);
        setupApp(app);
        app.setQuitOnLastWindowClosed(false);

        // --nohook: без перехвата Win (для отладки), --show: сразу открыть меню.
        const QStringList args = app.arguments();
        MenuProcess menu;
        auto toggle = [&menu] { menu.toggle(); };
        // Настроек ещё нет — это первый запуск после установки: показываем мастер.
        const bool firstRun = !QFileInfo::exists(Settings::filePath());
        Settings::get().load();
        Lang::load(Settings::get().language);
        // Пункт в Проводнике указывает на exe — после переезда программы в новую папку обновляем путь
        if (PinSync::explorerVerbInstalled())
            PinSync::setExplorerVerb(true);
        if (firstRun && !args.contains(QStringLiteral("--nohook")))
            QTimer::singleShot(800, [] { WinShell::startSelf(QStringLiteral("--welcome")); });

        QSystemTrayIcon tray(AppIcon::icon());
        tray.setToolTip("StartMenu");
        QMenu trayMenu;
        // Пауза: Win и кнопка «Пуск» снова открывают меню Windows (до повторного нажатия или перезапуска)
        bool paused = false;
        std::function<void()> reapplyHook;
        // Меню трея собирается заново при смене языка
        auto buildTrayMenu = [&] {
            trayMenu.clear();
            trayMenu.addAction(TXT("Открыть меню"), &app, toggle);
            trayMenu.addAction(TXT("Настройки…"), &app, [] { WinShell::startSelf(QStringLiteral("--settings")); });
            if (const auto upd = Updater::available())
                trayMenu.addAction(TXT("Обновить до версии %1").arg(upd->version), &app, [info = *upd] { Updater::install(info); });

            auto* pause = trayMenu.addAction(TXT("Приостановить (Win откроет меню Windows)"));
            pause->setCheckable(true);
            pause->setChecked(paused);
            QObject::connect(pause, &QAction::toggled, &app, [&](bool on) {
                paused = on;
                tray.setToolTip(on ? TXT("StartMenu — приостановлен") : QStringLiteral("StartMenu"));
                if (reapplyHook)
                    reapplyHook();
            });

            auto* autostart = trayMenu.addAction(TXT("Запускать с Windows"));
            autostart->setCheckable(true);
            autostart->setChecked(QSettings(kRunKey, QSettings::NativeFormat).contains("StartMenu"));
            QObject::connect(autostart, &QAction::toggled, [](bool on) {
                QSettings reg(kRunKey, QSettings::NativeFormat);
                if (on)
                    reg.setValue("StartMenu", QStringLiteral("\"%1\"").arg(QDir::toNativeSeparators(QCoreApplication::applicationFilePath())));
                else
                    reg.remove("StartMenu");
            });

            trayMenu.addAction(TXT("Открыть папку с настройками"), [] {
                const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
                QDir().mkpath(dir);
                QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
            });
            trayMenu.addAction(TXT("Сообщить об ошибке или идея"), &app,
                               [] { QDesktopServices::openUrl(QUrl(Updater::issuesUrl())); });
            trayMenu.addSeparator();
            trayMenu.addAction(TXT("Выход"), &app, &QApplication::quit);
        };
        buildTrayMenu();
        tray.setContextMenu(&trayMenu);
        QObject::connect(&tray, &QSystemTrayIcon::activated, &app, [&](QSystemTrayIcon::ActivationReason r) {
            if (r == QSystemTrayIcon::Trigger)
                toggle();
        });
        tray.show();

        // Клавиша Win и кнопка «Пуск» шлют один сигнал; если Win в настройках выключена — игнорируем её здесь.
        QObject::connect(&KeyHook::instance(), &KeyHook::winTapped, &app, toggle);
        const bool noHook = args.contains(QStringLiteral("--nohook"));
        auto applyHook = [&tray, &menu, &paused, noHook] {
            menu.setStandby(Settings::get().standby || Settings::get().reserveMemory);
            reserveHostMemory(Settings::get().reserveMemory);
            const auto& s = Settings::get();
            KeyHook::instance().setStartButtonEnabled(s.startButton);
            KeyHook::instance().setWinKeyEnabled(s.winKey);
            if (noHook || paused || (!s.winKey && !s.startButton))
                KeyHook::instance().uninstall();
            else if (!KeyHook::instance().installed() && !KeyHook::instance().install())
                tray.showMessage("StartMenu", TXT("Не удалось перехватить клавишу Win. Меню открывается кликом по иконке в трее."));
        };
        applyHook();
        reapplyHook = applyHook;

        // Обновления: проверка не чаще раза в сутки (сама следит по времени последней проверки)
        QTimer updateTimer;
        // Новая версия: точка на значке трея и строка в его меню. Всплывающих уведомлений нет — чтобы не отвлекать.
        // Автообновление (если включено): когда вышла новая версия, не чаще раза в ~сутки и не во время игры —
        // скачиваем и ставим молча. Проверка новых версий идёт каждые 30 минут, а ставится не чаще раза в сутки.
        auto autoInstall = [] {
            const Settings& s = Settings::get();
            if (!s.checkUpdates || !s.autoUpdate)
                return;
            const auto info = Updater::visible();
            if (!info)
                return;
            const qint64 now = QDateTime::currentSecsSinceEpoch();
            if (now - s.lastAutoUpdate < 20 * 3600 || !Updater::userIsFree())
                return;
            Settings::get().lastAutoUpdate = now;
            Settings::get().save();
            Updater::install(*info, {}, true);
        };
        auto refreshUpdateBadge = [&tray, &buildTrayMenu, autoInstall] {
            const bool has = Updater::visible().has_value();
            tray.setIcon(has ? AppIcon::iconWithDot() : AppIcon::icon());
            buildTrayMenu();
            autoInstall();
        };
        auto checkUpdates = [&refreshUpdateBadge] {
            if (Settings::get().checkUpdates)
                Updater::check(false, [&refreshUpdateBadge](bool) { refreshUpdateBadge(); });
        };
        QObject::connect(&updateTimer, &QTimer::timeout, &app, checkUpdates);
        updateTimer.start(30 * 60 * 1000); // раз в 30 минут спрашиваем; сама проверка пропускается, если недавно была
        QTimer::singleShot(60 * 1000, &app, checkUpdates);
        QTimer::singleShot(1000, &app, refreshUpdateBadge); // ответ предыдущей проверки уже сохранён на диске

        // Сторожок: Windows может молча снять хук, если система подвисла, — проверяем раз в 3 с.
        QTimer watchdog;
        menu.onGaveUp = [&tray] {
            tray.showMessage(QStringLiteral("StartMenu"), TXT("Меню «наготове» не запускается — оно будет открываться обычным способом."),
                             QSystemTrayIcon::Warning, 8000);
        };
        // Раз в 3 с: жив ли перехват; если запущен Start11 — держим наш перехват первым и один раз предупреждаем.
        bool warnedOther = false;
        bool otherRunning = KeyHook::otherStartMenuRunning();
        int watchdogTicks = 0;
        QObject::connect(&watchdog, &QTimer::timeout, &app, [&tray, &warnedOther, &otherRunning, &watchdogTicks] {
            if (KeyHook::instance().installed())
                KeyHook::instance().checkAlive();
            if (++watchdogTicks % 10 == 0) // список процессов — раз в 30 с (≈3,5 мс), а не каждые 3 с
                otherRunning = KeyHook::otherStartMenuRunning();
            if (otherRunning)
            {
                KeyHook::instance().reinstallOnTop();
                if (!warnedOther)
                {
                    warnedOther = true;
                    tray.showMessage(QStringLiteral("StartMenu"),
                                     TXT("Запущен Start11 — он тоже перехватывает Win и иногда открывается вместо этого меню. "
                                         "Закройте Start11 и уберите его из автозагрузки."),
                                     QSystemTrayIcon::Warning, 10000);
                }
            }
        });
        watchdog.start(3000);

        // Окно настроек сообщает об изменениях — перечитываем (нужен только флаг «Win открывает меню»).
        // История буфера обмена: после копирования ждём 150 мс (программы кладут данные в несколько приёмов) и записываем.
        HANDLE clipEvent = CreateEventW(nullptr, FALSE, FALSE, kClipboardEvent);
        QTimer clipDebounce;
        clipDebounce.setSingleShot(true);
        clipDebounce.setInterval(150);
        QThreadPool clipPool; // один фоновый поток: записи сохраняются по очереди
        clipPool.setMaxThreadCount(1);
        QObject::connect(&clipDebounce, &QTimer::timeout, &app, [clipEvent, &clipPool] {
            if (!Settings::get().clipboardHistory)
                return;
            ClipboardStore::Raw raw = ClipboardStore::readRaw();
            if (!raw.valid)
                return;
            clipPool.start([raw, clipEvent] {
                if (ClipboardStore::process(raw))
                    SetEvent(clipEvent); // открытое меню зажжёт точку на кнопке
            });
        });
        QObject::connect(QGuiApplication::clipboard(), &QClipboard::dataChanged, &clipDebounce, [&clipDebounce] { clipDebounce.start(); });

        HANDLE hostReload = CreateEventW(nullptr, FALSE, FALSE, kHostReloadEvent);
        QWinEventNotifier reloadNotifier(hostReload);
        QObject::connect(&reloadNotifier, &QWinEventNotifier::activated, &app, [applyHook, &buildTrayMenu] {
            const QString langBefore = Lang::current();
            Settings::get().load();
            Lang::load(Settings::get().language);
            if (Lang::current() != langBefore)
                buildTrayMenu();
            applyHook();
        });

        if (args.contains(QStringLiteral("--show")))
            QTimer::singleShot(0, &app, toggle);
        QTimer::singleShot(2000, &app, trimMemory);

        // Поменялись мониторы/разрешение/масштаб — кнопка «Пуск» могла переехать, ищем заново.
        auto relocate = [] { KeyHook::instance().requestRelocate(); };
        auto watchScreen = [relocate](QScreen* s) { QObject::connect(s, &QScreen::geometryChanged, relocate); };
        for (QScreen* s : QGuiApplication::screens())
            watchScreen(s);
        QObject::connect(&app, &QGuiApplication::screenAdded, &app, [relocate, watchScreen](QScreen* s) {
            watchScreen(s);
            relocate();
        });
        QObject::connect(&app, &QGuiApplication::screenRemoved, &app, relocate);
        QObject::connect(&app, &QGuiApplication::primaryScreenChanged, &app, relocate);

        const int rc = app.exec();
        KeyHook::instance().uninstall();
        menu.closeMenu();
        CloseHandle(hostReload);
        return rc;
    }
}

int main(int argc, char* argv[])
{
    for (int i = 1; i < argc; ++i)
    {
        if (qstrcmp(argv[i], "--menu") == 0)
            return runMenu(argc, argv);
        if (qstrcmp(argv[i], "--settings") == 0)
            return runSettings(argc, argv);
        if (qstrcmp(argv[i], "--write-ico") == 0 && i + 1 < argc)
        {
            // Служебное: сохранить иконку программы в .ico (она встраивается в exe через app.rc).
            QGuiApplication app(argc, argv);
            return AppIcon::writeIco(QString::fromLocal8Bit(argv[i + 1])) ? 0 : 1;
        }
        if (qstrcmp(argv[i], "--apply-skin") == 0 && i + 1 < argc)
        {
            // Служебное: StartMenu.exe --apply-skin 0..3 — включить стиль с его стандартными цветами.
            QGuiApplication app(argc, argv);
            app.setApplicationName("StartMenu");
            Settings& s = Settings::get();
            s.load();
            s.skin = std::clamp(atoi(argv[i + 1]), 0, 3);
            Skin::select(s.skin);
            Skin::current().preset(s);
            s.preset = 1;
            s.save();
            return 0;
        }
        if (qstrcmp(argv[i], "--selftest-loose") == 0 && i + 2 < argc)
        {
            // Служебное: проверка свободных ярлыков без экрана. Читает раскладку из файла (копии), «мышью» тащит плитки
            // из группы на пустое место и пишет итог в текстовый файл. --selftest-loose <items.json> <отчёт.txt>
            QApplication app(argc, argv);
            setupApp(app);
            Settings::get().load();
            Lang::load(QStringLiteral("en"));
            Config::instance().setFileOverride(QString::fromLocal8Bit(argv[i + 1]));
            Config::instance().load();
            IconCache icons;
            Workspace w(&icons);
            w.resize(1100, 500);
            w.show();
            QCoreApplication::processEvents();
            QStringList log;
            auto summary = [&](const QString& what) {
                log << what;
                const auto& groups = Config::instance().groups();
                for (int g = 0; g < groups.size(); ++g)
                {
                    QStringList names;
                    for (const Tile& t : groups[g].tiles())
                        names << t.item.name;
                    log << QStringLiteral("  [%1] %2 col=%3 loose=%4 tiles=%5").arg(g).arg(groups[g].loose ? QStringLiteral("(loose)") : groups[g].title())
                               .arg(groups[g].column).arg(groups[g].loose).arg(names.join(QStringLiteral(",")));
                }
            };
            auto drag = [&](int g, int t, QPoint to) {
                const QPoint from = w.tileViewRect(g, t).center();
                auto send = [&](QEvent::Type type, QPoint p, Qt::MouseButton b, Qt::MouseButtons bs) {
                    QMouseEvent ev(type, QPointF(p), QPointF(w.mapToGlobal(p)), b, bs, Qt::NoModifier);
                    QCoreApplication::sendEvent(&w, &ev);
                };
                send(QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
                send(QEvent::MouseMove, from + QPoint(30, 0), Qt::NoButton, Qt::LeftButton);
                send(QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton);
                send(QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton);
                QCoreApplication::processEvents();
            };
            summary(QStringLiteral("start"));
            drag(0, 0, QPoint(950, 440));            // из группы — на пустое место справа (новая колонка)
            summary(QStringLiteral("group tile -> empty space, new column"));
            drag(1, 0, QPoint(300, 460));            // свободный ярлык — на пустое место под колонкой 0
            summary(QStringLiteral("loose tile -> empty space under column 0"));
            const auto& gs = Config::instance().groups();
            drag(int(gs.size()) - 1, 0, QPoint(950, 460)); // из последнего блока на пустое место справа
            summary(QStringLiteral("last block tile -> empty space right"));
            QFile out(QString::fromLocal8Bit(argv[i + 2]));
            if (out.open(QIODevice::WriteOnly | QIODevice::Text))
                out.write(log.join(QLatin1Char('\n')).toUtf8());
            return 0;
        }
        if (qstrcmp(argv[i], "--selftest-align") == 0 && i + 2 < argc)
        {
            // Служебное: проверка блока свободных ярлыков мышью без экрана — ширина за правый край и выравнивание
            // потягиванием по пустому месту. --selftest-align <items.json> <отчёт.txt>
            QApplication app(argc, argv);
            setupApp(app);
            Settings::get().load();
            Lang::load(QStringLiteral("en"));
            Config::instance().setFileOverride(QString::fromLocal8Bit(argv[i + 1]));
            Config::instance().load();
            IconCache icons;
            Workspace w(&icons);
            w.resize(1100, 500);
            w.show();
            QCoreApplication::processEvents();
            QStringList log;
            int looseG = -1;
            const auto& gs = Config::instance().groups();
            for (int g = 0; g < gs.size(); ++g)
                if (gs[g].loose)
                    looseG = g;
            auto state = [&](const QString& what) {
                const Group& g = Config::instance().groups()[looseG];
                const QRect t0 = w.tileViewRect(looseG, 0), fr = w.groupViewRect(looseG);
                log << QStringLiteral("%1: width=%2 alignH=%3  frame x=%4..%5  first tile x=%6").arg(what).arg(g.width).arg(g.alignH)
                           .arg(fr.left()).arg(fr.right()).arg(t0.left());
            };
            auto send = [&](QEvent::Type type, QPoint p, Qt::MouseButton b, Qt::MouseButtons bs) {
                QMouseEvent ev(type, QPointF(p), QPointF(w.mapToGlobal(p)), b, bs, Qt::NoModifier);
                QCoreApplication::sendEvent(&w, &ev);
            };
            auto drag = [&](QPoint from, QPoint to) {
                send(QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
                send(QEvent::MouseMove, from + QPoint(6, 0), Qt::NoButton, Qt::LeftButton);
                send(QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton);
                send(QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton);
                QCoreApplication::processEvents();
            };
            if (looseG >= 0)
            {
                state(QStringLiteral("start"));
                // 1. Потянуть правый край блока вправо — блок растягивается
                QRect fr = w.groupViewRect(looseG);
                drag(QPoint(fr.right() - 1, fr.center().y()), QPoint(fr.right() + 200, fr.center().y()));
                state(QStringLiteral("after dragging the right edge +200px"));
                // 2. Потянуть по пустому месту блока в правую треть — плитки встают справа
                fr = w.groupViewRect(looseG);
                const int y = w.tileViewRect(looseG, 0).center().y();
                drag(QPoint(fr.right() - 40, y), QPoint(fr.right() - 40, y));
                state(QStringLiteral("press+release in the right third (empty area)"));
                // 3. И обратно — в левую треть
                fr = w.groupViewRect(looseG);
                drag(QPoint(fr.left() + 25, y), QPoint(fr.left() + 25, y));
                state(QStringLiteral("press+release in the left third (empty area)"));
                // 4. В середину
                fr = w.groupViewRect(looseG);
                drag(QPoint(fr.right() - 40, y), QPoint(fr.center().x(), y)); // нажали на пустом, довели до середины
                state(QStringLiteral("press in the empty right area, drag to the middle"));
            }
            QFile out(QString::fromLocal8Bit(argv[i + 2]));
            if (out.open(QIODevice::WriteOnly | QIODevice::Text))
                out.write(log.join(QLatin1Char('\n')).toUtf8());
            return 0;
        }
        if (qstrcmp(argv[i], "--media-info") == 0 && i + 1 < argc)
        {
            // Служебное: что сейчас играет в Windows (источник, трек, есть ли обложка). --media-info <файл-результат>
            QCoreApplication app(argc, argv);
            std::mutex mu;
            Media::State last;
            Media::Watcher w([&](const Media::State& s) {
                std::lock_guard<std::mutex> lock(mu);
                last = s;
            });
            QThread::msleep(3500);
            std::lock_guard<std::mutex> lock(mu);
            QFile out(QString::fromLocal8Bit(argv[i + 1]));
            if (out.open(QIODevice::WriteOnly | QIODevice::Text))
                out.write(QStringLiteral("present=%1 app=%2 title=%3 artist=%4 art_bytes=%5")
                              .arg(last.present).arg(QString::fromStdWString(last.app), QString::fromStdWString(last.title),
                                                     QString::fromStdWString(last.artist)).arg(last.art.size()).toUtf8());
            return 0;
        }
        if (qstrcmp(argv[i], "--whatsnew-shot") == 0 && i + 1 < argc)
        {
            // Служебное: снимок карточки «Что нового» на тёмном градиенте (стекло в снимке не видно).
            QApplication app(argc, argv);
            setupApp(app);
            Settings::get().load();
            Lang::load(Settings::get().language);
            WhatsNewCard card(nullptr);
            card.setContent(QStringLiteral("1.0.5"), WhatsNew::bulletsFor(QStringLiteral("1.0.5")));
            QImage img(card.width() + 60, card.height() + 60, QImage::Format_ARGB32_Premultiplied);
            QLinearGradient gr(0, 0, img.width(), img.height());
            gr.setColorAt(0, QColor(20, 40, 140));
            gr.setColorAt(1, QColor(10, 14, 40));
            QPainter p(&img);
            p.fillRect(img.rect(), gr);
            card.render(&p, QPoint(30, 30));
            p.end();
            img.save(QString::fromLocal8Bit(argv[i + 1]));
            return 0;
        }
        if (qstrcmp(argv[i], "--file-location") == 0 && i + 2 < argc)
        {
            // Служебное: где лежит файл у пункта меню. --file-location <путь пункта> <файл-результат>
            QApplication app(argc, argv); // с графическим приложением инициализируется COM, как в самом меню
            Item it;
            it.path = QString::fromLocal8Bit(argv[i + 1]);
            QFile out(QString::fromLocal8Bit(argv[i + 2]));
            if (out.open(QIODevice::WriteOnly | QIODevice::Text))
                out.write(WinShell::fileLocation(it).toUtf8());
            return 0;
        }
        if (qstrcmp(argv[i], "--check-updates") == 0)
        {
            // Служебное: сразу спросить GitHub о новой версии (код выхода 0 — ответ получен)
            QCoreApplication app(argc, argv);
            app.setApplicationName("StartMenu");
            if (const int bi = int(app.arguments().indexOf(QStringLiteral("--bench"))); bi >= 0 && bi + 1 < app.arguments().size())
            {
                // Замер: сколько стоит вызов проверки, когда ещё «не пора» (так бывает при каждом открытии меню)
                Updater::check(true, {});
                QThread::msleep(4000); // дать первой проверке записать время
                QElapsedTimer tm;
                tm.start();
                for (int k = 0; k < 100000; ++k)
                    Updater::check(false, {});
                QFile out(app.arguments()[bi + 1]);
                if (out.open(QIODevice::WriteOnly | QIODevice::Text))
                    out.write(QStringLiteral("100000 calls, %1 ms total, %2 us per call").arg(tm.elapsed()).arg(tm.nsecsElapsed() / 100000.0 / 1000.0, 0, 'f', 3).toUtf8());
                return 0;
            }
            // --soft — как делает меню само: с ограничением «не чаще раза в 2 часа»
            const bool soft = app.arguments().contains(QStringLiteral("--soft"));
            Updater::check(!soft, [](bool ok) { QCoreApplication::exit(ok ? 0 : 1); });
            return app.exec();
        }
        if (qstrcmp(argv[i], "--settings-shots") == 0 && i + 1 < argc)
        {
            // Служебное: снимки всех вкладок окна настроек в PNG без показа. [--lang <код>] — на этом языке.
            QApplication app(argc, argv);
            setupApp(app);
            Settings::get().load();
            const int li = int(app.arguments().indexOf(QStringLiteral("--lang")));
            Lang::load(li >= 0 && li + 1 < app.arguments().size() ? app.arguments()[li + 1] : Settings::get().language);
            const QString dir = QString::fromLocal8Bit(argv[i + 1]);
            QDir().mkpath(dir);
            SettingsWindow w;
            w.resize(w.sizeHint().expandedTo(QSize(720, 640)));
            if (auto* tabs = w.findChild<QTabWidget*>())
                for (int t = 0; t < tabs->count(); ++t)
                {
                    tabs->setCurrentIndex(t);
                    QCoreApplication::processEvents();
                    w.grab().save(dir + QStringLiteral("/settings-tab%1.png").arg(t + 1));
                }
            return 0;
        }
        if (qstrcmp(argv[i], "--welcome") == 0)
        {
            // Мастер первого запуска: стиль, приложения, подсказки. --welcome-shots <папка> — снимки страниц без показа.
            QApplication app(argc, argv);
            setupApp(app);
            Settings::get().load();
            {
                // --lang <код> — мастер на этом языке (для снимков; настройки не меняются)
                const int li = int(app.arguments().indexOf(QStringLiteral("--lang")));
                Lang::load(li >= 0 && li + 1 < app.arguments().size() ? app.arguments()[li + 1] : Settings::get().language);
            }
            auto* w = new WelcomeWindow;
            const int si = int(app.arguments().indexOf(QStringLiteral("--welcome-shots")));
            if (si >= 0 && si + 1 < app.arguments().size())
            {
                w->saveShots(app.arguments()[si + 1]);
                return 0;
            }
            w->resize(1140, 820);
            w->show();
            return app.exec();
        }
        if (qstrcmp(argv[i], "--render") == 0 && i + 1 < argc)
        {
            // Служебное: нарисовать меню в PNG, не показывая его. --render файл.png [стиль 0..3]
            // Файлы настроек не меняются — стиль подменяется только в памяти.
            // --demo — снимки для страницы проекта: выдуманные плитки, буфер, плеер и цифры, стиль из пресета.
            QApplication app(argc, argv);
            setupApp(app);
            const bool demo = app.arguments().contains(QStringLiteral("--demo"));
            Settings& s = Settings::get();
            if (demo)
            {
                Demo::enable();
                s = Settings(); // настройки по умолчанию, а не сохранённые пользователем
                s.skin = (i + 2 < argc && argv[i + 2][0] != '-') ? std::clamp(atoi(argv[i + 2]), 0, 3) : s.skin;
                Skin::select(s.skin);
                Skin::current().preset(s);
                s.preset = 1;
                s.systemMonitor = true;
                s.clipboardHistory = true;
                // Плеер — только если попросили (--large-player / --compact-player)
                s.mediaPlayer = app.arguments().contains(QStringLiteral("--compact-player")) ? 1 : 0;
                s.apply();
            }
            else
                s.load();
            {
                // --lang <код> — нарисовать на этом языке (настройки не меняются)
                const int li = int(app.arguments().indexOf(QStringLiteral("--lang")));
                Lang::load(li >= 0 && li + 1 < app.arguments().size() ? app.arguments()[li + 1] : s.language);
            }
            if (!demo && i + 2 < argc && argv[i + 2][0] != '-')
            {
                // --default-style — цвета стиля из его пресета, а не сохранённые пользователем
                if (app.arguments().contains(QStringLiteral("--default-style")))
                {
                    s.skin = std::clamp(atoi(argv[i + 2]), 0, 3);
                    Skin::select(s.skin);
                    Skin::current().preset(s);
                    s.preset = 1;
                }
                else if (!s.restoreStyle(std::clamp(atoi(argv[i + 2]), 0, 3)))
                {
                    Skin::select(s.skin);
                    Skin::current().preset(s);
                }
                s.apply();
            }
            if (app.arguments().contains(QStringLiteral("--large-player")))
                s.mediaPlayer = 2;
            if (app.arguments().contains(QStringLiteral("--art-bg")))
                s.mediaArt = 1;
            if (app.arguments().contains(QStringLiteral("--art-color")))
                s.mediaColor = 1;
            else if (app.arguments().contains(QStringLiteral("--accent-color")))
                s.mediaColor = 0;
            if (app.arguments().contains(QStringLiteral("--demo-glow")))
                qputenv("STARTMENU_DEMO_GLOW", "1"); // для снимка: подсветка и волна «на пике»
            {
                // --labels / --no-labels — подписи плиток; --size ШxВ — размер окна (только в памяти)
                if (app.arguments().contains(QStringLiteral("--no-labels")))
                    s.showLabels = false;
                else if (app.arguments().contains(QStringLiteral("--labels")))
                    s.showLabels = true;
                // --tile-fill N — заливка плиток: 0 нет, 1 цвет иконки, 2 размытая иконка (только в памяти)
                const int ti = int(app.arguments().indexOf(QStringLiteral("--tile-fill")));
                if (ti >= 0 && ti + 1 < app.arguments().size())
                    s.tileFill = std::clamp(app.arguments()[ti + 1].toInt(), 0, 2);
                // --light / --dark — тема (только в памяти)
                if (app.arguments().contains(QStringLiteral("--light")))
                    s.theme = 1;
                else if (app.arguments().contains(QStringLiteral("--dark")))
                    s.theme = 0;
                // --opacity N — непрозрачность фона, % (только в памяти, для проверки)
                const int oi = int(app.arguments().indexOf(QStringLiteral("--opacity")));
                if (oi >= 0 && oi + 1 < app.arguments().size())
                    s.opacity = std::clamp(app.arguments()[oi + 1].toInt(), 0, 100);
                const int zi = int(app.arguments().indexOf(QStringLiteral("--size")));
                if (zi >= 0 && zi + 1 < app.arguments().size())
                {
                    const QStringList wh = app.arguments()[zi + 1].split(QLatin1Char('x'));
                    if (wh.size() == 2)
                    {
                        s.width = wh[0].toInt();
                        s.height = wh[1].toInt();
                    }
                }
                s.apply();
            }
            if (demo)
                Demo::fillConfig();
            else
            {
                // --items <файл> — раскладка из копии items.json (например, с нейтральными названиями групп)
                const int ii = int(app.arguments().indexOf(QStringLiteral("--items")));
                if (ii >= 0 && ii + 1 < app.arguments().size())
                    Config::instance().setFileOverride(app.arguments()[ii + 1]);
                Config::instance().load();
            }
            if (app.arguments().contains(QStringLiteral("--empty")))
                Config::instance().groups().clear(); // только в памяти: проверить вид пустого меню
            if (app.arguments().contains(QStringLiteral("--demo-features")) && !Config::instance().groups().isEmpty())
            {
                // Только в памяти: группа-список со ссылками и второй слой — посмотреть, как выглядит
                auto& groups = Config::instance().groups();
                Group links;
                links.title() = TXT("Ссылки");
                links.listMode = true;
                links.width = 6;
                links.column = groups.last().column;
                for (const char* u : {"https://www.youtube.com", "https://github.com", "https://www.figma.com"})
                    links.tiles().append({Config::itemFromUrl(QString::fromLatin1(u)), TileSize::Medium, false, {}});
                links.tiles().append({Config::itemFromPath(QDir::homePath() + QStringLiteral("/Downloads")), TileSize::Medium, false, {}});
                groups.append(links);
                Config::instance().layouts().append({TXT("Игры"), {}});
            }
            StartWindow w(true);
            w.prepareHidden();
            w.prepareRender(app.arguments().contains(QStringLiteral("--clipboard-view")));
            QEventLoop wait; // иконки грузятся в фоне — даём им время
            QTimer::singleShot(1500, &wait, &QEventLoop::quit);
            wait.exec();
            const QImage shot = w.renderWithPlayer();
            if (app.arguments().contains(QStringLiteral("--transparent")))
                return shot.save(QString::fromLocal8Bit(argv[i + 1])) ? 0 : 1; // с прозрачностью: стекло и просвет между плеером и меню
            QImage out(shot.size(), QImage::Format_ARGB32_Premultiplied);
            out.fill(QColor(32, 34, 40)); // вместо рабочего стола под окном
            QPainter p(&out);
            p.drawImage(0, 0, shot);
            p.end();
            return out.save(QString::fromLocal8Bit(argv[i + 1])) ? 0 : 1;
        }
        if (qstrcmp(argv[i], "--explorer-verb") == 0 && i + 1 < argc)
        {
            // StartMenu.exe --explorer-verb on|off — пункт «Закрепить в StartMenu» в Проводнике.
            QCoreApplication app(argc, argv);
            PinSync::setExplorerVerb(qstrcmp(argv[i + 1], "on") == 0);
            return 0;
        }
        if (qstrcmp(argv[i], "--pin") == 0 && i + 1 < argc)
        {
            // Пункт «Закрепить в меню «Пуск»» из контекстного меню Проводника.
            QCoreApplication app(argc, argv);
            app.setApplicationName("StartMenu");
            Settings::get().load();
            Lang::load(Settings::get().language); // стандартные названия групп — на языке интерфейса
            bool pinned = false;
            {
                const FileLock lock; // прочитать-добавить-записать одним куском
                Config::instance().load();
                pinned = PinSync::pinPath(QString::fromLocal8Bit(argv[i + 1]));
                if (pinned)
                    Config::instance().save();
            }
            if (pinned)
            {
                if (HANDLE ev = OpenEventW(EVENT_MODIFY_STATE, FALSE, kItemsEvent))
                {
                    SetEvent(ev); // меню «наготове» перечитает плитки, а не затрёт их своей копией
                    CloseHandle(ev);
                }
            }
            return 0;
        }
        if (qstrcmp(argv[i], "--export-icons") == 0 && i + 1 < argc)
        {
            // Служебный режим: выгрузить иконки всех плиток в PNG 128 px (для макетов дизайна).
            QApplication app(argc, argv);
            setupApp(app);
            Config::instance().load();
            const QString dir = QString::fromLocal8Bit(argv[i + 1]);
            QDir().mkpath(dir);
            for (const Group& g : Config::instance().groups())
                for (const Tab& tab : g.tabs)
                    for (const Tile& t : tab.tiles)
                        for (const Item& it : t.isFolder ? t.children : QList<Item>{t.item})
                        {
                            const QPixmap pm = WinShell::loadIcon(it.path, 128);
                            QString name = it.name;
                            name.replace(QRegularExpression(QStringLiteral("[^\\w\\-. ]")), QStringLiteral("_"));
                            if (!pm.isNull())
                                pm.save(dir + QLatin1Char('/') + name + QStringLiteral(".png"), "PNG");
                        }
            return 0;
        }
    }
    return runHost(argc, argv);
}
