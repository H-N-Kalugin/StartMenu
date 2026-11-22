<div align="center">

<img src="docs/images/banner.png" alt="StartMenu — your Windows 10 tiles, back in Windows 11" width="100%">

**English** · [Русский](README.ru.md)

[![Download](https://img.shields.io/badge/⬇_Download-StartMenu_for_Windows_11-4C8DFF?style=for-the-badge)](https://github.com/NKF-XXX/StartMenu/releases/latest)

[![Latest release](https://img.shields.io/github/v/release/NKF-XXX/StartMenu?color=8A6BFF&label=release)](https://github.com/NKF-XXX/StartMenu/releases/latest)
[![Downloads](https://img.shields.io/github/downloads/NKF-XXX/StartMenu/total?color=4C8DFF)](https://github.com/NKF-XXX/StartMenu/releases)
[![License](https://img.shields.io/github/license/NKF-XXX/StartMenu?color=22C55E)](LICENSE)
[![Windows 11](https://img.shields.io/badge/Windows-11-0078D4?logo=windows11&logoColor=white)](#-install-in-one-minute)
[![Qt 6.8](https://img.shields.io/badge/Qt-6.8-41CD52?logo=qt&logoColor=white)](https://www.qt.io/)

<img src="docs/images/hero.png" alt="StartMenu on Windows 11" width="100%">

**Groups · folders · 4 tile sizes · instant search**<br>
Doesn’t patch Windows · no admin rights · no ads · free forever

</div>

## 🎨 Four styles

<table>
  <tr>
    <td width="50%"><img src="docs/images/style-classic.png" alt="Classic"><p align="center"><b>Classic</b></p></td>
    <td width="50%"><img src="docs/images/style-studio.png" alt="Studio"><p align="center"><b>Studio</b></p></td>
  </tr>
  <tr>
    <td width="50%"><img src="docs/images/style-bento.png" alt="Bento"><p align="center"><b>Bento</b></p></td>
    <td width="50%"><img src="docs/images/style-fluent.png" alt="Fluent"><p align="center"><b>Fluent</b></p></td>
  </tr>
</table>

## 🧩 Make it yours

Labels or clean icons, glass or solid — colors, corners and size are yours too.

<img src="docs/images/variants.png" alt="With and without tile labels, over wallpaper and on a solid background" width="100%">

## 🎵 Music player

Whatever plays in Windows — a separate panel above the menu with seeking and a beat glow.

<img src="docs/images/player.png" alt="Large music player above the menu" width="100%">

<table>
  <tr>
    <td width="50%"><img src="docs/images/clipboard.png" alt="Clipboard history"><p align="center"><b>📎 Clipboard history</b></p></td>
    <td width="50%"><img src="docs/images/settings.png" alt="Settings"><p align="center"><b>⚙️ Every detail in settings</b></p></td>
  </tr>
</table>

<p align="center">
🔎 Search apps, settings & files &nbsp;·&nbsp; 🧮 Calculator &nbsp;·&nbsp; ⌨️ Fixes the wrong keyboard layout &nbsp;·&nbsp; 📑 Layers<br>
🔊 Volume & output &nbsp;·&nbsp; 📊 System monitor &nbsp;·&nbsp; 🌐 Website links &nbsp;·&nbsp; 📌 Pin from Explorer &nbsp;·&nbsp; 🎮 Works over games
</p>

## 📦 Install in one minute

<img src="docs/images/install.png" alt="Download, allow once in SmartScreen, pick a style" width="100%">

The first-run wizard does the rest:

<table>
  <tr>
    <td width="33%"><img src="docs/images/wizard-1.png" alt="Step 1: language and style"></td>
    <td width="33%"><img src="docs/images/wizard-2.png" alt="Step 2: your apps and Windows pins"></td>
    <td width="33%"><img src="docs/images/wizard-3.png" alt="Step 3: tips"></td>
  </tr>
</table>

**Requirements:** Windows 11, 64-bit.

## ❓ FAQ

<details>
<summary><b>How do I open the original Start menu?</b></summary>

`Ctrl+Esc`. All `Win+…` shortcuts keep working. To pause StartMenu: tray icon → *Exit*.
</details>

<details>
<summary><b>How do I uninstall it?</b></summary>

*Settings → Apps → Installed apps → StartMenu → Uninstall.* The Windows Start menu is back immediately.
</details>

<details>
<summary><b>How much memory does it use?</b></summary>

~6 MB in the tray. With *Keep the menu ready* — ~12–25 MB, and it opens instantly.
</details>

<details>
<summary><b>Why does SmartScreen warn me? Is it safe?</b></summary>

The installer isn’t code-signed yet. StartMenu needs no admin rights, installs into your user folder (`%LOCALAPPDATA%\Programs\…`) and doesn’t touch system files. The code is open.
</details>

<details>
<summary><b>Does it send any data?</b></summary>

No telemetry, no accounts. The only request: the icon of a website link you add.
</details>

<details>
<summary><b>How do I move my layout to another PC?</b></summary>

Tray icon → *Open settings folder* → copy `items.json` (tiles) and `settings.json` (look).
</details>

## 🌍 12 languages

English · Русский · Українська · Deutsch · Français · Español · Italiano · Português · Polski · Türkçe · 中文 · 日本語

## 🛠 Build from source

Visual Studio 2022 (C++) + Qt 6.8.3 `msvc2022_64` in `C:\Qt`:

```bat
build.cmd
pwsh -File installer\make-installer.ps1
```

More: [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) · [Contributing](.github/CONTRIBUTING.md) · [Report a bug](https://github.com/NKF-XXX/StartMenu/issues/new/choose)

## 📄 License

[GPL-3.0](LICENSE) — free to use, study, share and modify; modified versions stay open.

If StartMenu makes your day a little better — **⭐ star the repo**.

<sub>Not affiliated with Microsoft. Windows is a trademark of Microsoft Corporation.</sub>
