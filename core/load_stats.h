#pragma once

// What's been loaded as the game runs (ours, for finding hitches and where the memory goes): the
// debug menu's Memory rows, and the host's hitch lines in my-client.log, which say what a slow
// frame loaded.
//   - Events: how many and how long, counted up from the start (Snapshot, then subtract).
//   - Bytes: what's held now, kept by a Bytes member of whatever holds it.

#include <atomic>
#include <chrono>
#include <cstdint>

namespace load_stats
{
	struct Events
	{
		std::atomic<uint64_t> count{0};
		std::atomic<uint64_t> us{0};
		void Add(uint64_t micros)
		{
			count.fetch_add(1, std::memory_order_relaxed);
			us.fetch_add(micros, std::memory_order_relaxed);
		}
	};
	inline Events textures_decoded; // TextureCache: a texture read from its archive and decoded
	inline Events textures_uploaded; // the GPU renderer: a texture's GPU copy made
	inline Events models_built;      // the zone view: a spawn's model built (a look not seen before)

	inline std::atomic<int64_t> archive_bytes{0}; // archives held whole in memory (PfsArchive, not lazy)
	inline std::atomic<int64_t> texel_bytes{0};   // decoded textures' pixels, every mip (TextureCache's)
	inline std::atomic<int64_t> gpu_bytes{0};     // their GPU copies

	// A count of bytes held, added to `total` while it's held; moved with its holder.
	class Bytes
	{
	public:
		explicit Bytes(std::atomic<int64_t> &total) : m_total(&total) {}
		~Bytes() { Set(0); }
		Bytes(const Bytes &) = delete;
		Bytes &operator=(const Bytes &) = delete;
		Bytes(Bytes &&other) noexcept : m_total(other.m_total), m_n(other.m_n) { other.m_n = 0; }
		Bytes &operator=(Bytes &&other) noexcept
		{
			if (this != &other) {
				Set(0);
				m_total = other.m_total;
				m_n = other.m_n;
				other.m_n = 0;
			}
			return *this;
		}
		void Set(int64_t n)
		{
			m_total->fetch_add(n - m_n, std::memory_order_relaxed);
			m_n = n;
		}
		void Add(int64_t n) { Set(m_n + n); }

	private:
		std::atomic<int64_t> *m_total;
		int64_t m_n = 0;
	};

	// The microseconds since `start`.
	inline uint64_t MicrosSince(std::chrono::steady_clock::time_point start)
	{
		return static_cast<uint64_t>(
			std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count());
	}

	// The events so far, to subtract one from another.
	struct Snapshot
	{
		uint64_t decoded = 0, decoded_us = 0, uploaded = 0, uploaded_us = 0, built = 0, built_us = 0;
		static Snapshot Now()
		{
			auto get = [](const std::atomic<uint64_t> &v) { return v.load(std::memory_order_relaxed); };
			return {get(textures_decoded.count), get(textures_decoded.us), get(textures_uploaded.count),
				get(textures_uploaded.us), get(models_built.count), get(models_built.us)};
		}
		Snapshot operator-(const Snapshot &o) const
		{
			return {decoded - o.decoded, decoded_us - o.decoded_us, uploaded - o.uploaded, uploaded_us - o.uploaded_us,
				built - o.built, built_us - o.built_us};
		}
	};
}
