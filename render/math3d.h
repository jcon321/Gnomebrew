#pragma once

// Small vector/rotation helpers for the 3D view. World space is the WLD frame:
// right-handed, z up, +x north, +y west (the server's x and y swapped).

#include <cmath>

namespace render
{
	constexpr float Pi = 3.14159265358979f;

	struct Vec3
	{
		float x = 0, y = 0, z = 0;

		Vec3() = default;
		constexpr Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}

		Vec3 operator+(const Vec3 &o) const { return {x + o.x, y + o.y, z + o.z}; }
		Vec3 operator-(const Vec3 &o) const { return {x - o.x, y - o.y, z - o.z}; }
		Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
		Vec3 &operator+=(const Vec3 &o)
		{
			x += o.x;
			y += o.y;
			z += o.z;
			return *this;
		}
	};

	inline float Dot(const Vec3 &a, const Vec3 &b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
	inline Vec3 Cross(const Vec3 &a, const Vec3 &b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
	inline float Length(const Vec3 &v) { return std::sqrt(Dot(v, v)); }
	inline Vec3 Normalize(const Vec3 &v)
	{
		float length = Length(v);
		return length > 0 ? v * (1.0f / length) : Vec3{0, 0, 0};
	}

	// EQ headings run 0..512 counterclockwise from north (+x).
	inline float HeadingToRadians(float heading) { return heading * (2 * Pi / 512.0f); }

	struct Quat
	{
		float w = 1, x = 0, y = 0, z = 0;

		Quat operator*(const Quat &o) const
		{
			return {w * o.w - x * o.x - y * o.y - z * o.z, w * o.x + x * o.w + y * o.z - z * o.y,
				w * o.y - x * o.z + y * o.w + z * o.x, w * o.z + x * o.y - y * o.x + z * o.w};
		}

		Vec3 Rotate(const Vec3 &v) const
		{
			// v + 2w(q x v) + 2 q x (q x v)
			Vec3 q{x, y, z};
			Vec3 t = Cross(q, v) * 2.0f;
			return v + t * w + Cross(q, t);
		}
	};

	inline Quat AxisAngle(const Vec3 &axis, float radians)
	{
		float s = std::sin(radians * 0.5f);
		return {std::cos(radians * 0.5f), axis.x * s, axis.y * s, axis.z * s};
	}

	// A rigid transform with uniform scale: p' = rotation(p) * scale + translation.
	struct Transform
	{
		Quat rotation;
		Vec3 translation;
		float scale = 1;

		Vec3 Apply(const Vec3 &p) const { return rotation.Rotate(p) * scale + translation; }
		Vec3 ApplyDirection(const Vec3 &d) const { return rotation.Rotate(d); }
		// this * child: apply child first, then this.
		Transform operator*(const Transform &child) const
		{
			return {rotation * child.rotation, Apply(child.translation), scale * child.scale};
		}
	};
}
