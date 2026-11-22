#pragma once
#include <QList>
#include <QLocale>
#include <QString>

// Язык интерфейса. Исходные строки в коде — на русском; переводы лежат в exe (i18n/<код>.json).
// Если перевода нет, берётся английский, а если нет и его — русский оригинал.
namespace Lang
{
    struct Info
    {
        QString code;   // «ru», «en», «de»…
        QString native; // название на самом языке: «Русский», «English», «Deutsch»…
    };

    const QList<Info>& available();
    QString resolve(const QString& setting); // «auto» → язык Windows (если его нет среди переводов — английский)
    void load(const QString& setting);       // загрузить переводы («auto» или код)
    QString current();                       // код загруженного языка
    QLocale locale();                        // числа и даты в формате этого языка

    QString tr(const QString& ru);
    // Стандартное название, которое программа сама дала слою или группе («Основное», «Закреплённые», «Слой 2»…),
    // на каком бы языке его ни сохранили, — на текущем языке. Свои названия возвращаются как есть.
    QString localizeDefault(const QString& stored);
    // Форма множественного числа для текущего языка: 0 — «1 группа», 1 — «3 группы», 2 — «5 групп».
    int pluralIndex(int n);
}

// Строка интерфейса: TXT("Открыть меню") — на выбранном языке.
#define TXT(s) Lang::tr(QStringLiteral(s))
