#pragma once

// Crash handling for every Wayland 95 program (the compositor and its apps).
//
// installCrashHandler("win95files") at the top of main():
//  - when the program crashes (SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL), it
//    writes a problem report to ~/.local/state/wayland95/crashes/:
//    the program, version, signal, a backtrace (function names where the
//    binary has them, and module+offset for addr2line with the debug
//    symbols), and the last messages it logged. Then it dies of the same
//    signal as before, so core dumps and exit statuses are unchanged.
//  - its log messages get the time in front, and a message repeated over
//    and over is written once with a count instead of thousands of times.
//
// The desktop notices new reports (ProblemReports) and offers to show them.
void installCrashHandler(const char *program);

// where the reports go: $XDG_STATE_HOME/wayland95/crashes
// (~/.local/state/wayland95/crashes)
const char *crashReportDir();
