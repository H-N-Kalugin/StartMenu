#include "KeyHook.h"
#include <QMetaObject>

#ifdef _WIN32
#include <windows.h>
#include <tlhelp32.h>
#include <ole2.h> // WIN32_LEAN_AND_MEAN выкидывает COM-заголовки, а UI Automation они нужны
#include <UIAutomation.h>
#include <atomic>
#include <thread>

namespace
{
    // Перехватчики живут в своём потоке с высоким приоритетом, а не в основном потоке трея.
    // Пока основной поток занят (буфер обмена, меню трея, запуск процессов), Windows ждёт ответа хука —
    // и весь ввод в системе (мышь в игре, клавиши) подтормаживал бы, а при долгой паузе Windows снимает хук.
    // Все переменные ниже трогает только поток перехвата (кроме помеченных atomic).
    HHOOK g_kbHook = nullptr;
    HHOOK g_mouseHook = nullptr;
    bool g_winDown = false;
    DWORD g_winDownTick = 0;      // когда нажали Win (зависшее «нажата» — сбрасываем)
    DWORD g_swallowTick = 0;      // когда проглотили нажатие мыши на кнопке «Пуск»
    bool g_combo = false;
    bool g_swallowUp = false;

    std::atomic<bool> g_wanted{false};   // хуки должны стоять
    std::atomic<bool> g_installed{false};
    std::atomic<DWORD> g_lastHookTick{0}; // когда хук последний раз что-то получил — для сторожка

    std::thread g_hookThread;
    std::atomic<DWORD> g_hookThreadId{0};
    enum : UINT
    {
        MsgCheckAlive = WM_APP + 1,
        MsgReinstallOnTop,
        MsgResync,
        MsgResetState,
    };

    // ---------- кнопка «Пуск» на панели задач ----------
    // Где она, узнаём через UI Automation в отдельном потоке (это медленно, в хуке так нельзя).
    // Хук мыши только сравнивает координаты клика с запомненным прямоугольником.
    std::atomic<bool> g_startEnabled{true};
    std::atomic<bool> g_winEnabled{true};
    std::atomic<long> g_startL{0}, g_startT{0}, g_startR{0}, g_startB{0};
    std::atomic<long> g_barL{0}, g_barT{0}, g_barR{0}, g_barB{0};
    std::atomic<DWORD> g_lastLocate{0};
    HANDLE g_locateEvent = nullptr;
    HANDLE g_stopEvent = nullptr;
    std::thread g_locator;

    void emitTapped()
    {
        // В хуке нельзя долго работать — только ставим сигнал в очередь основного потока.
        QMetaObject::invokeMethod(&KeyHook::instance(), [] { emit KeyHook::instance().winTapped(); }, Qt::QueuedConnection);
    }

    void requestLocate()
    {
        const DWORD now = GetTickCount();
        if (now - g_lastLocate.load() > 700 && g_locateEvent)
        {
            g_lastLocate = now;
            SetEvent(g_locateEvent);
        }
    }

    void locateStartButton(IUIAutomation* uia)
    {
        HWND bar = FindWindowW(L"Shell_TrayWnd", nullptr);
        RECT br = {};
        if (!bar || !GetWindowRect(bar, &br))
            return;
        g_barL = br.left; g_barT = br.top; g_barR = br.right; g_barB = br.bottom;

        IUIAutomationElement* root = nullptr;
        if (FAILED(uia->ElementFromHandle(bar, &root)) || !root)
            return;
        VARIANT id;
        VariantInit(&id);
        id.vt = VT_BSTR;
        id.bstrVal = SysAllocString(L"StartButton");
        IUIAutomationCondition* cond = nullptr;
        IUIAutomationElement* button = nullptr;
        if (SUCCEEDED(uia->CreatePropertyCondition(UIA_AutomationIdPropertyId, id, &cond)) && cond
            && SUCCEEDED(root->FindFirst(TreeScope_Descendants, cond, &button)) && button)
        {
            RECT r = {};
            if (SUCCEEDED(button->get_CurrentBoundingRectangle(&r)) && r.right > r.left)
            {
                g_startL = r.left; g_startT = r.top; g_startR = r.right; g_startB = r.bottom;
            }
            button->Release();
        }
        if (cond)
            cond->Release();
        VariantClear(&id);
        root->Release();
    }

    void locatorThread()
    {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        IUIAutomation* uia = nullptr;
        if (SUCCEEDED(CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&uia))))
        {
            HANDLE events[2] = {g_stopEvent, g_locateEvent};
            locateStartButton(uia);
            while (WaitForMultipleObjects(2, events, FALSE, INFINITE) == WAIT_OBJECT_0 + 1)
                locateStartButton(uia);
            uia->Release();
        }
        CoUninitialize();
    }

    bool inStartButton(POINT pt)
    {
        return g_startR.load() > g_startL.load() && pt.x >= g_startL && pt.x < g_startR && pt.y >= g_startT
               && pt.y < g_startB;
    }

    bool inTaskbar(POINT pt)
    {
        return pt.x >= g_barL && pt.x < g_barR && pt.y >= g_barT && pt.y < g_barB;
    }

    // Перед тем как «съесть» клик, проверяем по живым данным: панель задач видна (не уехала при автоскрытии)
    // и точка действительно на ней. Иначе запомненный прямоугольник мог устареть.
    bool taskbarReallyUnder(POINT pt)
    {
        HWND bar = FindWindowW(L"Shell_TrayWnd", nullptr);
        RECT r = {};
        if (!bar || !IsWindowVisible(bar) || !GetWindowRect(bar, &r) || !PtInRect(&r, pt))
            return false;
        // Над игрой в окне на весь экран панель задач «видима», но лежит ПОД игрой: клик достаётся игре.
        // Считаем клик нашим, только если под курсором действительно сама панель задач.
        HWND hit = WindowFromPoint(pt);
        if (!hit || GetAncestor(hit, GA_ROOT) != bar)
            return false;
        MONITORINFO mi = {sizeof(mi)};
        if (!GetMonitorInfoW(MonitorFromWindow(bar, MONITOR_DEFAULTTONEAREST), &mi))
            return false;
        RECT vis = {};
        IntersectRect(&vis, &r, &mi.rcMonitor);
        const long area = (r.right - r.left) * (r.bottom - r.top);
        const long visArea = (vis.right - vis.left) * (vis.bottom - vis.top);
        return area > 0 && visArea * 2 >= area; // при автоскрытии на экране остаётся пара пикселей
    }

    // Активное окно запущено от администратора? Тогда наш хук (обычные права) не видит его ввод — это не «смерть» хука.
    bool foregroundElevated()
    {
        HWND fg = GetForegroundWindow();
        DWORD pid = 0;
        if (!fg || !GetWindowThreadProcessId(fg, &pid) || !pid)
            return false;
        HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!proc)
            return true;
        bool elevated = true; // не смогли открыть токен — почти наверняка процесс выше нас по правам
        HANDLE token = nullptr;
        if (OpenProcessToken(proc, TOKEN_QUERY, &token))
        {
            TOKEN_ELEVATION e = {};
            DWORD size = 0;
            if (GetTokenInformation(token, TokenElevation, &e, sizeof(e), &size))
                elevated = e.TokenIsElevated != 0;
            CloseHandle(token);
        }
        CloseHandle(proc);
        return elevated;
    }

    bool winPhysicallyDown()
    {
        return (GetAsyncKeyState(VK_LWIN) & 0x8000) || (GetAsyncKeyState(VK_RWIN) & 0x8000);
    }

    // ---------- хуки ----------

    // Настоящее отпускание Win мы глушим, а вместо него вставляем «пустую» клавишу 0xE8
    // и уже потом отпускание Win. Shell видит Win↓ E8↓ E8↑ Win↑ — это «комбинация»,
    // и родной «Пуск» не открывается.
    void sendMaskedWinUp(DWORD vk)
    {
        INPUT in[3] = {};
        for (auto& i : in)
            i.type = INPUT_KEYBOARD;
        in[0].ki.wVk = 0xE8;
        in[1].ki.wVk = 0xE8;
        in[1].ki.dwFlags = KEYEVENTF_KEYUP;
        in[2].ki.wVk = static_cast<WORD>(vk);
        in[2].ki.dwFlags = KEYEVENTF_KEYUP | KEYEVENTF_EXTENDEDKEY;
        SendInput(3, in, sizeof(INPUT));
    }

    LRESULT CALLBACK keyboardProc(int code, WPARAM wParam, LPARAM lParam)
    {
        if (code == HC_ACTION)
        {
            g_lastHookTick = GetTickCount();
            const auto* k = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
            // Свои же вставленные события пропускаем, иначе зациклимся.
            if (g_winEnabled && !(k->flags & LLKHF_INJECTED))
            {
                const bool isWin = (k->vkCode == VK_LWIN || k->vkCode == VK_RWIN);
                const bool down = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
                const bool up = (wParam == WM_KEYUP || wParam == WM_SYSKEYUP);

                if (isWin && down)
                {
                    // Новое нажатие, а прошлое «нажата» висит дольше 5 с — значит, отпускание мы тогда не увидели
                    if (!g_winDown || GetTickCount() - g_winDownTick > 5000)
                    {
                        g_winDown = true;
                        g_combo = false;
                        g_winDownTick = GetTickCount();
                    }
                }
                else if (isWin && up)
                {
                    const bool tap = g_winDown && !g_combo;
                    g_winDown = false;
                    if (tap)
                    {
                        sendMaskedWinUp(k->vkCode);
                        emitTapped();
                        return 1; // настоящее отпускание Win проглочено
                    }
                }
                else if (g_winDown && down)
                {
                    g_combo = true;
                }
            }
        }
        return CallNextHookEx(g_kbHook, code, wParam, lParam);
    }

    LRESULT CALLBACK mouseProc(int code, WPARAM wParam, LPARAM lParam)
    {
        if (code == HC_ACTION)
        {
            g_lastHookTick = GetTickCount();
            const auto* m = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);
            // Win + клик/колёсико (сочетания в играх и утилитах) — это не одиночное нажатие Win
            if (g_winDown && wParam != WM_MOUSEMOVE && !(m->flags & LLMHF_INJECTED))
                g_combo = true;
            if (g_startEnabled && !(m->flags & LLMHF_INJECTED))
            {
                switch (wParam)
                {
                case WM_MOUSEMOVE:
                    // Курсор над панелью задач — уточним, где сейчас кнопка «Пуск»
                    // (при выравнивании по центру она сдвигается, когда открываются программы).
                    if (inTaskbar(m->pt) || g_barR.load() == 0)
                        requestLocate();
                    break;
                case WM_LBUTTONDOWN:
                    if (inStartButton(m->pt) && taskbarReallyUnder(m->pt))
                    {
                        g_swallowUp = true;
                        g_swallowTick = GetTickCount();
                        return 1; // родной «Пуск» не увидит клика
                    }
                    break;
                case WM_LBUTTONUP:
                    if (g_swallowUp)
                    {
                        g_swallowUp = false;
                        emitTapped();
                        return 1;
                    }
                    break;
                default:
                    break;
                }
            }
        }
        return CallNextHookEx(g_mouseHook, code, wParam, lParam);
    }

    // ---------- обслуживание (только в потоке перехвата) ----------

    void resyncImpl()
    {
        // Отпускание Win или кнопки мыши могло не дойти до нас (фокус ушёл в окно администратора,
        // чужой перехватчик проглотил). Тогда сверяем с настоящим состоянием клавиатуры и мыши —
        // иначе Win «залипла» бы навсегда и каждое следующее нажатие считалось бы сочетанием.
        if (g_winDown && !winPhysicallyDown() && GetTickCount() - g_winDownTick > 1000)
            g_winDown = g_combo = false;
        if (g_swallowUp && !(GetAsyncKeyState(VK_LBUTTON) & 0x8000) && GetTickCount() - g_swallowTick > 1000)
            g_swallowUp = false;
    }

    void installHooks()
    {
        if (!g_kbHook)
            g_kbHook = SetWindowsHookExW(WH_KEYBOARD_LL, keyboardProc, GetModuleHandleW(nullptr), 0);
        if (!g_mouseHook)
            g_mouseHook = SetWindowsHookExW(WH_MOUSE_LL, mouseProc, GetModuleHandleW(nullptr), 0);
        g_installed = g_kbHook != nullptr;
        g_lastHookTick = GetTickCount();
    }

    void removeHooks()
    {
        if (g_kbHook) { UnhookWindowsHookEx(g_kbHook); g_kbHook = nullptr; }
        if (g_mouseHook) { UnhookWindowsHookEx(g_mouseHook); g_mouseHook = nullptr; }
        g_installed = false;
        g_winDown = g_combo = g_swallowUp = false;
    }

    void reinstallOnTopImpl()
    {
        resyncImpl();
        // Windows отдаёт клавиши сначала последнему поставленному перехватчику.
        // Если другой «Пуск» (Start11) поставил свой позже — переставляем наш, чтобы Win доставалась нам первыми.
        if (!g_wanted || winPhysicallyDown() || (GetAsyncKeyState(VK_LBUTTON) & 0x8000))
            return; // посреди нажатия Win или клика не трогаем
        if (HHOOK fresh = SetWindowsHookExW(WH_KEYBOARD_LL, keyboardProc, GetModuleHandleW(nullptr), 0))
        {
            if (g_kbHook)
                UnhookWindowsHookEx(g_kbHook);
            g_kbHook = fresh;
        }
        // Клик по кнопке «Пуск» — то же самое для мыши
        if (HHOOK mouse = SetWindowsHookExW(WH_MOUSE_LL, mouseProc, GetModuleHandleW(nullptr), 0))
        {
            if (g_mouseHook)
                UnhookWindowsHookEx(g_mouseHook);
            g_mouseHook = mouse;
        }
        g_installed = g_kbHook != nullptr;
    }

    void checkAliveImpl()
    {
        if (!g_wanted)
            return;
        resyncImpl();
        // Система видела ввод заметно позже, чем до нас дошло последнее событие, — значит, хук сняли.
        // Исключение: ввод шёл в окно администратора — его наш хук и не должен видеть.
        LASTINPUTINFO li = {sizeof(li), 0};
        if (!GetLastInputInfo(&li))
            return;
        const bool dead = !g_kbHook || !g_mouseHook
                          || (static_cast<int>(li.dwTime - g_lastHookTick.load()) > 2000 && !foregroundElevated());
        if (!dead)
            return;
        // Win сейчас зажата — состояние «нажата» сохраняем, иначе её отпускание откроет родной «Пуск»
        const bool keepWin = g_winDown && winPhysicallyDown();
        if (g_kbHook)
            UnhookWindowsHookEx(g_kbHook);
        if (g_mouseHook)
            UnhookWindowsHookEx(g_mouseHook);
        g_kbHook = g_mouseHook = nullptr;
        installHooks();
        g_winDown = keepWin;
        if (!keepWin)
            g_combo = false;
        g_swallowUp = false;
    }

    void hookThreadMain(HANDLE ready)
    {
        // Перехват ввода не должен ждать, пока система занята чем-то ещё.
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
        MSG msg;
        PeekMessageW(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE); // создаём очередь сообщений потока
        installHooks();
        SetEvent(ready);
        while (GetMessageW(&msg, nullptr, 0, 0) > 0)
        {
            switch (msg.message)
            {
            case MsgCheckAlive:     checkAliveImpl(); break;
            case MsgReinstallOnTop: reinstallOnTopImpl(); break;
            case MsgResync:         resyncImpl(); break;
            case MsgResetState:     g_winDown = g_combo = false; break;
            default:
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
                break;
            }
        }
        removeHooks();
    }

    void post(UINT message)
    {
        if (const DWORD id = g_hookThreadId.load())
            PostThreadMessageW(id, message, 0, 0);
    }
}

bool KeyHook::install()
{
    g_wanted = true;
    if (!g_hookThread.joinable())
    {
        HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        g_hookThread = std::thread([ready] {
            g_hookThreadId = GetCurrentThreadId();
            hookThreadMain(ready);
        });
        WaitForSingleObject(ready, 5000);
        CloseHandle(ready);
    }
    if (!g_locator.joinable())
    {
        g_locateEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        g_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        g_locator = std::thread(locatorThread);
    }
    return g_installed;
}

void KeyHook::uninstall()
{
    g_wanted = false;
    if (g_hookThread.joinable())
    {
        post(WM_QUIT);
        g_hookThread.join();
        g_hookThreadId = 0;
    }
    if (g_locator.joinable())
    {
        SetEvent(g_stopEvent);
        g_locator.join();
        CloseHandle(g_locateEvent);
        CloseHandle(g_stopEvent);
        g_locateEvent = g_stopEvent = nullptr;
    }
}

bool KeyHook::installed() const
{
    return g_installed;
}

void KeyHook::setStartButtonEnabled(bool on)
{
    g_startEnabled = on;
}

void KeyHook::setWinKeyEnabled(bool on)
{
    g_winEnabled = on;
    post(MsgResetState);
}

void KeyHook::requestRelocate()
{
    g_lastLocate = 0;
    if (g_locateEvent)
        SetEvent(g_locateEvent);
}

void KeyHook::resyncState()
{
    post(MsgResync);
}

void KeyHook::reinstallOnTop()
{
    post(MsgReinstallOnTop);
}

bool KeyHook::otherStartMenuRunning()
{
    // Процессы Start11 (Stardock). Только смотрим, что они запущены, — ничего в них не трогаем.
    static const wchar_t* names[] = {L"Start11_64.exe", L"Start11.exe", L"Start10_64.exe", L"StartAllBack.exe"};
    bool found = false;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return false;
    PROCESSENTRY32W pe = {sizeof(pe)};
    for (BOOL ok = Process32FirstW(snap, &pe); ok && !found; ok = Process32NextW(snap, &pe))
        for (const wchar_t* n : names)
            if (_wcsicmp(pe.szExeFile, n) == 0)
                found = true;
    CloseHandle(snap);
    return found;
}

void KeyHook::checkAlive()
{
    post(MsgCheckAlive);
}
#else
bool KeyHook::install() { return false; }
void KeyHook::reinstallOnTop() {}
void KeyHook::resyncState() {}
bool KeyHook::otherStartMenuRunning() { return false; }
void KeyHook::uninstall() {}
bool KeyHook::installed() const { return false; }
void KeyHook::setStartButtonEnabled(bool) {}
void KeyHook::setWinKeyEnabled(bool) {}
void KeyHook::requestRelocate() {}
void KeyHook::checkAlive() {}
#endif

KeyHook& KeyHook::instance()
{
    static KeyHook h;
    return h;
}
