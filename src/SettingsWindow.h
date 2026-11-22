#pragma once
#include <QList>
#include <QWidget>
#include <functional>

#include "Settings.h"

class QCheckBox;
class QComboBox;
class QPushButton;
class QSlider;
class QSpinBox;
class QTimer;

// Окно настроек (StartMenu.exe --settings). Любое изменение сразу сохраняется и через ~0,1 с
// применяется к открытому меню и процессу трея. «Предпросмотр» открывает меню, которое не закрывается само.
class SettingsWindow : public QWidget
{
    Q_OBJECT
public:
signals:
    void languageChanged(); // окно нужно пересоздать на новом языке

public:
    SettingsWindow();
    void reloadFromDisk();     // меню поменяло settings.json (например, размер окна мышью)

protected:
    void closeEvent(QCloseEvent* e) override;

private:
    QWidget* buildLookTab();
    QWidget* buildTilesTab();
    QWidget* buildBehaviorTab();
    QPushButton* colorButton(QColor Settings::* field);
    QSlider* slider(int Settings::* field, int min, int max, const QString& suffix, QWidget** valueLabel = nullptr);
    QSpinBox* spin(int Settings::* field, int min, int max, const QString& suffix);
    QCheckBox* check(const QString& text, bool Settings::* field);

    void changed();            // что-то поменяли — сохранить и разослать (с задержкой)
    void notify();
    void loadIntoUi();         // после пресета/сброса — обновить все контролы
    void applyPreset(int index);
    void openPreview();

    QList<std::function<void()>> m_refreshers;

    QList<std::function<void()>> m_enablers; // включить/выключить зависимые элементы // как обновить каждый контрол из Settings
    QTimer* m_debounce = nullptr;
    bool m_loading = false;
    bool m_previewStarted = false;
    QSlider* m_blurSlider = nullptr;
    QComboBox* m_effect = nullptr;
    QComboBox* m_presetBox = nullptr;
    bool m_inPreset = false;   // правки идут от пресета — не сбрасывать его в «Свои настройки»
};
