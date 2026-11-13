#ifndef _LANGUAGE_H_
#define _LANGUAGE_H_

#include <string_view>

// User interface languages.  The strings the front-ends show are written in
// English in the source; Tr() looks up the translation of the current language
// and returns the English text when there is none, so a missing entry degrades
// to English instead of an empty label.
enum class Language {
	English,
	Chinese,
};

Language CurrentLanguage();
void SetLanguage(Language Language);

// "en", "zh-CN"; the codes --lang accepts.
const char *LanguageCode(Language Language);
bool LanguageFromCode(std::string_view Code, Language &Result);

// The language the system prefers, English when that is not one we have.
Language DetectSystemLanguage();

// Translation lookup; the argument is the English text used in the source.
const char *Tr(const char *English);

#endif
