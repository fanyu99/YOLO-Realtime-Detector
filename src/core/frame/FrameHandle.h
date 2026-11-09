#pragma once
#include <cstdint>
#include <memory>
#include <QString>
// 内存类型
enum class MemoryType {
	CPU = 0,
	GPU
};

// 像素格式：
enum class PixelFormat {
	BGR24,  
	BGRA,
	RGB24,
	GRAY,
	NV12,
	YUV420P,
	Other
};
struct PlaneDescriptor{
	const uint8_t* data{ nullptr }; // plane数据指针
	int stride = -1;
	int width = -1;
	int height = -1;
};

// 帧句柄FrameHandle，分为CPU、GPU
// 提供宽高、像素格式、内存类型、plane/stride帧序号、生命周期内部管理
class FrameHandle {
public:
	FrameHandle() = default;
	virtual ~FrameHandle() = default;
	virtual void setFrameIndex( int index ) = 0;
	virtual int width() const = 0;
	virtual int height() const = 0;
	virtual int channels() const = 0;
	virtual int stride() const = 0;
	virtual int frameIndex()const=0;
	virtual PixelFormat pixelFormat() const = 0;
	virtual PlaneDescriptor plane( int planeIndex )const = 0;
	virtual MemoryType memoryType()const = 0;	// 内存类型,CPU/GPU

};
using FrameHandlePtr = std::shared_ptr<FrameHandle>;


