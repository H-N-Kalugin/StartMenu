#pragma once
#include <QList>
#include <QString>

#include "Config.h"

// Дополнительные результаты поиска, кроме приложений:
// калькулятор («12*3,5» → «= 42»), страницы «Параметров Windows», поиск файлов и поиск в интернете.
namespace SearchExtras
{
    bool calculate(const QString& expr, double& result); // true, если это выражение и оно посчиталось
    // Запрос + его варианты: латиница по-русски («фото» → «foto», «photo»), набор не в той раскладке («ыеуфь» → «steam»).
    QStringList variants(const QString& query);
    QList<Item> top(const QString& query);                // ставится в начало списка (калькулятор)
    QList<Item> settings(const QString& query);           // подходящие страницы «Параметров»
    QList<Item> tail(const QString& query);               // в конец: файлы и интернет
}
