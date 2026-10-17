// eqclient_gui: Win32 host for the front end. Owns the window, a 640x480 32-bit
// DIB that FrontEnd draws into, GDI text, and input translation.
//
// Settings come from my-client.ini next to the exe, overridden by the command line:
//   [Paths]   EQDir=C:\TAKP            (folder with eqfeart*.pak and eqtext.txt)
//   [Login]   Server=192.168.0.18:6000
//             User=myname             (remembered from the last login)
//   [Display] Scale=2                 (0 or missing = fit the screen)
//   eqclient_gui.exe --eqdir C:\TAKP --login 192.168.0.18:6000 --scale 2

#include "eqstr.h"
#include "frontend.h"
#include "win32_text.h"

#include <windows.h>
#include <shellapi.h>

#include <cstdio>
#include <memory>
#include <string>

namespace
{
	struct App
	{
		HWND window = nullptr;
		HDC memory_dc = nullptr;
		HBITMAP dib = nullptr;
		HGDIOBJ old_bitmap = nullptr;
		uint32_t *pixels = nullptr;
		int scale = 1;
		int mouse_x = 0, mouse_y = 0;
		bool tracking_mouse = false;
		FrontEnd *frontend = nullptr;
		FILE *log = nullptr;
	};

	App g_app;

	std::wstring ExeDirectory()
	{
		wchar_t path[MAX_PATH] = {};
		GetModuleFileNameW(nullptr, path, MAX_PATH);
		std::wstring dir = path;
		size_t slash = dir.find_last_of(L"\\/");
		return slash == std::wstring::npos ? L"." : dir.substr(0, slash);
	}

	std::wstring IniString(const std::wstring &ini, const wchar_t *section, const wchar_t *key)
	{
		wchar_t buffer[MAX_PATH] = {};
		GetPrivateProfileStringW(section, key, L"", buffer, MAX_PATH, ini.c_str());
		return buffer;
	}

	bool FileExists(const std::wstring &path)
	{
		DWORD attributes = GetFileAttributesW(path.c_str());
		return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
	}

	void Log(const std::string &line)
	{
		OutputDebugStringW((Widen(line) + L"\n").c_str());
		if (g_app.log) {
			fprintf(g_app.log, "%s\n", line.c_str());
			fflush(g_app.log);
		}
	}

	uint64_t NowMs()
	{
		return GetTickCount64();
	}

	void Send(InputEvent event)
	{
		if (g_app.frontend)
			g_app.frontend->OnEvent(event);
	}

	InputEvent MouseEvent(InputEvent::Type type, LPARAM lparam)
	{
		InputEvent event;
		event.type = type;
		event.x = static_cast<short>(LOWORD(lparam)) / g_app.scale;
		event.y = static_cast<short>(HIWORD(lparam)) / g_app.scale;
		g_app.mouse_x = event.x;
		g_app.mouse_y = event.y;
		return event;
	}

	InputEvent KeyEvent(InputEvent::Key key)
	{
		InputEvent event;
		event.type = InputEvent::Type::Key;
		event.key = key;
		event.x = g_app.mouse_x;
		event.y = g_app.mouse_y;
		event.shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
		return event;
	}

	void Present(HDC window_dc)
	{
		SetStretchBltMode(window_dc, COLORONCOLOR); // nearest neighbour keeps the 640x480 art crisp
		StretchBlt(window_dc, 0, 0, FrontEnd::Width * g_app.scale, FrontEnd::Height * g_app.scale, g_app.memory_dc, 0, 0,
			FrontEnd::Width, FrontEnd::Height, SRCCOPY);
	}

	LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
	{
		switch (message) {
		case WM_SETCURSOR:
			if (LOWORD(lparam) == HTCLIENT) {
				SetCursor(nullptr); // the front end draws cursor.bmp itself
				return TRUE;
			}
			break;
		case WM_MOUSEMOVE:
			if (!g_app.tracking_mouse) {
				TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, window, 0};
				g_app.tracking_mouse = TrackMouseEvent(&track) != FALSE;
			}
			if (g_app.frontend)
				g_app.frontend->SetCursorVisible(true);
			Send(MouseEvent(InputEvent::Type::MouseMove, lparam));
			return 0;
		case WM_MOUSELEAVE:
			g_app.tracking_mouse = false;
			if (g_app.frontend)
				g_app.frontend->SetCursorVisible(false);
			return 0;
		case WM_LBUTTONDOWN:
			SetCapture(window);
			Send(MouseEvent(InputEvent::Type::MouseDown, lparam));
			return 0;
		case WM_LBUTTONDBLCLK:
			SetCapture(window);
			Send(MouseEvent(InputEvent::Type::DoubleClick, lparam));
			return 0;
		case WM_LBUTTONUP:
			ReleaseCapture();
			Send(MouseEvent(InputEvent::Type::MouseUp, lparam));
			return 0;
		case WM_MOUSEWHEEL: {
			InputEvent event;
			event.type = InputEvent::Type::Wheel;
			event.wheel = GET_WHEEL_DELTA_WPARAM(wparam) / WHEEL_DELTA;
			event.x = g_app.mouse_x;
			event.y = g_app.mouse_y;
			Send(event);
			return 0;
		}
		case WM_CHAR: {
			InputEvent event;
			event.type = InputEvent::Type::Char;
			event.ch = static_cast<uint32_t>(wparam);
			event.x = g_app.mouse_x;
			event.y = g_app.mouse_y;
			Send(event);
			return 0;
		}
		case WM_KEYDOWN:
			switch (wparam) {
			case VK_RETURN: Send(KeyEvent(InputEvent::Key::Enter)); break;
			case VK_ESCAPE: Send(KeyEvent(InputEvent::Key::Escape)); break;
			case VK_TAB: Send(KeyEvent(InputEvent::Key::Tab)); break;
			case VK_BACK: Send(KeyEvent(InputEvent::Key::Backspace)); break;
			case VK_UP: Send(KeyEvent(InputEvent::Key::Up)); break;
			case VK_DOWN: Send(KeyEvent(InputEvent::Key::Down)); break;
			case VK_PRIOR: Send(KeyEvent(InputEvent::Key::PageUp)); break;
			case VK_NEXT: Send(KeyEvent(InputEvent::Key::PageDown)); break;
			default: break;
			}
			return 0;
		case WM_PAINT: {
			PAINTSTRUCT paint;
			HDC dc = BeginPaint(window, &paint);
			Present(dc);
			EndPaint(window, &paint);
			return 0;
		}
		case WM_ERASEBKGND:
			return 1;
		case WM_DESTROY:
			PostQuitMessage(0);
			return 0;
		default:
			break;
		}
		return DefWindowProcW(window, message, wparam, lparam);
	}

	int PickScale(int requested)
	{
		if (requested > 0)
			return requested;
		RECT work{};
		SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
		int scale = 1;
		while ((scale + 1) * FrontEnd::Width <= (work.right - work.left) * 9 / 10 &&
		       (scale + 1) * FrontEnd::Height <= (work.bottom - work.top) * 9 / 10)
			++scale;
		return scale;
	}

	bool CreateSurface()
	{
		BITMAPINFO info{};
		info.bmiHeader.biSize = sizeof(info.bmiHeader);
		info.bmiHeader.biWidth = FrontEnd::Width;
		info.bmiHeader.biHeight = -FrontEnd::Height; // top-down
		info.bmiHeader.biPlanes = 1;
		info.bmiHeader.biBitCount = 32;
		info.bmiHeader.biCompression = BI_RGB;

		HDC screen = GetDC(nullptr);
		g_app.memory_dc = CreateCompatibleDC(screen);
		ReleaseDC(nullptr, screen);
		void *bits = nullptr;
		g_app.dib = CreateDIBSection(g_app.memory_dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
		if (!g_app.dib || !bits)
			return false;
		g_app.pixels = static_cast<uint32_t *>(bits);
		g_app.old_bitmap = SelectObject(g_app.memory_dc, g_app.dib);
		return true;
	}

	void DestroySurface()
	{
		if (g_app.memory_dc) {
			SelectObject(g_app.memory_dc, g_app.old_bitmap);
			DeleteDC(g_app.memory_dc);
		}
		if (g_app.dib)
			DeleteObject(g_app.dib);
	}

	void Fatal(const std::wstring &text)
	{
		Log(Narrow(text));
		MessageBoxW(nullptr, text.c_str(), L"my-client", MB_OK | MB_ICONERROR);
	}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show)
{
	SetProcessDPIAware(); // the window is sized in real pixels; don't let Windows blur-scale it

	std::wstring exe_dir = ExeDirectory();
	std::wstring ini = exe_dir + L"\\my-client.ini";
	g_app.log = _wfopen((exe_dir + L"\\my-client.log").c_str(), L"w");

	// Settings: ini first, then command line.
	std::wstring eq_dir = IniString(ini, L"Paths", L"EQDir");
	std::wstring login = IniString(ini, L"Login", L"Server");
	std::wstring user = IniString(ini, L"Login", L"User");
	int scale = GetPrivateProfileIntW(L"Display", L"Scale", 0, ini.c_str());

	int argc = 0;
	wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
	for (int i = 1; argv && i + 1 < argc; ++i) {
		std::wstring arg = argv[i];
		if (arg == L"--eqdir")
			eq_dir = argv[++i];
		else if (arg == L"--login")
			login = argv[++i];
		else if (arg == L"--scale")
			scale = _wtoi(argv[++i]);
	}
	if (argv)
		LocalFree(argv);

	if (eq_dir.empty())
		eq_dir = FileExists(L"eqfeart1.pak") ? L"." : exe_dir;

	FrontEndConfig config;
	config.username = Narrow(user);
	if (!login.empty()) {
		std::string value = Narrow(login);
		size_t colon = value.rfind(':');
		config.login_host = value.substr(0, colon);
		if (colon != std::string::npos)
			config.login_port = static_cast<uint16_t>(atoi(value.c_str() + colon + 1));
	}
	Log("EQDir: " + Narrow(eq_dir) + ", login server " + config.login_host + ":" + std::to_string(config.login_port));

	FrontEndArt art;
	std::string error;
	if (!art.Open(Narrow(eq_dir), error)) {
		Fatal(Widen(error) + L"\n\nSet EQDir in " + ini + L" or pass --eqdir.");
		return 1;
	}
	EqText strings;
	if (!strings.Load(Narrow(eq_dir) + "/eqtext.txt")) {
		Fatal(L"Couldn't read eqtext.txt in " + eq_dir);
		return 1;
	}

	// Character select is built from the SIDL UI files, like eqgame.
	std::string ui_dir = Narrow(eq_dir) + "/uifiles/default";
	SidlDefinitions sidl;
	SidlTextures textures(ui_dir);
	GameData ui;
	ui.eq_dir = Narrow(eq_dir);
	std::string sidl_error;
	if (sidl.Load(ui_dir, sidl_error)) {
		ui.definitions = &sidl;
		ui.textures = &textures;
	}
	else {
		ui.error = sidl_error;
		Log("SIDL: " + sidl_error);
	}
	for (const auto &warning : sidl.Warnings())
		Log("SIDL: " + warning);
	EqStr eqstr;
	if (eqstr.Load(Narrow(eq_dir) + "/eqstr_en.txt"))
		ui.eqstr = &eqstr;
	else
		Log("eqstr_en.txt not found; server messages will show string ids");

	if (!CreateSurface()) {
		Fatal(L"Couldn't create the drawing surface.");
		return 1;
	}

	g_app.scale = PickScale(scale);
	WNDCLASSW window_class{};
	window_class.style = CS_DBLCLKS;
	window_class.lpfnWndProc = WindowProc;
	window_class.hInstance = instance;
	window_class.hCursor = nullptr;
	window_class.lpszClassName = L"MyClientFrontEnd";
	RegisterClassW(&window_class);

	DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
	RECT rect{0, 0, FrontEnd::Width * g_app.scale, FrontEnd::Height * g_app.scale};
	AdjustWindowRect(&rect, style, FALSE);
	g_app.window = CreateWindowExW(0, window_class.lpszClassName, L"EverQuest (my-client)", style, CW_USEDEFAULT,
		CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr, instance, nullptr);
	if (!g_app.window) {
		Fatal(L"Couldn't create the window.");
		return 1;
	}

	GdiTextRenderer text(g_app.memory_dc);
	FrontEnd frontend(art, strings, text, config, ui);
	frontend.log = Log;
	g_app.frontend = &frontend;
	ShowWindow(g_app.window, show);

	Canvas canvas(g_app.pixels, FrontEnd::Width, FrontEnd::Height, FrontEnd::Width);
	MSG message{};
	bool running = true;
	while (running) {
		while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
			if (message.message == WM_QUIT) {
				running = false;
				break;
			}
			TranslateMessage(&message);
			DispatchMessageW(&message);
		}
		if (!running)
			break;
		if (frontend.QuitRequested()) {
			DestroyWindow(g_app.window); // WM_DESTROY posts WM_QUIT, which ends the loop
			g_app.window = nullptr;
			running = false;
			continue;
		}

		uint64_t now = NowMs();
		frontend.Update(now);
		frontend.Render(canvas, now);
		HDC dc = GetDC(g_app.window);
		Present(dc);
		ReleaseDC(g_app.window, dc);

		MsgWaitForMultipleObjects(0, nullptr, FALSE, 15, QS_ALLINPUT);
	}

	for (const auto &name : art.Missing())
		Log("missing art: " + name);
	for (const auto &name : textures.Missing())
		Log("missing UI texture: " + name);
	WritePrivateProfileStringW(L"Login", L"User", Widen(frontend.Username()).c_str(), ini.c_str());

	g_app.frontend = nullptr;
	DestroySurface();
	if (g_app.log)
		fclose(g_app.log);
	return 0;
}
