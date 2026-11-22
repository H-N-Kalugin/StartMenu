#include "ClipboardStore.h"
#include "Lang.h"
#include "Config.h"
#include "Demo.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QThread>
#include <QSize>
#include <algorithm>
#include <QStandardPaths>

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>

namespace ClipboardStore
{
namespace
{
    constexpr int kMaxEntries = 40;   // незакреплённых
    constexpr int kMaxImages = 12;    // картинок (они тяжёлые)
    constexpr int kMaxText = 20000;   // символов в одной записи

    QString historyPath() { return dir() + QStringLiteral("/history.json"); }
    QString seenPath() { return dir() + QStringLiteral("/seen.txt"); }

    // Менеджеры паролей и некоторые программы просят не сохранять их копирование в истории.
    bool excludedByOwner()
    {
        static const UINT exclude = RegisterClipboardFormatW(L"ExcludeClipboardContentFromMonitorProcessing");
        static const UINT viewerIgnore = RegisterClipboardFormatW(L"Clipboard Viewer Ignore");
        static const UINT canHistory = RegisterClipboardFormatW(L"CanIncludeInClipboardHistory");
        if (IsClipboardFormatAvailable(exclude) || IsClipboardFormatAvailable(viewerIgnore))
            return true;
        if (IsClipboardFormatAvailable(canHistory))
        {
            bool deny = false;
            if (OpenClipboard(nullptr))
            {
                if (HANDLE h = GetClipboardData(canHistory))
                    if (auto* v = static_cast<DWORD*>(GlobalLock(h)))
                    {
                        deny = *v == 0;
                        GlobalUnlock(h);
                    }
                CloseClipboard();
            }
            return deny;
        }
        return false;
    }

    // Открыть буфер с повторами: другая программа могла его ещё не отпустить.
    bool openClipboardRetry()
    {
        for (int i = 0; i < 5; ++i)
        {
            if (OpenClipboard(nullptr))
                return true;
            Sleep(10);
        }
        return false;
    }

    QByteArray readDib()
    {
        // CF_DIB → готовый файл BMP в памяти (сам разбор картинки — потом, в фоновом потоке)
        QByteArray none;
        HANDLE h = GetClipboardData(CF_DIB);
        if (!h)
            return none;
        const SIZE_T size = GlobalSize(h);
        auto* data = static_cast<const uchar*>(GlobalLock(h));
        if (!data)
            return none;
        // Битые данные от чужой программы: заголовок не помещается — не читаем дальше конца блока
        if (size < sizeof(BITMAPINFOHEADER) || reinterpret_cast<const BITMAPINFOHEADER*>(data)->biSize > size)
        {
            GlobalUnlock(h);
            return none;
        }
        const auto* bih = reinterpret_cast<const BITMAPINFOHEADER*>(data);
        int palette = bih->biClrUsed ? int(bih->biClrUsed) : (bih->biBitCount <= 8 ? 1 << bih->biBitCount : 0);
        int masks = (bih->biCompression == BI_BITFIELDS && bih->biSize == sizeof(BITMAPINFOHEADER)) ? 12 : 0;
        BITMAPFILEHEADER bf = {};
        bf.bfType = 0x4D42;
        bf.bfSize = DWORD(sizeof(bf) + size);
        bf.bfOffBits = DWORD(sizeof(bf) + bih->biSize + masks + palette * 4);
        QByteArray bmp(reinterpret_cast<const char*>(&bf), sizeof(bf));
        bmp.append(reinterpret_cast<const char*>(data), qsizetype(size));
        GlobalUnlock(h);
        return bmp;
    }

    QJsonObject toJson(const Entry& e)
    {
        QJsonObject o{{"type", int(e.type)}, {"time", e.time}};
        if (!e.text.isEmpty())
            o["text"] = e.text;
        if (!e.image.isEmpty())
        {
            o["image"] = e.image;
            o["w"] = e.size.width();
            o["h"] = e.size.height();
        }
        if (!e.files.isEmpty())
            o["files"] = QJsonArray::fromStringList(e.files);
        if (e.pinned)
            o["pinned"] = true;
        return o;
    }

    Entry fromJson(const QJsonObject& o)
    {
        Entry e;
        e.type = Type(std::clamp(o["type"].toInt(), 0, 2));
        e.text = o["text"].toString();
        e.image = o["image"].toString();
        e.size = QSize(o["w"].toInt(), o["h"].toInt());
        for (const auto& v : o["files"].toArray())
            e.files.append(v.toString());
        e.time = o["time"].toInteger();
        e.pinned = o["pinned"].toBool();
        return e;
    }

    bool sameContent(const Entry& a, const Entry& b)
    {
        if (a.type != b.type)
            return false;
        switch (a.type)
        {
        case Type::Text:  return a.text == b.text;
        case Type::Files: return a.files == b.files;
        case Type::Image: return a.size == b.size && a.text == b.text; // text у картинки — её «отпечаток»
        }
        return false;
    }

    // Лишнее — вон: старые незакреплённые записи и файлы картинок, на которые больше никто не ссылается.
    void trim(QList<Entry>& list)
    {
        int unpinned = 0, images = 0;
        for (int i = 0; i < list.size();)
        {
            const Entry& e = list[i];
            const bool overflow = !e.pinned && (++unpinned > kMaxEntries || (e.type == Type::Image && ++images > kMaxImages));
            if (overflow)
                list.removeAt(i);
            else
                ++i;
        }
        QSet<QString> used;
        for (const Entry& e : list)
            if (!e.image.isEmpty())
                used.insert(e.image);
        const QDateTime fresh = QDateTime::currentDateTime().addSecs(-60);
        for (const QFileInfo& fi : QDir(dir()).entryInfoList({QStringLiteral("img-*.png")}, QDir::Files))
            if (!used.contains(fi.fileName()) && fi.lastModified() < fresh) // свежий файл мог только что записать трей
                QFile::remove(fi.absoluteFilePath());
    }
}

QString dir()
{
    const QString d = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/StartMenu/clipboard");
    QDir().mkpath(d);
    return d;
}

QList<Entry> load()
{
    if (Demo::active())
        return Demo::clipboard(); // снимки для страницы проекта — без личной истории
    QList<Entry> out;
    const FileLock lock; // трей мог как раз подменять файл
    QFile f(historyPath());
    if (!f.open(QIODevice::ReadOnly))
        return out;
    for (const auto& v : QJsonDocument::fromJson(f.readAll()).array())
        out.append(fromJson(v.toObject()));
    return out;
}

void save(const QList<Entry>& entries)
{
    QJsonArray arr;
    for (const Entry& e : entries)
        arr.append(toJson(e));
    // Запись целиком во временный файл и замена — с повтором, если файл на миг занят другим процессом
    const FileLock lock;
    for (int attempt = 0; attempt < 3; ++attempt)
    {
        QSaveFile f(historyPath());
        if (f.open(QIODevice::WriteOnly) && f.write(QJsonDocument(arr).toJson(QJsonDocument::Compact)) >= 0 && f.commit())
            return;
        QThread::msleep(20);
    }
}

Raw readRaw()
{
    // Только забрать данные из буфера — быстро, в потоке трея, где работают перехваты клавиатуры и мыши
    Raw r;
    if (excludedByOwner())
        return r;
    if (!openClipboardRetry())
        return r;
    Entry& e = r.entry;
    e.time = QDateTime::currentMSecsSinceEpoch();
    if (HANDLE h = GetClipboardData(CF_HDROP))
    {
        auto drop = static_cast<HDROP>(h);
        const UINT n = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
        for (UINT i = 0; i < n && i < 200; ++i)
        {
            wchar_t buf[MAX_PATH * 2] = {};
            DragQueryFileW(drop, i, buf, MAX_PATH * 2);
            e.files.append(QString::fromWCharArray(buf));
        }
        e.type = Type::Files;
        r.valid = !e.files.isEmpty();
    }
    else if (HANDLE textData = GetClipboardData(CF_UNICODETEXT))
    {
        if (auto* s = static_cast<const wchar_t*>(GlobalLock(textData)))
        {
            e.text = QString::fromWCharArray(s).left(kMaxText);
            GlobalUnlock(textData);
        }
        e.type = Type::Text;
        r.valid = !e.text.trimmed().isEmpty();
    }
    else if (IsClipboardFormatAvailable(CF_DIB))
    {
        r.bmp = readDib();
        e.type = Type::Image;
        r.valid = !r.bmp.isEmpty();
    }
    CloseClipboard();
    return r;
}

bool process(Raw raw)
{
    // Всё тяжёлое — разбор картинки, PNG, запись истории — здесь, в фоновом потоке
    if (!raw.valid)
        return false;
    Entry e = raw.entry;
    QImage image;
    if (e.type == Type::Image)
    {
        image.loadFromData(raw.bmp, "BMP");
        if (image.isNull())
            return false;
        e.size = image.size();
        // «Отпечаток» картинки — чтобы одну и ту же не хранить дважды
        const QImage tiny = image.scaled(8, 8, Qt::IgnoreAspectRatio, Qt::SmoothTransformation).convertToFormat(QImage::Format_RGB32);
        QByteArray sig(reinterpret_cast<const char*>(tiny.constBits()), tiny.sizeInBytes());
        e.text = QString::number(qHash(sig), 16);
    }

    // Список читаем, меняем и записываем под замком: меню в это время может закреплять или удалять записи.
    // Медленную запись PNG делаем до замка, чтобы меню не ждало.
    auto findSame = [&e](const QList<Entry>& list) {
        for (int i = 0; i < list.size(); ++i)
            if (sameContent(list[i], e))
                return i;
        return -1;
    };
    if (e.type == Type::Image && findSame(load()) < 0)
    {
        e.image = QStringLiteral("img-%1.png").arg(e.time);
        // Огромные скриншоты храним уменьшенными до 2560 px по большей стороне
        const QImage stored = qMax(image.width(), image.height()) > 2560
                                  ? image.scaled(2560, 2560, Qt::KeepAspectRatio, Qt::SmoothTransformation)
                                  : image;
        if (!stored.save(dir() + QLatin1Char('/') + e.image, "PNG"))
            return false;
    }
    const FileLock lock;
    QList<Entry> list = load();
    if (const int i = findSame(list); i >= 0)
    {
        // Уже есть — поднимаем наверх (закрепление сохраняем)
        Entry old = list.takeAt(i);
        old.time = e.time;
        list.prepend(old);
        save(list);
        if (!e.image.isEmpty())
            QFile::remove(dir() + QLatin1Char('/') + e.image); // такую же картинку записали за это время
        return true;
    }
    if (e.type == Type::Image && e.image.isEmpty())
        return false; // была в истории при первой проверке, а теперь её удалили — не сохраняем без файла
    list.prepend(e);
    trim(list);
    save(list);
    return true;
}

bool putToClipboard(const Entry& e)
{
    if (!openClipboardRetry())
        return false;
    EmptyClipboard();
    bool ok = false;
    if (e.type == Type::Text)
    {
        const std::wstring w = e.text.toStdWString();
        const SIZE_T bytes = (w.size() + 1) * sizeof(wchar_t);
        if (HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, bytes))
        {
            memcpy(GlobalLock(g), w.c_str(), bytes);
            GlobalUnlock(g);
            ok = SetClipboardData(CF_UNICODETEXT, g) != nullptr;
        }
    }
    else if (e.type == Type::Files)
    {
        // CF_HDROP: DROPFILES + пути через \0, в конце двойной \0
        std::wstring list;
        for (const QString& f : e.files)
            list += QDir::toNativeSeparators(f).toStdWString() + L'\0';
        list += L'\0';
        const SIZE_T bytes = sizeof(DROPFILES) + list.size() * sizeof(wchar_t);
        if (HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, bytes))
        {
            auto* df = static_cast<DROPFILES*>(GlobalLock(g));
            df->pFiles = sizeof(DROPFILES);
            df->fWide = TRUE;
            memcpy(reinterpret_cast<char*>(df) + sizeof(DROPFILES), list.data(), list.size() * sizeof(wchar_t));
            GlobalUnlock(g);
            ok = SetClipboardData(CF_HDROP, g) != nullptr;
        }
    }
    else
    {
        const QImage img = QImage(dir() + QLatin1Char('/') + e.image).convertToFormat(QImage::Format_ARGB32);
        if (!img.isNull())
        {
            // CF_DIB: заголовок + строки снизу вверх, 32 бита
            BITMAPINFOHEADER bih = {};
            bih.biSize = sizeof(bih);
            bih.biWidth = img.width();
            bih.biHeight = img.height();
            bih.biPlanes = 1;
            bih.biBitCount = 32;
            bih.biCompression = BI_RGB;
            const SIZE_T row = SIZE_T(img.width()) * 4;
            const SIZE_T bytes = sizeof(bih) + row * img.height();
            if (HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, bytes))
            {
                auto* dst = static_cast<uchar*>(GlobalLock(g));
                memcpy(dst, &bih, sizeof(bih));
                for (int y = 0; y < img.height(); ++y)
                    memcpy(dst + sizeof(bih) + row * (img.height() - 1 - y), img.constScanLine(y), row);
                GlobalUnlock(g);
                ok = SetClipboardData(CF_DIB, g) != nullptr;
            }
        }
    }
    CloseClipboard();
    return ok;
}

namespace
{
    int indexOf(const QList<Entry>& list, const Entry& e)
    {
        for (int i = 0; i < list.size(); ++i)
            if (list[i].time == e.time && sameContent(list[i], e))
                return i;
        return -1;
    }
}

void remove(const Entry& e)
{
    const FileLock lock;
    QList<Entry> list = load();
    const int index = indexOf(list, e);
    if (index < 0)
        return;
    list.removeAt(index);
    trim(list);
    save(list);
}

void togglePin(const Entry& e)
{
    const FileLock lock;
    QList<Entry> list = load();
    const int index = indexOf(list, e);
    if (index < 0)
        return;
    list[index].pinned = !list[index].pinned;
    save(list);
}

void clearUnpinned()
{
    const FileLock lock;
    QList<Entry> list = load();
    list.erase(std::remove_if(list.begin(), list.end(), [](const Entry& e) { return !e.pinned; }), list.end());
    trim(list);
    save(list);
}

bool hasUnseen()
{
    if (Demo::active())
        return false;
    const QFileInfo hist(historyPath());
    if (!hist.exists())
        return false;
    QFile f(seenPath());
    qint64 seen = 0;
    if (f.open(QIODevice::ReadOnly))
        seen = f.readAll().trimmed().toLongLong();
    // Историю (сотни КБ) разбираем заново, только если файл изменился с прошлого раза
    static qint64 cachedStamp = -1, cachedNewest = 0;
    const qint64 stamp = hist.lastModified().toMSecsSinceEpoch() ^ hist.size();
    if (stamp != cachedStamp)
    {
        cachedStamp = stamp;
        cachedNewest = 0;
        for (const Entry& e : load())
            cachedNewest = qMax(cachedNewest, e.time);
    }
    return cachedNewest > seen;
}

void markSeen()
{
    if (Demo::active())
        return; // демо-режим ничего не пишет на диск
    QFile f(seenPath());
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QByteArray::number(QDateTime::currentMSecsSinceEpoch()));
}

QString describeAge(qint64 time)
{
    const QDateTime t = QDateTime::fromMSecsSinceEpoch(time);
    const qint64 sec = t.secsTo(QDateTime::currentDateTime());
    if (sec < 60)
        return TXT("сейчас");
    if (sec < 3600)
        return TXT("%1 мин").arg(sec / 60);
    if (t.date() == QDate::currentDate())
        return t.toString(QStringLiteral("HH:mm"));
    if (t.date() == QDate::currentDate().addDays(-1))
        return TXT("вчера");
    return t.toString(QStringLiteral("dd.MM"));
}
}
