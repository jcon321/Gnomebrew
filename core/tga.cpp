#include "tga.h"

bool DecodeTga(const std::vector<uint8_t> &d, Image &image)
{
	if (d.size() < 18)
		return false;
	uint8_t id_length = d[0];
	uint8_t colormap_type = d[1];
	uint8_t type = d[2];
	int width = d[12] | (d[13] << 8);
	int height = d[14] | (d[15] << 8);
	int bpp = d[16];
	bool top_down = (d[17] & 0x20) != 0;
	if (colormap_type != 0 || (type != 2 && type != 10) || (bpp != 24 && bpp != 32) || width <= 0 || height <= 0)
		return false;

	int bytes = bpp / 8;
	size_t pos = 18 + id_length;
	size_t count = static_cast<size_t>(width) * height;
	std::vector<uint32_t> linear;
	linear.reserve(count);

	auto read_pixel = [&](uint32_t &out) {
		if (pos + bytes > d.size())
			return false;
		uint32_t a = bytes == 4 ? d[pos + 3] : 0xff;
		out = (a << 24) | (d[pos + 2] << 16) | (d[pos + 1] << 8) | d[pos];
		pos += bytes;
		return true;
	};

	if (type == 2) {
		while (linear.size() < count) {
			uint32_t p;
			if (!read_pixel(p))
				return false;
			linear.push_back(p);
		}
	}
	else {
		while (linear.size() < count) {
			if (pos >= d.size())
				return false;
			uint8_t header = d[pos++];
			size_t run = (header & 0x7f) + 1;
			if (linear.size() + run > count)
				return false;
			if (header & 0x80) {
				uint32_t p;
				if (!read_pixel(p))
					return false;
				linear.insert(linear.end(), run, p);
			}
			else {
				for (size_t i = 0; i < run; ++i) {
					uint32_t p;
					if (!read_pixel(p))
						return false;
					linear.push_back(p);
				}
			}
		}
	}

	image.width = width;
	image.height = height;
	image.pixels.resize(count);
	image.has_alpha = false;
	for (int y = 0; y < height; ++y) {
		int src_row = top_down ? y : height - 1 - y;
		for (int x = 0; x < width; ++x) {
			uint32_t p = linear[static_cast<size_t>(src_row) * width + x];
			image.pixels[static_cast<size_t>(y) * width + x] = p;
			if ((p >> 24) != 0xff)
				image.has_alpha = true;
		}
	}
	return true;
}
