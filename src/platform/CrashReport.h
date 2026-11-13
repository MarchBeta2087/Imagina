#ifndef _CRASH_REPORT_H_
#define _CRASH_REPORT_H_

// Writes a text report when the program dies of an access violation and returns:
// the module and offset of the faulting instruction, the register state and a
// stack walk with symbols when a PDB is next to the executable.  Without a report
// the two front-ends only produce "it crashed", which is not enough to fix it.
//
// The report goes next to the executable ("Imagina-crash.txt") and to stderr,
// which is where a console build shows it.  Returns false when it cannot be
// installed (non-Windows builds).
bool InstallCrashHandler(const char *LogFileName);

#endif
