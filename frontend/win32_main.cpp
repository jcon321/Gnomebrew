// eqclient_gui: Win32 host for the front end. Owns the window, a 32-bit DIB the
// size of its client area (the game resolution) that FrontEnd draws into, GDI
// text, and input translation. Frames go out through Direct3D 11, which also
// draws the 3D views (win32_d3d11.cpp), or with GDI and the software rasterizer
// when Direct3D isn't available or Renderer=software.
//
// All settings live in my-client.ini next to the exe (the EQ folder is only read
// for assets; its eqclient.ini is never used). Missing keys are written with their
// defaults at startup. The command line overrides the ini:
//   [Paths]    EQDir=C:\TAKP           (folder with eqfeart*.pak, eqtext.txt, ...; empty = auto)
//   [Login]    Server=127.0.0.1:6000
//              User=myname            (remembered from the last login)
//   [Display]  Width=1920             (game resolution: the window's size)
//              Height=1080
//              Renderer=d3d11         (or software: the CPU rasterizer and GDI)
//              VSync=1                (d3d11: wait for the monitor's refresh; 0 runs as fast as it can;
//                                      the debug menu toggles it, kept on exit)
//   [Sound]    Sound=1                (0: no sound at all, as eqclient.ini's Sound=FALSE)
//              MusicVolume=10         (0..20, the options' music level)
//              SoundVolume=10         (0..20, the options' sound level)
//              Music=midi             (the XMI music: midi, Windows' MIDI synth like the real client;
//                                      sf2, through SoundFont below, in our own mix)
//              SoundFont=synthus2.sf2 (in the EQ folder, or a full path; synthusr.sf2 if it's missing)
//   [Defaults] LastCharSel=0          (remembered character select slot, like eqclient.ini's)
//   eqclient_gui.exe --eqdir C:\TAKP --login 192.168.0.18:6000 --size 1920x1080

#include "eqstr.h"
#include "frontend.h"
#include "win32_audio.h"
#include "win32_d3d11.h"
#include "win32_text.h"

#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <vector>
#include <chrono>
#include <cstdio>
#include <memory>
#include <string>

namespace
{
	struct Surface
	{
		HDC dc = nullptr;
		HBITMAP dib = nullptr;
		HGDIOBJ old_bitmap = nullptr;
		uint32_t *pixels = nullptr;
		int width = 0, height = 0;
	};

	struct App
	{
		HWND window = nullptr;
		Surface surface; // the window's client area
		int mouse_x = 0, mouse_y = 0;
		bool tracking_mouse = false;
		bool grabbed = false; // FrontEnd::MouseGrab: the cursor is held at grab_x, grab_y
		int grab_x = 0, grab_y = 0;
		FrontEnd *frontend = nullptr;
		D3d11Device *gpu = nullptr; // null: GDI presents the DIB
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

	// Writes key=value unless the key is already there (even empty), so the ini lists every setting.
	void IniDefault(const std::wstring &ini, const wchar_t *section, const wchar_t *key, const wchar_t *value)
	{
		wchar_t buffer[8] = {};
		GetPrivateProfileStringW(section, key, L"\x01", buffer, 8, ini.c_str());
		if (buffer[0] == L'\x01' && buffer[1] == 0)
			WritePrivateProfileStringW(section, key, value, ini.c_str());
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
		// Not GetTickCount64: its 16 ms steps leave frames at vsync rates with no time passed.
		return static_cast<uint64_t>(
			std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
	}

	// Moves the real cursor to (x, y) in the window's client area.
	void PlaceCursor(int x, int y)
	{
		POINT point{x, y};
		ClientToScreen(g_app.window, &point);
		POINT now{};
		if (GetCursorPos(&now) && now.x == point.x && now.y == point.y)
			return;
		SetCursorPos(point.x, point.y);
	}

	void Send(InputEvent event)
	{
		if (!g_app.frontend)
			return;
		g_app.frontend->OnEvent(event);
		// While a drag holds the mouse, keep the cursor where it started (so it can turn as far
		// as it likes, and the next move measures from there), and leave it there afterwards.
		int x, y;
		bool grabbed = g_app.frontend->MouseGrab(x, y);
		if (grabbed) {
			g_app.grab_x = x;
			g_app.grab_y = y;
		}
		if (grabbed || g_app.grabbed)
			PlaceCursor(g_app.grab_x, g_app.grab_y);
		g_app.grabbed = grabbed;
	}

	InputEvent MouseEvent(InputEvent::Type type, LPARAM lparam)
	{
		InputEvent event;
		event.type = type;
		event.x = static_cast<short>(LOWORD(lparam));
		event.y = static_cast<short>(HIWORD(lparam));
		g_app.mouse_x = event.x;
		g_app.mouse_y = event.y;
		return event;
	}

	InputEvent::Key TranslateKey(WPARAM vk)
	{
		switch (vk) {
		case VK_RETURN: return InputEvent::Key::Enter;
		case VK_ESCAPE: return InputEvent::Key::Escape;
		case VK_TAB: return InputEvent::Key::Tab;
		case VK_BACK: return InputEvent::Key::Backspace;
		case VK_UP: return InputEvent::Key::Up;
		case VK_DOWN: return InputEvent::Key::Down;
		case VK_LEFT: return InputEvent::Key::Left;
		case VK_RIGHT: return InputEvent::Key::Right;
		case VK_F9: return InputEvent::Key::F9;
		case VK_F11: return InputEvent::Key::F11;
		case VK_PRIOR: return InputEvent::Key::PageUp;
		case VK_NEXT: return InputEvent::Key::PageDown;
		case VK_SPACE: return InputEvent::Key::Space;
		case 'W': return InputEvent::Key::W;
		case 'A': return InputEvent::Key::A;
		case 'S': return InputEvent::Key::S;
		case 'D': return InputEvent::Key::D;
		case 'R': return InputEvent::Key::R;
		default: return InputEvent::Key::None;
		}
	}

	InputEvent KeyEvent(InputEvent::Key key)
	{
		InputEvent event;
		event.type = InputEvent::Type::Key;
		event.key = key;
		event.x = g_app.mouse_x;
		event.y = g_app.mouse_y;
		event.shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
		event.ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
		return event;
	}

	void Present(HDC window_dc)
	{
		const Surface &surface = g_app.surface;
		BitBlt(window_dc, 0, 0, surface.width, surface.height, surface.dc, 0, 0, SRCCOPY);
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
			if (!(wparam & MK_RBUTTON))
				ReleaseCapture();
			Send(MouseEvent(InputEvent::Type::MouseUp, lparam));
			return 0;
		case WM_RBUTTONDOWN:
		case WM_RBUTTONDBLCLK:
			SetCapture(window);
			Send(MouseEvent(InputEvent::Type::RightDown, lparam));
			return 0;
		case WM_RBUTTONUP:
			if (!(wparam & MK_LBUTTON))
				ReleaseCapture();
			Send(MouseEvent(InputEvent::Type::RightUp, lparam));
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
		case WM_KEYUP: {
			InputEvent::Key key = TranslateKey(wparam);
			if (key != InputEvent::Key::None) {
				InputEvent event = KeyEvent(key);
				if (message == WM_KEYUP)
					event.type = InputEvent::Type::KeyUp;
				Send(event);
			}
			return 0;
		}
		case WM_KILLFOCUS: {
			InputEvent event;
			event.type = InputEvent::Type::FocusLost;
			event.x = g_app.mouse_x;
			event.y = g_app.mouse_y;
			Send(event);
			break;
		}
		case WM_PAINT: {
			PAINTSTRUCT paint;
			HDC dc = BeginPaint(window, &paint);
			if (!g_app.gpu)
				Present(dc); // with Direct3D the next frame repaints
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

	// The game resolution: the requested size, shrunk to fit the work area with the frame.
	void PickResolution(int &width, int &height, DWORD style)
	{
		RECT work{};
		SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
		RECT frame{0, 0, 0, 0};
		AdjustWindowRect(&frame, style, FALSE);
		int max_w = (work.right - work.left) - (frame.right - frame.left);
		int max_h = (work.bottom - work.top) - (frame.bottom - frame.top);
		width = std::max(static_cast<int>(FrontEnd::Width), std::min(width, max_w));
		height = std::max(static_cast<int>(FrontEnd::Height), std::min(height, max_h));
	}

	bool CreateSurface(Surface &surface, int width, int height)
	{
		BITMAPINFO info{};
		info.bmiHeader.biSize = sizeof(info.bmiHeader);
		info.bmiHeader.biWidth = width;
		info.bmiHeader.biHeight = -height; // top-down
		info.bmiHeader.biPlanes = 1;
		info.bmiHeader.biBitCount = 32;
		info.bmiHeader.biCompression = BI_RGB;

		HDC screen = GetDC(nullptr);
		surface.dc = CreateCompatibleDC(screen);
		ReleaseDC(nullptr, screen);
		void *bits = nullptr;
		surface.dib = CreateDIBSection(surface.dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
		if (!surface.dib || !bits)
			return false;
		surface.pixels = static_cast<uint32_t *>(bits);
		surface.width = width;
		surface.height = height;
		surface.old_bitmap = SelectObject(surface.dc, surface.dib);
		return true;
	}

	void DestroySurface(Surface &surface)
	{
		if (surface.dc) {
			SelectObject(surface.dc, surface.old_bitmap);
			DeleteDC(surface.dc);
		}
		if (surface.dib)
			DeleteObject(surface.dib);
		surface = {};
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
	IniDefault(ini, L"Paths", L"EQDir", L"");
	IniDefault(ini, L"Login", L"Server", L"127.0.0.1:6000");
	IniDefault(ini, L"Login", L"User", L"");
	IniDefault(ini, L"Display", L"Width", L"1920");
	IniDefault(ini, L"Display", L"Height", L"1080");
	IniDefault(ini, L"Display", L"Renderer", L"d3d11");
	IniDefault(ini, L"Display", L"VSync", L"1");
	IniDefault(ini, L"Sound", L"Sound", L"1");
	IniDefault(ini, L"Sound", L"MusicVolume", L"10");
	IniDefault(ini, L"Sound", L"SoundVolume", L"10");
	IniDefault(ini, L"Sound", L"Music", L"midi");
	IniDefault(ini, L"Sound", L"SoundFont", L"synthus2.sf2");
	IniDefault(ini, L"Defaults", L"LastCharSel", L"0");
	std::wstring eq_dir = IniString(ini, L"Paths", L"EQDir");
	std::wstring login = IniString(ini, L"Login", L"Server");
	std::wstring user = IniString(ini, L"Login", L"User");
	int width = GetPrivateProfileIntW(L"Display", L"Width", 1920, ini.c_str());
	int height = GetPrivateProfileIntW(L"Display", L"Height", 1080, ini.c_str());
	std::wstring renderer = IniString(ini, L"Display", L"Renderer");
	bool vsync = GetPrivateProfileIntW(L"Display", L"VSync", 1, ini.c_str()) != 0;
	int last_character = GetPrivateProfileIntW(L"Defaults", L"LastCharSel", 0, ini.c_str());
	bool sound_on = GetPrivateProfileIntW(L"Sound", L"Sound", 1, ini.c_str()) != 0;
	int music_volume = std::clamp(static_cast<int>(GetPrivateProfileIntW(L"Sound", L"MusicVolume", 10, ini.c_str())), 0, 20);
	int sound_volume = std::clamp(static_cast<int>(GetPrivateProfileIntW(L"Sound", L"SoundVolume", 10, ini.c_str())), 0, 20);
	std::wstring music_synth = IniString(ini, L"Sound", L"Music");
	std::wstring soundfont = IniString(ini, L"Sound", L"SoundFont");

	int argc = 0;
	wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
	for (int i = 1; argv && i + 1 < argc; ++i) {
		std::wstring arg = argv[i];
		if (arg == L"--eqdir")
			eq_dir = argv[++i];
		else if (arg == L"--login")
			login = argv[++i];
		else if (arg == L"--size")
			swscanf(argv[++i], L"%dx%d", &width, &height);
		else if (arg == L"--renderer")
			renderer = argv[++i];
	}
	if (argv)
		LocalFree(argv);

	if (eq_dir.empty())
		eq_dir = FileExists(L"eqfeart1.pak") ? L"." : exe_dir;

	if (width <= 0 || height <= 0) {
		width = 1920;
		height = 1080;
	}

	FrontEndConfig config;
	config.username = Narrow(user);
	config.last_character = last_character;
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

	DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
	PickResolution(width, height, style);
	Log("resolution " + std::to_string(width) + "x" + std::to_string(height) + ", 640x480 screens at native size");
	if (!CreateSurface(g_app.surface, width, height)) {
		Fatal(L"Couldn't create the drawing surface.");
		return 1;
	}

	WNDCLASSW window_class{};
	window_class.style = CS_DBLCLKS;
	window_class.lpfnWndProc = WindowProc;
	window_class.hInstance = instance;
	window_class.hCursor = nullptr;
	window_class.lpszClassName = L"MyClientFrontEnd";
	RegisterClassW(&window_class);

	RECT rect{0, 0, width, height};
	AdjustWindowRect(&rect, style, FALSE);
	g_app.window = CreateWindowExW(0, window_class.lpszClassName, L"EverQuest (my-client)", style, CW_USEDEFAULT,
		CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr, instance, nullptr);
	if (!g_app.window) {
		Fatal(L"Couldn't create the window.");
		return 1;
	}

	std::unique_ptr<D3d11Device> gpu;
	if (renderer != L"software") {
		gpu = std::make_unique<D3d11Device>();
		std::string gpu_error;
		if (gpu->Create(g_app.window, width, height, vsync, gpu_error)) {
			Log("Direct3D 11 on " + gpu->AdapterName() +
				(vsync ? ", vsync" : gpu->Tearing() ? ", no vsync" : ", no vsync (but tearing unsupported: refresh rate)"));
			gpu->SetCanvas(g_app.surface.pixels, g_app.surface.width, g_app.surface.height, g_app.surface.width);
			D3d11Device *device = gpu.get();
			ui.make_renderer = [device] { return device->MakeRenderer(); };
			ui.vsync = vsync;
			ui.set_vsync = [device](bool on) { device->SetVSync(on); };
			g_app.gpu = device;
		}
		else {
			Log("Direct3D 11 unavailable (" + gpu_error + "); using the software renderer");
			gpu.reset();
		}
	}
	else
		Log("software renderer (Renderer=software)");

	// The sound: the mix, its synth for the XMI music, and waveOut. Both outlive the front end.
	audio::Mixer mixer;
	WaveOutDevice sound_device;
	if (sound_on) {
		std::shared_ptr<audio::MidiSynth> synth;
		std::string synth_error;
		if (music_synth != L"sf2") {
			synth = MakeWindowsMidiSynth(synth_error);
			if (synth)
				Log("music: Windows MIDI (the MIDI mapper)");
			else
				Log("music: " + synth_error + "; trying the SoundFont");
		}
		if (!synth) {
			std::string base = Narrow(eq_dir) + "\\";
			std::string font = Narrow(soundfont);
			std::vector<std::string> fonts;
			if (!font.empty())
				fonts.push_back(font.find(':') != std::string::npos || font[0] == '\\' ? font : base + font);
			fonts.push_back(base + "synthusr.sf2");
			for (const auto &path : fonts) {
				if ((synth = audio::MakeSoundFontSynth(path, audio::Mixer::Rate, synth_error))) {
					Log("music: SoundFont " + path);
					break;
				}
			}
			if (!synth)
				Log("music: " + synth_error + "; no XMI music");
		}
		mixer.SetSynth(synth);
		std::string device_error;
		if (sound_device.Start(mixer, device_error)) {
			ui.mixer = &mixer;
			ui.sound.music_volume = music_volume / 20.0f;
			ui.sound.sound_volume = sound_volume / 20.0f;
			Log("sound: waveOut " + std::to_string(audio::Mixer::Rate) + " Hz, music " + std::to_string(music_volume) +
				"/20, sounds " + std::to_string(sound_volume) + "/20");
		}
		else
			Log("sound: " + device_error);
	}
	else
		Log("sound off (Sound=0)");

	GdiTextRenderer text;
	text.AddSurface(g_app.surface.dc, g_app.surface.pixels, g_app.surface.width, g_app.surface.height);
	FrontEnd frontend(art, strings, text, config, ui);
	frontend.log = Log;
	frontend.SetGameResolution(width, height);
	g_app.frontend = &frontend;
	ShowWindow(g_app.window, show);

	Canvas canvas(g_app.surface.pixels, g_app.surface.width, g_app.surface.height, g_app.surface.width);
	MSG message{};
	bool running = true;
	bool gpu_lost_logged = false;
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
		if (gpu)
			gpu->BeginFrame();
		frontend.Update(now);
		frontend.Render(canvas, now);
		auto present_start = std::chrono::steady_clock::now();
		auto present_ms = [&] {
			return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - present_start).count();
		};
		if (gpu) {
			// Present waits for the vertical blank with vsync; without it, frames run flat out.
			if (!gpu->Present() && !gpu_lost_logged) {
				Log("Direct3D device lost");
				gpu_lost_logged = true;
			}
			frontend.SetPresentMs(present_ms());
			continue;
		}
		HDC dc = GetDC(g_app.window);
		Present(dc);
		ReleaseDC(g_app.window, dc);
		frontend.SetPresentMs(present_ms());

		// About 60 frames a second; input wakes the loop early.
		uint64_t spent = NowMs() - now;
		MsgWaitForMultipleObjects(0, nullptr, FALSE, spent < 16 ? static_cast<DWORD>(16 - spent) : 0, QS_ALLINPUT);
	}

	for (const auto &name : art.Missing())
		Log("missing art: " + name);
	for (const auto &name : textures.Missing())
		Log("missing UI texture: " + name);
	WritePrivateProfileStringW(L"Login", L"User", Widen(frontend.Username()).c_str(), ini.c_str());
	WritePrivateProfileStringW(L"Defaults", L"LastCharSel", std::to_wstring(frontend.LastCharacter()).c_str(), ini.c_str());
	if (gpu)
		WritePrivateProfileStringW(L"Display", L"VSync", gpu->VSync() ? L"1" : L"0", ini.c_str()); // the debug menu's toggle

	// The sound goes first, step by step in the log: the waveOut and MIDI timer threads, then the
	// MIDI synth (midiOutReset and midiOutClose). The front end's sound only talks to the mixer.
	Log("exit: stopping the sound device");
	sound_device.Stop();
	Log("exit: closing the MIDI synth");
	mixer.SetSynth(nullptr);
	Log("exit: sound closed");

	g_app.frontend = nullptr;
	g_app.gpu = nullptr;
	DestroySurface(g_app.surface);
	Log("exit: done");
	if (g_app.log)
		fclose(g_app.log);
	return 0;
}
