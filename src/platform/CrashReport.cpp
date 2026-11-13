#include "CrashReport.h"

#if defined(_WIN32)

#include <windows.h>
#include <dbghelp.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

std::string LogPath;
FILE *Log = nullptr;

void Write(const char *Format, ...) {
	va_list Arguments;
	va_start(Arguments, Format);
	if (Log) vfprintf(Log, Format, Arguments);
	va_end(Arguments);
	if (Log) fputc('\n', Log);

	va_start(Arguments, Format);
	vfprintf(stderr, Format, Arguments);
	va_end(Arguments);
	fputc('\n', stderr);
}

LONG WINAPI Handler(EXCEPTION_POINTERS *Info) {
	Log = fopen(LogPath.c_str(), "w");

	EXCEPTION_RECORD *Record = Info->ExceptionRecord;
	uintptr_t Address = uintptr_t(Record->ExceptionAddress);

	HMODULE Module = nullptr;
	char ModuleName[MAX_PATH] = "?";
	if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			(LPCSTR)Address, &Module)) {
		GetModuleFileNameA(Module, ModuleName, sizeof(ModuleName));
	}

	Write("Imagina crash report");
	Write("  exception code %08lx at %p", (unsigned long)Record->ExceptionCode, (void *)Address);
	Write("  module %s+0x%llx", ModuleName, (unsigned long long)(Address - uintptr_t(Module)));

	CONTEXT *Context = Info->ContextRecord;
	Write("  rip=%p rsp=%p rbp=%p", (void *)Context->Rip, (void *)Context->Rsp, (void *)Context->Rbp);

	if (Record->NumberParameters >= 2) {
		uintptr_t FaultAddress = uintptr_t(Record->ExceptionInformation[1]);
		MEMORY_BASIC_INFORMATION Region = {};
		if (VirtualQuery((LPCVOID)FaultAddress, &Region, sizeof(Region))) {
			Write("  fault at %p state=0x%lx protect=0x%lx type=0x%lx base=%p size=0x%llx", (void *)FaultAddress,
				(unsigned long)Region.State, (unsigned long)Region.Protect, (unsigned long)Region.Type,
				Region.BaseAddress, (unsigned long long)Region.RegionSize);
		}
		NT_TIB *Teb = (NT_TIB *)NtCurrentTeb();
		Write("  stack base=%p limit=%p, fault is %s the stack", (void *)Teb->StackBase, (void *)Teb->StackLimit,
			FaultAddress < uintptr_t(Teb->StackLimit) ? "below" : "inside");
	}

	Write("  stack:");
	HANDLE Process = GetCurrentProcess();
	HANDLE Thread = GetCurrentThread();
	SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
	if (SymInitialize(Process, nullptr, TRUE)) {
		CONTEXT Walk = *Info->ContextRecord;
		STACKFRAME64 Frame = {};
		Frame.AddrPC.Offset = Walk.Rip;
		Frame.AddrPC.Mode = AddrModeFlat;
		Frame.AddrFrame.Offset = Walk.Rbp;
		Frame.AddrFrame.Mode = AddrModeFlat;
		Frame.AddrStack.Offset = Walk.Rsp;
		Frame.AddrStack.Mode = AddrModeFlat;

		for (int i = 0; i < 32; i++) {
			if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, Process, Thread, &Frame, &Walk, nullptr,
					SymFunctionTableAccess64, SymGetModuleBase64, nullptr)) {
				break;
			}
			if (!Frame.AddrPC.Offset) break;

			char SymbolBuffer[sizeof(SYMBOL_INFO) + 256] = {};
			SYMBOL_INFO *Symbol = (SYMBOL_INFO *)SymbolBuffer;
			Symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
			Symbol->MaxNameLen = 255;
			DWORD64 Displacement = 0;
			const char *Name = "<unknown>";
			if (SymFromAddr(Process, Frame.AddrPC.Offset, &Displacement, Symbol)) Name = Symbol->Name;

			IMAGEHLP_LINE64 Line = {};
			Line.SizeOfStruct = sizeof(Line);
			DWORD LineDisplacement = 0;
			if (SymGetLineFromAddr64(Process, Frame.AddrPC.Offset, &LineDisplacement, &Line)) {
				Write("    #%02d %s+0x%llx  %s:%lu", i, Name, (unsigned long long)Displacement, Line.FileName,
					(unsigned long)Line.LineNumber);
			} else {
				Write("    #%02d %s+0x%llx  %p", i, Name, (unsigned long long)Displacement, (void *)Frame.AddrPC.Offset);
			}
		}
		SymCleanup(Process);
	}

	if (Log) {
		fflush(Log);
		fclose(Log);
		Log = nullptr;
	}

	// A window application has no console, so tell the user where the report is.
	char Message[MAX_PATH + 128];
	snprintf(Message, sizeof Message, "Imagina stopped after an unexpected error.\n\nThe stack trace was written to:\n%s",
		LogPath.c_str());
	MessageBoxA(nullptr, Message, "Imagina", MB_OK | MB_ICONERROR);

	return EXCEPTION_EXECUTE_HANDLER;
}

} // namespace

bool InstallCrashHandler(const char *LogFileName) {
	char Base[MAX_PATH] = {};
	if (!GetModuleFileNameA(nullptr, Base, sizeof(Base))) return false;
	char *Slash = strrchr(Base, '\\');
	if (Slash) *(Slash + 1) = '\0';
	LogPath = std::string(Base) + (LogFileName ? LogFileName : "Imagina-crash.txt");

	SetUnhandledExceptionFilter(Handler);
	return true;
}

#else

bool InstallCrashHandler(const char *LogFileName) {
	(void)LogFileName;
	return false;
}

#endif
