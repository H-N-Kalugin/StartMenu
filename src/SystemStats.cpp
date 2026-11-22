#include "SystemStats.h"
#include "Lang.h"

#include <QLocale>
#include <QStringList>

#include <windows.h>
#include <pdh.h>
#include <vector>

SystemStats::SystemStats()
{
    m_init = std::thread([this] { initGpu(); });
}

SystemStats::~SystemStats()
{
    if (m_init.joinable())
        m_init.join();
    if (m_query)
        PdhCloseQuery(static_cast<PDH_HQUERY>(m_query));
}

void SystemStats::initGpu()
{
    PDH_HQUERY q = nullptr;
    if (PdhOpenQueryW(nullptr, 0, &q) != ERROR_SUCCESS)
        return;
    PDH_HCOUNTER c = nullptr;
    // Загрузка 3D-движков всех видеокарт — то же, что «Графический процессор» в диспетчере задач
    if (PdhAddEnglishCounterW(q, L"\\GPU Engine(*engtype_3D)\\Utilization Percentage", 0, &c) != ERROR_SUCCESS)
    {
        PdhCloseQuery(q);
        return;
    }
    PdhCollectQueryData(q);
    m_query = q;
    m_gpuCounter = c;
    m_gpuReady = true;
}

void SystemStats::setFake(int cpu, double ramUsed, double ramTotal, int gpu, double diskFree)
{
    m_fake = true;
    m_cpu = cpu;
    m_ramUsed = ramUsed;
    m_ramTotal = ramTotal;
    m_gpu = gpu;
    m_diskFree = diskFree;
}

void SystemStats::sample()
{
    if (m_fake)
        return;
    // ЦП: доля «не простоя» между замерами
    FILETIME idle, kernel, user;
    if (GetSystemTimes(&idle, &kernel, &user))
    {
        auto u64 = [](const FILETIME& f) { return (ULONGLONG(f.dwHighDateTime) << 32) | f.dwLowDateTime; };
        const ULONGLONG i = u64(idle), t = u64(kernel) + u64(user);
        if (m_total && t > m_total)
            m_cpu = int(100.0 * (1.0 - double(i - m_idle) / double(t - m_total)) + 0.5);
        m_idle = i;
        m_total = t;
    }
    // Память
    MEMORYSTATUSEX ms = {sizeof(ms)};
    if (GlobalMemoryStatusEx(&ms))
    {
        m_ramTotal = ms.ullTotalPhys / 1073741824.0;
        m_ramUsed = (ms.ullTotalPhys - ms.ullAvailPhys) / 1073741824.0;
    }
    // Диск C
    ULARGE_INTEGER freeBytes;
    if (GetDiskFreeSpaceExW(L"C:\\", &freeBytes, nullptr, nullptr))
        m_diskFree = freeBytes.QuadPart / 1073741824.0;
    // Видеокарта
    if (m_gpuReady && m_query && PdhCollectQueryData(static_cast<PDH_HQUERY>(m_query)) == ERROR_SUCCESS)
    {
        DWORD size = 0, count = 0;
        PdhGetFormattedCounterArrayW(static_cast<PDH_HCOUNTER>(m_gpuCounter), PDH_FMT_DOUBLE, &size, &count, nullptr);
        if (size)
        {
            std::vector<BYTE> buf(size);
            auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buf.data());
            if (PdhGetFormattedCounterArrayW(static_cast<PDH_HCOUNTER>(m_gpuCounter), PDH_FMT_DOUBLE, &size, &count, items) == ERROR_SUCCESS)
            {
                double sum = 0;
                for (DWORD k = 0; k < count; ++k)
                    if (items[k].FmtValue.CStatus == ERROR_SUCCESS)
                        sum += items[k].FmtValue.doubleValue;
                m_gpu = qBound(0, int(sum + 0.5), 100);
            }
        }
    }
}

QString SystemStats::line() const
{
    const QLocale ru = Lang::locale();
    QStringList parts;
    if (m_cpu >= 0)
        parts << TXT("ЦП %1%").arg(qBound(0, m_cpu, 100));
    if (m_ramTotal > 0)
        parts << TXT("ОЗУ %1 / %2 ГБ").arg(ru.toString(m_ramUsed, 'f', 1), ru.toString(m_ramTotal, 'f', 0));
    if (m_gpu >= 0)
        parts << TXT("ГП %1%").arg(m_gpu);
    if (m_diskFree >= 0)
        parts << TXT("C: %1 ГБ свободно").arg(qRound(m_diskFree));
    return parts.join(QStringLiteral("   ·   "));
}
