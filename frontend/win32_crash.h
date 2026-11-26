#pragma once

// A crash leaves a minidump: an unhandled exception on any thread (SetUnhandledExceptionFilter),
// std::terminate (an exception out of a thread or a noexcept function) and abort() each write
// crashes\eqclient_gui-<date>-<time>.dmp next to the exe through dbghelp.dll's MiniDumpWriteDump,
// say where in my-client.log (through `log`) and in a message box, and end the process.
// Visual Studio or WinDbg opens the dump with the exe and its .pdb (RelWithDebInfo).

#include <functional>
#include <string>

void InstallCrashHandler(const std::wstring &dump_dir, std::function<void(const std::string &)> log);
