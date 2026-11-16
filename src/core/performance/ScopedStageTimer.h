#pragma once
#include <QElapsedTimer>
#include "IPerformanceMonitor.h"
#include "PerformanceTypes.h"
// 作用域计时器，退出阶段作用域后自动结束计时、返回时间
class ScopedStageTimer final {
public:

    // 禁用拷贝移动
    ScopedStageTimer( const ScopedStageTimer& ) = delete;
    ScopedStageTimer& operator=( const ScopedStageTimer& ) = delete;
    ScopedStageTimer( ScopedStageTimer&& ) = delete;
    ScopedStageTimer& operator=( ScopedStageTimer&& ) = delete;
    explicit ScopedStageTimer(
        IPerformanceMonitor* monitor,
        PerformanceStage stage ) noexcept
        : m_monitor( monitor ),
        m_stage( stage )
    {
        // 设置配置的级别为off后将其置为空，不进行记录耗时
        if (m_monitor && !m_monitor->isEnabled())
            m_monitor = nullptr;
        // 阶段被过滤，跳过计时
        if (m_monitor && !m_monitor->isStageEnabled( m_stage ))
            m_monitor = nullptr;

        if (m_monitor) {
            m_active = true;
            m_timer.start();
        }
    }

    ~ScopedStageTimer() noexcept {
        if (m_active) {
            const qint64 elapsedNs = m_timer.nsecsElapsed();
            m_monitor->recordStage( m_stage, elapsedNs );  // 退出时自动记录耗时
        }
    }

private:
    IPerformanceMonitor* m_monitor;
    PerformanceStage m_stage;
    bool m_active = false;	// 当前的阶段是否激活了
    QElapsedTimer m_timer;
};
