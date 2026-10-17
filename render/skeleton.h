#pragma once

// Posing WLD skeletons: bone transforms from the base pose or an animation
// (track sets named <code><part>_TRACK, e.g. "P01HUMPE_TRACK" is the "P01"
// standing idle of the human male pelvis), and skinning a mesh whose vertex
// pieces are attached to bones.

#include "math3d.h"
#include "wld.h"

#include <string>
#include <vector>

namespace render
{
	struct SkeletonAnimation
	{
		std::vector<std::vector<WldBoneTransform>> frames; // per bone; empty keeps the base pose
		uint32_t frame_ms = 100;
		size_t length = 0; // frames in the longest track
	};

	// Loads `animation` (e.g. "P01") for a skeleton whose tracks are named after
	// `own_code`, reading the tracks of `source_code`: models borrow each other's
	// animations (e.g. the human female plays the wood elf female's).
	SkeletonAnimation LoadAnimation(const WldFile &wld, const WldSkeleton &skeleton, const std::string &animation,
		const std::string &own_code, const std::string &source_code);

	// Model-space transform of every bone, at time_ms into a looping animation
	// (nullptr: the base pose). Frames are interpolated.
	std::vector<Transform> PoseSkeleton(const WldSkeleton &skeleton, const SkeletonAnimation *animation, uint64_t time_ms);

	// Moves the mesh's positions and normals from bone space into model space.
	void SkinMesh(WldMesh &mesh, const std::vector<Transform> &bones);
}
