# StartMenu 1.0.8

A full power menu and important reliability fixes. Update from inside the app: click the pulsing dot next to the version number in the bottom-right corner of the menu → **Update**.

## ⏻ Power menu

- **All the usual actions are back:** Lock, Sign out, Sleep, Hibernate (if enabled in Windows), Restart and Shut down — grouped like in Windows.
- **Show only what you need.** Each item has its own switch in **Settings → Behavior → Power button menu**. All are on by default.

## 🛡️ Reliability and safety

- **Verified updates.** Before installing, the downloaded installer is checked against the SHA-256 checksum GitHub publishes for the release file. Only installers from this repository's releases are accepted, and they are saved to StartMenu's own folder instead of the shared temp folder.
- **Your tiles can't be wiped by a busy file.** If an antivirus or sync tool briefly locked `items.json` or `settings.json`, StartMenu could load an empty layout and save it over your tiles. Now it retries, falls back to the backup copy, and never saves over a file it couldn't read.
- **Calculator crash fixed.** Pasting a very long string of minus signs or brackets into search no longer crashes the menu.
- **Pin from Explorer with any file name.** Files with characters outside your system code page (e.g. 日本, é) now pin correctly.
- **Safer drawing** right after background pin sync.

<details>
<summary><b>🇷🇺 По-русски</b></summary>

Полное меню кнопки питания и важные исправления надёжности. Обновиться можно прямо из программы: нажмите пульсирующий кружок рядом с номером версии в правом нижнем углу меню → **Обновить**.

**Меню кнопки питания**
- **Вернулись все привычные действия:** Блокировка, Выход из системы, Спящий режим, Гибернация (если включена в Windows), Перезагрузка и Завершение работы — по группам, как в Windows.
- **Только то, что нужно.** У каждого пункта свой переключатель: **Настройки → Поведение → Меню кнопки питания**. По умолчанию включены все.

**Надёжность и безопасность**
- **Проверенные обновления.** Перед установкой скачанный файл сверяется с контрольной суммой SHA-256, которую GitHub публикует для файла выпуска. Принимаются только установщики из выпусков этого репозитория, и сохраняются они в собственную папку StartMenu, а не в общую временную.
- **Плитки не затрутся из-за занятого файла.** Если антивирус или синхронизация на миг занимали `items.json` или `settings.json`, StartMenu мог загрузить пустую раскладку и сохранить её поверх плиток. Теперь он пробует ещё раз, берёт запасную копию и никогда не сохраняет поверх файла, который не смог прочитать.
- **Калькулятор больше не роняет меню**, если вставить в поиск очень длинную строку из минусов или скобок.
- **«Закрепить» из Проводника — с любым именем файла.** Файлы с символами вне системной кодировки (например, 日本, é) теперь закрепляются.
- **Надёжнее отрисовка** сразу после фоновой синхронизации закреплённого.

</details>
