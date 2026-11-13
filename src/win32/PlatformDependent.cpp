#include "PlatformDependent.h"
#include "Platform.h"
#include "MainWindow.h"

#if __has_include(<Windows.h>)
#include <Windows.h>
#else
#include <windows.h>
#endif

#include <CommDlg.h>
#include <cstring>
#include <string>
#include <vector>

void *PlatformGetGLProcAddress(const char *Name) {
	void *Address = (void *)wglGetProcAddress(Name);
	if (!Address) {
		static HMODULE OpenGL = LoadLibraryA("opengl32.dll");
		if (OpenGL) Address = (void *)GetProcAddress(OpenGL, Name);
	}
	return Address;
}

void PlatformPresent() {
	SwapBuffers(MainDC);
}

void PlatformKeepDisplayOn(bool KeepOn) {
	SetThreadExecutionState(KeepOn ? ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED : ES_CONTINUOUS);
}

void SetWorkerPriority() {
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_LOWEST);
}

void ErrorMessage(const char *Message) {
	MessageBoxA(nullptr, Message, nullptr, MB_TASKMODAL | MB_OK);
}

void ErrorMessage(const char *Title, const char *Message) {
	MessageBoxA(nullptr, Message, Title, MB_TASKMODAL | MB_OK);
}

namespace {

// "Description(*.im)|*.im|..." -> wide filter string with the double NUL the
// common dialogs expect.
std::wstring WidenFilter(const char *Filters) {
	std::wstring Result;
	if (!Filters) {
		Result.push_back(L'\0');
		Result.push_back(L'\0');
		return Result;
	}
	std::string Narrow = Filters;
	for (size_t Start = 0; Start < Narrow.size();) {
		size_t Next = Narrow.find('|', Start);
		if (Next == std::string::npos) Next = Narrow.size();
		int Length = MultiByteToWideChar(CP_UTF8, 0, Narrow.data() + Start, int(Next - Start), nullptr, 0);
		std::wstring Part(size_t(Length), L'\0');
		if (Length) MultiByteToWideChar(CP_UTF8, 0, Narrow.data() + Start, int(Next - Start), Part.data(), Length);
		Result += Part;
		Result.push_back(L'\0');
		Start = Next + 1;
	}
	Result.push_back(L'\0');
	return Result;
}

std::string Narrow(const wchar_t *Text, size_t Length) {
	int Size = WideCharToMultiByte(CP_UTF8, 0, Text, int(Length), nullptr, 0, nullptr, nullptr);
	std::string Result(size_t(Size), '\0');
	if (Size) WideCharToMultiByte(CP_UTF8, 0, Text, int(Length), Result.data(), Size, nullptr, nullptr);
	return Result;
}

bool ShowFileDialog(bool Save, const char *Title, const char *Filters, std::string &Path, int &FilterIndex) {
	std::wstring Filter = WidenFilter(Filters);
	std::wstring TitleWide;
	if (Title) {
		int Length = MultiByteToWideChar(CP_UTF8, 0, Title, -1, nullptr, 0);
		TitleWide.resize(size_t(Length));
		if (Length) MultiByteToWideChar(CP_UTF8, 0, Title, -1, TitleWide.data(), Length);
		TitleWide.resize(TitleWide.find(L'\0'));
	}

	std::vector<wchar_t> Buffer(32768, L'\0');
	OPENFILENAMEW OpenFileName = {};
	OpenFileName.lStructSize = sizeof(OpenFileName);
	OpenFileName.hwndOwner = nullptr;
	OpenFileName.lpstrFilter = Filter.c_str();
	OpenFileName.nFilterIndex = DWORD(FilterIndex > 0 ? FilterIndex : 1);
	OpenFileName.lpstrFile = Buffer.data();
	OpenFileName.nMaxFile = DWORD(Buffer.size());
	OpenFileName.lpstrTitle = TitleWide.empty() ? nullptr : TitleWide.c_str();
	OpenFileName.Flags = OFN_EXPLORER | (Save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);

	BOOL Result = Save ? GetSaveFileNameW(&OpenFileName) : GetOpenFileNameW(&OpenFileName);
	if (!Result) return false;

	FilterIndex = int(OpenFileName.nFilterIndex);
	Path = Narrow(Buffer.data(), wcslen(Buffer.data()));
	return true;
}

} // namespace

bool PlatformOpenFileDialog(const char *Title, const char *Filters, std::string &Path, int &FilterIndex) {
	return ShowFileDialog(false, Title, Filters, Path, FilterIndex);
}

bool PlatformSaveFileDialog(const char *Title, const char *Filters, std::string &Path, int &FilterIndex) {
	return ShowFileDialog(true, Title, Filters, Path, FilterIndex);
}
