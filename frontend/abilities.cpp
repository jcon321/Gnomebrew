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
		Backstab = 8, BindWoundSkill = 9, Bash = 10, Disarm = 16, DisarmTraps = 17, FeignDeath = 25, Forage = 27,
		HideSkill = 29, Mend = 32, SenseHeading = 40, SneakSkill = 42, PickPockets = 48, Tracking = 53,
		Fishing = 55, SenseTraps = 62, Begging = 67, Intimidation = 71, Taunt = 73,
	};
	constexpr uint8_t Standing = 100, NoControl = 102, Kneeling = 105, Lying = 115;
	constexpr int Bandage = 18, FishingPole = 36, Bait = 37; // item types
	constexpr uint16_t AppearanceInvis = 3, AppearanceSneak = 15;
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
	m_hidden = false;
	m_activity = -1;
	m_hide_ready_ms = 0;
	m_busy = m_waiting = 0;
	m_fishing = 0;
	m_fishing_ms = 0;
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
		if (skill == HideSkill) {
			// Hide's own time, and not while we're hidden.
			if (m_hide_ready_ms <= now_ms && !m_hidden)
				m_available[skill] = true;
			continue;
		}
		uint64_t since = m_last_use[skill] < now_ms ? now_ms - m_last_use[skill] : 0;
		if (Skills::Get(skill).reuse_ms < since && (skill != SneakSkill || !m_zone || m_zone->Self().sneaking == 0))
			m_available[skill] = true;
	}
	if (m_zone) {
		// Hiding ends as we move (but for a rogue sneaking).
		if (m_hidden || m_activity == HideSkill) {
			const ZoneProfile &me = m_zone->Profile();
			bool moved = m_still[0] != static_cast<int>(me.x) || m_still[1] != static_cast<int>(me.y) ||
			             m_still[2] != static_cast<int>(me.z);
			if (moved && (me.class_id != 9 || m_zone->Self().sneaking == 0)) {
				if (m_activity == HideSkill)
					m_activity = -1;
				m_hidden = false;
				UpdateMyVisibleStatus();
				Say(12682, "You have moved and are no longer hidden!!", SkillsColor);
			}
		}
		if (m_fishing != 0)
			DoFishingEvent(now_ms);
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
	list->row_color = 0xffffff; // CSkillsSelectWnd::CSkillsSelectWnd's SetColors(white, yellow, gray)
	list->hover_color = 0xffff00;
	list->select_color = 0x808080;
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
	// (11050, 10099, 14383), or with 2 Hand Bash (AA 0x4f) a two-handed slashing, blunt or piercing
	// weapon (item types 1, 4, 35) in the primary.
	const auto &items = m_zone->Items();
	if (auto two_handed = items.find(Inventory::Primary);
	    two_handed != items.end() && m_zone->Profile().aa_ranks[0x4f] != 0) {
		int type = two_handed->second.item_type;
		if (type == 1 || type == 4 || type == 35)
			return true;
	}
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
	if (m_zone->Self().stance == NoControl) {
		Say(12368, "You do not have control of yourself right now.", RedColor);
		return;
	}
	// On a horse (here only its buff, SPA 113): only these.
	if (m_host.total && m_host.total(113) != 0) {
		static const int allowed[] = {Tracking, Bash, Disarm, 30, Mend, SenseHeading, Fishing, Intimidation, Taunt, 105, 107};
		if (std::find(std::begin(allowed), std::end(allowed), skill) == std::end(allowed)) {
			Say(12393, "You can not use this skill on horse back.", RedColor);
			return;
		}
	}
	if (skill >= SkillCount)
		return; // the AA abilities (105 Harm Touch, 107 Lay on Hands, 111 Slam): the AAs aren't ours
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
	// Its timer starts now; sneaking stops, and hiding is called off, whether it's ready or not.
	bool stopping = (skill == SneakSkill && m_zone->Self().sneaking != 0) || (skill == HideSkill && m_hidden);
	uint64_t last = m_last_use[skill];
	if (!stopping) {
		if (!m_available[skill])
			return;
		const uint8_t *ranks = me.aa_ranks;
		// The AAs' shorter times: Hastened Stealth (0xd1) a second a rank off hide's
		// (CalculateHideTimeReduce); Adv. Trap Negotiation (0x79) and Rapid Feign (100) the sense and
		// disarm traps and feign death timers at 90, 70 or 50% (the last use set back the rest).
		auto shortened = [&](int rank) {
			uint64_t reuse = Skills::Get(skill).reuse_ms;
			uint64_t percent = rank == 1 ? 90 : rank == 2 ? 70 : rank == 3 ? 50 : 100;
			uint64_t cut = reuse - reuse * percent / 100;
			return now_ms > cut ? now_ms - cut : 0;
		};
		if (skill == HideSkill) {
			int64_t reuse = Skills::Get(HideSkill).reuse_ms;
			int rank = ranks[0xd1];
			if (rank >= 1 && rank <= 3)
				reuse -= 1000 * rank;
			m_hide_ready_ms = now_ms + reuse;
		}
		else if ((skill == SenseTraps || skill == DisarmTraps) && ranks[0x79] != 0)
			m_last_use[skill] = shortened(ranks[0x79]);
		else if (skill == FeignDeath && ranks[100] != 0)
			m_last_use[skill] = shortened(ranks[100]);
		else if (skill == Mend)
			m_mend_ready_ms = now_ms + Skills::Get(Mend).reuse_ms;
		else if (skill == PickPockets || skill == Begging) {
			// One timer for the two.
			m_last_use[PickPockets] = m_last_use[Begging] = now_ms;
			m_available[PickPockets] = m_available[Begging] = false;
		}
		else
			m_last_use[skill] = now_ms;
		m_available[skill] = false;
	}

	uint16_t target = m_host.target ? m_host.target() : 0;
	const ZoneSpawn *spawn = nullptr;
	if (target != 0 && target != m_zone->MySpawnId()) {
		auto it = m_zone->Spawns().find(target);
		if (it != m_zone->Spawns().end())
			spawn = &it->second;
	}
	bool attacking = m_host.attacking && m_host.attacking();
	switch (skill) {
	case SneakSkill:
		Sneak();
		return;
	case Forage:
		if (m_zone->Self().stance == Standing && !attacking) {
			m_zone->SendSkill(ZoneOp::Forage);
			return;
		}
		if (m_zone->Self().stance == Standing)
			Say(148, "You can't try to forage while attacking.", SkillsColor);
		else
			Say(149, "You must be standing to forage.", SkillsColor);
		m_last_use[skill] = last; // not used after all
		m_available[skill] = true;
		return;
	case BindWoundSkill:
		BindWound(target);
		return;
	case FeignDeath: {
		// Down at once; the zone works out whether they believe it.
		if (m_host.change_stance)
			m_host.change_stance(Lying);
		uint8_t body[6] = {};
		uint16_t id = m_zone->MySpawnId();
		std::memcpy(body, &id, 2);
		m_zone->SendSkill(ZoneOp::FeignDeath, body, sizeof(body));
		return;
	}
	case HideSkill:
		Hide();
		return;
	case PickPockets:
		PickPocket(target);
		return;
	case Tracking:
		if (m_host.open_tracking)
			m_host.open_tracking();
		m_zone->SendSkill(ZoneOp::Track);
		return;
	case Fishing:
		Fish();
		return;
	case Begging:
		Beg(target);
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
		else if (m_zone->Self().stance == Standing)
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
		// Disarming shows us (VisibilityState 0, the invisibility buffs off).
		if (m_zone->Self().invis != 0) {
			if (m_host.remove_affect)
				for (int spa : {12, 28, 29, 94})
					m_host.remove_affect(spa);
			m_zone->SendAppearance(AppearanceInvis, 0);
		}
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

void Abilities::Sneak()
{
	// Sneaking, it stops (the zone told: SpawnAppearance 15, 0).
	ZoneSpawn &self = m_zone->EditSelf();
	if (self.sneaking != 0) {
		m_zone->SendAppearance(AppearanceSneak, 0);
		Say(12338, "You stop sneaking.", SkillsColor);
		return;
	}
	if (m_host.attacking && m_host.attacking())
		Say(12420, "You can't start sneaking while attacking.", SkillsColor);
	else if (m_host.swimming && m_host.swimming())
		Say(12421, "You can only sneak on dry land.", SkillsColor);
	else {
		m_zone->SendSkill(ZoneOp::Sneak); // the zone's SpawnAppearance 15 says whether it took
		Say(12422, "You begin to sneak...", SkillsColor);
	}
}

void Abilities::Hide()
{
	if (m_activity == HideSkill) {
		m_hidden = false;
		Say(12415, "You stop hiding.", SkillsColor);
		UpdateMyVisibleStatus();
		m_activity = -1;
		return;
	}
	if (m_host.attacking && m_host.attacking()) {
		Say(12423, "You can't try to hide while attacking.", SkillsColor);
		return;
	}
	// Standing still: within a thousandth a tick each way, more over 50 skill (a two-thousandth a
	// point, half at most). Moving, Nimble Evasion (AA 0xcd) hides us anyway 20% of the time a rank
	// (CheckHidingSkillBonus: RandNbr % 100 + 1 under it).
	double still = 0.001;
	int skill = Skills::HasSkill(m_zone->Profile(), HideSkill);
	if (skill > 50)
		still = std::min(static_cast<double>(skill - 50) * 0.001 * 0.5, 0.5);
	float vx = 0, vy = 0, vz = 0;
	if (m_host.velocity)
		m_host.velocity(vx, vy, vz);
	bool moving = std::fabs(vx) > still || std::fabs(vy) > still || std::fabs(vz) > still;
	if (moving) {
		int chance = m_zone->Profile().aa_ranks[0xcd] * 20;
		if (Rand() % 100 + 1 < chance)
			moving = false;
	}
	if (moving) {
		Say(12424, "You must stand perfectly still to hide!", SkillsColor);
		return;
	}
	const ZoneProfile &me = m_zone->Profile();
	m_activity = HideSkill;
	m_still[0] = static_cast<int>(me.x);
	m_still[1] = static_cast<int>(me.y);
	m_still[2] = static_cast<int>(me.z);
	m_hidden = true;
	m_zone->SendSkill(ZoneOp::Hide);
	Say(12425, "You begin to hide...", SkillsColor);
	UpdateMyVisibleStatus();
}

bool Abilities::FindItemByType(int type, int16_t &slot, int &bag_slot) const
{
	slot = -1;
	bag_slot = -1;
	const auto &items = m_zone->Items();
	for (int16_t s = 0; s < 30; ++s) {
		auto it = items.find(s);
		if (it == items.end())
			continue;
		const ZoneItem &item = it->second;
		if (item.item_class == 0 && item.item_type == type) {
			slot = s;
			return true;
		}
		if (item.item_class == 1)
			for (int n = 0; n < item.bag_slots; ++n) {
				auto in = items.find(Inventory::BagSlot(s, n));
				if (in != items.end() && in->second.item_class == 0 && in->second.item_type == type) {
					slot = s;
					bag_slot = n;
					return true;
				}
			}
	}
	return false;
}

void Abilities::BindWound(uint16_t target)
{
	if (m_activity == BindWoundSkill)
		return;
	// A player (us too), bandages, within 15.
	uint16_t my_id = m_zone->MySpawnId();
	const ZoneSpawn *them = nullptr;
	if (target != 0 && target != my_id) {
		auto it = m_zone->Spawns().find(target);
		if (it != m_zone->Spawns().end())
			them = &it->second;
	}
	if (target == 0 || (target != my_id && (!them || them->npc != 0))) {
		Say(144, "You must target a player to bandage.", SkillsColor);
		return;
	}
	int16_t slot;
	int bag;
	if (!FindItemByType(Bandage, slot, bag)) {
		Say(146, "You can't bandage without bandages, go buy some.", SkillsColor);
		return;
	}
	if (them && m_host.distance && m_host.distance(target) >= 15) {
		Say(420, "You must be closer to bandage %1.", SkillsColor, {them->DisplayName()});
		return;
	}
	++m_waiting;
	++m_busy;
	uint8_t body[4] = {};
	std::memcpy(body, &target, 2);
	m_zone->SendSkill(ZoneOp::BindWound, body, sizeof(body));
	if (m_host.change_stance)
		m_host.change_stance(Kneeling);
	if (!them)
		Say(12436, "You begin to bandage yourself.", SkillsColor);
	else
		Say(12437, "You begin to bandage %1.", SkillsColor, {them->DisplayName()});
}

void Abilities::Beg(uint16_t target)
{
	const ZoneSpawn *them = nullptr;
	if (auto it = m_zone->Spawns().find(target); target != 0 && it != m_zone->Spawns().end())
		them = &it->second;
	if (!them || them->npc != 1) {
		Say(12401, "You must target someone to beg from first.", SkillsColor);
		return;
	}
	if (m_host.attacking && m_host.attacking()) {
		Say(12402, "You can't try to beg while attacking.", SkillsColor);
		return;
	}
	if (m_host.distance && m_host.distance(target) > 15) {
		Say(12403, "You are too far away to beg. Get closer.", SkillsColor);
		return;
	}
	if (m_zone->Self().invis != 0) {
		Say(12404, "How can you beg from someone when they can't see you?!", SkillsColor);
		return;
	}
	// Begging_Struct {target, us, our skill, 0xff asking, ..}.
	uint8_t body[18] = {};
	uint16_t me_id = m_zone->MySpawnId();
	int16_t skill = m_zone->Profile().skills[Begging];
	std::memcpy(body, &target, 2);
	std::memcpy(body + 2, &me_id, 2);
	std::memcpy(body + 4, &skill, 2);
	body[6] = 0xff;
	++m_busy;
	m_zone->SendSkill(ZoneOp::Begging, body, sizeof(body));
}

void Abilities::PickPocket(uint16_t target)
{
	uint16_t my_id = m_zone->MySpawnId();
	const ZoneSpawn *them = nullptr;
	if (auto it = m_zone->Spawns().find(target); target != 0 && it != m_zone->Spawns().end())
		them = &it->second;
	if (target == 0 || (!them && target != my_id)) {
		Say(12405, "You must target a player to steal from first.", SkillsColor);
		return;
	}
	if (them && them->gm != 0)
		return;
	if (them && them->npc > 1) {
		Say(12406, "You must target a player to steal from first.  You may not steal from corpses.", SkillsColor);
		return;
	}
	if (m_host.stunned && m_host.stunned()) {
		Say(12407, "You can't try to steal while STUNNED!", SkillsColor);
		return;
	}
	if (m_host.attacking && m_host.attacking()) {
		Say(12408, "You can't try to steal while attacking.", SkillsColor);
		return;
	}
	if (target == my_id) {
		Say(12409, "You can't seem to steal from yourself for some reason...", SkillsColor);
		return;
	}
	if (them->npc == 0) {
		// A player: both of us must be PvP (the deity rules of a PvP server's type 4 aren't ours).
		if (them->pvp == 0) {
			Say(12410, "You may not steal from a person who does not follow the ways of chaos....", SkillsColor);
			return;
		}
		if (m_zone->Self().pvp == 0) {
			Say(12411, "You must follow the ways of chaos to steal...", SkillsColor);
			return;
		}
	}
	if (m_host.distance && m_host.distance(target) > 15) {
		Say(12414, "You are too far away to steal. Get closer.", SkillsColor);
		return;
	}
	// PickPocket_Struct {target, us, our skill, 0xff asking, ..}; stealing shows us.
	uint8_t body[18] = {};
	int16_t skill = m_zone->Profile().skills[PickPockets];
	std::memcpy(body, &target, 2);
	std::memcpy(body + 2, &my_id, 2);
	std::memcpy(body + 4, &skill, 2);
	body[6] = 0xff;
	++m_busy;
	m_zone->SendSkill(ZoneOp::PickPocket, body, sizeof(body));
	MakeMeVisible();
}

void Abilities::Fish()
{
	if (m_fishing != 0) {
		Say(156, "You are already fishing!", SkillsColor);
		return;
	}
	if (m_host.swimming && m_host.swimming()) {
		Say(157, "You must be on dry land to fish.", SkillsColor);
		return;
	}
	if (m_zone->Items().count(Inventory::Cursor)) {
		Say(158, "You can't fish while holding something.", SkillsColor);
		return;
	}
	if (m_host.attacking && m_host.attacking()) {
		Say(159, "You can't start fishing while attacking!", SkillsColor);
		return;
	}
	int16_t slot;
	int bag;
	if (!FindItemByType(FishingPole, slot, bag)) {
		Say(160, "You can't fish without a fishing pole, go buy one.", SkillsColor);
		return;
	}
	if (bag != -1 || (slot != Inventory::Primary && slot != Inventory::Secondary)) {
		Say(161, "You need to put your fishing pole in your primary hand.", SkillsColor);
		return;
	}
	if (!FindItemByType(Bait, slot, bag)) {
		Say(162, "You can't fish without fishing bait, go buy some.", SkillsColor);
		return;
	}
	Say(163, "You cast your line.", SkillsColor);
	const ZoneProfile &me = m_zone->Profile();
	m_activity = Fishing;
	m_still[0] = static_cast<int>(me.x);
	m_still[1] = static_cast<int>(me.y);
	m_still[2] = static_cast<int>(me.z);
	// The cast (animation 5 at half speed), for everyone to see.
	if (m_zone->Self().stance == Standing) {
		if (m_host.animate)
			m_host.animate(5, 0.5f);
		m_zone->SendAnimation(5, 0.5f);
	}
	m_fishing_ms = m_now_ms + 2000;
	m_fishing = 1;
	m_zone->SendSkill(ZoneOp::Fishing);
}

void Abilities::DoFishingEvent(uint64_t now_ms)
{
	// EQ_Character::DoFishingEvent (@004bf8a7): moving calls it off.
	if (m_host.speed && std::fabs(m_host.speed()) > 0.01f) {
		if (m_activity == Fishing)
			m_activity = -1;
		m_fishing = 0;
		Say(165, "You stop fishing and go on your way.", SkillsColor);
		return;
	}
	if (now_ms < m_fishing_ms)
		return;
	if (m_fishing == 1) {
		// The hook lands 16..32 ahead: in water (within 30 of our height) it splashes and sits 9 s.
		float distance = static_cast<float>(Rand() % 17 + 16);
		float x = 0, y = 0, z = 0;
		if (m_host.hook_point && m_host.hook_point(distance, x, y, z)) {
			if (m_host.sound_at)
				m_host.sound_at(x, y, z, 99);
			m_fishing = 6;
			m_fishing_ms = now_ms + 9000;
			return;
		}
	}
	else if (m_fishing >= 2 && m_fishing <= 5) {
		m_fishing = 0;
		return;
	}
	if (m_activity == Fishing)
		m_activity = -1;
	m_fishing = 0;
}

void Abilities::UpdateMyVisibleStatus()
{
	if (!m_zone)
		return;
	// Hidden, or invisible (SPA 12), to animals (28) or to the undead (29).
	uint8_t was = m_zone->Self().invis;
	uint8_t state = 0;
	if (m_hidden)
		state = 1;
	else if (m_host.total) {
		if (m_host.total(12) >= 1)
			state = 1;
		else if (m_host.total(28) >= 1)
			state = 3;
		else if (m_host.total(29) >= 1)
			state = 2;
	}
	if (state == was)
		return;
	if (m_host.remove_affect) {
		if (state != 1)
			m_host.remove_affect(12);
		if (state != 3)
			m_host.remove_affect(28);
		if (state != 2)
			m_host.remove_affect(29);
	}
	m_zone->SendAppearance(AppearanceInvis, state);
}

void Abilities::CancelSneakHide()
{
	if (m_hidden || m_activity == HideSkill) {
		if (m_activity == HideSkill)
			m_activity = -1;
		m_hidden = false;
		UpdateMyVisibleStatus();
		Say(12337, "You are no longer hidden.", DefaultColor);
	}
	ZoneSpawn &self = m_zone->EditSelf();
	if (self.sneaking != 0) {
		self.sneaking = 0; // the zone isn't told
		Say(12338, "You stop sneaking.", DefaultColor);
	}
}

void Abilities::MakeMeVisible()
{
	if (!m_zone || m_zone->Self().invis == 0)
		return;
	if (m_host.remove_affect)
		for (int spa : {12, 28, 29})
			m_host.remove_affect(spa);
	m_zone->SendAppearance(AppearanceInvis, 0);
	CancelSneakHide();
}

void Abilities::AttackEndsHiding()
{
	if (!m_zone || (!m_hidden && m_activity != HideSkill))
		return;
	if (m_activity == HideSkill)
		m_activity = -1;
	m_hidden = false;
	UpdateMyVisibleStatus();
	Say(12682, "You have moved and are no longer hidden!!", DefaultColor);
}

void Abilities::Hit()
{
	// IWasHit: VisibilityState and IsSneaking go to 0 (without a word to the zone), and the
	// invisibility buffs.
	if (!m_zone)
		return;
	ZoneSpawn &self = m_zone->EditSelf();
	self.invis = 0;
	self.sneaking = 0;
}

void Abilities::ZoneEvent(const ZoneSession::Event &event)
{
	using Type = ZoneSession::Event::Type;
	switch (event.type) {
	case Type::BindWoundAnswered: {
		// The 0x4093 case: anything but "under way" (3) or 0 stands us up; 3 lets the counts go.
		int type = event.value;
		if (type != 0 && type != 3) {
			m_activity = -1;
			if (m_host.change_stance)
				m_host.change_stance(Standing);
		}
		static const char *const why[] = {"The person you were bandaging has died.", "The person you were bandaging has left.",
			"The person you were bandaging has moved away.", "You have moved and your attempt to bandage has failed."};
		if (type == 0 || type == 1)
			Say(1432, "The bandaging is complete.", 0xf);
		else if (type == 3) {
			m_waiting = std::max(m_waiting - 1, 0);
			Busy(-1);
		}
		else if (type >= 4 && type <= 7)
			Say(1429 + type, why[type - 4], 0xf);
		break;
	}
	case Type::Begged: {
		Busy(-1);
		static const char *const coins[] = {"You have begged %1 platinum.", "You have begged %1 gold.",
			"You have begged %1 silver.", "You have begged %1 copper."};
		if (event.id == 0)
			Say(12892, "Your attempt at begging was unsuccessful.", SkillsColor);
		else if (event.id >= 1 && event.id <= 4)
			Say(12892 + event.id, coins[event.id - 1], SkillsColor, {std::to_string(event.value)});
		break;
	}
	case Type::Stolen: {
		Busy(-1);
		static const char *const coins[] = {"You have stolen %1 platinum.", "You have stolen %1 gold.",
			"You have stolen %1 silver.", "You have stolen %1 copper."};
		if (event.id == 0)
			Say(12898, "Your attempt at stealing was unsuccessful.", SkillsColor);
		else if (event.id >= 1 && event.id <= 4)
			Say(12898 + event.id, coins[event.id - 1], SkillsColor, {std::to_string(event.value)});
		else if (event.id == 5) {
			Say(12903, "You have stolen a %1", SkillsColor, {event.text});
			if (event.value < 0)
				Say(12904, "There was no room in your inventory, and the item has dropped to the ground.", SkillsColor);
		}
		break;
	}
	default:
		break;
	}
}

void Abilities::UseCombatSkill(int skill, uint64_t now_ms)
{
	// EQPlayer::DoAttack(100, skill, target): not while casting, nor stunned ("You can't attack while
	// stunned!", 125, color 0xd); attacking ends our invisibility and hiding.
	if (m_host.casting && m_host.casting())
		return;
	if (m_host.stunned && m_host.stunned()) {
		Say(125, "You can't attack while stunned!", 0xd);
		return;
	}
	if (m_host.remove_affect)
		for (int spa : {12, 28, 29, 94})
			m_host.remove_affect(spa);
	AttackEndsHiding();
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
