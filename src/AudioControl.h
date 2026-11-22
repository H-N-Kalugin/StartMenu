#pragma once
#include <QList>
#include <QString>

// Громкость и устройство вывода звука (то же, что в значке динамика Windows).
namespace Audio
{
    struct Device
    {
        QString id, name;
        bool isDefault = false;
    };

    QList<Device> outputs();          // включённые устройства вывода
    bool setDefault(const QString& id); // сделать устройством по умолчанию (для всех ролей)
    float volume();                   // 0…1, у устройства по умолчанию
    void setVolume(float v);
    bool muted();
    void setMuted(bool on);
}
