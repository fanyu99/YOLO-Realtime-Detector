#pragma once
// CUDA错误信息
struct CudaKernelError {
	bool hasError = false;
	char message[256] = { 0 };  // 错误信息
};
