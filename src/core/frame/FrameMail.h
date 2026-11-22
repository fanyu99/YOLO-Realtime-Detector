#pragma once
#include <atomic>
#include <memory>
#include <mutex>
#include <utility>
#include "FrameSnapshot.h"

// 邮箱，只存最新的快照
// Worker借用指针进行post
// App保证生命周期并调用take取走快照指针
class FrameMail {
public:
	explicit FrameMail () = default; 
	~FrameMail () = default;
	// 发送快照，覆盖旧快照，Worker调用发送到App
	void post ( FrameSnapshotPtr snapshot ) {
		FrameSnapshotPtr old;	   // 旧快照
		{
			// 临界区进行交换
			std::lock_guard<std::mutex> lock ( m_mutex );
			old = std::move ( m_snapshot );
			m_snapshot = std::move ( snapshot );
		}
		// 旧快照被覆盖，计数
		if (old)
			m_dropped.fetch_add ( 1, std::memory_order_relaxed );
	}
	// 获取丢弃的帧数
	quint64 dropped ()const { return m_dropped.load ( std::memory_order_relaxed ); }
	// 取出最新的快照（置空）， App在UI线程调用
	FrameSnapshotPtr take () {
		std::lock_guard<std::mutex> lock ( m_mutex );
		return std::exchange ( m_snapshot, nullptr );
	}
	// 判断是否能够进行GPU互操作
	bool isNativeOk () const { return m_nativeOk.load ( std::memory_order_relaxed ); }
	void setNativeOk(bool ok) { m_nativeOk.store ( ok, std::memory_order_relaxed ); }
private:
	mutable std::mutex m_mutex;
	FrameSnapshotPtr m_snapshot{nullptr};   // 最新快照
	std::atomic<quint64> m_dropped{ 0 }; // 丢弃的帧数
	std::atomic_bool m_nativeOk{ false };

};
using 	FrameMailPtr = std::unique_ptr<FrameMail>;