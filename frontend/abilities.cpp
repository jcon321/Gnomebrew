#include "abilities.h"

#include "chat_text.h"
#include "game_windows.h"
#include "ini_file.h"
#include "opcodes.h"
#include "inventory.h"
#include "skills.h"
#include "window_manager.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

namespace
{
	constexpr int SkillsColor = 0x10e; // UseSkill's messages
	constexpr int RedColor = 13;
	constexpr int DefaultColor = 0x111;

	// The combat page's skill buttons (CActionsWnd's 2..5) and the abilities page's six.
	const char *const CombatButtons[Abilities::CombatSlots] = {
		"ACP_FirstAbilityButton", "ACP_SecondAbilityButton", "ACP_ThirdAbilityButton", "ACP_FourthAbilityButton"};
	const char *const AbilityButtons[Abilities::AbilitySlots] = {"AAP_FirstAbilityButton", "AAP_SecondAbilityButton",
		"AAP_ThirdAbilityButton", "AAP_FourthAbilityButton", "AAP_FifthAbilityButton", "AAP_SixthAbilityButton"};

	enum Skill
	{
		Backstab = 8, Bash = 10, Disarm = 16, DisarmTraps = 17, Forage = 27, Mend = 32, SenseHeading = 40,
		SenseTraps = 62, Intimidation = 71, Taunt = 73,
	};
}

void Abilities::Attach(WindowManager &windows, GameWindowState &state, ZoneSession &zone, const EqStr *eqstr, Host host)
{
	m_windows = &windows;
	m_state = &state;
	m_zone = &zone;
	m_eqstr = eqstr;
	m_host = std::move(host);
	m_windows->Show("SkillsSelectWindow", false);
}

void Abilities::Reset()
{
	for (int &skill : m_abilities)
		skill = -1;
	for (int &skill : m_combat)
		skill = -1;
	// SetDefaultGameValues: every ability ready.
	for (int i = 0; i < SkillCount; ++i) {
		m_available[i] = true;
		m_last_use[i] = 0;
	}
	m_mend_ready_ms = m_combat_last_ms = 0;
	m_combat_reuse_ms = 0;
	m_alt_ready = true;
	m_select_index = -1;
	m_changed = false;
}

void Abilities::Load(const IniFile &ini)
{
	// loadCustomFromINI: a key that's there sets the button; "*" (missing) leaves it.
	auto read = [&](const char *section, const std::string &key, int &slot) {
		std::string value = ini.Get(section, key, "*NULL*");
		if (value.empty() || value[0] != '*')
			slot = std::atoi(value.c_str());
	};
	for (int i = 0; i < AbilitySlots; ++i)
		read("Abilities", "Ability" + std::to_string(i + 1), m_abilities[i]);
	for (int i = 0; i < CombatSlots; ++i)
		read("CombatSkills", "CombatSkill" + std::to_string(i + 1), m_combat[i]);
	// loadCustom: a skill we don't have comes off its button (the AA abilities aren't ours yet), and
	// a hot button holding an empty one is cleared.
	const ZoneProfile &me = m_zone->Profile();
	auto check = [&](int &skill) {
		if (skill < 0)
			return;
		if (skill >= 100 || Skills::HasSkill(me, skill) == 0) {
			skill = -1;
			m_changed = true;
		}
	};
	for (int &skill : m_abilities)
		check(skill);
	for (int &skill : m_combat)
		check(skill);
	if (m_state)
		for (HotButton &button : m_state->hot_buttons) {
			bool empty = (button.type == HotButton::CombatSkill && CombatSkill(button.value) < 0) ||
			             (button.type == HotButton::Ability && Ability(button.value) < 0);
			if (empty) {
				button = {};
				m_state->hot_buttons_changed = true;
			}
		}
	UpdateButtons();
}

void Abilities::Store(IniFile &ini) const
{
	for (int i = 0; i < AbilitySlots; ++i)
		ini.Set("Abilities", "Ability" + std::to_string(i + 1), std::to_string(m_abilities[i]));
	for (int i = 0; i < CombatSlots; ++i)
		ini.Set("CombatSkills", "CombatSkill" + std::to_string(i + 1), std::to_string(m_combat[i]));
}

uint32_t Abilities::ModifyAttackSpeed(uint32_t delay, int haste)
{
	if (delay != 0 && haste > 0)
		delay = static_cast<uint32_t>(static_cast<double>(delay) / (static_cast<float>(haste) * 0.01f));
	return delay;
}

bool Abilities::Ready(int skill) const
{
	if (skill < 0 || skill >= SkillCount)
		return false;
	return Skills::Get(skill).type == Skills::Combat ? m_alt_ready : m_available[skill];
}

void Abilities::Frame(uint64_t now_ms)
{
	m_now_ms = now_ms;
	// DoPassageOfTime: each ability ready once its reuse time has gone by since its last use (mend's
	// at its own time); the combat timer by the last combat skill's reuse, hastened.
	for (int skill = 0; skill < SkillCount; ++skill) {
		if (m_available[skill])
			continue;
		if (skill == Mend) {
			if (m_mend_ready_ms <= now_ms)
				m_available[skill] = true;
			continue;
		}
		uint64_t since = m_last_use[skill] < now_ms ? now_ms - m_last_use[skill] : 0;
		if (Skills::Get(skill).reuse_ms < since)
			m_available[skill] = true;
	}
	int haste = m_host.haste ? m_host.haste() : 0;
	if (ModifyAttackSpeed(m_combat_reuse_ms, haste) <= now_ms - m_combat_last_ms)
		m_alt_ready = true;
	UpdateButtons();
}

void Abilities::UpdateButtons()
{
	if (!m_windows)
		return;
	SidlWindow *wnd = m_windows->Find("ActionsWindow");
	if (!wnd)
		return;
	// UpdateButtonText: the skill's name; UpdateAbilityButtonStates: down while it isn't ready.
	auto update = [&](const char *id, int skill) {
		SidlWindow::Control *button = wnd->Find(id);
		if (!button)
			return;
		if (skill < 0) {
			button->checked = false;
			return;
		}
		button->text = SkillName(m_eqstr, skill);
		button->checked = !Ready(skill);
	};
	for (int i = 0; i < AbilitySlots; ++i)
		update(AbilityButtons[i], m_abilities[i]);
	for (int i = 0; i < CombatSlots; ++i)
		update(CombatButtons[i], m_combat[i]);
}

bool Abilities::RightClick(const std::string &window, const std::string &control)
{
	if (window != "ActionsWindow")
		return false;
	for (int i = 0; i < CombatSlots; ++i)
		if (control == CombatButtons[i]) {
			OpenSelect(true, i);
			return true;
		}
	for (int i = 0; i < AbilitySlots; ++i)
		if (control == AbilityButtons[i]) {
			OpenSelect(false, i);
			return true;
		}
	return false;
}

void Abilities::OpenSelect(bool combat, int index)
{
	// The tab's SetTypesToDisplay (2 the combat page, 3 the abilities page), then Activate.
	SidlWindow *wnd = m_windows ? m_windows->Find("SkillsSelectWindow") : nullptr;
	SidlWindow::Control *list = wnd ? wnd->Find("SKSW_SkillSelectorList") : nullptr;
	if (!list)
		return;
	int types = combat ? 2 : 3;
	const ZoneProfile &me = m_zone->Profile();
	m_select_skills.clear();
	list->rows.clear();
	list->selected_row = -1;
	list->row_color = 0xffffff;
	for (int skill = 0; skill < SkillCount; ++skill) {
		const Skills::Info &info = Skills::Get(skill);
		if (!info.activated || Skills::HasSkill(me, skill) == 0)
			continue;
		bool passive = info.type == Skills::Passive;
		if (((types & 1) && passive) || ((types & 2) && !passive)) {
			m_select_skills.push_back(skill);
			list->rows.push_back({SkillName(m_eqstr, skill)});
		}
	}
	m_select_combat = combat;
	m_select_index = index;
	m_windows->Show("SkillsSelectWindow", true);
	m_windows->Raise("SkillsSelectWindow");
}

bool Abilities::Click(const std::string &window, const std::string &control, uint64_t now_ms)
{
	if (window == "SkillsSelectWindow") {
		if (control != "SKSW_SkillSelectorList")
			return false;
		// The page's notification: the row's skill on the button the window was opened for, kept.
		SidlWindow *wnd = m_windows->Find("SkillsSelectWindow");
		SidlWindow::Control *list = wnd ? wnd->Find(control) : nullptr;
		int row = list ? list->selected_row : -1;
		if (m_select_index >= 0 && row >= 0 && row < static_cast<int>(m_select_skills.size())) {
			int &slot = m_select_combat ? m_combat[m_select_index] : m_abilities[m_select_index];
			slot = m_select_skills[row];
			m_changed = true;
			UpdateButtons();
		}
		m_windows->Show("SkillsSelectWindow", false);
		m_select_index = -1;
		return true;
	}
	if (window != "ActionsWindow")
		return false;
	for (int i = 0; i < CombatSlots; ++i)
		if (control == CombatButtons[i]) {
			if (m_combat[i] >= 0)
				Use(m_combat[i], now_ms);
			return true;
		}
	for (int i = 0; i < AbilitySlots; ++i)
		if (control == AbilityButtons[i]) {
			if (m_abilities[i] >= 0)
				Use(m_abilities[i], now_ms);
			return true;
		}
	return false;
}

void Abilities::DoAbility(const std::string &args, uint64_t now_ms)
{
	int n = std::atoi(args.c_str()) - 1;
	if (n < 0 || n > 9) {
		Say(13256, "Format: /doability 1 - 10", DefaultColor);
		Say(13257, "(1 - 6 are abilities, 7 - 10 are combat skills)", DefaultColor);
		return;
	}
	int skill = n < AbilitySlots ? m_abilities[n] : m_combat[n - AbilitySlots];
	if (skill != -1)
		Use(skill, now_ms);
}

void Abilities::Say(uint32_t id, const std::string &fallback, int type, const std::vector<std::string> &args)
{
	if (m_host.chat)
		m_host.chat(FormatEqString(m_eqstr, id, fallback, args), type);
}

int Abilities::Rand()
{
	m_random = m_random * 214013u + 2531011u;
	return static_cast<int>((m_random >> 16) & 0x7fff);
}

bool Abilities::CanBash() const
{
	// A shield (not a bag) in the secondary hand, or one of the three primaries that bash
	// (11050, 10099, 14383). The two-handed bash of AA 0x4f isn't ours yet.
	const auto &items = m_zone->Items();
	auto secondary = items.find(Inventory::Secondary);
	if (secondary != items.end() && secondary->second.item_class == 0 && secondary->second.item_type == 8)
		return true;
	auto primary = items.find(Inventory::Primary);
	if (primary != items.end()) {
		uint16_t id = primary->second.id;
		if (id == 11050 || id == 10099 || id == 14383)
			return true;
	}
	return false;
}

void Abilities::Use(int skill, uint64_t now_ms)
{
	if (!m_zone || skill < 0)
		return;
	const ZoneProfile &me = m_zone->Profile();
	if (m_host.hp && m_host.hp() < 1)
		return;
	if (skill < 100 && skill < SkillCount && me.skills[skill] > 253)
		return;
	if (m_zone->Self().stance == 102) {
		Say(12368, "You do not have control of yourself right now.", RedColor);
		return;
	}
	if (skill >= SkillCount)
		return; // the AA abilities (105, 107, 111): not yet
	const Skills::Info &info = Skills::Get(skill);
	if (info.type == Skills::Combat) {
		m_alt_ready = false;
		if (skill == Bash && !CanBash()) {
			uint16_t race = me.race;
			if (race != 2 && race != 9 && race != 10) {
				Say(12394, "You need to equip a shield in order to BASH", SkillsColor);
				return;
			}
		}
		if (skill == Bash || skill == Backstab || skill == 21 || skill == 23 || skill == 26 || skill == 30 ||
			skill == 38 || skill == 52)
			UseCombatSkill(skill, now_ms);
		return;
	}
	switch (skill) {
	case Forage: case Mend: case SenseHeading: case Taunt: case Intimidation: case Disarm: case DisarmTraps:
	case SenseTraps:
		break;
	default:
		return; // sneak, hide, feign death, bind wound, begging, pick pockets, tracking, fishing: not yet
	}
	if (!m_available[skill])
		return;
	// Its timer starts now (mend's runs to its own time).
	uint64_t last = m_last_use[skill];
	if (skill == Mend)
		m_mend_ready_ms = now_ms + Skills::Get(Mend).reuse_ms;
	else
		m_last_use[skill] = now_ms;
	m_available[skill] = false;

	uint16_t target = m_host.target ? m_host.target() : 0;
	const ZoneSpawn *spawn = nullptr;
	if (target != 0 && target != m_zone->MySpawnId()) {
		auto it = m_zone->Spawns().find(target);
		if (it != m_zone->Spawns().end())
			spawn = &it->second;
	}
	bool attacking = m_host.attacking && m_host.attacking();
	switch (skill) {
	case Forage:
		if (m_zone->Self().stance == 100 && !attacking) {
			m_zone->SendSkill(ZoneOp::Forage);
			return;
		}
		if (m_zone->Self().stance == 100)
			Say(148, "You can't try to forage while attacking.", SkillsColor);
		else
			Say(149, "You must be standing to forage.", SkillsColor);
		m_last_use[skill] = last; // not used after all
		m_available[skill] = true;
		return;
	case Mend:
		m_zone->SendSkill(ZoneOp::Mend);
		return;
	case DisarmTraps:
		m_zone->SendSkill(ZoneOp::DisarmTraps);
		return;
	case SenseTraps:
		m_zone->SendSkill(ZoneOp::SenseTraps);
		return;
	case SenseHeading: {
		// A roll of 1..100 (under 8 counts as 0) against the skill; over 95 always fails.
		int roll = Rand() % 100 + 1;
		if (roll < 8)
			roll = 0;
		if (Skills::HasSkill(me, SenseHeading) < roll || roll > 95) {
			Say(12426, "You have no idea what direction you are facing.", SkillsColor);
			return;
		}
		if (m_host.sense_heading)
			m_host.sense_heading();
		int heading = m_host.heading ? static_cast<int>(m_host.heading()) : 0;
		static const char *const names[] = {"North", "NorthEast", "East", "SouthEast", "South", "SouthWest", "West",
			"NorthWest"};
		int which = 0; // North: 0..32 and 480 and up
		if (heading >= 0 && heading < 480) {
			static const int bounds[] = {0x1a0, 0x160, 0x120, 0xe0, 0xa0, 0x60, 0x20};
			for (int i = 0; i < 7; ++i)
				if (heading > bounds[i]) {
					which = i + 1;
					break;
				}
		}
		std::string direction = FormatEqString(m_eqstr, 12427 + which, names[which], {});
		Say(12435, "You think you are heading %1.", SkillsColor, {direction});
		m_zone->SendSkill(ZoneOp::SenseHeading);
		return;
	}
	case Intimidation:
		if (target == 0)
			Say(12398, "You must have a target to intimidate!", SkillsColor);
		else if (target == m_zone->MySpawnId())
			Say(12691, "You can't intimidate yourself!", SkillsColor);
		else if (m_zone->Self().stance == 100)
			m_zone->SendSkill(ZoneOp::InstillDoubt);
		else
			Say(12400, "Stand up first!", SkillsColor);
		return;
	case Taunt: {
		if (!spawn || spawn->npc != 1) {
			Say(12395, "You must target an NPC to taunt first.", SkillsColor);
			return;
		}
		float range = m_host.melee_range ? m_host.melee_range(target) : 14;
		if (m_host.distance && m_host.distance(target) > range) {
			Say(386, "You are too far away from your target to taunt.", SkillsColor);
			return;
		}
		if (spawn->race == 141 || spawn->race == 72 || spawn->race == 73) {
			Say(12396, "You can't taunt inanimate objects.", SkillsColor);
			return;
		}
		Say(12397, "You taunt %1 to ignore others and attack you!", SkillsColor, {spawn->DisplayName()});
		m_zone->SendSkill(ZoneOp::Taunt, &target, sizeof(target));
		return;
	}
	case Disarm: {
		if (target == 0) {
			Say(12416, "You must target someone to disarm first.", SkillsColor);
			return;
		}
		if (target == m_zone->MySpawnId()) {
			Say(12417, "You cannot disarm yourself!", SkillsColor);
			return;
		}
		if (m_host.distance && m_host.distance(target) > 15) {
			Say(12418, "You are too far away to disarm. Get closer.", SkillsColor);
			return;
		}
		if (spawn && spawn->npc > 1) {
			Say(12419, "You see no sport in disarming the dead.", SkillsColor);
			return;
		}
		if (m_host.can_hit && !m_host.can_hit(target))
			return;
		// Disarm_Struct {us, target, our skill, 0xff}.
		uint8_t body[8] = {};
		uint16_t me_id = m_zone->MySpawnId();
		int16_t value = me.skills[Disarm];
		std::memcpy(body, &me_id, 2);
		std::memcpy(body + 2, &target, 2);
		std::memcpy(body + 4, &value, 2);
		body[6] = 0xff;
		m_zone->SendSkill(ZoneOp::Disarm, body, sizeof(body));
		return;
	}
	default:
		return;
	}
}

void Abilities::UseCombatSkill(int skill, uint64_t now_ms)
{
	// EQPlayer::DoAttack(100, skill, target): not while casting; attacking ends our invisibility
	// (the stunned check and hide's end aren't ours yet).
	if (m_host.casting && m_host.casting())
		return;
	if (m_host.remove_affect)
		for (int spa : {12, 28, 29, 94})
			m_host.remove_affect(spa);
	const ZoneProfile &me = m_zone->Profile();
	const Skills::Info &info = Skills::Get(skill);
	if (info.combat < 1)
		return;
	bool big = me.race == 2 || me.race == 9 || me.race == 10; // they bash with anything
	if (skill != Bash || !big) {
		if (Skills::HasSkill(me, skill) == 0)
			return;
		if (skill == Bash && !CanBash())
			return;
	}
	if (skill == Backstab) {
		auto primary = m_zone->Items().find(Inventory::Primary);
		if (primary == m_zone->Items().end() || primary->second.item_type != 2) {
			Say(12874, "You need a piercing weapon as your primary weapon in order to backstab!", DefaultColor);
			return;
		}
	}
	// The combat timer: the last skill's reuse, hastened, from its use; then this one's from now.
	int haste = m_host.haste ? m_host.haste() : 0;
	if (now_ms - m_combat_last_ms < ModifyAttackSpeed(m_combat_reuse_ms, haste))
		return;
	m_combat_last_ms = now_ms;
	m_combat_reuse_ms = info.reuse_ms;
	uint8_t stance = m_zone->Self().stance;
	if (stance != 100) {
		if (stance == 102)
			Say(12368, "You do not have control of yourself right now.", RedColor);
		else
			Say(12369, "You must be standing to attack!", RedColor);
		return;
	}
	uint16_t target = m_host.target ? m_host.target() : 0;
	if (m_host.can_hit && !m_host.can_hit(target))
		return;
	if (target == 0)
		return;
	// DoMissileHit: CombatAbility_Struct {target, 0, 100, skill}.
	uint8_t body[12] = {};
	uint32_t attack = 100, used = static_cast<uint32_t>(skill);
	std::memcpy(body, &target, 2);
	std::memcpy(body + 4, &attack, 4);
	std::memcpy(body + 8, &used, 4);
	m_zone->SendSkill(ZoneOp::CombatAbility, body, sizeof(body));
}
