#pragma once

// Direct3D 11 for the Windows build. D3d11Device owns the window's swap chain:
// the 3D views draw straight into its back buffer through the renderers it
// makes, then the front end's canvas (the DIB everything 2D is drawn into on the
// CPU) goes up as a texture and is blended over them, its top byte saying how
// much of the 3D view shows through (canvas.h).

#include "renderer.h"

#include <windows.h>

#include <memory>
#include <string>

class D3d11Device
{
public:
	D3d11Device();
	~D3d11Device();
	D3d11Device(const D3d11Device &) = delete;
	D3d11Device &operator=(const D3d11Device &) = delete;

	// A swap chain for the window's width x height client area. False, with why, when
	// Direct3D 11 or the shader compiler (d3dcompiler_47.dll) isn't available.
	bool Create(HWND window, int width, int height, bool vsync, std::string &error);
	const std::string &AdapterName() const;
	bool Tearing() const; // without vsync, frames go out as soon as they're done (not at the refresh rate)
	bool VSync() const;
	void SetVSync(bool on); // from the next Present

	// The canvas the front end draws into, the size of the window. A renderer's Finish into
	// a block of it draws the 3D view in that part of the window.
	void SetCanvas(uint32_t *pixels, int width, int height, int stride);
	// The renderers share the device; only one draws at a time.
	std::unique_ptr<render::Renderer> MakeRenderer();

	void BeginFrame();
	// Blends the canvas over the frame and shows it (waiting for the vertical blank with
	// vsync). False once the device is lost.
	bool Present();
	double UploadMs() const; // the last Present's canvas upload (the rest of it is the swap chain's)

	struct State; // the Direct3D objects (win32_d3d11.cpp)

private:
	std::unique_ptr<State> m;
};
