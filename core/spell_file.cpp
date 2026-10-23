#include "spell_file.h"

#include <cstdlib>
#include <fstream>
#include <vector>

namespace
{
	constexpr size_t NameField = 1, ManaField = 19, SkillField = 88, FirstClassField = 92;
}

bool SpellFile::Load(const std::string &path)
{
	std::ifstream file(path, std::ios::binary);
	if (!file)
		return false;
	m_spells.clear();
	std::string line;
	std::vector<std::string> fields;
	while (std::getline(file, line)) {
		if (!line.empty() && line.back() == '\r')
			line.pop_back();
		fields.clear();
		size_t start = 0;
		while (true) {
			size_t end = line.find('^', start);
			fields.push_back(line.substr(start, end == std::string::npos ? std::string::npos : end - start));
			if (end == std::string::npos)
				break;
			start = end + 1;
		}
		if (fields.size() < FirstClassField + 15 || fields[0].empty())
			continue;
		SpellInfo spell;
		spell.name = fields[NameField];
		spell.mana = std::atoi(fields[ManaField].c_str());
		spell.skill = std::atoi(fields[SkillField].c_str());
		for (int i = 0; i < 15; ++i)
			spell.class_levels[i] = static_cast<uint8_t>(std::atoi(fields[FirstClassField + i].c_str()));
		m_spells[std::atoi(fields[0].c_str())] = std::move(spell);
	}
	return !m_spells.empty();
}

const SpellInfo *SpellFile::Find(int id) const
{
	if (id < 1 || id > 3999)
		return nullptr;
	auto it = m_spells.find(id);
	return it != m_spells.end() ? &it->second : nullptr;
}
