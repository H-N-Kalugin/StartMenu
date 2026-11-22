#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// Что сейчас играет в Windows (Spotify, браузер, любой плеер с системными медиа-кнопками)
// и управление им. Работает в своём фоновом потоке; Qt здесь нет намеренно —
// заголовки C++/WinRT конфликтуют с макросами Qt, поэтому они живут только в MediaSession.cpp.
namespace Media
{
    struct State
    {
        bool present = false;          // есть ли вообще что-то, чем можно управлять
        std::wstring title, artist, app;
        bool playing = false;
        bool canPrev = false, canNext = false;
        std::vector<std::uint8_t> art; // обложка (PNG/JPEG как есть), может быть пустой
        // Позиция в треке (мс). duration = 0 — плеер её не сообщает (ползунок не показываем).
        std::int64_t positionMs = 0, durationMs = 0;
        bool canSeek = false;
    };

    enum class Command { PlayPause, Pause, Next, Previous, Seek };

    class Watcher
    {
    public:
        // onChange вызывается из фонового потока — в Qt передавайте результат через QueuedConnection.
        explicit Watcher(std::function<void(const State&)> onChange);
        ~Watcher();
        void command(Command c, std::int64_t ms = 0); // ms — куда перемотать (для Seek)
        void refresh();
        // false — окно меню спрятано: частые события «позиция трека изменилась» не обрабатываем
        // (позиция всё равно перечитывается при показе), чтобы спрятанное меню не просыпалось каждую секунду.
        void setActive(bool active);

    private:
        struct Impl;
        std::shared_ptr<Impl> d;
    };
}
