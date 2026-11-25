#pragma once

// CaptureScreenshot (@004aa6d1), from CDisplay::RealRender_World once ExecuteCmd 78 asked for one:
// eqgfx's t3dScreenCapture (@1006beb0) is D3DXSaveSurfaceToFile of the back buffer as a BMP, the
// windows in it (t3dRenderPartialScene draws the deferred 2D before the capture).

#include <cstdint>
#include <string>

namespace Screenshot
{
	// "EQ%06d.bmp" in `dir`, the first number from 0 with no file that opens ("rb").
	std::string NextPath(const std::string &dir);
	// saveBitmapToFile (@1008fce5) of a 32-bit surface (D3DX makes it R8G8B8): 24 bits, bottom up,
	// 2834 pixels a meter, the file padded with zeros to a multiple of 4. `pixels` 0xXXRRGGBB, top
	// down, `stride` pixels a row.
	bool WriteBmp(const std::string &path, const uint32_t *pixels, int width, int height, int stride);
}
