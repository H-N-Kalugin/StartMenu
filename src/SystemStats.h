#pragma once
#include <QString>

// Тихий монитор системы для нижней строки меню: загрузка ЦП и видеокарты, занятая память, свободное место на диске C.
// Замеры раз в секунду, только пока меню на экране.
#include <atomic>
#include <thread>

class SystemStats
{
public:
    SystemStats();
    ~SystemStats();
    void sample();            // снять новые значения (между двумя вызовами должно пройти время)
    QString line() const;     // «ЦП 12% · ОЗУ 18,2 / 31,9 ГБ · ГП 34% · C: 210 ГБ свободно»
    // Демо-режим для снимков: постоянные выдуманные значения вместо настоящих
    void setFake(int cpu, double ramUsed, double ramTotal, int gpu, double diskFree);

private:
    void initGpu();
    unsigned long long m_idle = 0, m_total = 0;
    int m_cpu = -1, m_gpu = -1;
    double m_ramUsed = 0, m_ramTotal = 0, m_diskFree = -1;
    void* m_query = nullptr;  // PDH (счётчики Windows) для видеокарты
    void* m_gpuCounter = nullptr;
    std::thread m_init;              // подключение счётчика занимает ~130 мс — делаем его в фоне
    std::atomic<bool> m_gpuReady{false};
    bool m_fake = false;
};
