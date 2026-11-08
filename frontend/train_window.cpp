#include "train_window.h"
#include "chat_text.h"
#include "eqstr.h"
#include "game_windows.h"
#include "inventory.h"
#include "sidl.h"
#include "skills.h"
#include "window_manager.h"

#include <cstring>
#include <iterator>

namespace
{
	constexpr float TrainRange = 20; // RightClickedOnPlayer, ProcessGuildMasterProximityCheck
	constexpr uint64_t CheckMs = 500;
	constexpr int TrainerMusic = 7;
	constexpr int Columns = 7;
	// CSkillsWnd's ranks (DAT_005fe8e0, eqstr 3224 on), as UpdateSkill picks them.
	constexpr int RankLimits[] = {10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 2000};
	constexpr uint32_t FirstRankName = 3224;
	// The skills SetGMData leaves out: 72, offense, defense.
	bool NeverListed(int skill) { return skill == 72 || skill == 33 || skill == 15; }
}

void TrainWindow::Attach(WindowManager &windows, ZoneSession &zone, const EqStr *eqstr, Host host)
{
	m_windows = &windows;
	m_zone = &zone;
	m_eqstr = eqstr;
	m_host = std::move(host);
	// CTrainWnd::CTrainWnd: the list's look, Train disabled, sorted by name first.
	if (SidlWindow *wnd = m_windows->Find("TrainWindow")) {
		if (SidlWindow::Control *list = wnd->Find("SkillList")) {
			list->row_color = 0xffffff;
			list->header_color = 0x8aa3ff;
			list->row_height = 24;
			list->justification = {0, 1, 1, 1, 1, 1, 1};
			list->column_clicks = true;
		}
		if (SidlWindow::Control *train = wnd->Find("TrainButton"))
			train->enabled = false;
	}
	m_name_direction = 1;
	m_value_direction = -1;
	m_by_name = true;
	Reset();
}

void TrainWindow::Reset()
{
	if (m_active && m_host.music)
		m_host.music(false);
	m_guildmaster = 0;
	m_waiting = false;
	m_active = false;
	m_rows.clear();
	if (m_windows)
		m_windows->Show("TrainWindow", false);
}

std::string TrainWindow::GuildmasterName() const
{
	if (!m_zone)
		return "";
	auto it = m_zone->Spawns().find(m_guildmaster);
	return it != m_zone->Spawns().end() ? it->second.DisplayName() : "";
}

std::string TrainWindow::Text(uint32_t id, const std::vector<std::string> &args, const std::string &fallback) const
{
	return FormatEqString(m_eqstr, id, fallback, args);
}

std::string TrainWindow::RowName(int id) const
{
	if (id < 100)
		return SkillName(m_eqstr, id);
	uint32_t name = LanguageNameId(id - 100);
	if (!name)
		return "Unknown";
	return m_eqstr && m_eqstr->Has(name) ? m_eqstr->Format(name, {}) : "Language " + std::to_string(id - 100);
}

int TrainWindow::RowValue(int id) const
{
	const ZoneProfile &me = m_zone->Profile();
	return id < 100 ? me.skills[id] : me.languages[id - 100];
}

int TrainWindow::CostToTrain(bool language, int skill) const
{
	// CostToTrain (@004ef6c7): from a language's value, or a skill's if we have it (HasSkill), less
	// 10 and no less than 0, cubed, times the guildmaster's rate and 0.01, truncated.
	const ZoneProfile &me = m_zone->Profile();
	int value = 0;
	if (language)
		value = (skill >= 0 && skill < 32 ? me.languages[skill] : 0) - 10;
	else
		value = Skills::HasSkill(me, skill) != 0 ? me.skills[skill] - 10 : 0;
	if (value < 0)
		value = 0;
	return static_cast<int>(static_cast<float>(value * value * value) * m_greed * 0.01f);
}

int32_t TrainWindow::Money() const
{
	const ZoneProfile &me = m_zone->Profile();
	return me.copper + (me.silver + (me.gold + me.platinum * 10) * 10) * 10;
}

void TrainWindow::Request(uint16_t guildmaster)
{
	if (!m_zone || m_guildmaster != 0)
		return;
	m_waiting = true;
	m_zone->GMTraining(guildmaster);
}

void TrainWindow::ZoneEvent(const ZoneSession::Event &event)
{
	if (event.type != ZoneSession::Event::Type::TrainerAnswered || !m_zone)
		return;
	const ZoneTrainer &trainer = event.trainer;
	if (trainer.npc == 0 || m_zone->Spawns().find(trainer.npc) == m_zone->Spawns().end())
		return;
	// The new UI's path: with success, this guildmaster's list up (even over another's); without,
	// no longer waiting (DAT_007d0258 back down from 1, so no OP_ClientError).
	if (trainer.success) {
		m_guildmaster = trainer.npc;
		SetGMData(trainer);
		Activate();
		return;
	}
	m_waiting = false;
}

void TrainWindow::SetGMData(const ZoneTrainer &trainer)
{
	const ZoneProfile &me = m_zone->Profile();
	m_greed = trainer.greed;
	m_rows.clear();
	for (int skill = 0; skill < 100; ++skill) {
		int cap = static_cast<int16_t>(trainer.skills[skill]);
		if (!NeverListed(skill) && cap < 253 && cap > 0 && me.skills[skill] != 255)
			m_rows.push_back({skill, cap, 0});
	}
	for (int language = 0; language < 32; ++language)
		if (Skills::CanLearnLanguage(me, language) && trainer.languages[language] != 0)
			m_rows.push_back({100 + language, 100, 0});
	UpdateAll(false);
}

void TrainWindow::Activate()
{
	m_next_check_ms = 0;
	if (m_host.music)
		m_host.music(true);
	UpdateAll(false);
	m_windows->Show("TrainWindow", true);
	m_windows->Raise("TrainWindow");
	m_active = true;
}

void TrainWindow::Deactivate()
{
	m_active = false;
	if (m_host.music)
		m_host.music(false);
	if (m_guildmaster != 0) {
		m_zone->GMEndTraining(m_guildmaster);
		m_waiting = false;
		m_guildmaster = 0;
		if (m_host.clear_target)
			m_host.clear_target();
	}
	m_windows->Show("TrainWindow", false);
}

void TrainWindow::Frame(uint64_t now_ms)
{
	if (!m_active)
		return;
	// The close box hides it as Done would.
	if (!m_windows->Shown("TrainWindow")) {
		Deactivate();
		return;
	}
	if (now_ms >= m_next_check_ms) {
		m_next_check_ms = now_ms + CheckMs;
		if (m_guildmaster != 0 && m_zone->SpawnDistance(m_guildmaster) > TrainRange)
			Deactivate();
	}
}

void TrainWindow::UpdateAll(bool sort)
{
	SidlWindow *wnd = m_windows->Find("TrainWindow");
	SidlWindow::Control *list = wnd ? wnd->Find("SkillList") : nullptr;
	if (list)
		list->selected_row = -1;
	if (sort) {
		if (m_by_name)
			SortNames(m_name_direction);
		else
			SortByValue(m_value_direction);
	}
	if (list) {
		list->rows.assign(m_rows.size(), std::vector<std::string>(Columns));
		for (int row = 0; row < static_cast<int>(m_rows.size()); ++row)
			UpdateSkill(row);
		wnd->UpdateListScroll(*list);
	}
	UpdateRight();
}

void TrainWindow::UpdateSkill(int row)
{
	SidlWindow *wnd = m_windows->Find("TrainWindow");
	SidlWindow::Control *list = wnd ? wnd->Find("SkillList") : nullptr;
	if (!list || row < 0 || row >= static_cast<int>(m_rows.size()) || row >= static_cast<int>(list->rows.size()))
		return;
	const ZoneProfile &me = m_zone->Profile();
	Row &entry = m_rows[row];
	std::vector<std::string> &cells = list->rows[row];
	cells.assign(Columns, "");
	int id = entry.id, value = RowValue(id);
	cells[0] = RowName(id);
	// The rank: the first limit the value's no more than (none: the first); a language spoken
	// fully is mastered.
	int rank = 0;
	for (int i = 0; i < static_cast<int>(std::size(RankLimits)); ++i)
		if (value <= RankLimits[i]) {
			rank = i == 9 && id > 99 && value == 100 ? 10 : i;
			break;
		}
	if (value == 254 || value == 255) {
		int cls = me.class_id;
		std::string level =
			value == 255 ? "0" : std::to_string(cls >= 1 && cls <= Skills::Classes ? Skills::Get(id).level[cls - 1] : 0);
		cells[1] = Text(2161, {level}, "LEVEL: %1");
	}
	else
		cells[1] = m_eqstr && m_eqstr->Has(FirstRankName + rank) ? m_eqstr->Format(FirstRankName + rank, {}) : "";
	if (value < 254)
		cells[2] = std::to_string(value);
	// The cost: copper, silver and gold a digit each from the right, the rest platinum; "--" for
	// nothing.
	int cost = CostToTrain(id > 99, id > 99 ? id - 100 : id);
	entry.cost = cost;
	if (cost < 1) {
		for (int c = 3; c < Columns; ++c)
			cells[c] = "--";
		return;
	}
	int column = 6;
	do {
		cells[column] = std::to_string(cost % 10);
		cost /= 10;
	} while (cost != 0 && --column > 3);
	cells[3] = std::to_string(cost);
}

void TrainWindow::UpdateRight()
{
	SidlWindow *wnd = m_windows->Find("TrainWindow");
	if (!wnd || !m_zone)
		return;
	const ZoneProfile &me = m_zone->Profile();
	if (SidlWindow::Control *count = wnd->Find("PracticeCount"))
		count->text = std::to_string(me.practice_points);
	SidlWindow::Control *train = wnd->Find("TrainButton");
	if (train)
		train->enabled = false;
	SidlWindow::Control *list = wnd->Find("SkillList");
	int row = list ? list->selected_row : -1;
	if (row >= 0 && row < static_cast<int>(m_rows.size()) && me.practice_points > 0 && m_rows[row].cost <= Money()) {
		int id = m_rows[row].id, cls = me.class_id;
		int level = id < 100 ? (cls >= 1 && cls <= Skills::Classes ? Skills::Get(id).level[cls - 1] : 0) : 1;
		if (level <= me.level && train)
			train->enabled = true;
	}
	const int32_t coins[4] = {me.platinum, me.gold, me.silver, me.copper};
	for (int coin = 0; coin < 4; ++coin)
		if (SidlWindow::Control *label = wnd->Find("CoinCount" + std::to_string(coin)))
			label->text = std::to_string(coins[coin]);
}

void TrainWindow::SortNames(int direction)
{
	// strcmp's -1, 0 or 1 against the direction: each place takes the name that compares that way
	// to the best so far, from the rows after it.
	int n = static_cast<int>(m_rows.size());
	for (int i = 0; i < n; ++i) {
		std::string best = RowName(m_rows[i].id);
		int pick = i;
		for (int j = i + 1; j < n; ++j) {
			std::string name = RowName(m_rows[j].id);
			int compared = std::strcmp(name.c_str(), best.c_str());
			compared = compared < 0 ? -1 : compared > 0 ? 1 : 0;
			if (compared == direction) {
				best = name;
				pick = j;
			}
		}
		if (pick != i)
			std::swap(m_rows[i], m_rows[pick]);
	}
}

void TrainWindow::SortByValue(int direction)
{
	// Going down (direction above 0), a greater value takes the place; going up, any not greater.
	int n = static_cast<int>(m_rows.size());
	for (int i = 0; i < n; ++i) {
		int best = RowValue(m_rows[i].id), pick = i;
		for (int j = i + 1; j < n; ++j) {
			int value = RowValue(m_rows[j].id);
			if ((value > best) == (direction > 0)) {
				best = value;
				pick = j;
			}
		}
		if (pick != i)
			std::swap(m_rows[i], m_rows[pick]);
	}
}

bool TrainWindow::BitchCanTrain(int cap, bool language, int skill)
{
	const ZoneProfile &me = m_zone->Profile();
	std::string name = GuildmasterName();
	auto say = [&](uint32_t id, const std::vector<std::string> &args, const std::string &fallback) {
		if (m_host.chat)
			m_host.chat(Text(id, args, fallback), ChatType::Default);
		return false;
	};
	if (skill == -1)
		return say(12923, {name}, "%1 tells you, 'I don't know that skill, so I can't teach it.'");
	if (language) {
		if (!Skills::CanLearnLanguage(me, skill))
			return say(12924, {name},
				"%1 tells you, 'You are not experienced enough to learn that language yet.  Come back after you have adventured more.'");
		if (me.practice_points <= 0)
			return say(12925, {name}, "%1 tells you, 'You don't have any practices left.  Come back after you have adventured more.'");
		int value = me.languages[skill];
		if (value == 100)
			return say(12926, {name, RowName(100 + skill)},
				"%1 tells you, 'You are already fluent to the limit of your abilities in %2.'");
		if (value < cap)
			return true;
		return say(12927, {name}, "%1 tells you, 'You are more fluent than I!  What could I possibly teach you?'");
	}
	int value = me.skills[skill];
	if (Skills::FieldTradeSkill(skill) && value != 254 && value > 20) {
		if (m_host.chat) // token_string_to_client: in red
			m_host.chat(Text(117, {}, "You must learn advanced trade skills in the field."), ChatType::Red);
		return false;
	}
	if (me.practice_points <= 0)
		return say(12925, {name}, "%1 tells you, 'You don't have any practices left.  Come back after you have adventured more.'");
	if (value == 255)
		return say(12928, {name}, "%1 tells you, 'You will never be able to acquire that skill.'");
	int cls = me.class_id;
	if (cls >= 1 && cls <= Skills::Classes && me.level < Skills::Get(skill).level[cls - 1])
		return say(12929, {name}, "%1 tells you, 'You are not ready to learn that skill.  Come back after you have adventured more.'");
	if (!Skills::AtLevelCap(me, skill) || value == 254) {
		if (value < Skills::SkillLimit(me, skill) || value == 254) {
			if (value < cap || value == 254)
				return true;
			return say(12931, {name}, "%1 tells you, 'You are more skilled than I!  What could I possibly teach you?'");
		}
	}
	return say(12930, {name, SkillName(m_eqstr, skill)},
		"%1 tells you, 'You are already trained to the limit of your abilities in %2.'");
}

void TrainWindow::Train()
{
	SidlWindow *wnd = m_windows->Find("TrainWindow");
	SidlWindow::Control *list = wnd ? wnd->Find("SkillList") : nullptr;
	if (!list || m_guildmaster == 0)
		return;
	int row = list->selected_row;
	if (row < 0 || row >= static_cast<int>(m_rows.size()))
		return;
	int id = m_rows[row].id;
	bool language = id > 99;
	int skill = language ? id - 100 : id;
	ZoneProfile &me = m_zone->EditProfile();
	if (!BitchCanTrain(m_rows[row].cap, language, skill))
		return;
	bool first = !language && Skills::HasSkill(me, skill) == 0;
	std::string name = GuildmasterName();
	auto say = [&](uint32_t text, const std::vector<std::string> &args, const std::string &fallback) {
		if (m_host.chat)
			m_host.chat(Text(text, args, fallback), ChatType::Default);
	};
	int cost = CostToTrain(language, skill);
	if (Money() < cost) {
		say(12065, {name}, "%1 tells you, 'You cannot afford the training.  Come back after you have acquired more money.'");
		return;
	}
	if (cost != 0)
		Inventory::HandleMoney(me, cost);
	m_zone->GMTrainSkill(m_guildmaster, language ? 1 : 0, static_cast<uint32_t>(skill));
	--me.practice_points;
	std::string what = RowName(id);
	if (first) {
		// A new skill starts where our class gets it.
		int cls = me.class_id;
		me.skills[skill] = static_cast<int16_t>(cls >= 1 && cls <= Skills::Classes ? Skills::Get(skill).level[cls - 1] : 0);
		say(12066, {name, what}, "%1 tells you, 'You have learned the basics of %2.'");
	}
	else if (language) {
		if (me.languages[skill] == 0)
			say(12066, {name, what}, "%1 tells you, 'You have learned the basics of %2.'");
		else
			say(12067, {name, what}, "%1 tells you, 'You have increased your fluency in %2.'");
		++me.languages[skill];
	}
	else {
		++me.skills[skill];
		say(12068, {name, what}, "%1 tells you, 'You have increased your skill in %2.'");
	}
	UpdateSkill(row);
}

bool TrainWindow::Click(const std::string &window, const std::string &control)
{
	if (window != "TrainWindow" || !m_active)
		return false;
	if (control == "DoneButton") {
		Deactivate();
		return true;
	}
	if (control == "TrainButton")
		Train();
	else if (control == "SkillList") {
		SidlWindow *wnd = m_windows->Find("TrainWindow");
		SidlWindow::Control *list = wnd ? wnd->Find("SkillList") : nullptr;
		if (list && list->header_clicked >= 0) {
			// The heading: the name's, else the rank's or value's, the other way from last time.
			int column = list->header_clicked;
			list->header_clicked = -1;
			if (column == 0) {
				m_by_name = true;
				m_name_direction = -m_name_direction;
				UpdateAll(true);
			}
			else if (column == 1 || column == 2) {
				m_by_name = false;
				m_value_direction = -m_value_direction;
				UpdateAll(true);
			}
			return true;
		}
		int row = list ? list->selected_row : -1;
		if (row >= 0 && row < static_cast<int>(m_rows.size()) && m_guildmaster != 0) {
			int id = m_rows[row].id;
			if (id < 100)
				m_host.chat(Text(12056, {GuildmasterName(), RowName(id)}, "%1 says, 'Interested in training in %2 ?'"),
					ChatType::Yellow);
			else
				m_host.chat(Text(12055, {GuildmasterName(), RowName(id)},
					"%1 says, 'Interested in training in the %2 language ?'"), ChatType::Yellow);
		}
	}
	else
		return true;
	UpdateRight();
	return true;
}
