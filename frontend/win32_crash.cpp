#include "win32_crash.h"

#include "win32_strings.h"

#include <windows.h>
#include <dbghelp.h>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <exception>
#ifdef _MSC_VER
#include <intrin.h>
#define RETURN_ADDRESS() _ReturnAddress()
#else
#define RETURN_ADDRESS() __builtin_return_address(0)
#endif

namespace
{
	std::wstring g_dump_dir;
	std::function<void(const std::string &)> g_log;
	volatile LONG g_crashing = 0; // the first crash writes; any after it (on another thread) waits

	using MiniDumpWriteDumpFn = BOOL(WINAPI *)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE, PMINIDUMP_EXCEPTION_INFORMATION,
		PMINIDUMP_USER_STREAM_INFORMATION, PMINIDUMP_CALLBACK_INFORMATION);

	// Writes the dump (of `pointers`' thread state when there are some, else of where it's called)
	// and returns its path, empty when it couldn't.
	std::wstring WriteDump(EXCEPTION_POINTERS *pointers)
	{
		HMODULE dbghelp = LoadLibraryW(L"dbghelp.dll");
		auto write = dbghelp ? reinterpret_cast<MiniDumpWriteDumpFn>(
			                       reinterpret_cast<void *>(GetProcAddress(dbghelp, "MiniDumpWriteDump")))
		                     : nullptr;
		if (!write)
			return {};
		CreateDirectoryW(g_dump_dir.c_str(), nullptr);
		SYSTEMTIME now;
		GetLocalTime(&now);
		wchar_t name[64];
		swprintf(name, 64, L"eqclient_gui-%04u%02u%02u-%02u%02u%02u.dmp", now.wYear, now.wMonth, now.wDay, now.wHour,
			now.wMinute, now.wSecond);
		std::wstring path = g_dump_dir + L"\\" + name;
		HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file == INVALID_HANDLE_VALUE)
			return {};
		MINIDUMP_EXCEPTION_INFORMATION info{};
		info.ThreadId = GetCurrentThreadId();
		info.ExceptionPointers = pointers;
		info.ClientPointers = FALSE;
		auto type = static_cast<MINIDUMP_TYPE>(MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithThreadInfo |
		                                       MiniDumpWithUnloadedModules | MiniDumpWithDataSegs);
		BOOL ok = write(GetCurrentProcess(), GetCurrentProcessId(), file, type, pointers ? &info : nullptr, nullptr, nullptr);
		CloseHandle(file);
		if (!ok) {
			DeleteFileW(path.c_str());
			return {};
		}
		return path;
	}

	[[noreturn]] void Crash(const std::string &what, EXCEPTION_POINTERS *pointers)
	{
		if (InterlockedExchange(&g_crashing, 1) != 0)
			for (;;)
				Sleep(INFINITE); // another thread is writing the dump and will end the process
		std::wstring path = WriteDump(pointers);
		std::string line = "crash: " + what + (path.empty() ? std::string(", no dump written") : ", dump " + Narrow(path));
		if (g_log)
			g_log(line);
		std::wstring text = L"my-client crashed (" + Widen(what) + L").\n\n" +
		                    (path.empty() ? std::wstring(L"No crash dump could be written.")
		                                  : L"A crash dump was written to\n" + path);
		MessageBoxW(nullptr, text.c_str(), L"my-client", MB_OK | MB_ICONERROR | MB_TOPMOST);
		TerminateProcess(GetCurrentProcess(), 3);
		for (;;)
			Sleep(INFINITE);
	}

	LONG WINAPI UnhandledException(EXCEPTION_POINTERS *pointers)
	{
		char what[96];
		const EXCEPTION_RECORD *record = pointers ? pointers->ExceptionRecord : nullptr;
		snprintf(what, sizeof(what), "exception 0x%08lx at %p",
			record ? static_cast<unsigned long>(record->ExceptionCode) : 0ul, record ? record->ExceptionAddress : nullptr);
		Crash(what, pointers);
	}

	// The others have no exception record: one raised here gives the dump this thread's state.
	[[noreturn]] void CrashHere(const char *what)
	{
		EXCEPTION_RECORD record{};
		CONTEXT context{};
		RtlCaptureContext(&context);
		record.ExceptionCode = 0xE0000001; // ours: not a real exception
		record.ExceptionAddress = RETURN_ADDRESS();
		EXCEPTION_POINTERS pointers{&record, &context};
		Crash(what, &pointers);
	}

	void Terminate()
	{
		std::string what = "std::terminate";
		if (std::exception_ptr current = std::current_exception()) {
			try {
				std::rethrow_exception(current);
			}
			catch (const std::exception &e) {
				what += std::string(" (") + e.what() + ")";
			}
			catch (...) {
				what += " (an unknown exception)";
			}
		}
		CrashHere(what.c_str());
	}

	void Abort(int) { CrashHere("abort"); }
}

void InstallCrashHandler(const std::wstring &dump_dir, std::function<void(const std::string &)> log)
{
	g_dump_dir = dump_dir;
	g_log = std::move(log);
	SetUnhandledExceptionFilter(UnhandledException);
	std::set_terminate(Terminate);
	std::signal(SIGABRT, Abort);
	_set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT); // no "abnormal program termination" box first
}
