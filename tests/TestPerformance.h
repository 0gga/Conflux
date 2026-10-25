#pragma once
#include <QDebug>
#include <QtGlobal>
namespace test_performance {
// Counters, return values, I/O limits and event-loop progress are always checked.
// GCC -O0/atomic profiling overhead is not an optimized desktop latency result.
inline bool releaseBudgetMet(qint64 milliseconds, qint64 limit) {
#ifdef CONFLUX_COVERAGE_BUILD
    qInfo() << "Instrumented elapsed ms" << milliseconds << "(Release-only budget ms" << limit << ')';
    return true;
#else
    return milliseconds < limit;
#endif
}
inline int completionTimeout(int milliseconds) {
#ifdef CONFLUX_COVERAGE_BUILD
    return qMax(milliseconds, 30000);
#else
    return milliseconds;
#endif
}
} // namespace test_performance
