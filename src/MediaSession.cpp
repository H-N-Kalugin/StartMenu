#include "MediaSession.h"

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <chrono>
#include <thread>

using namespace winrt;
using namespace winrt::Windows::Media::Control;
using namespace winrt::Windows::Storage::Streams;

namespace Media
{
    // Задачи для фонового потока: «обновить» или «команда».
    struct Task
    {
        bool isCommand = false;
        Command cmd = Command::PlayPause;
        std::int64_t ms = 0;
    };

    struct Watcher::Impl
    {
        std::function<void(const State&)> onChange;
        std::thread thread;
        std::mutex m;
        std::condition_variable cv;
        std::deque<Task> queue;
        bool stop = false;
        std::atomic<bool> finished{false};
        std::atomic<bool> active{true};

        // Живут только в фоновом потоке.
        GlobalSystemMediaTransportControlsSessionManager manager{nullptr};
        GlobalSystemMediaTransportControlsSession session{nullptr};
        event_token sessionChangedToken{}, propsToken{}, playbackToken{}, timelineToken{};
        std::int64_t startTicks = 0; // начало шкалы трека (для перемотки)
        std::wstring lastArtKey;
        std::vector<std::uint8_t> lastArt;
        int artTries = 0;          // сколько раз пробовали получить обложку этого трека
        bool needArtRetry = false; // трек есть, а обложки пока нет — Spotify присылает её с задержкой, повторим

        void push(Task t)
        {
            {
                std::lock_guard lock(m);
                // Несколько «обновить» подряд — достаточно одного.
                if (!t.isCommand && !queue.empty() && !queue.back().isCommand)
                    return;
                queue.push_back(t);
            }
            cv.notify_one();
        }

        void unsubscribeSession()
        {
            if (!session)
                return;
            try
            {
                session.MediaPropertiesChanged(propsToken);
                session.PlaybackInfoChanged(playbackToken);
                session.TimelinePropertiesChanged(timelineToken);
            }
            catch (...) {}
            session = nullptr;
        }

        void attachSession()
        {
            auto current = manager ? manager.GetCurrentSession() : nullptr;
            if (current == session)
                return;
            unsubscribeSession();
            session = current;
            if (!session)
                return;
            propsToken = session.MediaPropertiesChanged([this](auto&&, auto&&) { push({}); });
            playbackToken = session.PlaybackInfoChanged([this](auto&&, auto&&) { push({}); });
            timelineToken = session.TimelinePropertiesChanged([this](auto&&, auto&&) {
                if (active)
                    push({});
            });
        }

        std::vector<std::uint8_t> readArt(const IRandomAccessStreamReference& ref)
        {
            std::vector<std::uint8_t> out;
            if (!ref)
                return out;
            auto stream = ref.OpenReadAsync().get();
            const auto size = static_cast<std::uint32_t>(std::min<std::uint64_t>(stream.Size(), 4u * 1024 * 1024));
            if (!size)
                return out;
            Buffer buf(size);
            auto res = stream.ReadAsync(buf, size, InputStreamOptions::None).get();
            out.assign(res.data(), res.data() + res.Length());
            return out;
        }

        State read()
        {
            State s;
            attachSession();
            if (!session)
                return s;
            s.present = true;
            s.app = std::wstring(session.SourceAppUserModelId());
            try
            {
                const auto info = session.GetPlaybackInfo();
                s.playing = info.PlaybackStatus() == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing;
                const auto c = info.Controls();
                s.canPrev = c.IsPreviousEnabled();
                s.canNext = c.IsNextEnabled();
                s.canSeek = c.IsPlaybackPositionEnabled();
            }
            catch (...) {}
            try
            {
                // Плеер сообщает позицию на момент LastUpdatedTime — досчитываем, сколько прошло с тех пор.
                const auto tl = session.GetTimelineProperties();
                const std::int64_t start = tl.StartTime().count(), end = tl.EndTime().count();
                startTicks = start;
                std::int64_t pos = tl.Position().count() - start;
                if (s.playing)
                {
                    const std::int64_t since = (winrt::clock::now() - tl.LastUpdatedTime()).count();
                    if (since > 0 && since < 36000000000LL) // не больше часа — иначе время у плеера сбито
                        pos += since;
                }
                s.durationMs = std::max<std::int64_t>(0, (end - start) / 10000);
                s.positionMs = std::clamp<std::int64_t>(pos / 10000, 0, s.durationMs);
            }
            catch (...) {}
            try
            {
                const auto props = session.TryGetMediaPropertiesAsync().get();
                s.title = std::wstring(props.Title());
                s.artist = std::wstring(props.Artist());
                // Обложку читаем заново при смене трека. Spotify в момент смены трека часто ещё не готов отдать картинку —
                // поэтому, пока её нет, пробуем снова (до 20 раз с паузой около секунды), а не «запоминаем», что обложки нет.
                const std::wstring key = s.app + L"|" + s.title + L"|" + s.artist;
                if (key != lastArtKey)
                {
                    lastArtKey = key;
                    lastArt.clear();
                    artTries = 0;
                }
                if (lastArt.empty() && artTries < 20)
                {
                    ++artTries;
                    try { lastArt = readArt(props.Thumbnail()); } catch (...) {}
                }
                needArtRetry = lastArt.empty() && artTries < 20;
                s.art = lastArt;
            }
            catch (...) {}
            if (s.title.empty())
                s.present = false; // сессия есть, но трека нет (плеер только открыт) — показывать нечего
            return s;
        }

        void runCommand(Command c, std::int64_t ms)
        {
            if (!session)
                return;
            try
            {
                switch (c)
                {
                case Command::PlayPause: session.TryTogglePlayPauseAsync().get(); break;
                case Command::Pause:     session.TryPauseAsync().get(); break;
                case Command::Next:      session.TrySkipNextAsync().get(); break;
                case Command::Previous:  session.TrySkipPreviousAsync().get(); break;
                case Command::Seek:      session.TryChangePlaybackPositionAsync(startTicks + ms * 10000).get(); break;
                }
            }
            catch (...) {}
        }

        void loop()
        {
            init_apartment(apartment_type::multi_threaded);
            try
            {
                manager = GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
                sessionChangedToken = manager.CurrentSessionChanged([this](auto&&, auto&&) { push({}); });
            }
            catch (...) { manager = nullptr; }
            for (;;)
            {
                Task t;
                {
                    std::unique_lock lock(m);
                    cv.wait(lock, [this] { return stop || !queue.empty(); });
                    if (stop)
                        break;
                    t = queue.front();
                    queue.pop_front();
                }
                if (t.isCommand)
                    runCommand(t.cmd, t.ms);
                State s;
                needArtRetry = false;
                try { s = read(); } catch (...) {}
                std::function<void(const State&)> cb;
                {
                    std::lock_guard lock(m);
                    cb = onChange;
                }
                if (cb)
                    cb(s);
                if (needArtRetry)
                {
                    // Обложки ещё нет — через секунду читаем снова (если за это время ничего другого не пришло)
                    std::unique_lock lock(m);
                    cv.wait_for(lock, std::chrono::seconds(1), [this] { return stop || !queue.empty(); });
                    if (!stop && queue.empty())
                        queue.push_back({});
                }
            }
            unsubscribeSession();
            try
            {
                if (manager)
                    manager.CurrentSessionChanged(sessionChangedToken);
            }
            catch (...) {}
            manager = nullptr;
            uninit_apartment();
        }
    };

    Watcher::Watcher(std::function<void(const State&)> onChange)
        : d(std::make_shared<Impl>())
    {
        d->onChange = std::move(onChange);
        d->queue.push_back({});
        // Поток держит свою ссылку на данные: если он завис на запросе к плееру, его можно отпустить
        std::shared_ptr<Impl> impl = d;
        d->thread = std::thread([impl] {
            impl->loop();
            impl->finished = true;
        });
    }

    Watcher::~Watcher()
    {
        {
            std::lock_guard lock(d->m);
            d->stop = true;
            d->onChange = nullptr; // окна уже нет — ответы больше никому не нужны
        }
        d->cv.notify_one();
        // Ждём не дольше секунды: зависший плеер (запрос к нему без ответа) не должен подвешивать закрытие
        for (int i = 0; i < 100 && !d->finished; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if (d->finished && d->thread.joinable())
            d->thread.join();
        else if (d->thread.joinable())
            d->thread.detach();
    }

    void Watcher::command(Command c, std::int64_t ms) { d->push({true, c, ms}); }
    void Watcher::refresh() { d->push({}); }
    void Watcher::setActive(bool active) { d->active = active; }
}
