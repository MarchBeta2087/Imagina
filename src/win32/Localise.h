#ifndef _LOCALISE_H_
#define _LOCALISE_H_

#include <windows.h>

#include <string>

// The Win32 front-end builds its menus and dialog boxes from English literals in
// UTF-16, so it needs the translations of Language.h in wide form.

// UTF-16 version of Tr().
std::wstring TrW(const char *English);

// Replaces the text of every item of a menu, and of its submenus, with the
// translation of that text.  A "\tCtrl+O" accelerator is kept as it is.
void LocaliseMenu(HMENU Menu);

// Replaces the title of a window and the text of every control inside it with the
// translations of those texts.  Controls without a translation keep their text,
// which is what makes this a single call per dialog.
void LocaliseWindow(HWND Window);

#endif
