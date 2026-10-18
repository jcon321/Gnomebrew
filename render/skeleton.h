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

	// Loads `animation` (e.g. "P01") for a skeleton whose tracks are named after `own_code`,
	// as initializeHierarchicalSprite (@004d8cab) does: each bone plays its own track, else
	// the one of `source_code`'s model (AnimationSource: the human male plays the elf male's),
	// as it is: the models that borrow have the same bone lengths, and the root bone's shift is
	// what lowers the body to sit, duck or lie down. Tracks are looked up in each of `wlds` in turn.
	SkeletonAnimation LoadAnimation(const std::vector<const WldFile *> &wlds, const WldSkeleton &skeleton,
		const std::string &animation, const std::string &own_code, const std::string &source_code);

	// Model-space transform of every bone, at time_ms into the animation (nullptr: the base pose).
	// Frames are interpolated; a looping one blends its last frame back into the first, one that
	// doesn't loop stops on its last frame.
	std::vector<Transform> PoseSkeleton(const WldSkeleton &skeleton, const SkeletonAnimation *animation, uint64_t time_ms,
		bool loop = true);

	// Moves the mesh's positions and normals from bone space into model space.
	void SkinMesh(WldMesh &mesh, const std::vector<Transform> &bones);
}
