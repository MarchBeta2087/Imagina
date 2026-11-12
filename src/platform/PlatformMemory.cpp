#include "Platform.h"

#include <cstdlib>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <sys/mman.h>
#if __has_include(<sys/sysinfo.h>)
#include <sys/sysinfo.h>
#endif
#endif

void PlatformDebugBreak() {
#if defined(_MSC_VER)
	__debugbreak();
#elif defined(__clang__) || defined(__GNUC__)
	__builtin_trap();
#else
	abort();
#endif
}

void *PlatformReserveMemory(size_t Size) {
#ifdef _WIN32
	return VirtualAlloc(nullptr, Size, MEM_RESERVE, PAGE_READWRITE);
#else
	void *Address = mmap(nullptr, Size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	return Address == MAP_FAILED ? nullptr : Address;
#endif
}

bool PlatformCommitMemory(void *Address, size_t Size) {
#ifdef _WIN32
	return VirtualAlloc(Address, Size, MEM_COMMIT, PAGE_READWRITE) != nullptr;
#else
	return mprotect(Address, Size, PROT_READ | PROT_WRITE) == 0;
#endif
}

void PlatformDecommitMemory(void *Address, size_t Size) {
	if (!Address) return;
#ifdef _WIN32
	VirtualFree(Address, Size, MEM_DECOMMIT);
#else
	madvise(Address, Size, MADV_DONTNEED);
	mprotect(Address, Size, PROT_NONE);
#endif
}

void PlatformReleaseMemory(void *Address, size_t Size) {
	if (!Address) return;
#ifdef _WIN32
	(void)Size;
	VirtualFree(Address, 0, MEM_RELEASE);
#else
	munmap(Address, Size);
#endif
}

size_t PlatformAvailableMemory() {
#ifdef _WIN32
	MEMORYSTATUSEX Status = {};
	Status.dwLength = sizeof(Status);
	GlobalMemoryStatusEx(&Status);
	return size_t(Status.ullTotalPhys);
#else
	long Pages = sysconf(_SC_PHYS_PAGES);
	long PageSize = sysconf(_SC_PAGE_SIZE);
	if (Pages > 0 && PageSize > 0) return size_t(Pages) * size_t(PageSize);
	return size_t(1) << 30;
#endif
}

void *PlatformAllocateExecutableMemory(size_t Size) {
#ifdef _WIN32
	return VirtualAlloc(nullptr, Size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
#else
	void *Address = mmap(nullptr, Size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	return Address == MAP_FAILED ? nullptr : Address;
#endif
}
