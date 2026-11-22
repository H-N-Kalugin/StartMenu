#include "Lang.h"

#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>

namespace
{
    QString g_code = QStringLiteral("ru");
    QHash<QString, QString> g_map;      // русская строка → перевод
    QHash<QString, QString> g_fallback; // русская строка → английский (если в выбранном языке пропуск)

    QJsonArray keys()
    {
        static QJsonArray k = [] {
            QFile f(QStringLiteral(":/i18n/keys.json"));
            return f.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(f.readAll()).array() : QJsonArray();
        }();
        return k;
    }

    // Файл языка: {"индекс строки в keys.json": "перевод", …}
    QHash<QString, QString> readLang(const QString& code)
    {
        QHash<QString, QString> map;
        QFile f(QStringLiteral(":/i18n/%1.json").arg(code));
        if (!f.open(QIODevice::ReadOnly))
            return map;
        const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
        const QJsonArray k = keys();
        for (auto it = o.begin(); it != o.end(); ++it)
        {
            const int i = it.key().toInt();
            if (i >= 0 && i < k.size() && !it.value().toString().isEmpty())
                map.insert(k[i].toString(), it.value().toString());
        }
        return map;
    }
}

namespace Lang
{
const QList<Info>& available()
{
    static const QList<Info> list{
        {QStringLiteral("ru"), QStringLiteral("Русский")},   {QStringLiteral("en"), QStringLiteral("English")},
        {QStringLiteral("uk"), QStringLiteral("Українська")}, {QStringLiteral("de"), QStringLiteral("Deutsch")},
        {QStringLiteral("fr"), QStringLiteral("Français")},   {QStringLiteral("es"), QStringLiteral("Español")},
        {QStringLiteral("it"), QStringLiteral("Italiano")},   {QStringLiteral("pt"), QStringLiteral("Português")},
        {QStringLiteral("pl"), QStringLiteral("Polski")},     {QStringLiteral("tr"), QStringLiteral("Türkçe")},
        {QStringLiteral("zh"), QStringLiteral("中文")},        {QStringLiteral("ja"), QStringLiteral("日本語")},
    };
    return list;
}

QString resolve(const QString& setting)
{
    auto known = [](const QString& c) {
        for (const Info& i : available())
            if (i.code == c)
                return true;
        return false;
    };
    if (setting != QStringLiteral("auto") && known(setting))
        return setting;
    // Язык интерфейса Windows: «ru-RU» → «ru». Белорусский и казахский интерфейс чаще читают по-русски.
    for (const QString& ui : QLocale::system().uiLanguages())
    {
        const QString c = ui.section(QLatin1Char('-'), 0, 0).toLower();
        if (c == QStringLiteral("be") || c == QStringLiteral("kk"))
            return QStringLiteral("ru");
        if (known(c))
            return c;
    }
    return QStringLiteral("en");
}

void load(const QString& setting)
{
    g_code = resolve(setting);
    g_map.clear();
    g_fallback.clear();
    if (g_code == QStringLiteral("ru"))
        return;
    g_map = readLang(g_code);
    if (g_code != QStringLiteral("en"))
        g_fallback = readLang(QStringLiteral("en"));
}

QString current()
{
    return g_code;
}

QLocale locale()
{
    return QLocale(g_code);
}

QString tr(const QString& ru)
{
    if (g_code == QStringLiteral("ru"))
        return ru;
    if (const auto it = g_map.constFind(ru); it != g_map.constEnd())
        return *it;
    if (const auto it = g_fallback.constFind(ru); it != g_fallback.constEnd())
        return *it;
    return ru;
}

QString localizeDefault(const QString& stored)
{
    // Русские оригиналы стандартных названий; «Слой %1» — с номером
    static const QStringList defaults{QStringLiteral("Основное"), QStringLiteral("Закреплённые"), QStringLiteral("Новая группа"),
                                      QStringLiteral("Избранное"), QStringLiteral("Ссылки"), QStringLiteral("Папки"),
                                      QStringLiteral("Без названия"), QStringLiteral("Слой %1")};
    // Все переводы этих названий во всех языках (читаются один раз)
    static const QHash<QString, QStringList> variants = [] {
        QHash<QString, QStringList> v;
        for (const QString& d : defaults)
            v[d].append(d);
        for (const Info& l : available())
        {
            const QHash<QString, QString> map = readLang(l.code);
            for (const QString& d : defaults)
                if (const auto it = map.constFind(d); it != map.constEnd() && !v[d].contains(*it))
                    v[d].append(*it);
        }
        return v;
    }();
    const QString s = stored.trimmed();
    for (const QString& d : defaults)
        for (const QString& form : variants.value(d))
        {
            if (!form.contains(QStringLiteral("%1")))
            {
                if (s == form)
                    return tr(d);
                continue;
            }
            // «Layer 3» / «レイヤー 3» → номер
            const QString pre = form.section(QStringLiteral("%1"), 0, 0), post = form.section(QStringLiteral("%1"), 1);
            if (s.startsWith(pre) && s.endsWith(post) && s.size() > pre.size() + post.size())
            {
                bool ok = false;
                const int n = s.mid(pre.size(), s.size() - pre.size() - post.size()).toInt(&ok);
                if (ok)
                    return tr(d).arg(n);
            }
        }
    return stored;
}

int pluralIndex(int n)
{
    n = qAbs(n);
    const int m10 = n % 10, m100 = n % 100;
    if (g_code == QStringLiteral("ru") || g_code == QStringLiteral("uk"))
    {
        if (m10 == 1 && m100 != 11)
            return 0;
        if (m10 >= 2 && m10 <= 4 && (m100 < 12 || m100 > 14))
            return 1;
        return 2;
    }
    if (g_code == QStringLiteral("pl"))
    {
        if (n == 1)
            return 0;
        if (m10 >= 2 && m10 <= 4 && (m100 < 12 || m100 > 14))
            return 1;
        return 2;
    }
    if (g_code == QStringLiteral("zh") || g_code == QStringLiteral("ja"))
        return 2;
    if (g_code == QStringLiteral("fr"))
        return n <= 1 ? 0 : 2;
    return n == 1 ? 0 : 2;
}
}
