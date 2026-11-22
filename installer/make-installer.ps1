# Сборка установщика: dist\StartMenu-<версия>.exe
# Запуск (после build.cmd):  pwsh -File installer\make-installer.ps1
# Установщик собирается встроенным в Windows IExpress — ничего ставить не нужно.
param([string]$Version = '1.0.0')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root 'build'
$dist = Join-Path $root 'dist'
$stage = Join-Path $dist 'stage'     # что окажется в папке программы
$pkg = Join-Path $dist 'pkg'         # что упаковывается в установщик
Remove-Item $stage, $pkg -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $stage, $pkg, (Join-Path $stage 'platforms'), (Join-Path $stage 'tools') | Out-Null

# Подпись кода — убирает предупреждение SmartScreen «Windows защитила компьютер».
# Включается сама, если задан сертификат разработчика (его выдаёт центр сертификации после проверки личности):
#   $env:STARTMENU_SIGN_PFX = 'C:\путь\cert.pfx'; $env:STARTMENU_SIGN_PASSWORD = '…'      — файл сертификата
#   или $env:STARTMENU_SIGN_THUMBPRINT = '<отпечаток>'                                       — сертификат в хранилище Windows
# Без сертификата установщик собирается как раньше, неподписанным.
function Sign-File([string]$file) {
    if (-not ($env:STARTMENU_SIGN_PFX -or $env:STARTMENU_SIGN_THUMBPRINT)) { return }
    $signtool = Get-ChildItem 'C:\Program Files (x86)\Windows Kits\10\bin\*\x64\signtool.exe' -ErrorAction SilentlyContinue |
                Sort-Object FullName -Descending | Select-Object -First 1 -ExpandProperty FullName
    if (-not $signtool) { throw 'Для подписи нужен signtool (Windows SDK)' }
    $signArgs = @('sign', '/fd', 'SHA256', '/tr', 'http://timestamp.digicert.com', '/td', 'SHA256', '/d', 'StartMenu')
    if ($env:STARTMENU_SIGN_PFX) { $signArgs += @('/f', $env:STARTMENU_SIGN_PFX); if ($env:STARTMENU_SIGN_PASSWORD) { $signArgs += @('/p', $env:STARTMENU_SIGN_PASSWORD) } }
    else { $signArgs += @('/sha1', $env:STARTMENU_SIGN_THUMBPRINT) }
    & $signtool @signArgs $file
    if ($LASTEXITCODE -ne 0) { throw "Подпись не удалась: $file" }
    "подписано: $file"
}

# 1. Программа и Qt (только то, что реально нужно)
foreach ($f in 'StartMenu.exe', 'Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll') { Copy-Item (Join-Path $build $f) $stage }
Sign-File (Join-Path $stage 'StartMenu.exe')
Copy-Item (Join-Path $build 'platforms\qwindows.dll') (Join-Path $stage 'platforms')
# Форматы картинок для своих картинок на плитках (JPG/WebP/GIF), обложек в плеере и иконок сайтов (ICO)
New-Item -ItemType Directory -Force (Join-Path $stage 'imageformats') | Out-Null
Copy-Item (Join-Path $build 'imageformats\*.dll') (Join-Path $stage 'imageformats')

# 2. Библиотеки C++ (Visual C++ 2015–2022) — рядом с программой, чтобы не требовать установки vc_redist
$redistRoot = 'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Redist\MSVC'
$crt = Get-ChildItem $redistRoot -Directory | Where-Object Name -match '^\d' | Sort-Object { [version]$_.Name } -Descending |
       ForEach-Object { Join-Path $_.FullName 'x64\Microsoft.VC143.CRT' } | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $crt) { throw 'Не найдены библиотеки Visual C++ (Microsoft.VC143.CRT)' }
foreach ($f in 'msvcp140.dll', 'msvcp140_1.dll', 'msvcp140_2.dll', 'vcruntime140.dll', 'vcruntime140_1.dll') { Copy-Item (Join-Path $crt $f) $stage }

# 3. Удаление и инструменты
Copy-Item (Join-Path $PSScriptRoot 'uninstall.ps1') $stage
Copy-Item (Join-Path $root 'tools\import-start11.ps1') (Join-Path $stage 'tools')

# 4. Упаковка: IExpress не умеет вложенные папки, поэтому программа едет одним zip-архивом
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath (Join-Path $pkg 'payload.zip') -CompressionLevel Optimal
Copy-Item (Join-Path $PSScriptRoot 'install.ps1'), (Join-Path $PSScriptRoot 'install.cmd') $pkg
Set-Content (Join-Path $pkg 'version.txt') $Version -Encoding ascii

# 5. IExpress. Имя файла без слов setup/install — иначе Windows сама запросит права администратора,
#    а установка идёт для текущего пользователя и они не нужны.
$target = Join-Path $dist "StartMenu-$Version.exe"
Remove-Item $target -ErrorAction SilentlyContinue
$sed = @"
[Version]
Class=IEXPRESS
SEDVersion=3
[Options]
PackagePurpose=InstallApp
ShowInstallProgramWindow=1
HideExtractAnimation=1
UseLongFileName=1
InsideCompressed=0
CAB_FixedSize=0
CAB_ResvCodeSigning=0
RebootMode=N
InstallPrompt=%InstallPrompt%
DisplayLicense=%DisplayLicense%
FinishMessage=%FinishMessage%
TargetName=%TargetName%
FriendlyName=%FriendlyName%
AppLaunched=%AppLaunched%
PostInstallCmd=%PostInstallCmd%
AdminQuietInstCmd=%AdminQuietInstCmd%
UserQuietInstCmd=%UserQuietInstCmd%
SourceFiles=SourceFiles
[Strings]
InstallPrompt=
DisplayLicense=
FinishMessage=
TargetName=$target
FriendlyName=StartMenu $Version
AppLaunched=cmd /c install.cmd
PostInstallCmd=<None>
AdminQuietInstCmd=
UserQuietInstCmd=
FILE0="install.cmd"
FILE1="install.ps1"
FILE2="payload.zip"
FILE3="version.txt"
[SourceFiles]
SourceFiles0=$pkg\
[SourceFiles0]
%FILE0%=
%FILE1%=
%FILE2%=
%FILE3%=
"@
$sedPath = Join-Path $pkg 'StartMenu.sed'
# IExpress понимает только переводы строк Windows (CRLF)
[IO.File]::WriteAllText($sedPath, ($sed -replace "`r?`n", "`r`n"), [Text.Encoding]::ASCII)
$p = Start-Process "$env:WINDIR\System32\iexpress.exe" -ArgumentList '/N', '/Q', $sedPath -Wait -PassThru
if (-not (Test-Path $target)) { throw "IExpress не собрал установщик (код $($p.ExitCode))" }
Sign-File $target
'{0}  ({1:N1} МБ)' -f $target, ((Get-Item $target).Length / 1MB)
