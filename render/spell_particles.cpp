#include "spell_particles.h"

#include "bitmap.h"
#include "dds.h"
#include "tga.h"
#include "textures.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace render
{
	namespace
	{
		constexpr size_t RecordSize = 0x138;
		constexpr int MaxParticles = 100000;

		float Cos512(float angle) { return std::cos(angle * (2 * Pi / 512.0f)); }
		float Sin512(float angle) { return std::sin(angle * (2 * Pi / 512.0f)); }
		// t3dAngleArcTangentFloat: the angle of (x, y), in 512ths.
		float Atan512(float y, float x) { return std::atan2(y, x) * (512.0f / (2 * Pi)); }

		std::string Lower(std::string s)
		{
			for (char &c : s)
				c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			return s;
		}

		float F(const uint8_t *r, size_t o)
		{
			float v;
			std::memcpy(&v, r + o, 4);
			return v;
		}

		int I(const uint8_t *r, size_t o)
		{
			int32_t v;
			std::memcpy(&v, r + o, 4);
			return v;
		}

		std::string Text(const uint8_t *r, size_t o, size_t n)
		{
			size_t len = 0;
			while (len < n && r[o + len])
				++len;
			return std::string(reinterpret_cast<const char *>(r + o), len);
		}

		// The two axes `a` and `b` turned by `angle` (512ths), a toward b.
		void Turn(Vec3 &a, Vec3 &b, float angle)
		{
			float c = Cos512(angle), s = Sin512(angle);
			Vec3 na = a * c + b * s;
			b = b * c - a * s;
			a = na;
		}

		uint32_t Rgb(int r, int g, int b)
		{
			auto clamp = [](int v) { return static_cast<uint32_t>(std::clamp(v, 0, 255)); };
			return (clamp(r) << 16) | (clamp(g) << 8) | clamp(b);
		}
	}

	bool SpellParticles::Load(const std::string &eq_dir, std::string &error)
	{
		m_defs.clear();
		m_textures.clear();
		m_dir = eq_dir + "/SpellEffects";
		std::ifstream file(eq_dir + "/spellsnew.edd", std::ios::binary);
		if (!file) {
			error = "can't read spellsnew.edd";
			return false;
		}
		std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
		if (data.size() < 8 || std::memcmp(data.data(), "EDD", 4) != 0 || std::memcmp(data.data() + 4, "105", 4) != 0) {
			error = "spellsnew.edd isn't version 105";
			return false;
		}
		for (size_t at = 8; at + RecordSize <= data.size(); at += RecordSize) {
			const uint8_t *r = data.data() + at;
			EmitterDef d;
			d.name = Text(r, 0, 0x40);
			d.texture = Text(r, 0x40, 0x20);
			d.bone_axes = I(r, 0x60) == 1;
			d.no_depth_write = I(r, 0x64) == 1;
			d.additive = I(r, 0x68) == 1;
			d.streak = I(r, 0x6c) == 1;
			d.follow = I(r, 0x70) == 1;
			d.duration = F(r, 0x74);
			d.life = F(r, 0x78);
			d.initial = I(r, 0x7c);
			d.per_emit = I(r, 0x80);
			d.rate = F(r, 0x84);
			d.stop_early = F(r, 0x88);
			d.fade_in = F(r, 0x8c);
			d.fade_out = F(r, 0x90);
			d.grow = F(r, 0x94);
			d.shrink = F(r, 0x98);
			d.lod_distance = F(r, 0x9c);
			d.max_alpha = F(r, 0xa0);
			d.shape = I(r, 0xa4);
			d.radius = F(r, 0xa8);
			d.radius2 = F(r, 0xac);
			d.height = F(r, 0xb0);
			for (int i = 0; i < 3; ++i)
				d.offset[i] = F(r, 0xb4 + i * 4);
			d.tilt[0] = F(r, 0xc0);
			d.tilt[1] = F(r, 0xc4);
			d.size = F(r, 0xc8);
			d.depth_bias = F(r, 0xcc);
			for (int i = 0; i < 3; ++i) {
				d.start[i] = I(r, 0xd0 + i * 4);
				d.end[i] = I(r, 0xdc + i * 4);
			}
			d.up_velocity[0] = F(r, 0xe8), d.up_velocity[1] = F(r, 0xec), d.up_accel = F(r, 0xf0);
			d.axis1_velocity[0] = F(r, 0xf4), d.axis1_velocity[1] = F(r, 0xf8), d.axis1_accel = F(r, 0xfc);
			d.axis2_velocity[0] = F(r, 0x100), d.axis2_velocity[1] = F(r, 0x104), d.axis2_accel = F(r, 0x108);
			d.out_velocity[0] = F(r, 0x10c), d.out_velocity[1] = F(r, 0x110), d.out_accel = F(r, 0x114);
			d.spin_velocity[0] = F(r, 0x118), d.spin_velocity[1] = F(r, 0x11c), d.spin_accel = F(r, 0x120);
			d.gravity = F(r, 0x124);
			d.drift = F(r, 0x128);
			d.cells = I(r, 0x12c);
			d.cell_rate = F(r, 0x130);
			d.sprite_spin = F(r, 0x134);
			m_defs.push_back(std::move(d));
		}
		return !m_defs.empty();
	}

	const EmitterDef *SpellParticles::Def(int index) const
	{
		return index >= 0 && static_cast<size_t>(index) < m_defs.size() ? &m_defs[index] : nullptr;
	}

	const Texture *SpellParticles::TextureFor(const EmitterDef &def)
	{
		std::string key = Lower(def.texture);
		auto it = m_textures.find(key);
		if (it != m_textures.end())
			return it->second.get();
		std::unique_ptr<Texture> texture;
		// D3DXCreateTextureFromFileEx on SpellEffects\<name>; the folder's case may differ from the name's.
		std::error_code ec;
		for (const auto &entry : std::filesystem::directory_iterator(m_dir, ec)) {
			if (Lower(entry.path().filename().string()) != key)
				continue;
			std::ifstream file(entry.path(), std::ios::binary);
			std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
			Image image;
			bool dds = data.size() >= 4 && std::memcmp(data.data(), "DDS ", 4) == 0;
			if ((dds ? DecodeDds(data, image) : DecodeTga(data, image)) && image.width > 0 && image.height > 0)
				texture = MakeTexture(image, true);
			break;
		}
		const Texture *result = texture.get();
		m_textures.emplace(key, std::move(texture));
		return result;
	}

	int SpellParticles::Rand()
	{
		m_random = m_random * 214013u + 2531011u;
		return static_cast<int>((m_random >> 16) & 0x7fff);
	}

	int SpellParticles::Create(int index, uint32_t tag, int duration_ms, float density, float opacity, const Frame &frame,
		uint32_t binding)
	{
		const EmitterDef *def = Def(index);
		if (!def || density <= 0 || !TextureFor(*def))
			return 0;
		// createParticleEmitter: the burst and the emissions at the density (a burst of 16 or more
		// keeps 16; one emission of 16 or more is thinned, under that they come less often).
		int initial = static_cast<int>(def->initial * density);
		if (initial < 16)
			initial = def->initial < 16 ? def->initial : 16;
		int per_emit = def->per_emit;
		float rate = def->rate;
		if (per_emit >= 16)
			per_emit = static_cast<int>(per_emit * density);
		else
			rate *= density;
		float duration = def->duration < 0 ? -def->duration : duration_ms > 0 ? duration_ms * 0.001f : def->duration;
		float emitting = duration - def->stop_early;
		if (emitting < 0)
			return 0;
		// As many as are ever alive at once.
		int count;
		if (def->life >= 0 && def->life < emitting) {
			int emissions = static_cast<int>(rate * def->life);
			count = initial > per_emit ? emissions * per_emit + initial : (emissions + 1) * per_emit;
		}
		else
			count = static_cast<int>(emitting * rate) * per_emit + initial;
		count = std::min(count, MaxParticles);
		if (count <= 0)
			return 0;
		float shows;
		if (def->life < 0)
			shows = duration;
		else if (per_emit > 0 && rate > 0)
			shows = static_cast<int>(emitting * rate) / rate + def->life + def->stop_early;
		else
			shows = def->life + def->stop_early;

		Emitter e;
		e.def = index;
		e.tag = tag;
		e.binding = binding;
		e.capacity = count;
		e.initial = initial;
		e.per_emit = per_emit;
		e.rate = rate;
		e.duration = e.left = duration;
		e.opacity = opacity;
		e.frame = frame;
		// initializeParticleSystem: the level of detail never takes them under 16.
		e.lod_initial = initial > 16 ? 16.0f / initial : 1;
		int steady = per_emit < initial ? count - initial + per_emit : count;
		e.lod_emit = steady > 16 ? 16.0f / steady : 1;
		m_emitters.push_back(std::move(e));
		return static_cast<int>(shows * 1000);
	}

	void SpellParticles::DestroyTagged(uint32_t tag)
	{
		m_emitters.erase(std::remove_if(m_emitters.begin(), m_emitters.end(), [&](const Emitter &e) { return e.tag == tag; }),
			m_emitters.end());
	}

	void SpellParticles::DestroyBound(uint32_t binding)
	{
		m_emitters.erase(
			std::remove_if(m_emitters.begin(), m_emitters.end(), [&](const Emitter &e) { return binding && e.binding == binding; }),
			m_emitters.end());
	}

	void SpellParticles::Rebind(uint32_t from, uint32_t to)
	{
		for (Emitter &e : m_emitters)
			if (from && e.binding == from)
				e.binding = to;
	}

	size_t SpellParticles::ParticleCount() const
	{
		size_t n = 0;
		for (const Emitter &e : m_emitters)
			n += e.particles.size();
		return n;
	}

	void SpellParticles::Place(const EmitterDef &def, const Frame &frame, Particle &p) const
	{
		p.origin = frame.position;
		for (int i = 0; i < 3; ++i)
			p.axes[i] = frame.axes[i];
		for (int i = 0; i < 3; ++i)
			if (def.offset[i] != 0)
				p.origin = p.origin + p.axes[i] * def.offset[i];
		if (def.tilt[0] != 0)
			Turn(p.axes[0], p.axes[1], def.tilt[0]);
		if (def.tilt[1] != 0)
			Turn(p.axes[0], p.axes[2], def.tilt[1]);
	}

	void SpellParticles::Make(const EmitterDef &def, Emitter &e, int count)
	{
		// How the rings go round: shape 1 one ring of this frame's; shape 2 rings stacked up its
		// height, as many as suit the number.
		static const int Steps[][3] = {{9, 8, 0}, {0x13, 3, 0}, {0x21, 4, 0}, {0x49, 6, 0}, {0x81, 8, 0}, {0xc9, 10, 0},
			{0x121, 12, 0}, {0x201, 16, 0}, {0x481, 24, 0}, {0xc81, 40, 0}};
		if (def.shape == 1)
			m_ring = count;
		else if (def.shape == 2) {
			bool found = false;
			for (const auto &step : Steps) {
				if (count < step[0]) {
					m_rings = step[1];
					m_ring = count < 9 ? 16 : count / m_rings;
					found = true;
					break;
				}
			}
			if (!found) {
				m_rings = count > 0x1c20 ? 100 : 60;
				m_ring = count / m_rings;
			}
		}
		int target = e.made + count;
		for (; e.made < target; ++e.made) {
			if (static_cast<int>(e.particles.size()) >= e.capacity)
				break; // the ring buffer is full: the rest are skipped
			Particle p;
			Place(def, e.frame, p);
			float a8 = def.radius, ac = def.radius2, b0 = def.height;
			auto ellipse = [&](float angle) {
				float x = Cos512(angle) * a8, y = Sin512(angle) * ac;
				p.angle = Atan512(y, x);
				return std::sqrt(x * x + y * y);
			};
			switch (def.shape) {
			case 1:
			case 2: {
				float angle = 512.0f / static_cast<float>(m_ring) * static_cast<float>(e.made);
				if (ac != 0)
					p.radius = ellipse(angle);
				else {
					p.angle = angle;
					p.radius = a8;
				}
				if (def.shape == 2 && m_ring > 0)
					p.height = b0 / static_cast<float>(m_rings) * static_cast<float>((e.made / m_ring) % m_rings) - b0 * 0.5f;
				break;
			}
			case 3:
				if (ac == 0) {
					p.angle = Rand01() * 512;
					p.radius = a8;
				}
				else
					p.radius = ellipse(Rand01() * 512);
				p.height = (Rand01() - 0.5f) * b0;
				break;
			case 4: {
				float x, y, d;
				do {
					x = Rand01() * 2 - 1;
					y = Rand01() * 2 - 1;
					d = x * x + y * y;
				} while (d > 1);
				if (ac != 0) {
					x *= a8;
					y *= ac;
					p.angle = Atan512(y, x);
					p.radius = std::sqrt(x * x + y * y);
				}
				else {
					p.angle = Atan512(y, x);
					p.radius = std::sqrt(d) * a8;
				}
				break;
			}
			case 5: {
				float z = Rand01() * 2 - 1;
				float across = std::sqrt(1 - z * z);
				if (ac == 0) {
					p.angle = Rand01() * 512;
					p.radius = across * a8;
				}
				else
					p.radius = ellipse(Rand01() * 512) * across;
				p.height = z * (b0 != 0 ? b0 : a8);
				break;
			}
			case 6: {
				float x = Rand01() * 2 - 1, y = Rand01() * 2 - 1;
				float bx = x, by = y, bz = y;
				switch (static_cast<int>(Rand01() * 6)) {
				case 0: bx = -1; by = x; break;
				case 1: bx = 1; by = x; break;
				case 2: by = -1; break;
				case 3: by = 1; break;
				case 4: by = y; bz = -1; break;
				default: by = y; bz = 1; break;
				}
				bx *= a8;
				by *= ac != 0 ? ac : a8;
				bz *= b0 != 0 ? b0 : a8;
				p.angle = Atan512(by, bx);
				p.radius = std::sqrt(bx * bx + by * by);
				p.height = bz;
				break;
			}
			case 7:
				if (ac == 0) {
					float f = Rand01();
					p.angle = Rand01() * 512;
					p.radius = f * a8;
					p.height = f * b0;
				}
				else {
					float r = ellipse(Rand01() * 512);
					float f = Rand01();
					p.radius = r * f;
					p.height = f * b0;
				}
				break;
			case 8: {
				float around = Rand01() * 512;
				float c = Cos512(around), s = Sin512(around);
				if (ac == 0) {
					p.angle = Rand01() * 512;
					p.radius = c * b0 + a8;
				}
				else
					p.radius = c * b0 + ellipse(Rand01() * 512);
				p.height = s * b0;
				break;
			}
			case 9:
				if (ac != 0)
					p.radius = ellipse(Rand01() * 512);
				else {
					p.angle = Rand01() * 512;
					p.radius = a8;
				}
				break;
			default: // 0: a point
				break;
			}
			if ((def.shape == 5 || def.shape == 6) && (p.radius != 0 || p.height != 0)) {
				float n = 1.0f / std::sqrt(p.radius * p.radius + p.height * p.height);
				p.out[0] = p.radius * n;
				p.out[1] = p.height * n;
			}
			auto between = [&](const float range[2]) { return (range[1] - range[0]) * Rand01() + range[0]; };
			p.up_v = between(def.up_velocity);
			p.axis1_v = between(def.axis1_velocity);
			p.axis2_v = between(def.axis2_velocity);
			p.out_v = between(def.out_velocity);
			p.spin_v = between(def.spin_velocity);
			p.life = p.left = def.life < 0 ? e.left : def.life;
			e.particles.push_back(p);
		}
		e.made = target;
	}

	void SpellParticles::Update(uint64_t now_ms, const Vec3 &camera, const Locate &locate)
	{
		float dt = m_last_ms ? std::min(static_cast<float>(now_ms - m_last_ms) * 0.001f, 1.0f) : 0;
		m_last_ms = now_ms;
		m_ring = 1;
		for (size_t i = 0; i < m_emitters.size();) {
			Emitter &e = m_emitters[i];
			const EmitterDef &def = m_defs[e.def];
			if (e.left <= 0) {
				if (e.particles.empty()) {
					m_emitters.erase(m_emitters.begin() + static_cast<std::ptrdiff_t>(i));
					continue;
				}
				e.left = 0;
			}
			if (e.binding) {
				Frame now;
				if (!locate || !locate(e.binding, now)) {
					m_emitters.erase(m_emitters.begin() + static_cast<std::ptrdiff_t>(i));
					continue;
				}
				e.frame.position = now.position;
				for (int k = 0; k < 3; ++k)
					e.frame.axes[k] = def.bone_axes && now.bone ? now.bone_axes[k] * -1.0f : now.axes[k];
			}
			e.particles.erase(std::remove_if(e.particles.begin(), e.particles.end(), [](const Particle &p) { return p.left <= 0; }),
				e.particles.end());
			if (e.left > 0) {
				float elapsed = e.duration - e.left - def.stop_early;
				int ticks = elapsed < 0 ? -1 : static_cast<int>(elapsed * e.rate);
				int count = 0;
				auto detail = [&](float least) {
					float distance = Length(e.frame.position - camera);
					float lod = distance > def.lod_distance ? def.lod_distance / distance : 1;
					return std::max(lod, least);
				};
				if (e.ticks < 0 && ticks >= 0) {
					count = static_cast<int>(e.initial * detail(e.lod_initial));
					e.ticks = 0;
				}
				if (e.ticks < ticks) {
					int more = static_cast<int>(static_cast<float>((ticks - e.ticks) * e.per_emit) * detail(e.lod_emit));
					if (more > 0) {
						count += more;
						e.ticks = ticks;
					}
				}
				Make(def, e, count);
			}
			for (Particle &p : e.particles) {
				p.up_v += dt * def.up_accel;
				p.height += p.up_v * dt;
				p.axis1_v += dt * def.axis1_accel;
				if (p.axis1_v != 0)
					p.origin = p.origin + p.axes[1] * (dt * p.axis1_v);
				p.axis2_v += dt * def.axis2_accel;
				if (p.axis2_v != 0)
					p.origin = p.origin + p.axes[2] * (dt * p.axis2_v);
				p.out_v += dt * def.out_accel;
				if (p.out_v != 0) {
					p.radius += dt * p.out_v * p.out[0];
					p.height += dt * p.out_v * p.out[1];
				}
				p.spin_v += dt * def.spin_accel;
				p.angle += p.spin_v * dt;
				p.fall_v += dt * def.gravity;
				p.origin.z -= p.fall_v * dt;
				p.origin.x += dt * def.drift;
				if (def.follow && e.binding)
					Place(def, e.frame, p);
				p.left -= dt;
			}
			e.left -= dt;
			++i;
		}
	}

	void SpellParticles::Draw(Renderer &renderer)
	{
		const Camera &camera = renderer.CurrentCamera();
		Vec3 forward = Normalize(camera.forward);
		Vec3 right = Normalize(Cross(forward, camera.up));
		Vec3 up = Cross(right, forward);
		float focal = renderer.Focal();
		renderer.SetVertexLight({1, 1, 1}, {0, 0, 0});
		renderer.SetDynamicLights(false, 0);
		// Blended ones go in batches of about the same alpha (a material's opacity), additive ones
		// carry it in their color.
		constexpr int Buckets = 32;
		std::vector<Vertex> bucket_vertices[Buckets];
		std::vector<uint32_t> bucket_indices[Buckets];
		for (const Emitter &e : m_emitters) {
			const EmitterDef &def = m_defs[e.def];
			const Texture *texture = TextureFor(def);
			if (!texture || e.particles.empty())
				continue;
			m_vertices.clear();
			m_indices.clear();
			for (auto &b : bucket_vertices)
				b.clear();
			for (auto &b : bucket_indices)
				b.clear();
			bool fixed_color = def.start[0] == def.end[0] && def.start[1] == def.end[1] && def.start[2] == def.end[2];
			for (const Particle &p : e.particles) {
				if (p.radius < 0 || p.left <= 0)
					continue;
				Vec3 at = p.origin + p.axes[0] * p.height + p.axes[1] * (p.radius * Cos512(p.angle)) +
				          p.axes[2] * (p.radius * Sin512(p.angle));
				at = at - forward * def.depth_bias;
				float depth = Dot(at - camera.position, forward);
				if (depth < std::max(m_near_clip, camera.near_plane) || depth > camera.far_plane)
					continue;
				float age = p.life - p.left;
				float grown = 1;
				if (age < def.grow)
					grown = age / def.grow;
				if (p.left < def.shrink)
					grown *= p.left / def.shrink;
				float half = grown * def.size * 0.5f;
				if (half * focal / depth < 0.75f)
					continue;
				Vec3 d = right * half, n = up * half;
				if (def.streak) {
					d = p.axes[0] * def.size;
					Vec3 across = Cross(forward, d);
					float length = Length(across);
					if (length <= 0)
						continue;
					n = across * (half / length);
				}
				if (def.sprite_spin != 0) {
					float c = Cos512(age * def.sprite_spin), s = Sin512(age * def.sprite_spin);
					Vec3 nd = d * c + n * s;
					n = n * c - d * s;
					d = nd;
				}
				float alpha = 1;
				if (age < def.fade_in)
					alpha = age / def.fade_in;
				if (p.left < def.fade_out)
					alpha *= p.left / def.fade_out;
				alpha = std::min(alpha, def.max_alpha);
				int alpha8 = std::clamp(static_cast<int>(std::lround(alpha * e.opacity * 255)), 0, 255);
				if (alpha8 < 8)
					continue; // the alpha test drops it whatever the texel
				int rgb[3];
				for (int k = 0; k < 3; ++k)
					rgb[k] = fixed_color ? def.start[k]
					                     : def.start[k] + static_cast<int>(std::lround((def.end[k] - def.start[k]) * age / p.life));
				float u0 = 0, v0 = 0, cell = 1;
				if (def.cells == 16 || def.cells == 4) {
					int side = def.cells == 16 ? 4 : 2;
					int index = static_cast<int>(std::lround(age * def.cell_rate)) % def.cells;
					if (index < 0)
						index += def.cells;
					cell = 1.0f / side;
					u0 = (index % side) * cell;
					v0 = (index / side) * cell;
				}
				uint32_t color;
				std::vector<Vertex> *vertices;
				std::vector<uint32_t> *indices;
				if (def.additive) {
					color = Rgb(rgb[0] * alpha8 / 255, rgb[1] * alpha8 / 255, rgb[2] * alpha8 / 255);
					vertices = &m_vertices;
					indices = &m_indices;
				}
				else {
					color = Rgb(rgb[0], rgb[1], rgb[2]);
					int b = alpha8 * Buckets / 256;
					vertices = &bucket_vertices[b];
					indices = &bucket_indices[b];
				}
				uint32_t base = static_cast<uint32_t>(vertices->size());
				Vertex v;
				v.color = color;
				v.position = at - d + n, v.u = u0, v.v = v0;
				vertices->push_back(v);
				v.position = at + d + n, v.u = u0 + cell, v.v = v0;
				vertices->push_back(v);
				v.position = at + d - n, v.u = u0 + cell, v.v = v0 + cell;
				vertices->push_back(v);
				v.position = at - d - n, v.u = u0, v.v = v0 + cell;
				vertices->push_back(v);
				const uint32_t quad[6] = {0, 1, 2, 0, 2, 3};
				for (uint32_t k : quad)
					indices->push_back(base + k);
			}
			Material material;
			material.texture = texture;
			material.two_sided = true;
			material.fog = false;
			material.texel_alpha = true;
			if (def.additive) {
				material.blend = Blend::Additive;
				if (!m_indices.empty())
					renderer.Draw(m_vertices.data(), m_vertices.size(), m_indices.data(), m_indices.size(), material);
			}
			else {
				material.blend = Blend::Translucent;
				for (int b = 0; b < Buckets; ++b) {
					if (bucket_indices[b].empty())
						continue;
					material.opacity = static_cast<uint8_t>(std::min(255, b * 256 / Buckets + 256 / Buckets / 2));
					renderer.Draw(bucket_vertices[b].data(), bucket_vertices[b].size(), bucket_indices[b].data(),
						bucket_indices[b].size(), material);
				}
			}
		}
	}
}
