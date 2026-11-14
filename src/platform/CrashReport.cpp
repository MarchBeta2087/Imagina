#include "CrashReport.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>

#if defined(_WIN32)

#include <windows.h>
#include <dbghelp.h>
// Includes.h defines "unused" as [[maybe_unused]], which breaks the member of
// the HPSXA handle declared by shlobj.h.
#pragma push_macro("unused")
#undef unused
#include <shlobj.h>
#pragma pop_macro("unused")

namespace {

std::wstring LogPath;
std::wstring LogFileName = L"Imagina-crash.txt";
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

std::string Narrow(const std::wstring &Text) {
	if (Text.empty()) return {};
	int Length = WideCharToMultiByte(CP_UTF8, 0, Text.c_str(), int(Text.size()), nullptr, 0, nullptr, nullptr);
	if (Length <= 0) return {};
	std::string Result(size_t(Length), '\0');
	WideCharToMultiByte(CP_UTF8, 0, Text.c_str(), int(Text.size()), Result.data(), Length, nullptr, nullptr);
	return Result;
}

// Creates the directory and checks that a file can really be written there: an
// installed copy lives below Program Files, where the executable's own folder is
// read-only.
bool PrepareDirectory(const std::wstring &Directory) {
	std::error_code Error;
	std::filesystem::create_directories(std::filesystem::path(Directory), Error);
	if (Error) return false;

	std::wstring Probe = Directory + L"\\.imagina-write-test";
	FILE *File = _wfopen(Probe.c_str(), L"w");
	if (!File) return false;
	fclose(File);
	_wremove(Probe.c_str());
	return true;
}

std::wstring LocalAppDataDirectory() {
	PWSTR Known = nullptr;
	if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &Known))) {
		std::wstring Result(Known);
		CoTaskMemFree(Known);
		return Result + L"\\Imagina";
	}

	wchar_t Buffer[MAX_PATH] = {};
	DWORD Length = GetEnvironmentVariableW(L"LOCALAPPDATA", Buffer, DWORD(std::size(Buffer)));
	if (Length > 0 && Length < std::size(Buffer)) return std::wstring(Buffer) + L"\\Imagina";
	return {};
}

std::wstring TempDirectory() {
	wchar_t Buffer[MAX_PATH + 1] = {};
	DWORD Length = GetTempPathW(DWORD(std::size(Buffer)), Buffer);
	if (Length > 0 && Length < std::size(Buffer)) {
		std::wstring Result = Buffer;
		while (!Result.empty() && (Result.back() == L'\\' || Result.back() == L'/')) Result.pop_back();
		return Result;
	}
	return {};
}

LONG WINAPI Handler(EXCEPTION_POINTERS *Info) {
	if (!LogPath.empty()) Log = _wfopen(LogPath.c_str(), L"w");
	if (!Log) {
		// The user directory can disappear or become read-only between install and
		// crash; %TEMP% is the last place that is normally writable.
		std::wstring Temp = TempDirectory();
		if (!Temp.empty() && !LogFileName.empty()) {
			LogPath = Temp + L"\\" + LogFileName;
			Log = _wfopen(LogPath.c_str(), L"w");
		}
	}

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

	bool Written = Log != nullptr;
	if (Log) {
		fflush(Log);
		fclose(Log);
		Log = nullptr;
	}

	// A window application has no console, so tell the user where the report is.
	// The path is part of the message: "where is the report" is otherwise the
	// first follow-up question.
	wchar_t Message[2048];
	if (Written) {
		swprintf(Message, std::size(Message), L"Imagina stopped after an unexpected error.\n\nThe stack trace was written to:\n%s",
			LogPath.c_str());
	} else {
		swprintf(Message, std::size(Message),
			L"Imagina stopped after an unexpected error.\n\nThe crash report could not be written; the stack trace was "
			L"printed to stderr.");
	}
	MessageBoxW(nullptr, Message, L"Imagina", MB_OK | MB_ICONERROR);

	return EXCEPTION_EXECUTE_HANDLER;
}

} // namespace

bool InstallCrashHandler(const char *LogFileNameArgument) {
	if (LogFileNameArgument && *LogFileNameArgument) {
		wchar_t Buffer[260];
		if (MultiByteToWideChar(CP_UTF8, 0, LogFileNameArgument, -1, Buffer, int(std::size(Buffer)))) {
			LogFileName = Buffer;
		}
	}

	std::wstring Directory = LocalAppDataDirectory();
	if (Directory.empty() || !PrepareDirectory(Directory)) {
		Directory = TempDirectory();
		if (Directory.empty() || !PrepareDirectory(Directory)) Directory.clear();
	}

	if (Directory.empty()) {
		fprintf(stderr, "Imagina: no writable directory for the crash report; it will go to stderr only\n");
	} else {
		LogPath = Directory + L"\\" + LogFileName;
		fprintf(stderr, "Imagina: crash reports will be written to %s\n", Narrow(LogPath).c_str());
	}

	SetUnhandledExceptionFilter(Handler);
	return true;
}

#else

#include <csignal>
#include <fcntl.h>
#include <unistd.h>

#if defined(__has_include)
#if __has_include(<execinfo.h>)
#include <execinfo.h>
#define IMAGINA_HAVE_BACKTRACE 1
#endif
#endif

namespace {

char LogPath[4096] = {};
char FallbackPath[4096] = {};
volatile sig_atomic_t InHandler = 0;

bool PrepareDirectory(const std::filesystem::path &Directory) {
	std::error_code Error;
	std::filesystem::create_directories(Directory, Error);
	if (Error) return false;

	std::filesystem::path Probe = Directory / ".imagina-write-test";
	FILE *File = fopen(Probe.c_str(), "w");
	if (!File) return false;
	fclose(File);
	std::filesystem::remove(Probe, Error);
	return true;
}

std::filesystem::path StateDirectory() {
	if (const char *Xdg = getenv("XDG_STATE_HOME"); Xdg && Xdg[0] == '/') return std::filesystem::path(Xdg) / "imagina";
	if (const char *Home = getenv("HOME"); Home && *Home) return std::filesystem::path(Home) / ".local" / "state" / "imagina";
	return {};
}

std::filesystem::path TempDirectory() {
	const char *Temp = getenv("TMPDIR");
	if (!Temp || !*Temp) Temp = "/tmp";
	return std::filesystem::path(Temp);
}

void Write(int File, const char *Text) {
	size_t Length = strlen(Text);
	while (Length > 0) {
		ssize_t Done = write(File, Text, Length);
		if (Done <= 0) break;
		Text += Done;
		Length -= size_t(Done);
	}
}

void WritePath(int File, const char *Prefix, const char *Path) {
	Write(File, Prefix);
	Write(File, Path);
	Write(File, "\n");
}

void Handler(int Signal) {
	if (InHandler) _exit(128 + Signal);
	InHandler = 1;

	const char *Path = nullptr;
	int File = -1;
	if (LogPath[0]) {
		File = open(LogPath, O_WRONLY | O_CREAT | O_TRUNC, 0600);
		if (File >= 0) Path = LogPath;
	}
	if (File < 0 && FallbackPath[0]) {
		File = open(FallbackPath, O_WRONLY | O_CREAT | O_TRUNC, 0600);
		if (File >= 0) Path = FallbackPath;
	}

	char Header[128];
	snprintf(Header, sizeof(Header), "Imagina crash report\n  signal %d\n  stack:\n", Signal);
	Write(STDERR_FILENO, Header);
	if (Path) WritePath(STDERR_FILENO, "  report: ", Path);
	if (File >= 0) {
		Write(File, Header);
		WritePath(File, "  report: ", Path);
	}

#if defined(IMAGINA_HAVE_BACKTRACE)
	void *Frames[64];
	int Count = backtrace(Frames, 64);
	backtrace_symbols_fd(Frames, Count, STDERR_FILENO);
	if (File >= 0) backtrace_symbols_fd(Frames, Count, File);
#endif

	if (File >= 0) close(File);

	_exit(128 + Signal);
}

} // namespace

bool InstallCrashHandler(const char *LogFileName) {
	std::filesystem::path Directory = StateDirectory();
	if (Directory.empty() || !PrepareDirectory(Directory)) {
		Directory = TempDirectory();
		if (!PrepareDirectory(Directory)) Directory.clear();
	}

	if (!Directory.empty()) {
		snprintf(LogPath, sizeof(LogPath), "%s", (Directory / (LogFileName ? LogFileName : "Imagina-crash.txt")).c_str());
		fprintf(stderr, "Imagina: crash reports will be written to %s\n", LogPath);
	} else {
		fprintf(stderr, "Imagina: no writable directory for the crash report; the stack will go to stderr only\n");
	}

	std::filesystem::path Temp = TempDirectory();
	if (!Temp.empty()) {
		snprintf(FallbackPath, sizeof(FallbackPath), "%s", (Temp / (LogFileName ? LogFileName : "Imagina-crash.txt")).c_str());
	}

	struct sigaction Action = {};
	Action.sa_handler = Handler;
	sigemptyset(&Action.sa_mask);
	Action.sa_flags = SA_RESETHAND;
	for (int Signal : {SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT}) sigaction(Signal, &Action, nullptr);
	return true;
}

#endif
