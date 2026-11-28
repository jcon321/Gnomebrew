#include "block_compress.h"

#include <algorithm>
#include <cmath>

namespace block_compress
{
	namespace
	{
		uint16_t To565(float r, float g, float b)
		{
			auto q = [](float v, int max) {
				return static_cast<uint16_t>(std::clamp(static_cast<int>(std::lround(v / 255.0f * max)), 0, max));
			};
			return static_cast<uint16_t>(q(r, 31) << 11 | q(g, 63) << 5 | q(b, 31));
		}

		void Expand565(uint16_t c, int rgb[3])
		{
			int r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
			rgb[0] = r << 3 | r >> 2;
			rgb[1] = g << 2 | g >> 4;
			rgb[2] = b << 3 | b >> 2;
		}

		// The palette BC1's four-color mode makes of two endpoints, as DecodeDds reads it.
		void Palette(uint16_t c0, uint16_t c1, int palette[4][3])
		{
			Expand565(c0, palette[0]);
			Expand565(c1, palette[1]);
			for (int c = 0; c < 3; ++c) {
				palette[2][c] = (palette[0][c] * 2 + palette[1][c]) / 3;
				palette[3][c] = (palette[0][c] + palette[1][c] * 2) / 3;
			}
		}

		// Each texel's nearest of the endpoints' palette (c0 > c1), and the squared error.
		float Fit(const float px[16][3], uint16_t c0, uint16_t c1, int indices[16])
		{
			int palette[4][3];
			Palette(c0, c1, palette);
			float total = 0;
			for (int i = 0; i < 16; ++i) {
				int best = 0;
				float best_distance = 1e30f;
				for (int k = 0; k < (c0 == c1 ? 1 : 4); ++k) {
					float distance = 0;
					for (int c = 0; c < 3; ++c) {
						float d = px[i][c] - static_cast<float>(palette[k][c]);
						distance += d * d;
					}
					if (distance < best_distance) {
						best_distance = distance;
						best = k;
					}
				}
				indices[i] = best;
				total += best_distance;
			}
			return total;
		}

		// The block's colors as BC1's four-color mode (c0 > c1): endpoints along the colors'
		// principal axis, then refined by least squares for the texels' palette entries.
		void ColorBlock(const uint32_t texels[16], uint8_t out[8])
		{
			float px[16][3];
			float mean[3] = {0, 0, 0};
			for (int i = 0; i < 16; ++i) {
				px[i][0] = static_cast<float>((texels[i] >> 16) & 0xff);
				px[i][1] = static_cast<float>((texels[i] >> 8) & 0xff);
				px[i][2] = static_cast<float>(texels[i] & 0xff);
				for (int c = 0; c < 3; ++c)
					mean[c] += px[i][c] / 16;
			}
			// The principal axis: the covariance's largest eigenvector, by power iteration.
			float cov[6] = {0, 0, 0, 0, 0, 0}; // rr rg rb gg gb bb
			for (int i = 0; i < 16; ++i) {
				float d[3] = {px[i][0] - mean[0], px[i][1] - mean[1], px[i][2] - mean[2]};
				cov[0] += d[0] * d[0];
				cov[1] += d[0] * d[1];
				cov[2] += d[0] * d[2];
				cov[3] += d[1] * d[1];
				cov[4] += d[1] * d[2];
				cov[5] += d[2] * d[2];
			}
			// Started from the texel farthest from the mean (a start square to the axis stays put).
			float axis[3] = {1, 1, 1}, farthest = 0;
			for (int i = 0; i < 16; ++i) {
				float d[3] = {px[i][0] - mean[0], px[i][1] - mean[1], px[i][2] - mean[2]};
				float distance = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
				if (distance > farthest) {
					farthest = distance;
					std::copy(d, d + 3, axis);
				}
			}
			for (int k = 0; k < 8; ++k) {
				float next[3] = {cov[0] * axis[0] + cov[1] * axis[1] + cov[2] * axis[2],
					cov[1] * axis[0] + cov[3] * axis[1] + cov[4] * axis[2],
					cov[2] * axis[0] + cov[4] * axis[1] + cov[5] * axis[2]};
				float length = std::sqrt(next[0] * next[0] + next[1] * next[1] + next[2] * next[2]);
				if (length < 1e-6f)
					break;
				for (int c = 0; c < 3; ++c)
					axis[c] = next[c] / length;
			}
			float low = 0, high = 0;
			for (int i = 0; i < 16; ++i) {
				float t = (px[i][0] - mean[0]) * axis[0] + (px[i][1] - mean[1]) * axis[1] + (px[i][2] - mean[2]) * axis[2];
				low = std::min(low, t);
				high = std::max(high, t);
			}
			auto endpoints = [&](float inset, uint16_t &c0, uint16_t &c1) {
				float h = high - (high - low) * inset, l = low + (high - low) * inset;
				c0 = To565(mean[0] + axis[0] * h, mean[1] + axis[1] * h, mean[2] + axis[2] * h);
				c1 = To565(mean[0] + axis[0] * l, mean[1] + axis[1] * l, mean[2] + axis[2] * l);
				if (c0 < c1)
					std::swap(c0, c1);
			};
			uint16_t best0, best1;
			endpoints(1.0f / 16, best0, best1);
			int indices[16];
			float best = Fit(px, best0, best1, indices);
			uint16_t c0, c1;
			endpoints(0, c0, c1);
			int trial[16];
			if (float error = Fit(px, c0, c1, trial); error < best) {
				best = error;
				best0 = c0;
				best1 = c1;
				std::copy(trial, trial + 16, indices);
			}
			// Least squares: the endpoints that best give each texel its entry's mix of them
			// (entry 2 is 2/3 of c0, 3 is 1/3), twice over.
			for (int pass = 0; pass < 2 && best > 0 && best0 != best1; ++pass) {
				static const float weight[4] = {1, 0, 2.0f / 3, 1.0f / 3};
				float aa = 0, ab = 0, bb = 0, ax[3] = {0, 0, 0}, bx[3] = {0, 0, 0};
				for (int i = 0; i < 16; ++i) {
					float a = weight[indices[i]], b = 1 - a;
					aa += a * a;
					ab += a * b;
					bb += b * b;
					for (int c = 0; c < 3; ++c) {
						ax[c] += a * px[i][c];
						bx[c] += b * px[i][c];
					}
				}
				float det = aa * bb - ab * ab;
				if (std::fabs(det) < 1e-6f)
					break;
				float e0[3], e1[3];
				for (int c = 0; c < 3; ++c) {
					e0[c] = (ax[c] * bb - bx[c] * ab) / det;
					e1[c] = (bx[c] * aa - ax[c] * ab) / det;
				}
				c0 = To565(e0[0], e0[1], e0[2]);
				c1 = To565(e1[0], e1[1], e1[2]);
				if (c0 < c1)
					std::swap(c0, c1);
				float error = Fit(px, c0, c1, trial);
				if (error >= best)
					break;
				best = error;
				best0 = c0;
				best1 = c1;
				std::copy(trial, trial + 16, indices);
			}
			uint32_t bits = 0;
			for (int i = 0; i < 16; ++i)
				bits |= static_cast<uint32_t>(best0 == best1 ? 0 : indices[i]) << (i * 2);
			out[0] = static_cast<uint8_t>(best0);
			out[1] = static_cast<uint8_t>(best0 >> 8);
			out[2] = static_cast<uint8_t>(best1);
			out[3] = static_cast<uint8_t>(best1 >> 8);
			for (int k = 0; k < 4; ++k)
				out[4 + k] = static_cast<uint8_t>(bits >> (8 * k));
		}

		// BC3's alpha: its highest and lowest as the ends of the eight-value mode.
		void AlphaBlock(const uint32_t texels[16], uint8_t out[8])
		{
			int a0 = 0, a1 = 255;
			for (int i = 0; i < 16; ++i) {
				int a = static_cast<int>(texels[i] >> 24);
				a0 = std::max(a0, a);
				a1 = std::min(a1, a);
			}
			int values[8] = {a0, a1, 0, 0, 0, 0, 0, 0};
			for (int i = 1; i < 7; ++i)
				values[i + 1] = ((7 - i) * a0 + i * a1) / 7;
			uint64_t bits = 0;
			if (a0 != a1) {
				for (int i = 0; i < 16; ++i) {
					int a = static_cast<int>(texels[i] >> 24);
					int best = 0;
					for (int k = 1; k < 8; ++k)
						if (std::abs(values[k] - a) < std::abs(values[best] - a))
							best = k;
					bits |= static_cast<uint64_t>(best) << (3 * i);
				}
			}
			out[0] = static_cast<uint8_t>(a0);
			out[1] = static_cast<uint8_t>(a1);
			for (int k = 0; k < 6; ++k)
				out[2 + k] = static_cast<uint8_t>(bits >> (8 * k));
		}
	}

	std::vector<uint8_t> Compress(const uint32_t *texels, int width, int height, Format format)
	{
		int across = (width + 3) / 4, down = (height + 3) / 4;
		int bytes = BlockBytes(format);
		std::vector<uint8_t> out(static_cast<size_t>(across) * down * bytes);
		uint8_t *block = out.data();
		for (int by = 0; by < down; ++by) {
			for (int bx = 0; bx < across; ++bx) {
				uint32_t texel[16];
				for (int y = 0; y < 4; ++y)
					for (int x = 0; x < 4; ++x)
						texel[y * 4 + x] = texels[static_cast<size_t>(std::min(by * 4 + y, height - 1)) * width +
						                          std::min(bx * 4 + x, width - 1)];
				if (format == Format::BC3) {
					AlphaBlock(texel, block);
					ColorBlock(texel, block + 8);
				}
				else
					ColorBlock(texel, block);
				block += bytes;
			}
		}
		return out;
	}
}
