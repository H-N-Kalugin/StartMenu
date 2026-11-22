#pragma once
#include <QByteArray>
#include <QDateTime>
#include <QSize>
#include <QList>
#include <QString>
#include <QStringList>

// История буфера обмена. Записывает трей (он работает всегда), показывает и вставляет меню.
// Файлы: %LOCALAPPDATA%/StartMenu/clipboard/history.json и картинки рядом.
// Пароли не сохраняются: менеджеры паролей помечают такое копирование особым форматом — его пропускаем.
namespace ClipboardStore
{
    enum class Type { Text, Image, Files };

    struct Entry
    {
        Type type = Type::Text;
        QString text;          // текст (Text) или подпись
        QString image;         // имя файла картинки (Image)
        QStringList files;     // пути (Files)
        QSize size;            // размер картинки
        qint64 time = 0;       // мс с 1970
        bool pinned = false;   // закреплённое не вытесняется новыми
    };

    QString dir();
    QList<Entry> load();
    void save(const QList<Entry>& entries);

    // Трей: быстро забрать содержимое буфера (readRaw), а разобрать и сохранить — в фоновом потоке (process).
    struct Raw
    {
        bool valid = false;
        Entry entry;
        QByteArray bmp;   // картинка как файл BMP — разбирается уже в фоне
    };
    Raw readRaw();
    bool process(Raw raw); // true — запись добавлена или поднята наверх
    // Меню: положить запись обратно в буфер обмена (данные остаются и после выхода из процесса).
    bool putToClipboard(const Entry& e);

    void remove(const Entry& e);
    void togglePin(const Entry& e);
    void clearUnpinned();

    // Точка-индикатор «есть новое»: новее ли последняя запись, чем момент последнего просмотра.
    bool hasUnseen();
    void markSeen();

    QString describeAge(qint64 time); // «сейчас», «5 мин», «2 ч», «вчера», «12.11»
}
