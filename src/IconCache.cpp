#include "IconCache.h"
#include "Config.h"
#include "Demo.h"
#include "Shell.h"
#include "Theme.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDateTime>
#include <QImageReader>
#include <windows.h>
#include <urlmon.h>
#include <thread>
#include <QUrl>
#include <QRegularExpression>
#include <QPointer>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QPainter>
#include <QStandardPaths>
#include <algorithm>

IconCache::IconCache(QObject* parent)
    : QObject(parent)
{
    m_timer.setInterval(0);
    connect(&m_timer, &QTimer::timeout, this, &IconCache::work);
}

int IconCache::sourceSizeFor(int px)
{
    // Исходник вдвое больше нужного (так уменьшение получается чётким), но в пределах 64…256.
    if (px * 2 <= 64)
        return 64;
    if (px * 2 <= 128)
        return 128;
    return 256;
}

QImage IconCache::downscale(QImage img, int px)
{
    // Уменьшаем ступенями по 2× — у одного большого шага сглаживание «теряет» детали и даёт рябь.
    while (img.width() >= px * 2)
        img = img.scaled(img.width() / 2, img.height() / 2, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    if (img.width() != px)
        img = img.scaled(px, px, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return img;
}

QPixmap IconCache::fromSource(const QString& source, int px, qreal dpr)
{
    const QImage img = downscale(m_sources.value(source), px);
    // Готовый размер — на диск: при следующем открытии меню он читается сразу, без большого исходника.
    // Пока окно тянут мышью, размеры меняются каждый кадр — их не пишем.
    if (s_persistExact)
    {
        QDir().mkpath(diskCacheDir() + QStringLiteral("/exact"));
        img.save(diskFile(source, px, true), "PNG");
    }
    QPixmap pm = QPixmap::fromImage(img);
    pm.setDevicePixelRatio(dpr);
    m_done.insert(source + QLatin1Char('|') + QString::number(px), pm);
    return pm;
}

QPixmap IconCache::get(const Item& item, int logicalSize, qreal dpr)
{
    const int px = qMax(1, qRound(logicalSize * dpr)); // точный размер — рисуется 1:1, без масштабирования
    const QString& source = item.icon.isEmpty() ? item.path : item.icon;
    const QString key = source + QLatin1Char('|') + QString::number(px);
    const auto it = m_done.constFind(key);
    if (it != m_done.constEnd())
        return *it;

    // Демо-режим для снимков: значки-заглушки, ни диска, ни Windows
    if (const QImage demo = Demo::icon(source, px); !demo.isNull())
    {
        QPixmap pm = QPixmap::fromImage(demo);
        pm.setDevicePixelRatio(dpr);
        m_done.insert(key, pm);
        return pm;
    }

    // Служебные строки поиска (калькулятор) — своя иконка, Windows не спрашиваем.
    if (source.startsWith(QStringLiteral("calc:")) || source.isEmpty())
    {
        QPixmap pm = letterIcon(source.isEmpty() ? item.name : QStringLiteral("="), px);
        pm.setDevicePixelRatio(dpr);
        m_done.insert(key, pm);
        return pm;
    }

    const int need = sourceSizeFor(px);
    const QString jobKey = source + QLatin1Char('#') + QString::number(need);
    const auto src = m_sources.constFind(source);
    // Иконка уже в очереди на загрузку у Windows — значит, на диске её нет: не ищем файлы заново на каждой
    // перерисовке (пока грузятся десятки иконок, это сотни лишних чтений диска).
    if (m_queued.contains(jobKey))
    {
        if (src == m_sources.constEnd())
            return {};
        QPixmap pm = QPixmap::fromImage(downscale(*src, px));
        pm.setDevicePixelRatio(dpr);
        return pm;
    }

    // Самый быстрый путь: на диске уже лежит иконка ровно нужного размера.
    {
        QPixmap exact;
        if (exact.load(diskFile(source, px, true), "PNG"))
        {
            exact.setDevicePixelRatio(dpr);
            m_done.insert(key, exact);
            return exact;
        }
    }

    // Исходник есть и достаточно большой — просто уменьшаем (доли миллисекунды, Windows не нужен).
    if (src != m_sources.constEnd() && (src->width() >= need || src->width() >= 256))
        return fromSource(source, px, dpr);

    // Исходник на диске?
    QImage disk;
    if (disk.load(diskFile(source, need), "PNG"))
    {
        m_sources.insert(source, disk);
        return fromSource(source, px, dpr);
    }

    // Грузим у Windows в фоне; пока — то, что есть (меньший исходник), чтобы плитка не была пустой.
    m_queued.insert(jobKey);
    m_jobs.append({source, item.name, need});
    if (!m_timer.isActive())
        m_timer.start();
    if (src != m_sources.constEnd())
    {
        QPixmap pm = QPixmap::fromImage(downscale(*src, px));
        pm.setDevicePixelRatio(dpr);
        return pm; // временная, в кэш не кладём — придёт чёткая
    }
    return {};
}

void IconCache::work()
{
    QElapsedTimer t;
    t.start();
    while (!m_jobs.isEmpty() && t.elapsed() < 10)
    {
        const Job job = m_jobs.takeFirst();
        m_queued.remove(job.source + QLatin1Char('#') + QString::number(job.size));
        if (Config::isWebLink(job.source))
        {
            // Ссылка на сайт: иконка самого сайта (скачивается в фоне один раз), пока — буква
            QImage fav;
            const QFileInfo none(faviconFile(job.source) + QStringLiteral(".none"));
            if (fav.load(faviconFile(job.source), "PNG"))
                m_sources.insert(job.source, fav);
            else
            {
                // Иконки у сайта нет — пробуем снова не раньше чем через неделю
                if (!none.exists() || none.lastModified().daysTo(QDateTime::currentDateTime()) >= 7)
                    fetchFavicon(job.source);
                m_sources.insert(job.source, letterIcon(job.name, job.size).toImage());
            }
            const QString prefix = job.source + QLatin1Char('|');
            for (auto i = m_done.begin(); i != m_done.end();)
                i = i.key().startsWith(prefix) ? m_done.erase(i) : std::next(i);
            continue;
        }
        QImage img = WinShell::loadIcon(job.source, job.size).toImage();
        if (!img.isNull())
        {
            QDir().mkpath(diskCacheDir());
            img.save(diskFile(job.source, job.size), "PNG");
        }
        else
            img = letterIcon(job.name, job.size).toImage(); // запасную иконку на диск не пишем
        m_sources.insert(job.source, img);
        // Готовые размеры этой иконки пересчитаем из нового исходника при следующей отрисовке.
        for (auto i = m_done.constBegin(); i != m_done.constEnd(); ++i)
            if (i.key().startsWith(job.source + QLatin1Char('|')))
                QFile::remove(diskFile(job.source, i.key().section(QLatin1Char('|'), -1).toInt(), true));
        const QString prefix = job.source + QLatin1Char('|');
        for (auto i = m_done.begin(); i != m_done.end();)
            i = i.key().startsWith(prefix) ? m_done.erase(i) : std::next(i);
    }
    if (m_jobs.isEmpty())
        m_timer.stop();
    emit changed();
}

QImage IconCache::anyImage(const Item& item)
{
    const QString& source = item.icon.isEmpty() ? item.path : item.icon;
    if (const auto src = m_sources.constFind(source); src != m_sources.constEnd())
        return *src;
    get(item, 48, 1.0); // загрузит иконку (из кэша на диске — сразу)
    QImage best;
    const QString prefix = source + QLatin1Char('|');
    for (auto i = m_done.constBegin(); i != m_done.constEnd(); ++i)
        if (i.key().startsWith(prefix) && (best.isNull() || i.value().width() > best.width()))
            best = i.value().toImage();
    return best;
}

IconCache::Backdrop IconCache::backdrop(const Item& item)
{
    const QString& source = item.icon.isEmpty() ? item.path : item.icon;
    const auto it = m_backdrops.constFind(source);
    if (it != m_backdrops.constEnd())
        return *it;
    const QImage base = anyImage(item);
    if (base.isNull())
        return {};
    const QImage img = downscale(base, 48).convertToFormat(QImage::Format_ARGB32);

    // Рамка непрозрачной части иконки
    int x0 = img.width(), y0 = img.height(), x1 = -1, y1 = -1;
    for (int y = 0; y < img.height(); ++y)
    {
        const auto* line = reinterpret_cast<const QRgb*>(img.constScanLine(y));
        for (int x = 0; x < img.width(); ++x)
            if (qAlpha(line[x]) > 200)
            {
                x0 = qMin(x0, x); y0 = qMin(y0, y); x1 = qMax(x1, x); y1 = qMax(y1, y);
            }
    }
    Backdrop b;
    if (x1 < 0)
    {
        b.plate = QColor(60, 62, 70);
    }
    else
    {
        // Цвет «подложки» — медиана по внешнему кольцу непрозрачной части (не по самому яркому пятну логотипа).
        const int ring = qMax(2, qMin(x1 - x0, y1 - y0) * 12 / 100);
        QList<int> rs, gs, bs, all_r, all_g, all_b;
        for (int y = y0; y <= y1; ++y)
        {
            const auto* line = reinterpret_cast<const QRgb*>(img.constScanLine(y));
            for (int x = x0; x <= x1; ++x)
            {
                const QRgb c = line[x];
                if (qAlpha(c) <= 200)
                    continue;
                all_r << qRed(c); all_g << qGreen(c); all_b << qBlue(c);
                if (x - x0 < ring || x1 - x < ring || y - y0 < ring || y1 - y < ring)
                {
                    rs << qRed(c); gs << qGreen(c); bs << qBlue(c);
                }
            }
        }
        auto median = [](QList<int>& v) {
            std::nth_element(v.begin(), v.begin() + v.size() / 2, v.end());
            return v[v.size() / 2];
        };
        if (rs.size() >= 20)
            b.plate = QColor(median(rs), median(gs), median(bs));
        else
            b.plate = QColor(median(all_r), median(all_g), median(all_b));
    }
    // Мягкий фон: иконка на своей подложке, сжатая до 4×4 — при растягивании даёт плавное пятно её цветов.
    QImage onPlate(img.size(), QImage::Format_ARGB32_Premultiplied);
    onPlate.fill(b.plate);
    {
        QPainter p(&onPlate);
        p.drawImage(0, 0, img);
    }
    // Настоящее размытие на 32×32 (три прохода «коробочного» фильтра ≈ гаусс) — без полос при растягивании.
    QImage soft = onPlate.scaled(32, 32, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                      .convertToFormat(QImage::Format_ARGB32_Premultiplied);
    {
        const int w = soft.width(), h = soft.height(), r = 5;
        QList<QRgb> line;
        auto blurLine = [&](QRgb* px, int count, int stride) {
            line.resize(count);
            for (int i = 0; i < count; ++i)
                line[i] = px[i * stride];
            int sa = 0, sr = 0, sg = 0, sb = 0;
            auto at = [&](int i) { return line[std::clamp(i, 0, count - 1)]; };
            for (int i = -r; i <= r; ++i)
            {
                const QRgb c = at(i);
                sa += qAlpha(c); sr += qRed(c); sg += qGreen(c); sb += qBlue(c);
            }
            const int n = 2 * r + 1;
            for (int i = 0; i < count; ++i)
            {
                px[i * stride] = qRgba(sr / n, sg / n, sb / n, sa / n);
                const QRgb out = at(i - r), in = at(i + r + 1);
                sa += qAlpha(in) - qAlpha(out); sr += qRed(in) - qRed(out);
                sg += qGreen(in) - qGreen(out); sb += qBlue(in) - qBlue(out);
            }
        };
        for (int pass = 0; pass < 3; ++pass)
        {
            for (int y = 0; y < h; ++y)
                blurLine(reinterpret_cast<QRgb*>(soft.scanLine(y)), w, 1);
            auto* bits = reinterpret_cast<QRgb*>(soft.bits());
            const int stride = int(soft.bytesPerLine() / 4);
            for (int x = 0; x < w; ++x)
                blurLine(bits + x, h, stride);
        }
    }
    b.soft = soft;
    // Средний цвет нижней трети (под подписью) — считаем сразу, а не при каждой отрисовке плитки
    if (!b.soft.isNull())
        b.softBottom = QColor(b.soft.copy(0, b.soft.height() * 2 / 3, b.soft.width(), b.soft.height() / 3)
                                      .scaled(1, 1, Qt::IgnoreAspectRatio, Qt::SmoothTransformation).pixel(0, 0));
    m_backdrops.insert(source, b);
    return b;
}

QColor IconCache::dominantColor(const Item& item)
{
    const QString& source = item.icon.isEmpty() ? item.path : item.icon;
    const auto it = m_colors.constFind(source);
    if (it != m_colors.constEnd())
        return *it;
    // Картинка для подсчёта: исходник, а если его нет (иконка пришла из кэша готовых размеров) —
    // любой уже загруженный размер этой иконки.
    QImage base;
    if (const auto src = m_sources.constFind(source); src != m_sources.constEnd())
        base = *src;
    else
    {
        get(item, 48, 1.0); // загрузит иконку (из кэша на диске — сразу)
        const QString prefix = source + QLatin1Char('|');
        for (auto i = m_done.constBegin(); i != m_done.constEnd(); ++i)
            if (i.key().startsWith(prefix) && (base.isNull() || i.value().width() > base.width()))
                base = i.value().toImage();
    }
    if (base.isNull())
        return {}; // ещё грузится — посчитаем при следующей отрисовке
    // Гистограмма оттенков по насыщенным непрозрачным пикселям; берём самый частый оттенок,
    // а его цвет — среднее по пикселям этого оттенка.
    const QImage img = downscale(base, 32).convertToFormat(QImage::Format_ARGB32);
    int hist[36] = {};
    long long sr[36] = {}, sg[36] = {}, sb[36] = {};
    long long grayR = 0, grayG = 0, grayB = 0, grayN = 0;
    for (int y = 0; y < img.height(); ++y)
    {
        const auto* line = reinterpret_cast<const QRgb*>(img.constScanLine(y));
        for (int x = 0; x < img.width(); ++x)
        {
            const QRgb c = line[x];
            if (qAlpha(c) < 160)
                continue;
            const QColor col(c);
            if (col.hsvSaturation() < 70 || col.value() < 50)
            {
                grayR += qRed(c); grayG += qGreen(c); grayB += qBlue(c); ++grayN;
                continue;
            }
            const int b = qBound(0, col.hsvHue() / 10, 35);
            ++hist[b];
            sr[b] += qRed(c); sg[b] += qGreen(c); sb[b] += qBlue(c);
        }
    }
    int best = 0;
    for (int b = 1; b < 36; ++b)
        if (hist[b] > hist[best])
            best = b;
    QColor result;
    if (hist[best] > 6)
        result = QColor(int(sr[best] / hist[best]), int(sg[best] / hist[best]), int(sb[best] / hist[best]));
    else if (grayN > 0)
        result = QColor(int(grayR / grayN), int(grayG / grayN), int(grayB / grayN)); // почти бесцветная иконка
    else
        result = QColor(110, 123, 255);
    m_colors.insert(source, result);
    return result;
}

QString IconCache::faviconFile(const QString& url)
{
    QString host = QUrl(url).host();
    if (host.startsWith(QStringLiteral("www.")))
        host = host.mid(4);
    return diskCacheDir() + QStringLiteral("/fav-") + host + QStringLiteral(".png");
}

void IconCache::fetchFavicon(const QString& url)
{
    if (m_favPending.contains(url))
        return;
    m_favPending.insert(url);
    const QString file = faviconFile(url);
    const QUrl u(url);
    const QString origin = u.scheme() + QStringLiteral("://") + u.host();
    QPointer<IconCache> self(this);
    std::thread([self, url, file, origin] {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        QDir().mkpath(QFileInfo(file).absolutePath());
        const QString tmp = file + QStringLiteral(".part");
        auto download = [&](const QString& from) {
            QFile::remove(tmp);
            return URLDownloadToFileW(nullptr, reinterpret_cast<LPCWSTR>(from.utf16()), reinterpret_cast<LPCWSTR>(tmp.utf16()), 0, nullptr) == S_OK;
        };
        auto decode = [&](QImage& out) {
            if (out.load(tmp) && !out.isNull())
                return true; // PNG и т.п.
            // .ico — через Windows: самая большая картинка из файла
            if (HICON h = static_cast<HICON>(LoadImageW(nullptr, reinterpret_cast<LPCWSTR>(tmp.utf16()), IMAGE_ICON, 256, 256, LR_LOADFROMFILE)))
            {
                out = QImage::fromHICON(h);
                DestroyIcon(h);
            }
            return !out.isNull();
        };
        QImage best;
        // 1) крупная иконка для iPhone (обычно 180×180), 2) ссылки на иконки из страницы, 3) favicon.ico
        QStringList candidates{origin + QStringLiteral("/apple-touch-icon.png")};
        if (download(origin + QLatin1Char('/')))
        {
            QFile page(tmp);
            if (page.open(QIODevice::ReadOnly))
            {
                const QString html = QString::fromUtf8(page.read(400000));
                // Не static: поток может доработать, когда программа уже закрывается и статические объекты разрушены
                const QRegularExpression link(QStringLiteral("<link[^>]+rel=[\"']?[^\"'>]*icon[^>]*>"), QRegularExpression::CaseInsensitiveOption);
                const QRegularExpression href(QStringLiteral("href=[\"']([^\"']+)[\"']"), QRegularExpression::CaseInsensitiveOption);
                for (auto it = link.globalMatch(html); it.hasNext();)
                {
                    const auto h = href.match(it.next().captured(0));
                    if (h.hasMatch())
                        candidates.append(QUrl(origin + QLatin1Char('/')).resolved(QUrl(h.captured(1).replace(QStringLiteral("&amp;"), QStringLiteral("&")))).toString());
                }
            }
        }
        candidates.append(origin + QStringLiteral("/favicon.ico"));
        for (const QString& c : candidates)
        {
            QImage img;
            if (!c.endsWith(QStringLiteral(".svg"), Qt::CaseInsensitive) && download(c) && decode(img) && img.width() > best.width())
                best = img;
            if (best.width() >= 96)
                break; // достаточно чёткая
        }
        QFile::remove(tmp);
        if (!best.isNull())
            best.convertToFormat(QImage::Format_ARGB32).save(file, "PNG");
        else
        {
            QFile none(file + QStringLiteral(".none")); // пометка: иконки нет, не качать при каждом открытии
            none.open(QIODevice::WriteOnly);
        }
        CoUninitialize();
        // Обратно в основной поток: перечитать иконку
        QMetaObject::invokeMethod(qApp, [self, url] {
            if (!self)
                return;
            self->m_favPending.remove(url);
            self->m_sources.remove(url);
            const QString prefix = url + QLatin1Char('|');
            for (auto i = self->m_done.begin(); i != self->m_done.end();)
                i = i.key().startsWith(prefix) ? self->m_done.erase(i) : std::next(i);
            emit self->changed();
        }, Qt::QueuedConnection);
    }).detach();
}

void IconCache::pruneExact()
{
    // В фоне: оставить 800 самых свежих готовых размеров, остальное (старые размеры окна) удалить.
    // Заодно убрать «точные» файлы старого формата, лежавшие вперемешку с исходниками.
    std::thread([] {
        const QString dir = diskCacheDir();
        QFileInfoList exact = QDir(dir + QStringLiteral("/exact")).entryInfoList({QStringLiteral("*.png")}, QDir::Files, QDir::Time);
        for (int i = 800; i < exact.size(); ++i)
            QFile::remove(exact[i].absoluteFilePath());
        const QString marker = dir + QStringLiteral("/exact/.migrated");
        if (!QFileInfo::exists(marker))
        {
            // Старые «точные» файлы не отличить от исходников по имени — исходники только 64/128/256 px,
            // поэтому удаляем всё, что не этих размеров
            for (const QFileInfo& fi : QDir(dir).entryInfoList({QStringLiteral("*.png")}, QDir::Files))
            {
                const int w = QImageReader(fi.absoluteFilePath()).size().width(); // только заголовок, без распаковки
                if (w > 0 && w != 64 && w != 128 && w != 256 && !fi.fileName().startsWith(QStringLiteral("fav-")))
                    QFile::remove(fi.absoluteFilePath());
            }
            QDir().mkpath(dir + QStringLiteral("/exact"));
            QFile f(marker);
            f.open(QIODevice::WriteOnly);
        }
    }).detach();
}

QString IconCache::diskCacheDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/icons");
}

void IconCache::clearDiskCache()
{
    QDir(diskCacheDir()).removeRecursively();
}

// Имя файла в кэше: хэш от пути, размера и даты изменения файла —
// поменялся ярлык или exe, и иконка перечитается сама.
QString IconCache::diskFile(const QString& path, int px, bool exact)
{
    qint64 stamp = 0;
    if (!path.startsWith(QStringLiteral("shell:"), Qt::CaseInsensitive))
    {
        const QFileInfo fi(path);
        if (fi.exists())
            stamp = fi.lastModified().toMSecsSinceEpoch();
    }
    // Дата изменения — у самого файла (а не у строки с приставкой), чтобы после обновления программы иконка обновилась
    const QByteArray key = ((exact ? QStringLiteral("exact|") : QStringLiteral("src2|")) + path.toLower() + QLatin1Char('|')
                            + QString::number(px) + QLatin1Char('|') + QString::number(stamp))
                               .toUtf8();
    // Готовые размеры — в отдельной папке: их много, и старые оттуда чистятся (pruneExact)
    return diskCacheDir() + (exact ? QStringLiteral("/exact/") : QStringLiteral("/"))
           + QString::fromLatin1(QCryptographicHash::hash(key, QCryptographicHash::Sha1).toHex()) + QStringLiteral(".png");
}

// Запасная иконка: скруглённый квадрат с первой буквой.
QPixmap IconCache::letterIcon(const QString& name, int px)
{
    QPixmap pm(px, px);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(Theme::Accent.color());
    p.drawRoundedRect(QRectF(0, 0, px, px), px / 5.0, px / 5.0);
    QFont f = p.font();
    f.setPixelSize(qMax(6, px / 2));
    f.setBold(true);
    p.setFont(f);
    p.setPen(Qt::white);
    p.drawText(QRect(0, 0, px, px), Qt::AlignCenter, name.left(1).toUpper());
    return pm;
}
