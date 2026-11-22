@echo off
chcp 65001 >nul
rem Сборка Release + windeployqt. Запуск: build.cmd
setlocal
set QTDIR=C:\Qt\6.8.3\msvc2022_64
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
cd /d "%~dp0"
if not exist build\build.ninja (
    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=%QTDIR% || exit /b 1
)
cmake --build build || exit /b 1
"%QTDIR%\bin\windeployqt.exe" --release --no-translations --no-system-d3d-compiler --no-system-dxc-compiler --no-opengl-sw --no-compiler-runtime --skip-plugin-types iconengines,generic,networkinformation,tls,styles build\StartMenu.exe >nul || exit /b 1
rem Форматы картинок: JPEG/WebP/GIF — для своих картинок на плитках и обложек в плеере, ICO — для иконок сайтов.
rem Остальные (SVG, TIFF, ...) не нужны.
for %%f in (build\imageformats\*.dll) do (
    if /i not "%%~nf"=="qjpeg" if /i not "%%~nf"=="qwebp" if /i not "%%~nf"=="qgif" if /i not "%%~nf"=="qico" del /q "%%f"
)
if exist build\Qt6Svg.dll del /q build\Qt6Svg.dll
rem Лишнее от прошлых деплоев: стиль не нужен (встроенный Fusion), DXC не используется.
if exist build\styles rmdir /s /q build\styles
if exist build\dxcompiler.dll del /q build\dxcompiler.dll
if exist build\dxil.dll del /q build\dxil.dll
echo BUILD OK
