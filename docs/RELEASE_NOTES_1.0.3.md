# StartMenu 1.0.3

A small follow-up to 1.0.2: a cheaper update check and a better music player. Update from inside the app: click the pulsing dot next to the version number in the bottom-right corner of the menu → **Update**.

## 🎵 Music player

- **Spotify cover art now shows up.** Spotify often sends the cover a moment after the track changes, and the player used to give up right away. It now keeps trying for a few seconds until the cover arrives.
- **A proper placeholder icon.** Until the cover is there (or when a track has none), the player shows a clean vector music note instead of the old hand-drawn one.

## ⚡ Improved

- **The update check costs nothing when it isn't due.** The menu checks for new versions when it opens (at most once every couple of hours), and that check now reads nothing from disk and touches no network until it's time. The actual request runs in a separate lowest-priority thread, starts a couple of seconds after the menu appears, and never competes with opening the menu or with games. If you're offline, it backs off for ten minutes instead of retrying on every open.

<details>
<summary><b>🇷🇺 По-русски</b></summary>

Небольшое дополнение к 1.0.2: проверка обновлений стала дешевле, а плеер лучше. Обновиться можно прямо из программы: нажмите пульсирующий кружок рядом с номером версии в правом нижнем углу меню → **Обновить**.

**Плеер**
- **Обложка Spotify теперь появляется.** Spotify часто присылает обложку чуть позже смены трека, а плеер раньше сразу сдавался. Теперь он несколько секунд пробует снова, пока обложка не придёт.
- **Нормальная заглушка.** Пока обложки нет (или у трека её нет), плеер показывает аккуратную векторную ноту вместо прежней самодельной.

**Улучшено**
- **Проверка обновлений ничего не стоит, пока не пора.** Меню проверяет новые версии при открытии (не чаще раза в пару часов), и теперь эта проверка до нужного времени не читает диск и не трогает сеть. Сам запрос идёт в отдельном потоке с самым низким приоритетом, стартует через пару секунд после появления меню и не мешает ни открытию меню, ни играм. Нет интернета — повтор откладывается на десять минут, а не на каждое открытие.

</details>
