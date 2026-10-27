#include "spell_file.h"

#include <cstdlib>
#include <fstream>
#include <vector>

namespace
{
	constexpr size_t NameField = 1, YouCastField = 4, OtherCastsField = 5, CastOnYouField = 6, CastOnOtherField = 7,
	                 FadesField = 8, RangeField = 9, CastTimeField = 13, RecastField = 15, AeDurationField = 18, ManaField = 19,
	                 GoodEffectField = 71, FirstSpaField = 74, TargetTypeField = 86, SkillField = 88, FirstClassField = 92,
	                 CastingAnimField = 107, TargetAnimField = 108, EffectField = 110, IconField = 131,
	                 NewEffectField = 132, DeletableField = 136;
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
		if (fields.size() <= DeletableField || fields[0].empty())
			continue;
		auto number = [&](size_t field) { return std::atoi(fields[field].c_str()); };
		SpellInfo spell;
		spell.name = fields[NameField];
		spell.you_cast = fields[YouCastField];
		spell.other_casts = fields[OtherCastsField];
		spell.cast_on_you = fields[CastOnYouField];
		spell.cast_on_other = fields[CastOnOtherField];
		spell.fades = fields[FadesField];
		spell.range = static_cast<float>(std::atof(fields[RangeField].c_str()));
		spell.cast_ms = number(CastTimeField);
		spell.recast_ms = number(RecastField);
		spell.ae_duration = number(AeDurationField);
		spell.mana = number(ManaField);
		spell.good_effect = static_cast<uint8_t>(number(GoodEffectField));
		spell.first_spa = static_cast<uint8_t>(number(FirstSpaField));
		spell.target_type = static_cast<uint8_t>(number(TargetTypeField));
		spell.skill = number(SkillField);
		for (int i = 0; i < 15; ++i)
			spell.class_levels[i] = static_cast<uint8_t>(number(FirstClassField + i));
		// An empty field is ParserGetIntField's atoi(""), 0, not the constructor's default.
		spell.casting_anim = static_cast<uint8_t>(number(CastingAnimField));
		spell.target_anim = static_cast<uint8_t>(number(TargetAnimField));
		spell.effect = number(EffectField);
		spell.new_effect = number(NewEffectField);
		spell.icon = number(IconField);
		spell.deletable = number(DeletableField) != 0;
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
