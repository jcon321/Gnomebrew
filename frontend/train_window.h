#pragma once

// eqgame's guildmaster training window, CTrainWnd (@00439f9c on, the TrainWindow screen):
//   - CEverQuest::RightClickedOnPlayer (@00532949): the right button on a guildmaster (class 17..31)
//     no more than 20 away: our own class's (our class + 16), with none being trained by, makes us
//     visible, turns auto attack off and asks the zone (OP_GMTraining), waiting for it
//     (DAT_007d0258); another class's says "You are not a member of the %1 class guild." (1467).
//     Either way /consider follows, as for anyone;
//   - msg_req_guildmaster (@004de70a): the zone's answer with success opens it: SetGMData
//     (@0043ad3d) lists each skill the guildmaster teaches (its cap 1..252) that our class can
//     have (ours isn't 255), but for 72, offense (33) and defense (15), then each language we may
//     learn (EQ_PC_checkLang) that it teaches, to 100; and Activate (@0043a210) puts the trainer
//     music on (track 7), fills the list (UpdateAll) and shows it. Without success it just stops
//     waiting;
//   - UpdateSkill (@0043a9e5), a row: the name, the rank (CSkillsWnd's, "LEVEL: %1" for a skill our
//     class gets later, and "LEVEL: 0" for 255), our value (blank for 254 and 255), and what the
//     next point costs (CostToTrain: (value - 10)^3 * greed / 100 copper, nothing for one we haven't
//     got) across the coin columns, "--" in each for nothing;
//   - UpdateRight (@0043a4a0): our practices and the coins we carry; Train enabled with a row
//     picked, a practice left, the money for it, and our level at the skill's;
//   - WndNotification (@0043a33e): a row clicked has the guildmaster ask "Interested in training
//     in %2 ?" (12056; 12055 a language); Train trains; Done closes; a heading clicked sorts by
//     name (the first column) or value (the next two), the other way each time (SortNames,
//     SortSkillsByValue, by selection as eqgame does);
//   - Train (@0043ae1d): EQ_PC::BitchCanTrain (@004efc95) says why not (12923..12931, 117),
//     12065 if we can't afford it; else the money out (HandleMoney), OP_GMTrainSkill, a practice
//     spent and the skill up one (a new one starts at the level our class gets it: 12066; a
//     language 12066 for its first point, 12067 after; a skill 12068), and the row again;
//   - ProcessGuildMasterProximityCheck (@0043a2d4): every 500 ms, the guildmaster more than 20 away
//     closes it (CEverQuest_LeaveGuildMaster), as do its despawning, its death, and Escape;
//   - Deactivate (@0043a246): the music off, OP_GMEndTraining, not waiting, no guildmaster, our
//     target cleared, the window down.

#include "zone_session.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class EqStr;
class WindowManager;

class TrainWindow
{
public:
	struct Host
	{
		std::function<void(const std::string &, int)> chat; // a message, in a chat color type
		std::function<void(bool)> music;                    // SetMusicSelection(7, on)
		std::function<void()> clear_target;                  // pinstTarget = 0
	};

	void Attach(WindowManager &windows, ZoneSession &zone, const EqStr *eqstr, Host host);
	void Reset(); // a new zone: nobody training us

	bool Active() const { return m_active; }                  // field200_0x134: the window's up
	bool Waiting() const { return m_waiting; }                // DAT_007d0258: asked, no answer yet
	uint16_t Guildmaster() const { return m_guildmaster; }    // pinstActiveGMaster; 0 none
	void Request(uint16_t guildmaster);                       // OP_GMTraining
	void ZoneEvent(const ZoneSession::Event &event);          // msg_req_guildmaster
	void Deactivate();                                        // and CEverQuest_LeaveGuildMaster
	void Frame(uint64_t now_ms);
	bool Click(const std::string &window, const std::string &control); // the list, Train, Done

private:
	struct Row
	{
		int id = 0;     // a skill, or 100 + a language
		int cap = 0;    // the most the guildmaster teaches it
		int cost = 0;   // CostToTrain, in copper
	};

	std::string GuildmasterName() const; // trimName
	std::string RowName(int id) const;   // CTrainWnd_GetSkillOrLanguageName
	int RowValue(int id) const;          // CTrainWnd_GetSkillOrLanguageValue
	std::string Text(uint32_t id, const std::vector<std::string> &args, const std::string &fallback) const;
	int CostToTrain(bool language, int skill) const;
	int32_t Money() const; // the coins we carry, in copper
	bool BitchCanTrain(int cap, bool language, int skill);
	void SetGMData(const ZoneTrainer &trainer);
	void Activate();
	void UpdateAll(bool sort);
	void UpdateSkill(int row);
	void UpdateRight();
	void SortNames(int direction);
	void SortByValue(int direction);
	void Train();

	WindowManager *m_windows = nullptr;
	ZoneSession *m_zone = nullptr;
	const EqStr *m_eqstr = nullptr;
	Host m_host;
	uint16_t m_guildmaster = 0;
	bool m_waiting = false;
	bool m_active = false;
	float m_greed = 1;            // field1802_0x7a0
	std::vector<Row> m_rows;      // field223_0x16c, field1801_0x79c of them
	int m_name_direction = 1;     // field1803_0x7a4
	int m_value_direction = -1;   // field1804_0x7a8
	bool m_by_name = true;        // field1805_0x7ac
	uint64_t m_next_check_ms = 0; // field204_0x138
};
