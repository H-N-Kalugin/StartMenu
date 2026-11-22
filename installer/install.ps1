# Установка StartMenu (запускается установщиком). Без прав администратора: всё ставится для текущего пользователя.
param(
    [string]$Target = "$env:LOCALAPPDATA\Programs\StartMenu",
    [switch]$Quiet,       # без сообщений
    [switch]$NoRegistry,  # без ярлыков, автозапуска и записи в «Установленных приложениях» (для проверки)
    [switch]$NoLaunch     # не запускать после установки
)
$ErrorActionPreference = 'Stop'
# Автообновление запускает установщик молча: сообщения не нужны, программа сама перезапустится
if ($env:STARTMENU_QUIET -eq '1') { $Quiet = $true }
Add-Type -AssemblyName System.Windows.Forms
function Say([string]$text, [string]$icon = 'Information') {
    if (-not $Quiet) { [Windows.Forms.MessageBox]::Show($text, 'StartMenu', 'OK', $icon) | Out-Null }
}
# Сообщения — по-русски для русской Windows, иначе по-английски (сама программа выберет язык сама)
$ru = (Get-UICulture).TwoLetterISOLanguageName -in 'ru', 'be', 'kk'
function T([string]$r, [string]$e) { if ($ru) { $r } else { $e } }
try {
    $here = Split-Path -Parent $MyInvocation.MyCommand.Path
    $version = (Get-Content (Join-Path $here 'version.txt') -Raw).Trim()
    $firstRun = -not (Test-Path "$env:APPDATA\StartMenu\settings.json")

    # Если StartMenu уже запущен (обновление) — закрываем, иначе файлы заняты.
    Get-Process StartMenu -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Sleep -Milliseconds 800

    New-Item -ItemType Directory -Force $Target | Out-Null
    Expand-Archive -Path (Join-Path $here 'payload.zip') -DestinationPath $Target -Force
    $exe = Join-Path $Target 'StartMenu.exe'

    if (-not $NoRegistry) {
        # Ярлыки в меню «Пуск» Windows (по ним и поиск Windows находит программу)
        $programs = [Environment]::GetFolderPath('Programs')
        $sh = New-Object -ComObject WScript.Shell
        $l = $sh.CreateShortcut((Join-Path $programs 'StartMenu.lnk'))
        $l.TargetPath = $exe; $l.WorkingDirectory = $Target; $l.IconLocation = "$exe,0"
        $l.Description = (T 'Своё меню «Пуск»' 'Custom Start menu'); $l.Save()
        $l = $sh.CreateShortcut((Join-Path $programs ((T 'Настройки StartMenu' 'StartMenu Settings') + '.lnk')))
        $l.TargetPath = $exe; $l.Arguments = '--settings'; $l.WorkingDirectory = $Target
        $l.IconLocation = "$exe,0"; $l.Description = (T 'Настройки меню «Пуск»' 'Start menu settings'); $l.Save()

        # Автозапуск (то же имя, что использует сама программа — переключатель в трее продолжит работать)
        Set-ItemProperty 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run' -Name 'StartMenu' -Value "`"$exe`""

        # Запись в «Установленных приложениях» — чтобы удалить штатно
        $u = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\StartMenu.App'
        New-Item $u -Force | Out-Null
        $sizeKb = [int]((Get-ChildItem $Target -Recurse -File | Measure-Object Length -Sum).Sum / 1KB)
        Set-ItemProperty $u -Name DisplayName -Value 'StartMenu'
        Set-ItemProperty $u -Name DisplayVersion -Value $version
        Set-ItemProperty $u -Name Publisher -Value 'StartMenu'
        Set-ItemProperty $u -Name DisplayIcon -Value "$exe,0"
        Set-ItemProperty $u -Name InstallLocation -Value $Target
        Set-ItemProperty $u -Name UninstallString -Value "powershell.exe -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File `"$Target\uninstall.ps1`""
        Set-ItemProperty $u -Name NoModify -Value 1 -Type DWord
        Set-ItemProperty $u -Name NoRepair -Value 1 -Type DWord
        Set-ItemProperty $u -Name EstimatedSize -Value $sizeKb -Type DWord
    }

    if (-not $NoLaunch) { Start-Process $exe -WorkingDirectory $Target }
    # При первой установке программа сама откроет мастер настройки — лишнее окно не показываем
    if (-not $firstRun) {
        Say (T "StartMenu $version установлен.`n`nМеню открывается клавишей Win или кнопкой «Пуск».`nНастройки — правой кнопкой по значку в трее.`n`nПапка программы:`n$Target" `
               "StartMenu $version is installed.`n`nOpen the menu with the Win key or the Start button.`nSettings — right-click the tray icon.`n`nProgram folder:`n$Target")
    }
}
catch {
    Say ((T "Не удалось установить StartMenu:`n`n" "Could not install StartMenu:`n`n") + $_.Exception.Message) 'Error'
    exit 1
}