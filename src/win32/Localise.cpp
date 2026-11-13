#include "Localise.h"
#include "Language.h"

#include <cstddef>

namespace {

std::wstring Widen(const char *Text) {
	if (!Text || !*Text) return std::wstring();
	int Length = MultiByteToWideChar(CP_UTF8, 0, Text, -1, nullptr, 0);
	if (Length <= 1) return std::wstring();
	std::wstring Result(size_t(Length), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, Text, -1, Result.data(), Length);
	Result.resize(size_t(Length - 1));
	return Result;
}

std::string Narrow(const wchar_t *Text) {
	int Length = WideCharToMultiByte(CP_UTF8, 0, Text, -1, nullptr, 0, nullptr, nullptr);
	if (Length <= 1) return std::string();
	std::string Result(size_t(Length), '\0');
	WideCharToMultiByte(CP_UTF8, 0, Text, -1, Result.data(), Length, nullptr, nullptr);
	Result.resize(size_t(Length - 1));
	return Result;
}

// The accelerator after a tab is not part of the lookup, and an untranslated
// string is returned unchanged so the caller can skip the assignment.
std::wstring Translate(const std::wstring &Text) {
	size_t Tab = Text.find(L'\t');
	std::wstring Label = Text.substr(0, Tab);
	if (Label.empty()) return Text;
	std::string NarrowLabel = Narrow(Label.c_str());
	std::wstring Translated = Widen(Tr(Untranslate(NarrowLabel.c_str())));
	if (Translated == Label) return Text;
	if (Tab == std::wstring::npos) return Translated;
	return Translated + Text.substr(Tab);
}

bool Same(const std::wstring &A, const std::wstring &B) {
	return A == B;
}

void LocaliseChildren(HWND Parent) {
	for (HWND Child = GetWindow(Parent, GW_CHILD); Child; Child = GetWindow(Child, GW_HWNDNEXT)) {
		wchar_t Buffer[1024];
		int Length = GetWindowTextW(Child, Buffer, int(std::size(Buffer)));
		if (Length > 0) {
			std::wstring Text(Buffer, size_t(Length));
			std::wstring Translated = Translate(Text);
			if (!Same(Translated, Text)) SetWindowTextW(Child, Translated.c_str());
		}
		LocaliseChildren(Child);
	}
}

} // namespace

std::wstring TrW(const char *English) {
	return Widen(Tr(English));
}

void LocaliseMenu(HMENU Menu) {
	if (!Menu) return;

	int Count = GetMenuItemCount(Menu);
	for (int i = 0; i < Count; i++) {
		HMENU Sub = GetSubMenu(Menu, i);

		wchar_t Buffer[512];
		int Length = GetMenuStringW(Menu, UINT(i), Buffer, int(std::size(Buffer)), MF_BYPOSITION);
		if (Length > 0) {
			std::wstring Text(Buffer, size_t(Length));
			std::wstring Translated = Translate(Text);
			if (!Same(Translated, Text)) {
				if (Sub) ModifyMenuW(Menu, UINT(i), MF_BYPOSITION | MF_POPUP, (UINT_PTR)Sub, Translated.c_str());
				else ModifyMenuW(Menu, UINT(i), MF_BYPOSITION | MF_STRING, GetMenuItemID(Menu, UINT(i)), Translated.c_str());
			}
		}

		if (Sub) LocaliseMenu(Sub);
	}
}

void LocaliseWindow(HWND Window) {
	if (!Window) return;

	wchar_t Buffer[512];
	int Length = GetWindowTextW(Window, Buffer, int(std::size(Buffer)));
	if (Length > 0) {
		std::wstring Text(Buffer, size_t(Length));
		std::wstring Translated = Translate(Text);
		if (!Same(Translated, Text)) SetWindowTextW(Window, Translated.c_str());
	}

	LocaliseChildren(Window);
}
