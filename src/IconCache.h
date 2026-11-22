#pragma once
#include <QHash>
#include <QImage>
#include <QList>
#include <QObject>
#include <QPixmap>
#include <QSet>
#include <QTimer>

#include "Config.h"

// Иконки в хорошем качестве и без лишней памяти:
//  • у Windows берём иконку ОДИН раз в высоком разрешении («исходник»: вдвое больше нужного, 64/128/256 px) —
//    у маленьких размеров (16–32 px) Windows отдаёт упрощённые, грубые варианты иконок;
//  • нужный размер получаем ступенчатым уменьшением исходника — чётко, без «мыла»;
//  • при изменении окна новые размеры считаются из исходника в памяти, Windows не трогаем;
//  • исходники лежат и на диске (%LOCALAPPDATA%/StartMenu/cache/icons), чтобы следующее открытие было мгновенным.
class IconCache : public QObject
{
    Q_OBJECT
public:
    explicit IconCache(QObject* parent = nullptr);

    QPixmap get(const Item& item, int logicalSize, qreal dpr);
    QColor dominantColor(const Item& item);      // основной насыщенный цвет иконки (для стиля Bento)
    // Фон плитки из иконки: plate — цвет «подложки» иконки (медиана по её внешнему кольцу:
    // у Figma — тёмный, у Asana — белый, у Telegram — синий), soft — иконка 4×4 для мягкого «размытого» фона.
    struct Backdrop
    {
        QColor plate;
        QImage soft;
        QColor softBottom;        // средний цвет нижней трети soft
    };
    Backdrop backdrop(const Item& item);
    static Backdrop makeBackdrop(const QImage& base); // то же по готовой картинке (для плеера: значок источника звука)
    static QPixmap letterIcon(const QString& name, int px);
    static QString diskCacheDir();
    static void pruneExact();            // убрать старые готовые размеры (в фоне)
    static void prefetch(const QStringList& sources); // заранее загрузить иконки у Windows в кэш на диске (в фоне)
    static inline bool s_persistExact = true; // false — пока окно тянут мышью: размеры не пишем на диск
    static QString faviconFile(const QString& url); // где лежит скачанная иконка сайта
    static void clearDiskCache();
    // Меню спрятали: большие исходники (до 256 px) больше не нужны — готовые размеры остаются,
    // а исходник при смене размера снова прочитается с диска. Меню «наготове» не копит память неделями.
    void releaseSources() { m_sources.clear(); m_sources.squeeze(); }

signals:
    void changed();

private:
    static QString diskFile(const QString& path, int px, bool exact = false);
    static int sourceSizeFor(int px);
    static QImage downscale(QImage img, int px);
    QPixmap fromSource(const QString& source, int px, qreal dpr);

    struct Job
    {
        QString source;
        QString name;
        int size;              // размер исходника
    };
    void work();

    QHash<QString, QImage> m_sources;   // путь → исходник в высоком разрешении
    QHash<QString, QPixmap> m_done;     // путь|px → готовая картинка точного размера
    QHash<QString, QColor> m_colors;
    QHash<QString, Backdrop> m_backdrops;
    QImage anyImage(const Item& item); // исходник или самый крупный готовый размер
    QSet<QString> m_queued;
    QList<Job> m_jobs;
    QTimer m_timer;
    void fetchFavicon(const QString& url);  // скачать иконку сайта в фоне
    QSet<QString> m_favPending;
};
