// Death, corpses and resurrection, as eqgame does them for us:
//   - CEverQuest::ReportDeath's part for us (CEverQuest_ReportDeath_ClearOutRentables @0052afd1,
//     ClearOutRentables @004ef891, EQPlayer::IDied @0050b637) and the DeathTimer's going home
//     (CDisplay::RealRender_World, CEverQuest::PrepareLocalPCForRepop @0052b067, MoveToZone_2's
//     "Your Home Zone is unavailable");
//   - being knocked out and coming to (EQPlayer::KnockedOut @0050b5a7, GetConscious @0050b52b,
//     from DoPassageOfTime);
//   - the zone's questions in the confirmation dialog: msg_resurrect (@004e1f5f), msg_sacrifice
//     (@004e1afe) and msg_translocate (@004e1c3e), answered by CConfirmationDialog::ProcessYes and
//     ProcessNo (categories 2, 0x14 and 0x19), and msg_resurrect_complete (@004e212b);
//   - HandleWorldMessage's 0x406c (OP_GMKill), /corpse (@004f5eeb) and /hidecorpses (@004fb5eb).

#include "frontend.h"
#include "inventory.h"
#include "spell_file.h"
#include "zone_names.h"

#include <cstring>

namespace
{
	constexpr uint64_t QuestionMs = 3600000; // CConfirmationDialog::Activate's timeout for all three

	// CEverQuest::trimName: '_' a space, the digits gone.
	std::string TrimName(const std::string &name)
	{
		std::string out;
		for (char c : name) {
			if (std::isdigit(static_cast<unsigned char>(c)))
				continue;
			out.push_back(c == '_' ? ' ' : c);
		}
		return out;
	}

	// CEverQuest::stripName: up to an apostrophe, the digits and '#' gone, '_' a space ("Soandso's
	// corpse0" is "Soandso").
	std::string StripName(const std::string &name)
	{
		std::string out;
		for (char c : name) {
			if (c == '\'')
				break;
			if (std::isdigit(static_cast<unsigned char>(c)) || c == '#')
				continue;
			out.push_back(c == '_' ? ' ' : c);
		}
		return out;
	}

	bool SameName(const std::string &a, const std::string &b) // stricmp
	{
		if (a.size() != b.size())
			return false;
		for (size_t i = 0; i < a.size(); ++i)
			if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
				return false;
		return true;
	}

	std::string CString(const std::vector<uint8_t> &data, size_t offset, size_t size)
	{
		if (offset >= data.size())
			return "";
		size = std::min(size, data.size() - offset);
		const char *text = reinterpret_cast<const char *>(data.data() + offset);
		return std::string(text, strnlen(text, size));
	}

	template <typename T> T Read(const std::vector<uint8_t> &data, size_t offset)
	{
		T value{};
		if (offset + sizeof(T) <= data.size())
			std::memcpy(&value, data.data() + offset, sizeof(T));
		return value;
	}

	// msg_translocate's spells that send us to our bind point: Translocate: Group (1334), Translocate
	// (1422) and Teleport (3243).
	bool ToBind(int32_t spell) { return spell == 0x536 || spell == 0x58e || spell == 0xcab; }
}

bool FrontEnd::DeathTimerRunning() const
{
	return m_dead && m_death_ms != 0 && m_death_ms != UINT64_MAX;
}

void FrontEnd::IDied()
{
	// CEverQuest_ReportDeath_ClearOutRentables: auto attack off, StunnedState 4, the group's spawns
	// let go (relinked as the next zone comes), the overhead view (Zeal_camera_view 1) and our
	// temporary items gone, but not dying in stance 102 (ReportDeath's flag for it); then the loot
	// window closed (CLootWnd::DoorLoading_(1)).
	SetAutoAttack(false);
	m_knocked_out = false;
	m_group.ReleaseSpawns();
	if (m_zone_view)
		m_zone_view->SetOverheadView();
	if (m_zone.Self().stance != 102)
		ClearOutRentables();
	m_loot.Died();
	// EQPlayer::IDied: we fall and lie there, still, to the death music; MovePlayer starts the
	// DeathTimer.
	m_dead = true;
	m_death_ms = 0;
	m_autorun = false;
	if (m_zone_view)
		m_zone_view->SetDead(true);
	if (m_sound)
		m_sound->SetMusicSelection(audio::EqSound::Death, true);
}

void FrontEnd::ClearOutRentables()
{
	// Each of the 30 (the cursor, 21 worn, 8 general): a bag's temporary items go, and a temporary
	// bag with all that's in it; any other temporary item goes.
	std::map<int16_t, ZoneItem> &items = m_zone.EditItems();
	for (int16_t slot = Inventory::Cursor; slot <= Inventory::LastGeneral; ++slot) {
		auto it = items.find(slot);
		if (it == items.end())
			continue;
		if (it->second.item_class == 1)
			for (int n = 0; n < Inventory::BagSlots; ++n) {
				int16_t inside = Inventory::BagSlot(slot, n);
				auto item = inside >= 0 ? items.find(inside) : items.end();
				if (item != items.end() && item->second.no_rent == 0)
					items.erase(item);
			}
		if (it->second.no_rent == 0)
			Inventory::Remove(items, slot);
	}
}

void FrontEnd::KnockoutPassageOfTime()
{
	// DoPassageOfTime for a player alive: at 0 HP or under with StunnedState under 3, KnockedOut;
	// over 0 with it 3, GetConscious. Not before the zone has told us our HP.
	if (m_dead || m_zoning || m_zone.GetState() != ZoneSession::State::InZone || m_zone.MaxHp() <= 0)
		return;
	int hp = m_game_values.CurHp();
	if (hp > 0 && m_knocked_out) {
		// GetConscious: StunnedState 0, the default camera, "You are conscious again!" (12876),
		// then StandingState 0x69 (kneeling) and ChangeStance(100): up as from kneeling.
		m_knocked_out = false;
		if (m_zone_view) {
			m_zone_view->SwitchToDefaultCamera();
			m_zone_view->SetStance(105);
		}
		ChatOut(EqString(12876, "You are conscious again!"), ChatType::Red);
		ChangeStance(100);
	}
	else if (hp <= 0 && !m_knocked_out) {
		// KnockedOut: lying down (ChangeStance 0x73); stunned, that's all for now; else StunnedState
		// 3, the overhead view and "You have been knocked unconscious!" (12877).
		ChangeStance(115);
		if (!m_stunned) {
			m_knocked_out = true;
			m_autorun = false;
			if (m_zone_view)
				m_zone_view->SetOverheadView();
			ChatOut(EqString(12877, "You have been knocked unconscious!"), ChatType::Yellow);
		}
	}
	// Further on: under -10 HP (under 1 for deity 20000) with no DeathTimer, our own death, from no
	// one we know (the PC whose DoT ticked us down eqgame takes from its own ticks, which aren't
	// ours), no spell, skill 0x17, damage 0. The zone sends the dying no OP_Death when a client or
	// the victim killed it, or a DoT did (GenerateDeathPackets' skipsender).
	int least = m_zone.Profile().deity != 20000 ? -10 : 1;
	if (hp < least && m_death_ms == 0)
		SendDeathMessage(0, -1, 0x17, 0);
}

void FrontEnd::SendDeathMessage(uint16_t killer, int16_t spell, uint8_t skill, int32_t damage)
{
	if (m_dead)
		return; // StunnedState 4
	ReportDeath(m_zone.SendDeath(killer, spell, skill, damage));
}

void FrontEnd::AskResurrect(const std::vector<uint8_t> &request)
{
	// Only a question to us (your_name @24). The text: "%1 wants to cast %2 (%3 percent) upon
	// you." (10001) with the rezzer (@92, trimName), the spell (@156) and its first effect's base
	// value; without a spell, "%1 wants to RESURRECT you." (10000). The dialog already up, the
	// question goes unanswered.
	if (!SameName(CString(request, 24, 64), m_zone.Profile().name))
		return;
	std::string rezzer = TrimName(CString(request, 92, 64));
	int16_t spell_id = Read<int16_t>(request, 156);
	const SpellInfo *spell = m_ui.spells && spell_id >= 1 && spell_id <= 3999 ? m_ui.spells->Find(spell_id) : nullptr;
	std::string text = spell ? FormatEqString(m_ui.eqstr, 10001, "%1 wants to cast %2 (%3 percent) upon you. Do you wish this?",
	                               {rezzer, spell->name, std::to_string(spell->base[0])})
	                         : FormatEqString(m_ui.eqstr, 10000, "%1 wants to RESURRECT you. Do you wish this?", {rezzer});
	std::vector<uint8_t> kept = request;
	if (!AskInGame(text, QuestionMs, [this, kept](bool yes) { m_zone.RezzAnswer(kept, yes); }))
		return;
	m_rez_request = request;
}

void FrontEnd::ResurrectComplete(const std::vector<uint8_t> &packet)
{
	if (Read<uint32_t>(packet, 224) != 1) {
		ChatOut(EqString(12918, "Resurrect failed, unable to find corpse."), ChatType::Spells);
		return;
	}
	ChatOut(EqString(12917, "You are being resurrected..."), ChatType::Spells);
	m_buffs.Clear();
	ZoneProfile &me = m_zone.EditProfile();
	if (Read<int32_t>(packet, 20) < 100) {
		// Under 100%: no mana, a third of our HP, and in slot 0 Resurrection Effects (756; 757,
		// the lighter one, for an ogre, troll, barbarian or dwarf): type 2, level 52, 40 ticks.
		me.mana = 0;
		m_zone.ModifyHp(m_game_values.MaxHp() / 3 - m_zone.BaseHp());
		ZoneBuff sickness;
		sickness.type = 2;
		sickness.level = 0x34;
		sickness.spell = me.race == 10 || me.race == 9 || me.race == 2 || me.race == 8 ? 0x2f5 : 0x2f4;
		sickness.duration = 0x28;
		m_buffs.Put(0, sickness);
	}
	else {
		me.mana = static_cast<int16_t>(m_game_values.MaxMana());
		m_zone.ModifyHp(m_game_values.MaxHp() - m_zone.BaseHp());
	}
	// Off any boat, still, at the corpse's place the question gave (the dialog's copy of it): in
	// this zone onto the floor there, else MoveToZone(zone, "resurrect_player", 0, 9).
	const std::vector<uint8_t> &at = m_rez_request.size() >= 228 ? m_rez_request : packet;
	uint32_t zone = Read<uint32_t>(at, 4);
	float y = Read<float>(at, 8), x = Read<float>(at, 12), z = Read<float>(at, 16);
	m_zone.SetBoat("");
	if (zone == m_zone.Profile().zone_id) {
		if (m_zone_view)
			m_zone_view->MoveToSafePoint(x, y, z);
	}
	else
		m_zone.RequestZoneChange(zone, 9);
}

void FrontEnd::AskSacrifice(uint16_t caster, uint32_t target)
{
	// "%1 wants to SACRIFICE you. ..." (10008), the caster's spawn name or "Someone"; the dialog
	// already up, No at once (ProcessNo 0x14). (The old UI's asked only when the target is us; the
	// new UI's, ours, whoever.)
	auto it = m_zone.Spawns().find(caster);
	std::string name = caster && it != m_zone.Spawns().end() ? it->second.name : "Someone";
	auto no = [this]() {
		m_zone.SacrificeAnswer(false);
		ChatOut(EqString(10010, "You declined the request to be sacrificed."), ChatType::Default);
	};
	if (m_windows.Shown("ConfirmationDialogBox")) {
		no();
		return;
	}
	(void)target;
	std::string text = FormatEqString(m_ui.eqstr, 10008,
		"%1 wants to SACRIFICE you. You get NO experience back with Resurrection, even GM. Die & lose exp?", {name});
	AskInGame(text, QuestionMs, [this, no](bool yes) {
		if (!yes) {
			no();
			return;
		}
		m_zone.SacrificeAnswer(true);
		ChatOut(EqString(10009, "You agreed to be sacrificed."), ChatType::Default);
	});
}

void FrontEnd::AskTranslocate(const std::vector<uint8_t> &packet)
{
	// To our bind point (10016), else "Are you sure you wish to be translocated by %1 to %2?"
	// (10017: the caster @12, trimName, and the zone's full name); the dialog already up, No at
	// once (ProcessNo 0x19).
	auto no = [this]() {
		m_zone.TranslocateAnswer(false);
		ChatOut(EqString(10018, "You declined the request to be translocated."), ChatType::Default);
	};
	if (m_windows.Shown("ConfirmationDialogBox")) {
		no();
		return;
	}
	std::string text = ToBind(Read<int32_t>(packet, 4))
	                       ? EqString(10016, "Are you sure you wish to be translocated to your bind point?")
	                       : FormatEqString(m_ui.eqstr, 10017, "Are you sure you wish to be translocated by %1 to %2?",
	                             {TrimName(CString(packet, 12, 64)), FullZoneName(Read<uint32_t>(packet, 0), m_ui.eqstr)});
	AskInGame(text, QuestionMs, [this, no](bool yes) {
		if (yes)
			m_zone.TranslocateAnswer(true);
		else
			no();
	});
}

void FrontEnd::Translocated(const std::vector<uint8_t> &packet)
{
	// Complete 1: off any boat, still, at our bind point for those spells, else where it says;
	// wave 0x89 on us; in this zone onto the floor there, else MoveToZone(zone, "zone_command", 0,
	// 8 with a caster, 4 without).
	const ZoneProfile &me = m_zone.Profile();
	uint32_t zone = Read<uint32_t>(packet, 0);
	float y = Read<float>(packet, 76), x = Read<float>(packet, 80), z = Read<float>(packet, 84);
	if (ToBind(Read<int32_t>(packet, 4))) {
		y = me.bind_y;
		x = me.bind_x;
		z = me.bind_z;
	}
	m_zone.SetBoat("");
	if (m_zone_view)
		m_zone_view->PlayActorSound(m_zone.MySpawnId(), 0x89);
	if (zone == me.zone_id) {
		if (m_zone_view)
			m_zone_view->MoveToSafePoint(x, y, z);
		return;
	}
	m_zone.RequestZoneChange(zone, packet.size() > 12 && packet[12] != 0 ? 8 : 4);
}

void FrontEnd::StruckDown(const std::string &name)
{
	const ZoneProfile &me = m_zone.Profile();
	if (!SameName(name, me.name))
		return;
	ChatOut(FormatEqString(m_ui.eqstr, 1391, "%1 HAS BEEN STRUCK DOWN BY THE GODS.", {TrimName(me.name)}), 10);
	if (m_sound)
		m_sound->PlayWave(0x8f);
	m_zone.ModifyHp(-10000 - m_zone.BaseHp());
	if (m_death_ms != UINT64_MAX)
		m_death_ms = 0;
}

void FrontEnd::SummonCorpse()
{
	// Our target if it's a corpse (Type 2 or over); else the last spawn (not us) that's a corpse
	// with our name. "Summoning %1..." (13047), or "You don't have any corpses in this zone."
	// (13048).
	const ZoneSpawn *corpse = nullptr;
	auto target = m_zone.Spawns().find(m_game_values.target);
	if (m_game_values.target != 0 && target != m_zone.Spawns().end() && target->second.npc >= 2)
		corpse = &target->second;
	else
		for (const auto &[id, spawn] : m_zone.Spawns())
			if (id != m_zone.MySpawnId() && spawn.npc >= 2 && SameName(m_zone.Profile().name, StripName(spawn.name)))
				corpse = &spawn;
	if (!corpse) {
		ChatOut(EqString(13048, "You don't have any corpses in this zone."), ChatType::Default);
		return;
	}
	float y = m_zone.Profile().y, x = m_zone.Profile().x, z = m_zone.Profile().z;
	if (m_zone_view) {
		ZoneMovement here = m_zone_view->Movement();
		y = here.y;
		x = here.x;
		z = here.z;
	}
	m_zone.SummonCorpse(corpse->name, y, x, z);
	ChatOut(FormatEqString(m_ui.eqstr, 13047, "Summoning %1...", {TrimName(corpse->name)}), ChatType::Default);
}

void FrontEnd::HideCorpses(const std::string &how)
{
	if (how.empty()) {
		ChatOut(EqString(13202, "Please specify what corpses you want to hide: NONE (show all corpses), ALL (hide all corpses), "
		                        "ALLBUTGROUP (hide ALL, except corpses of group members)."),
			ChatType::Default);
		ChatOut(EqString(13203, "NOTE: Your own corpse(s) will never be hidden.  Also, any corpses created after use of this "
		                        "command will not be hidden."),
			ChatType::Default);
		ChatOut(EqString(13204, "WARNING: ALL and ALLBUTGROUP hide NPC corpses too, not just player corpses!"), ChatType::Default);
		return;
	}
	const ZoneProfile &me = m_zone.Profile();
	auto grouped = [&me](const std::string &name) { // EQ_PC::IsAGroupMember
		for (const std::string &member : me.group_members)
			if (!member.empty() && SameName(member, name))
				return true;
		return false;
	};
	bool all = SameName(how, EqString(13205, "ALL"));
	bool all_but_group = !all && SameName(how, EqString(13207, "ALLBUTGROUP"));
	if (all)
		ChatOut(EqString(13206, "Hiding all existing corpses except yours."), ChatType::Yellow);
	else if (all_but_group)
		ChatOut(EqString(13208, "Hiding all existing corpses except yours and those of your group members."), ChatType::Yellow);
	else {
		ChatOut(EqString(13209, "Making all existing corpses visible."), ChatType::Yellow);
		m_hidden_corpses.clear();
		return;
	}
	for (const auto &[id, spawn] : m_zone.Spawns()) {
		if (spawn.npc != 2 && spawn.npc != 3)
			continue;
		std::string owner = StripName(spawn.name);
		if (SameName(owner, me.name))
			continue;
		if (all_but_group && grouped(owner))
			m_hidden_corpses.erase(id);
		else
			m_hidden_corpses.insert(id);
	}
}
