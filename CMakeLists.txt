cmake_minimum_required(VERSION 3.21)
project(StartMenu VERSION 1.0.6 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

add_executable(StartMenu WIN32
    src/main.cpp
    src/Lang.h          src/Lang.cpp           # язык интерфейса
    i18n/i18n.qrc       # переводы (встраиваются в exe)
    res/res.qrc         # картинки интерфейса (галочка чекбокса)
    src/Theme.h
    src/AppIcon.h       src/AppIcon.cpp
    src/app.rc          # иконка и сведения о версии exe
    src/Config.h        src/Config.cpp
    src/Settings.h      src/Settings.cpp
    src/SettingsWindow.h src/SettingsWindow.cpp
    src/KeyHook.h       src/KeyHook.cpp
    src/Shell.h         src/Shell.cpp
    src/IconCache.h     src/IconCache.cpp
    src/Glyphs.h        src/Glyphs.cpp
    src/Workspace.h     src/Workspace.cpp
    src/AppList.h       src/AppList.cpp
    src/Sidebar.h       src/Sidebar.cpp
    src/SearchExtras.h  src/SearchExtras.cpp
    src/PinSync.h       src/PinSync.cpp
    src/Skin.h          src/Skin.cpp src/SkinStyles.cpp
    src/StartWindow.h   src/StartWindow.cpp
    src/MediaSession.h  src/MediaSession.cpp   # что играет в Windows (C++/WinRT)
    src/MediaPlayer.h   src/MediaPlayer.cpp
    src/PlayerWindow.h  src/PlayerWindow.cpp   # панель большого плеера над меню
    src/Welcome.h       src/Welcome.cpp        # мастер первого запуска
    src/ClipboardStore.h src/ClipboardStore.cpp # история буфера обмена (пишет трей)
    src/ClipboardView.h src/ClipboardView.cpp
    src/AudioControl.h  src/AudioControl.cpp   # громкость и устройство вывода
    src/SoundPopup.h    src/SoundPopup.cpp
    src/SystemStats.h   src/SystemStats.cpp    # ЦП / ОЗУ / ГП / диск
    src/Demo.h          src/Demo.cpp           # --render --demo: выдуманные данные для снимков
    src/Backups.h       src/Backups.cpp        # резервные копии меню
    src/Updater.h       src/Updater.cpp        # проверка обновлений на GitHub
    src/WhatsNew.h      src/WhatsNew.cpp       # карточка «Что нового» после обновления
)

target_link_libraries(StartMenu PRIVATE Qt6::Widgets)
if(WIN32)
    target_link_libraries(StartMenu PRIVATE user32 shell32 ole32 oleaut32 uuid advapi32 psapi PowrProf dwmapi shlwapi windowsapp pdh urlmon)
    target_compile_definitions(StartMenu PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX APP_VERSION="${PROJECT_VERSION}")
endif()
if(MSVC)
    # Исходники в UTF-8 без BOM: без /utf-8 кириллица в строках превратится в кракозябры.
    target_compile_options(StartMenu PRIVATE /utf-8 /W4 /permissive-)
endif()
