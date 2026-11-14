#ifndef _CRASH_REPORT_H_
#define _CRASH_REPORT_H_

// Writes a text report when the program dies of an access violation and returns:
// the module and offset of the faulting instruction, the register state and a
// stack walk with symbols when the debug information is available.  Without a
// report the front-ends only produce "it crashed", which is not enough to fix it.
//
// The report goes to a per-user writable directory and to stderr, which is where
// a console build shows it:
//   Windows:     %LOCALAPPDATA%\Imagina\Imagina-crash.txt
//   Linux/BSD:   $XDG_STATE_HOME/imagina/Imagina-crash.txt,
//                or ~/.local/state/imagina/Imagina-crash.txt
// When that directory cannot be created or written (an installed copy below
// Program Files, /usr/bin or /app, for example) the report falls back to the
// temporary directory.  Returns false when the handler cannot be installed.
bool InstallCrashHandler(const char *LogFileName);

#endif
