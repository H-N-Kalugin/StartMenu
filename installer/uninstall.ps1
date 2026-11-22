# Удаление StartMenu («Установленные приложения» → Удалить или запуск этого файла).
param([switch]$Quiet)
Add-Type -AssemblyName System.Windows.Forms
function Ask([string]$text) {
    if ($Quiet) { return $true }
    return [Windows.Forms.MessageBox]::Show($text, 'StartMenu', 'YesNo', 'Question') -eq 'Yes'
}
$ru = (Get-UICulture).TwoLetterISOLanguageName -in 'ru', 'be', 'kk'
function T([string]$r, [string]$e) { if ($ru) { $r } else { $e } }
$dir = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not (Ask (T 'Удалить StartMenu с этого компьютера?' 'Remove StartMenu from this computer?'))) { exit 0 }

Get-Process StartMenu -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 800

Remove-ItemProperty 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run' -Name 'StartMenu' -ErrorAction SilentlyContinue
foreach ($k in 'HKCU:\Software\Classes\*\shell\StartMenu.App.Pin', 'HKCU:\Software\Classes\Directory\shell\StartMenu.App.Pin',
               'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\StartMenu.App') {
    if (Test-Path -LiteralPath $k) { Remove-Item -LiteralPath $k -Recurse -Force }
}
$programs = [Environment]::GetFolderPath('Programs')
Remove-Item -LiteralPath (Join-Path $programs 'StartMenu.lnk'), (Join-Path $programs 'Настройки StartMenu.lnk'), (Join-Path $programs 'StartMenu Settings.lnk') -ErrorAction SilentlyContinue

if (Ask (T "Удалить и ваши плитки, группы и настройки?`n(Нет — они останутся и подхватятся при повторной установке.)" "Also remove your tiles, groups and settings?`n(No — they stay and will be picked up on reinstall.)")) {
    Remove-Item -LiteralPath "$env:APPDATA\StartMenu", "$env:LOCALAPPDATA\StartMenu" -Recurse -Force -ErrorAction SilentlyContinue
}

# Папку программы удаляем, когда этот скрипт завершится (он сам в ней лежит).
# Защита: только если это действительно папка StartMenu.
if (Test-Path -LiteralPath (Join-Path $dir 'StartMenu.exe')) {
    Start-Process cmd.exe -ArgumentList "/c timeout /t 2 /nobreak >nul & rmdir /s /q `"$dir`"" -WindowStyle Hidden
}
if (-not $Quiet) { [Windows.Forms.MessageBox]::Show((T 'StartMenu удалён.' 'StartMenu has been removed.'), 'StartMenu') | Out-Null }