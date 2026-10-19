#pragma once

// Occlusion culling as eqgfx does it with Umbra (dPVS; eqclient.ini's UseUmbra, on unless it
// starts with F):
//   initializeWorldGraphics (@1007e440)        one DPVS cell for the whole zone, no portals.
//   loadZoneObjectForRendering (@1007edf0)     an object for each region's mesh, and
//   createDPVSObjectForRendering (@1007f770)   for each actor: its test model the mesh, its write
//     model (the occluder) the mesh's solid triangles (not passable, 0x10) with an opaque,
//     unmasked material.
//   renderSceneWithDPVS (@1007eaf0)            resolveVisibility each frame: nearest first, an
//     object is drawn unless what the objects before it wrote hides it, and then writes its own.
// Here the write models go into a small depth buffer of the view (1/z at pixel centers) and an
// object is tested by its bounding box.

#include "renderer.h"

#include <cstddef>
#include <vector>

namespace render
{
	class OcclusionBuffer
	{
	public:
		static constexpr int Width = 256;

		// Empties the buffer for a view: the camera and the width / height of the image.
		void Begin(const Camera &camera, float aspect);
		// Whether any of a world-space box could be seen past what has been written.
		bool BoxVisible(const Vec3 &low, const Vec3 &high) const;
		// Writes front-facing world-space triangles (3 vertices each) as occluders.
		void Write(const Vec3 *vertices, size_t triangle_count);

	private:
		struct ViewPoint
		{
			float x, y, z;
		};
		ViewPoint ToView(const Vec3 &p) const;
		void Rasterize(const ViewPoint &a, const ViewPoint &b, const ViewPoint &c);

		int m_height = 0;
		Vec3 m_eye, m_right, m_up, m_forward;
		float m_focal = 1, m_near = 0.25f;
		std::vector<float> m_depth; // 1/z of the nearest occluder per pixel, 0 for none
	};
}
