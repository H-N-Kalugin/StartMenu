#include "SearchExtras.h"
#include "Lang.h"
#include "Shell.h"

#include <QDir>
#include <QLocale>
#include <QUrl>
#include <cmath>

namespace
{
    // Маленький разбор выражений: + - * / % ^, скобки, десятичная точка или запятая.
    class Parser
    {
    public:
        explicit Parser(const QString& s) : m_s(s) {}

        bool parse(double& out)
        {
            m_pos = 0;
            m_ok = true;
            out = expr();
            skip();
            return m_ok && m_pos == m_s.size() && std::isfinite(out);
        }

    private:
        void skip()
        {
            while (m_pos < m_s.size() && m_s[m_pos].isSpace())
                ++m_pos;
        }
        bool take(QChar c)
        {
            skip();
            if (m_pos < m_s.size() && m_s[m_pos] == c)
            {
                ++m_pos;
                return true;
            }
            return false;
        }
        double expr()
        {
            double v = term();
            for (;;)
            {
                if (take('+')) v += term();
                else if (take('-')) v -= term();
                else return v;
            }
        }
        double term()
        {
            double v = power();
            for (;;)
            {
                if (take('*') || take(QChar(0x00D7))) v *= power();
                else if (take('/') || take(':')) v /= power();
                else if (take('%')) v = std::fmod(v, power());
                else return v;
            }
        }
        double power()
        {
            const double base = unary();
            if (take('^'))
                return std::pow(base, power()); // степень — справа налево
            return base;
        }
        double unary()
        {
            if (take('-')) return -unary();
            if (take('+')) return unary();
            return primary();
        }
        double primary()
        {
            if (take('('))
            {
                const double v = expr();
                if (!take(')'))
                    m_ok = false;
                return v;
            }
            skip();
            const int start = m_pos;
            while (m_pos < m_s.size() && (m_s[m_pos].isDigit() || m_s[m_pos] == '.' || m_s[m_pos] == ','))
                ++m_pos;
            if (start == m_pos)
            {
                m_ok = false;
                return 0;
            }
            bool ok = false;
            const double v = m_s.mid(start, m_pos - start).replace(',', '.').toDouble(&ok);
            if (!ok)
                m_ok = false;
            return v;
        }

        QString m_s;
        int m_pos = 0;
        bool m_ok = true;
    };

    // Иконка для страниц «Параметров» — иконка самого приложения «Параметры».
    const QString kSettingsIcon =
        QStringLiteral("shell:AppsFolder\\windows.immersivecontrolpanel_cw5n1h2txyewy!microsoft.windows.immersivecontrolpanel");

    struct Page
    {
        const char* name;
        const char* uri;
        const char* keywords; // дополнительные слова для поиска
    };

    const Page kPages[] = {
        {"Дисплей", "ms-settings:display", "экран монитор разрешение масштаб яркость"},
        {"Звук", "ms-settings:sound", "громкость динамики микрофон аудио"},
        {"Уведомления", "ms-settings:notifications", "оповещения"},
        {"Питание", "ms-settings:powersleep", "сон батарея энергия"},
        {"Память устройства", "ms-settings:storagesense", "диск хранилище место очистка"},
        {"Bluetooth и устройства", "ms-settings:bluetooth", "блютуз наушники устройства"},
        {"Принтеры и сканеры", "ms-settings:printers", "печать"},
        {"Мышь", "ms-settings:mousetouchpad", "курсор указатель тачпад"},
        {"Сеть и Интернет", "ms-settings:network", "интернет ethernet"},
        {"Wi-Fi", "ms-settings:network-wifi", "вайфай беспроводная сеть"},
        {"VPN", "ms-settings:network-vpn", "впн"},
        {"Фон рабочего стола", "ms-settings:personalization-background", "обои фото картинка персонализация"},
        {"Цвета", "ms-settings:colors", "тёмная тема светлая акцент"},
        {"Темы", "ms-settings:themes", "оформление"},
        {"Экран блокировки", "ms-settings:lockscreen", "блокировка"},
        {"Панель задач", "ms-settings:taskbar", "трей"},
        {"Установленные приложения", "ms-settings:appsfeatures", "удалить программы"},
        {"Приложения по умолчанию", "ms-settings:defaultapps", "браузер открывать"},
        {"Автозагрузка", "ms-settings:startupapps", "автозапуск"},
        {"Учётные записи", "ms-settings:accounts", "аккаунт профиль пользователь"},
        {"Варианты входа", "ms-settings:signinoptions", "пароль пин windows hello"},
        {"Дата и время", "ms-settings:dateandtime", "часы часовой пояс"},
        {"Язык и регион", "ms-settings:regionlanguage", "раскладка клавиатура"},
        {"Игровой режим", "ms-settings:gaming-gamemode", "игры"},
        {"Графика", "ms-settings:display-advancedgraphics", "видеокарта gpu производительность"},
        {"Многозадачность", "ms-settings:multitasking", "окна прикрепление snap"},
        {"Буфер обмена", "ms-settings:clipboard", "копировать"},
        {"Специальные возможности", "ms-settings:easeofaccess", "доступность"},
        {"Конфиденциальность", "ms-settings:privacy", "приватность разрешения"},
        {"Центр обновления Windows", "ms-settings:windowsupdate", "обновления апдейт"},
        {"Безопасность Windows", "ms-settings:windowsdefender", "защитник антивирус"},
        {"О системе", "ms-settings:about", "компьютер характеристики имя"},
        {"Удалённый рабочий стол", "ms-settings:remotedesktop", "rdp"},
    };
}

namespace SearchExtras
{
QStringList variants(const QString& query)
{
    const QString q = query.toLower();
    QStringList out{q};
    static const QString ru = QStringLiteral("йцукенгшщзхъфывапролджэячсмитьбю");
    static const QString en = QStringLiteral("qwertyuiop[]asdfghjkl;'zxcvbnm,.");
    // 1. Набрали не в той раскладке
    QString swapped;
    for (const QChar c : q)
    {
        const int i = int(ru.indexOf(c)), j = int(en.indexOf(c));
        swapped += i >= 0 ? en[i] : j >= 0 ? ru[j] : c;
    }
    if (swapped != q)
        out << swapped;
    // 2. Русские буквы латиницей (два варианта для «ф»: f и ph — «фото» → Photo)
    static const char* tr[] = {"a", "b", "v", "g", "d", "e", "zh", "z", "i", "y", "k", "l", "m", "n", "o", "p",
                               "r", "s", "t", "u", "f", "h", "c", "ch", "sh", "sch", "", "y", "", "e", "yu", "ya"};
    QString lat, latPh;
    bool cyr = false;
    for (const QChar c : q)
    {
        const ushort u = c.unicode();
        if (u >= 0x0430 && u <= 0x044F)
        {
            cyr = true;
            lat += QLatin1String(tr[u - 0x0430]);
            latPh += u == 0x0444 ? QStringLiteral("ph") : QLatin1String(tr[u - 0x0430]);
        }
        else if (u == 0x0451) // ё
        {
            cyr = true;
            lat += QLatin1Char('e');
            latPh += QLatin1Char('e');
        }
        else
        {
            lat += c;
            latPh += c;
        }
    }
    if (cyr)
    {
        out << lat;
        if (latPh != lat)
            out << latPh;
    }
    out.removeDuplicates();
    return out;
}

bool calculate(const QString& expr, double& result)
{
    // Считаем только то, что похоже на выражение: есть цифра и хотя бы один оператор.
    static const QString ops = QStringLiteral("+-*/%^:×()");
    bool digit = false, op = false;
    for (const QChar c : expr)
    {
        digit |= c.isDigit();
        op |= ops.contains(c);
    }
    if (!digit || !op)
        return false;
    return Parser(expr).parse(result);
}

QList<Item> top(const QString& query)
{
    double v = 0;
    if (!calculate(query, v))
        return {};
    QLocale loc = Lang::locale();
    loc.setNumberOptions(QLocale::OmitGroupSeparator); // «1000000», а не «1 000 000» — вставляется куда угодно
    const QString shown = loc.toString(v, 'g', 12);
    // Enter/клик — скопировать результат (см. WinShell::launch, схема calc:).
    return {Item{QStringLiteral("= ") + shown, QStringLiteral("calc:") + shown, {}, QStringLiteral("calc:")}};
}

QList<Item> settings(const QString& query)
{
    QList<Item> out;
    for (const Page& p : kPages)
    {
        const QString ruName = QString::fromUtf8(p.name);
        const QString name = Lang::tr(ruName);
        // Ключевые слова сравниваем по началу слова: «фон» не должен находить «микроФОН».
        bool hit = name.contains(query, Qt::CaseInsensitive) || ruName.contains(query, Qt::CaseInsensitive);
        for (const QString& w : QString::fromUtf8(p.keywords).split(QLatin1Char(' '), Qt::SkipEmptyParts))
            hit = hit || w.startsWith(query, Qt::CaseInsensitive);
        if (hit)
            out.append({TXT("Параметры: ") + name, QString::fromLatin1(p.uri), {}, kSettingsIcon});
    }
    return out;
}

QList<Item> tail(const QString& query)
{
    const QString home = QDir::toNativeSeparators(QDir::homePath());
    const QString files = QStringLiteral("search-ms:query=%1&crumb=location:%2")
                              .arg(QString::fromLatin1(QUrl::toPercentEncoding(query)),
                                   QString::fromLatin1(QUrl::toPercentEncoding(home)));
    const QString web = QStringLiteral("https://www.google.com/search?q=") + QString::fromLatin1(QUrl::toPercentEncoding(query));
    return {
        Item{TXT("Найти файлы «%1»").arg(query), files, {}, QStringLiteral("C:/Windows/explorer.exe")},
        Item{TXT("Искать «%1» в интернете").arg(query), web, {}, WinShell::defaultBrowserExe()},
    };
}
}
