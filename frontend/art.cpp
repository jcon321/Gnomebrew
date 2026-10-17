#include "art.h"
#include "canvas.h"

bool FrontEndArt::Open(const std::string &eq_dir, std::string &error)
{
	m_archives.clear();
	m_cache.clear();
	m_missing.clear();

	std::string base = eq_dir;
	if (!base.empty() && base.back() != '/' && base.back() != '\\')
		base += '/';

	for (int i = 1; i <= 17; ++i) {
		auto archive = std::make_unique<PfsArchive>();
		if (archive->Open(base + "eqfeart" + std::to_string(i) + ".pak"))
			m_archives.push_back(std::move(archive));
	}
	if (m_archives.empty()) {
		error = "No eqfeart*.pak files found in '" + eq_dir + "'. Point EQDir at your EverQuest (TAKP) folder.";
		return false;
	}
	return true;
}

const Image &FrontEndArt::Get(const std::string &name)
{
	std::string key = ToLower(name);
	auto it = m_cache.find(key);
	if (it != m_cache.end())
		return it->second;

	Image image;
	std::vector<uint8_t> data;
	for (const auto &archive : m_archives) {
		if (archive->Read(key, data) && DecodeBmp(data, image))
			return m_cache.emplace(key, std::move(image)).first->second;
	}

	m_missing.push_back(key);
	image.width = image.height = 1;
	image.pixels.assign(1, ColorKey);
	return m_cache.emplace(key, std::move(image)).first->second;
}
