#pragma once

// Tracking: eqgame's tracking window (CTrackingWnd @00437f45 on, EQUI_TrackingWnd.xml's
// "TrackingWnd"), its commands and DoPassageOfTime's directions.
//   - UseSkill's Tracking (53) opens it (CTrackingWnd::Activate @004382a1: the controls, the list,
//     shown) and sends OP_Track.
//   - GenerateTrackingList (@0043885a): the range is Tracking * 10 + 500 (+ (Tracking - 100) * 10
//     over 100) in a zone with the time of day (DAT_0079892c 1, 2 or 5), else Tracking / 2 + 500 (+
//     (Tracking - 100) * 4). The group's members in the zone go in first, whatever the distance,
//     then each spawn but us that's a player or an NPC (Type under 2), not race 143, 141, 72, 73,
//     114 or 142, not a ridden horse (ActorInfo's Rider), not invisible to us (CDisplay_IsInvisible)
//     and not in the group, within the range across the ground (PlayerDistance with no height): 1000
//     entries at most, each {spawn, distance, the order it went in, flags}. Then the sort, the
//     filter and the list.
//   - DoTrackSort (@00438a9b), DAT_007989cc (a ranger's; anyone else's is normal): 0 normal puts
//     each entry back at its order; 1 distance and 3 rdistance swap each entry with any later one
//     nearer (farther) that has a distance; 2 consider and 4 rconsider, for an NPC at i, swap it
//     with a later NPC of a higher (lower) level, or with the first later entry that isn't an NPC
//     and stop there (so the players come first).
//   - DoTrackFilter (@00438d53): each entry's consider color as flags: 0x100 green (under the
//     gray level, which is level 61: 48, 62: 51, under 61: level * 3 / 4, else level - 10, and more
//     than 3 under us), turned 0x200 light blue when its sum (level² * 100, and level² * 100 / 5 times
//     its distance from the gray level, one more under for each of our levels under 55, 41 and 31;
//     -4 and -5 give level² * -80) is over 0 and we're over level 7; else 0x80 blue under us, 0x40
//     white at our level, 0x20 yellow 1..2 over, 0x10 red 3 or more over. A ranger's
//     DAT_007989d0 hides an NPC of a color (1 green, 2 blue, 4 white, 8 yellow, 0x10 red, 0x20
//     light blue). A player is flagged 2 and hidden with TrackPlayers off (0), shown on (1), and
//     with group (2) shown only in the group. A pet of a player (+0x96, the owner's id, not an
//     NPC) is flagged 4. Hidden ones are flagged 1.
//   - UpdateTrackingList (@00438722): a row per shown entry, the trimmed name (":" before a
//     player's pet's) in GetTrackingColorIndex's color (@0043868b): a player 7 (12 in our group),
//     green 14, blue 16, light blue 18, white 10, yellow 15, red 13; the selection kept on the same
//     spawn. The list's colors are SetColors(white, 0xff8aa3ff).
//   - OnProcessFrame (@004382c4): every 500 ms, UpdateTrackingControls (@0043851b): the sort combo
//     at DAT_007989cc, the players combo at DAT_007989c8, the six filter buttons checked by
//     DAT_007989d0; not a ranger, the sort and filter forced to 0 and their controls disabled.
//   - WndNotification (@004382fe): Done stops tracking if it's going (DAT_007989e2 53) and closes
//     it; Track, with a row selected: DAT_007989e2 53, the spawn (DAT_007989f0), 6 minutes
//     (DAT_007989e8), eqstr 12040 "You begin tracking %1." (color 0x10e), closed. The filter buttons
//     are /trackfilter green..lblue; the sort combo /tracksort normal..rconsider; the players combo
//     /trackplayers off, on, group.
//   - /trackplayers (@004f9764): on, off or group (eqstr 12947, 12946, 13148, any case): 13140,
//     13141, 13142, kept as [Defaults] TrackPlayers TRUE, FALSE, GROUP; else 13143. Then
//     DoTrackFilter(1).
//   - /tracksort (@004f98b1): not a ranger 13155; normal (or N...), distance (D), consider (C),
//     rdistance (RD), rconsider (RC): 13149..13153, kept as [Defaults] TrackSortType NORMAL,
//     DISTANCE, CONSIDER, RDISTANCE, RCONSIDER; else 13154. Then DoTrackSort(1).
//   - /trackfilter (@004f9b3b): not a ranger 13155; green, blue, white, yellow, red, lblue (or their
//     first letter) toggle their bit: 13164 "You will NOT see %1 NPCs" set, 13163 cleared; else
//     13162. Then DoTrackFilter(1). [Defaults] TrackFilterType is only read (atoi).
//   - CEverQuest::CEverQuest reads TrackPlayers (no key: TRUE written; T, t, 1 on; G, g, 2 group;
//     else off) and TrackSortType (no key: NORMAL written; N, 0 normal; D, 1; C, 2; RD, 3; RC, 4;
//     else normal).
//   - DoPassageOfTime, tracking (DAT_007989e2 53): no spawn, 12499 (0x10e) and it stops; else every
//     2 s a line by where they are from our heading (12676 straight ahead within 32 of 512, 12677
//     ahead within 96, 12678 to the side within 160, 12679 behind within 224, 12680 behind), with
//     12675 left or 12674 right; after the 6 minutes, 12681 and it stops.
//   - The spawn tracked dying (ReportDeath) or despawning says 12102 "You have lost your target." and
//     it stops; RemovePlayerFromTracking takes it out of the list.

#include "zone_session.h"

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class IniFile;
class WindowManager;

class Tracking
{
public:
	static constexpr const char *Screen = "TrackingWnd";
	static constexpr int MaxEntries = 1000;
	static constexpr int Activity = 53; // DAT_007989e2 while tracking

	struct Host
	{
		// eqstr_en.txt's string `id` with its %1.. filled in, or `fallback`.
		std::function<std::string(uint32_t, const std::vector<std::string> &, const std::string &)> text;
		std::function<void(const std::string &, int)> chat;      // print_chat_dsp_chat, in a color type
		std::function<uint32_t(int)> color;                      // CChatManager::GetRGBAFromIndex
		std::function<int()> activity;                           // DAT_007989e2
		std::function<void(int)> set_activity;
		std::function<bool(const ZoneSpawn &)> invisible;        // CDisplay_IsInvisible to us
		std::function<bool(uint16_t)> ridden;                    // a horse with a rider (ActorInfo's Rider)
		std::function<std::vector<uint16_t>()> group;            // pinstGroup's members in the zone, in order
		std::function<float()> heading;                          // ours, 0..512
		std::function<void(const char *, const std::string &)> save; // [Defaults] key = value
	};

	void Attach(WindowManager &windows, ZoneSession &zone, Host host);
	void Load(const IniFile &settings); // CEverQuest::CEverQuest's TrackPlayers, TrackSortType, TrackFilterType
	void Reset();                       // a new zone (CEverQuest_EnterZone): not tracking, closed

	void Activate();               // CTrackingWnd::Activate
	void Deactivate();
	bool Active() const { return m_active; }
	void Frame(uint64_t now_ms);   // OnProcessFrame, and DoPassageOfTime's tracking
	bool Click(const std::string &window, const std::string &control);
	void TrackPlayers(const std::string &args); // /trackplayers
	void TrackSort(const std::string &args);    // /tracksort
	void TrackFilter(const std::string &args);  // /trackfilter
	// ReportDeath and HandleWorldMessage's despawn: the spawn tracked lost (12102), and it out of
	// the list (RemovePlayerFromTracking).
	void SpawnGone(uint16_t id);

	uint16_t Target() const { return m_target; } // DAT_007989f0
	int PlayersMode() const { return m_players; } // DAT_007989c8
	int SortType() const { return m_sort; }       // DAT_007989cc
	int FilterBits() const { return m_filter; }   // DAT_007989d0

	struct Entry
	{
		uint16_t id = 0;       // 0 empty
		float distance = -1;
		int index = -1;        // the order it went in
		uint32_t flags = 0;
	};
	const std::array<Entry, MaxEntries> &Entries() const { return m_entries; }

private:
	std::string Say(uint32_t id, const std::vector<std::string> &args, const std::string &fallback) const;
	void Line(uint32_t id, const std::string &fallback); // print_chat_dsp_chat_2
	bool Ranger() const;
	bool InGroup(uint16_t id) const;
	void Generate();
	void Sort(bool update);
	void Filter(bool update);
	void UpdateList(bool keep);
	void UpdateControls();
	uint32_t RowColor(const Entry &e) const; // GetTrackingColorIndex
	void Stop(); // DAT_007989e2 -1 when tracking, DAT_007989f0 0

	WindowManager *m_windows = nullptr;
	ZoneSession *m_zone = nullptr;
	Host m_host;
	bool m_active = false;            // +0x134
	uint64_t m_controls_ms = 0;       // +0x138
	std::array<Entry, MaxEntries> m_entries;
	int m_players = 1;                // DAT_007989c8: 0 off, 1 on, 2 group
	int m_sort = 0;                   // DAT_007989cc
	int m_filter = 0;                 // DAT_007989d0
	uint16_t m_target = 0;            // DAT_007989f0
	uint64_t m_end_ms = 0;            // DAT_007989e8
	uint64_t m_line_ms = 0;           // DAT_007989ec
	uint64_t m_now_ms = 0;
	std::vector<int> m_row_entries;   // the list's rows' entries (GetItemData)
};
