#pragma once
#ifdef YOLO_HAVE_CUDA

#include <cuda_runtime.h>
#include <QtGlobal>
#include <string_view>
#include "IPerformanceMonitor.h"
#include "PerformanceTypes.h"
#include "TimeBases.h"

// 作用域计时器，CUDA内部的kernel计时
class CudaStageTimer final {
public:
	// 可以选用出参，出现错误返回-1
	explicit CudaStageTimer ( cudaStream_t stream, IPerformanceMonitor* monitor,
		PerformanceStage stage, qint64* scopeTime = nullptr ) noexcept : m_stream ( stream )
		, m_monitor ( monitor )
		, m_stage ( stage )
		, m_scopeTime ( scopeTime )
	{
		if (m_scopeTime)
			*m_scopeTime = -1;
		if (cudaEventCreateWithFlags ( &m_start, cudaEventDefault ) != cudaSuccess) {
			fail ( "[CudaStageTimer]: 创建CUDA事件失败(start)" );
			return;
		}
		if (cudaEventCreateWithFlags ( &m_stop, cudaEventDefault ) != cudaSuccess) {
			fail ( "[CudaStageTimer]: 创建CUDA事件失败(stop)" );
			return;
		}
		m_eventsReady = true;
	}
	~CudaStageTimer () noexcept {
		if (m_active)
			markStop ();
		if (m_stopped)
			waitForResult ();
		if (m_start)
			cudaEventDestroy ( m_start );
		if (m_stop)
			cudaEventDestroy ( m_stop );
	}

	CudaStageTimer ( const CudaStageTimer& ) = delete;
	CudaStageTimer& operator= ( const CudaStageTimer& ) = delete;
	CudaStageTimer ( CudaStageTimer&& ) = delete;
	CudaStageTimer& operator= ( CudaStageTimer&& ) = delete;
	// 记录起点
	void begin () noexcept {
		m_active = false;
		m_stopped = false;
		if (!m_eventsReady || !m_stream)
			return;
		m_error = {};
		const bool monitorActive = m_monitor && m_monitor->isEnabled ()
			&& m_monitor->isStageEnabled ( m_stage );
		// 如果没开启监控且没有出参，那么直接退出
		if (!monitorActive && !m_scopeTime)
			return;
		if (m_scopeTime)
			*m_scopeTime = 0;
		if (cudaEventRecord ( m_start, m_stream ) != cudaSuccess) {
			fail ( "[CudaStageTimer]: 记录CUDA事件失败(start)" );
			return;
		}
		m_active = true;
	}
	// 设置终点（非阻塞）
	void markStop () noexcept {
		if (!m_active || m_stopped)
			return;
		if (cudaEventRecord ( m_stop, m_stream ) != cudaSuccess) {
			fail ( "[CudaStageTimer]: 记录CUDA事件失败(stop)" );
			m_active = false;
			return;
		}
		m_stopped = true;
	}
	// 等待终点完成后计算返回结果（阻塞）
	qint64 waitForResult () noexcept {
		if (!m_stopped)
			return -1;
		m_stopped = false;
		m_active = false;
		// 进行同步等待结果完成
		if (cudaEventSynchronize ( m_stop ) != cudaSuccess) {
			fail ( "[CudaStageTimer]: 同步CUDA事件失败(stop)" );
			return -1;
		}
		float ms = 0.0f;
		// 计算耗时
		if (cudaEventElapsedTime ( &ms, m_start, m_stop ) != cudaSuccess) {
			fail ( "[CudaStageTimer]: 计算CUDA事件时间失败" );
			return -1;
		}
		const qint64 ns = static_cast<qint64>(static_cast<double>(ms) * static_cast<double>(TimeBases::Ms2Ns));
		if (m_monitor)
			m_monitor->recordStage ( m_stage, static_cast<quint64>(ns) );
		if (m_scopeTime)
			*m_scopeTime = ns;
		return ns;
	}
	// 设置终点并等待完成返回结果
	qint64 end () noexcept {
		markStop ();
		return waitForResult ();
	}
	 // 返回错误
	const std::string_view& error () const noexcept { return m_error; }

private:
	// 设置错误并设置出参结果为-1
	void fail ( std::string_view reason ) noexcept {
		if (m_error.empty ())
			m_error = reason;
		if (m_scopeTime)
			*m_scopeTime = -1;
	}

	cudaStream_t m_stream = nullptr;	// 借用统一的 stream指针
	IPerformanceMonitor* m_monitor = nullptr;
	PerformanceStage m_stage;
	qint64* m_scopeTime = nullptr;		// 出参，返回阶段计时， ns
	cudaEvent_t m_start = nullptr;
	cudaEvent_t m_stop = nullptr;
	bool m_eventsReady = false;
	bool m_active = false;	// 当前是否已经有起点
	bool m_stopped = false;				// 当前是否已经关闭了计时
	std::string_view m_error;
};
#endif
