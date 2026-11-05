#pragma once

// The Actions window's ability and combat skill buttons, and using a skill (eqgame's CActionsWnd
// abilities and combat pages, CSkillsSelectWnd, EQ_Character::UseSkill @004bdf2f, EQPlayer::DoAttack's
// combat skills @0050a0f8, DoPassageOfTime's ability timers):
//   - The abilities page's six buttons (DAT_007d026c) and the combat page's four skill buttons
//     (__DoAbilityList) each hold a skill, -1 none, kept in <character>_<server>.ini as
//     [Abilities] Ability1..6 and [CombatSkills] CombatSkill1..4 (saveCustom, loadCustomFromINI);
//     loadCustom drops one we don't have (HasSkill), and the hot buttons that held it. A button shows
//     its skill's name (UpdateButtonText) and is drawn down while the skill isn't ready
//     (UpdateAbilityButtonStates).
//   - The right button on one opens SkillsSelectWindow (CSkillsSelectWnd) listing what it can take:
//     the skills we have that are used from a button, on the combat page only those that aren't
//     passive (SetTypesToDisplay 2), on the abilities page all of them (3). A click on one puts it
//     on the button.
//   - A left click (or its hot button, or /doability 1..10) uses the skill. A combat skill (backstab,
//     bash, the kicks, the monk strikes) goes on the combat timer (__AltTimerReady: the skill's reuse
//     time, hastened as ModifyAttackSpeed does, from its last use) through DoAttack's checks and
//     CanIHitTarget to OP_CombatAbility; the zone does the rest. Anything else is ready once its own
//     reuse time has gone by (__DoAbilityAvailable; mend's from its use, at DAT_007cd6d0).
// Done of UseSkill's own branches: forage, sense heading (the compass pointing true), mend, taunt,
// intimidation, disarm, disarm traps and sense traps. Not yet: sneak, hide, feign death, bind wound,
// begging, pick pockets, tracking, fishing, and the AA abilities (105, 107, 111).

#include "zone_session.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class EqStr;
class IniFile;
class WindowManager;
struct GameWindowState;

class Abilities
{
public:
	static constexpr int AbilitySlots = 6, CombatSlots = 4;

	struct Host
	{
		std::function<void(const std::string &, int)> chat; // a line in a chat color type
		std::function<uint16_t()> target;                   // ours, 0 none
		std::function<bool()> attacking;                    // __Attack
		std::function<bool()> casting;                      // a cast going (a bard's song doesn't count)
		std::function<int()> hp;                            // EQ_Character::Cur_HP
		std::function<int()> haste;                         // TotalSpellAffects of SPA 11
		std::function<bool(uint16_t)> can_hit;              // EQPlayer::CanIHitTarget, with its messages
		std::function<float(uint16_t)> melee_range;         // get_melee_range from us
		std::function<float(uint16_t)> distance;            // CDisplay::PlayerDistance, across the ground
		std::function<float()> heading;                     // ours, 0..512
		std::function<void()> sense_heading;                // CCompassWnd::SenseHeading
		std::function<void(int)> remove_affect;             // GetPCSpellAffect + RemovePCAffect of an SPA
	};

	void Attach(WindowManager &windows, GameWindowState &state, ZoneSession &zone, const EqStr *eqstr, Host host);
	void Reset(); // a new character: no skills on the buttons, every timer ready

	void Load(const IniFile &ini); // loadCustomFromINI, then loadCustom's check of what we have
	void Store(IniFile &ini) const;
	bool TakeChanged() { bool c = m_changed; m_changed = false; return c; } // saveCustom wanted

	// DoPassageOfTime's timers, then the buttons' text and state.
	void Frame(uint64_t now_ms);
	// EQ_Character::UseSkill.
	void Use(int skill, uint64_t now_ms);
	// A left click on one of the buttons or the skill select window's list; true if it was one.
	bool Click(const std::string &window, const std::string &control, uint64_t now_ms);
	bool RightClick(const std::string &window, const std::string &control);
	// /doability: 1..6 the abilities page's, 7..10 the combat page's skill buttons.
	void DoAbility(const std::string &args, uint64_t now_ms);

	int Ability(int i) const { return i >= 0 && i < AbilitySlots ? m_abilities[i] : -1; }
	int CombatSkill(int i) const { return i >= 0 && i < CombatSlots ? m_combat[i] : -1; }
	bool Ready(int skill) const; // __AltTimerReady for a combat skill, else __DoAbilityAvailable

	// EQPlayer::ModifyAttackSpeed (@0050a039): a delay hastened by SPA 11 (delay * 100 / haste).
	static uint32_t ModifyAttackSpeed(uint32_t delay, int haste);

private:
	void UseCombatSkill(int skill, uint64_t now_ms); // DoAttack 100
	bool CanBash() const;                            // UseSkill's shield (or two-hander) check
	void Say(uint32_t id, const std::string &fallback, int type, const std::vector<std::string> &args = {});
	void UpdateButtons();
	void OpenSelect(bool combat, int index);
	int Rand();

	WindowManager *m_windows = nullptr;
	GameWindowState *m_state = nullptr;
	ZoneSession *m_zone = nullptr;
	const EqStr *m_eqstr = nullptr;
	Host m_host;

	int m_abilities[AbilitySlots] = {-1, -1, -1, -1, -1, -1};
	int m_combat[CombatSlots] = {-1, -1, -1, -1};
	bool m_changed = false;

	// DoPassageOfTime's: each skill's last use (DAT_007cd650) and whether it's ready
	// (__DoAbilityAvailable); mend's ready time; the combat timer's last use and length
	// (ActorInfo +0x164) and __AltTimerReady.
	static constexpr int SkillCount = 74;
	uint64_t m_last_use[SkillCount] = {};
	bool m_available[SkillCount] = {};
	uint64_t m_mend_ready_ms = 0;
	uint64_t m_combat_last_ms = 0;
	uint32_t m_combat_reuse_ms = 0;
	bool m_alt_ready = true;
	uint64_t m_now_ms = 0;

	// The skill select window: the button it's for (-1 none) and the skills its rows hold.
	bool m_select_combat = false;
	int m_select_index = -1;
	std::vector<int> m_select_skills;
	uint32_t m_random = 0x1234567u;
};
