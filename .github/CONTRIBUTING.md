# Contributing to StartMenu

Thanks for wanting to help! StartMenu is a small project, so every bug report, idea and translation matters.
*Писать можно и по-русски — issues и pull request’ы на русском тоже принимаются.*

## 🐞 Reporting a bug

[Open a bug report](https://github.com/NKF-XXX/StartMenu/issues/new?template=bug_report.yml) and include:

- StartMenu version (*Settings → Apps → Installed apps*, or the installer file name) and your Windows build (`winver`);
- the style you use (Classic / Studio / Bento / Fluent) and the language;
- steps to reproduce, what you expected and what happened;
- a screenshot or a short video if it’s visual.

Please check [existing issues](https://github.com/NKF-XXX/StartMenu/issues) first.

## 💡 Suggesting an idea

[Open a feature request](https://github.com/NKF-XXX/StartMenu/issues/new?template=feature_request.yml). Describe the problem you want to solve, not only the solution — it helps to find the best one.

## 🌍 Translations

Strings live in [`i18n/`](../i18n): `keys.json` is the list of source strings (Russian), and `<lang>.json` holds translations in the same order.
To fix a translation, edit the line with the same index in your language file and open a pull request. New languages are welcome too — mention it in an issue first.

## 🛠 Code

1. Install **Visual Studio 2022** (C++ desktop workload) and **Qt 6.8.3 `msvc2022_64`** into `C:\Qt`.
2. Run `build.cmd` — it builds `build\StartMenu.exe` (Release) and deploys Qt next to it.
3. Read [docs/DEVELOPMENT.md](../docs/DEVELOPMENT.md): how the three processes (tray, menu, settings) work, command-line switches (`--render`, `--preview`, `--nohook`…) and where things are in `src/`.

Guidelines:

- Keep it light: memory and opening speed are core features. Measure with `STARTMENU_PERF=<file>` if you touch the menu window.
- Match the surrounding code style (C++20, Qt Widgets, 4 spaces). Comments in the code are in Russian — English is fine for new ones.
- User-visible strings go through `TXT("…")` so they can be translated.
- One topic per pull request; describe what changed and how you tested it. Screenshots for UI changes are very welcome (`StartMenu.exe --render shot.png <style 0..3>` renders the menu without showing it).

## 📜 License

By contributing, you agree that your contributions are licensed under the project’s [license](../LICENSE).
