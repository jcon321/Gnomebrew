#include "skeleton.h"

#include <algorithm>

namespace render
{
	namespace
	{
		WldBoneTransform Blend(const WldBoneTransform &a, const WldBoneTransform &b, float t)
		{
			// Normalized lerp, taking the short way round.
			float sign = a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z < 0 ? -1.0f : 1.0f;
			WldBoneTransform out;
			out.w = a.w + (b.w * sign - a.w) * t;
			out.x = a.x + (b.x * sign - a.x) * t;
			out.y = a.y + (b.y * sign - a.y) * t;
			out.z = a.z + (b.z * sign - a.z) * t;
			float length = std::sqrt(out.w * out.w + out.x * out.x + out.y * out.y + out.z * out.z);
			if (length > 0) {
				out.w /= length;
				out.x /= length;
				out.y /= length;
				out.z /= length;
			}
			out.shift = {a.shift.x + (b.shift.x - a.shift.x) * t, a.shift.y + (b.shift.y - a.shift.y) * t,
				a.shift.z + (b.shift.z - a.shift.z) * t};
			out.scale = a.scale + (b.scale - a.scale) * t;
			return out;
		}
	}

	SkeletonAnimation LoadAnimation(const std::vector<const WldFile *> &wlds, const WldSkeleton &skeleton,
		const std::string &animation, const std::string &own_code, const std::string &source_code)
	{
		SkeletonAnimation result;
		result.frames.resize(skeleton.bones.size());
		auto read = [&](const std::string &track, std::vector<WldBoneTransform> &frames, uint32_t &frame_ms) {
			for (const WldFile *wld : wlds)
				if (wld->ReadTrack(track, frames, frame_ms))
					return true;
			return false;
		};
		for (size_t i = 0; i < skeleton.bones.size(); ++i) {
			const std::string &track = skeleton.bones[i].track; // e.g. HUMPE_TRACK
			if (track.size() <= own_code.size() || track.compare(0, own_code.size(), own_code) != 0)
				continue;
			std::string part = track.substr(own_code.size());
			uint32_t frame_ms = 0;
			if (!read(animation + own_code + part, result.frames[i], frame_ms) &&
				(source_code == own_code || !read(animation + source_code + part, result.frames[i], frame_ms)))
				continue;
			result.length = std::max(result.length, result.frames[i].size());
			if (frame_ms)
				result.frame_ms = frame_ms;
		}
		return result;
	}

	std::vector<Transform> PoseSkeleton(const WldSkeleton &skeleton, const SkeletonAnimation *animation, uint64_t time_ms,
		bool loop)
	{
		size_t frame = 0;
		float t = 0;
		if (animation && animation->length > 1) {
			uint64_t step = animation->frame_ms ? animation->frame_ms : 100;
			if (!loop && time_ms >= (animation->length - 1) * step)
				frame = animation->length - 1;
			else {
				frame = (time_ms / step) % animation->length;
				t = static_cast<float>(time_ms % step) / static_cast<float>(step);
			}
		}

		std::vector<Transform> world(skeleton.bones.size());
		for (size_t i = 0; i < skeleton.bones.size(); ++i) {
			const auto &bone = skeleton.bones[i];
			WldBoneTransform pose = bone.pose;
			size_t track = bone.animation_bone >= 0 ? static_cast<size_t>(bone.animation_bone) : i;
			if (animation && track < animation->frames.size() && !animation->frames[track].empty()) {
				const auto &frames = animation->frames[track];
				// Tracks shorter than the animation hold their last frame.
				size_t a = std::min(frame, frames.size() - 1);
				size_t b = frame + 1 < animation->length ? std::min(frame + 1, frames.size() - 1) : loop ? 0 : a;
				pose = Blend(frames[a], frames[b], t);
			}
			Transform local;
			local.rotation = {pose.w, pose.x, pose.y, pose.z};
			local.translation = {pose.shift.x, pose.shift.y, pose.shift.z};
			local.scale = pose.scale; // scales the bone's mesh, and its children's shifts and scales
			// Parents come before children in the bone list.
			world[i] = bone.parent >= 0 && static_cast<size_t>(bone.parent) < i ? world[bone.parent] * local : local;
		}
		return world;
	}

	void SkinMesh(WldMesh &mesh, const std::vector<Transform> &bones)
	{
		size_t v = 0;
		for (const auto &piece : mesh.pieces) {
			const Transform *bone = piece.bone < bones.size() ? &bones[piece.bone] : nullptr;
			for (uint16_t k = 0; k < piece.count && v < mesh.positions.size(); ++k, ++v) {
				if (!bone)
					continue;
				Vec3 p = bone->Apply({mesh.positions[v].x, mesh.positions[v].y, mesh.positions[v].z});
				Vec3 n = bone->ApplyDirection({mesh.normals[v].x, mesh.normals[v].y, mesh.normals[v].z});
				mesh.positions[v] = {p.x, p.y, p.z};
				mesh.normals[v] = {n.x, n.y, n.z};
			}
		}
	}
}
