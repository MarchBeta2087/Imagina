#include "Platform.h"
#include "PlatformDependent.h"

#ifdef _WIN32
#if __has_include(<Windows.h>)
#include <Windows.h>
#else
#include <windows.h>
#endif
#endif

uintptr_t PlatformBeginWorkerTask(size_t ThreadID, size_t ThreadCount, size_t HardwareConcurrency) {
#ifdef _WIN32
	// A cooperative task that only needs a few threads pins itself to a pair of
	// cores and raises its priority, which helps the latency of the reference
	// computation.  Wider tasks keep the normal priority.
	if (ThreadCount <= HardwareConcurrency / 2) {
		SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
		DWORD_PTR AffinityMask = DWORD_PTR(0b11) << (ThreadID * 2);
		if (AffinityMask) return uintptr_t(SetThreadAffinityMask(GetCurrentThread(), AffinityMask));
	} else {
		SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);
	}
	return 0;
#else
	// No portable way to pin a thread; the priority of the pool is lowered once
	// in SetWorkerPriority() instead.
	(void)ThreadID;
	(void)ThreadCount;
	(void)HardwareConcurrency;
	return 0;
#endif
}

void PlatformEndWorkerTask(uintptr_t PreviousState) {
#ifdef _WIN32
	if (PreviousState) SetThreadAffinityMask(GetCurrentThread(), DWORD_PTR(PreviousState));
#else
	(void)PreviousState;
#endif
	SetWorkerPriority();
}
