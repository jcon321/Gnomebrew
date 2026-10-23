#pragma once

// A Windows-style .ini file, read and written in place: sections and keys keep their order
// and spelling, and are found case-insensitively (as GetPrivateProfileString does). Lines we
// never touch (keys, ; comments, blank lines, anything else) are written back as they were.
// The comments right above a [section] line go with it, so a new key goes after its section's
// last key and a new section between two others, without splitting a section from its comments.

#include <string>
#include <vector>

class IniFile
{
public:
	bool Load(const std::string &path); // false when it can't be read (it starts empty)
	bool Save(const std::string &path) const;

	bool Has(const std::string &section, const std::string &key) const;
	std::string Get(const std::string &section, const std::string &key, const std::string &fallback = "") const;
	int GetInt(const std::string &section, const std::string &key, int fallback) const;
	// "true", "T..." or "1" (eqgame's LoadIniInfo checks the first character).
	bool GetBool(const std::string &section, const std::string &key, bool fallback) const;

	void Set(const std::string &section, const std::string &key, const std::string &value);
	void SetInt(const std::string &section, const std::string &key, int value) { Set(section, key, std::to_string(value)); }
	void SetBool(const std::string &section, const std::string &key, bool value) { Set(section, key, value ? "true" : "false"); }
	// The key gone (WritePrivateProfileString with no value).
	void Remove(const std::string &section, const std::string &key);
	// key=value, under `comment` (each of its lines a ; comment), unless the key is there already;
	// true when it was added.
	bool Add(const std::string &section, const std::string &key, const std::string &value, const std::string &comment);
	// The section, unless it's there already, right after `after` (empty: first, after the lines
	// at the top; not there: last). Set and Add make a missing section last.
	void AddSection(const std::string &section, const std::string &after);

private:
	struct Entry
	{
		std::string key, value; // key empty: not a setting (a comment, a blank line), just `line`
		std::string line;       // as read; empty once Set changes it (written as key=value)
	};
	struct Section
	{
		std::string name;
		std::vector<Entry> entries;
		bool header = true;     // false: the lines before the first [section]
		std::vector<Entry> lead; // the blank lines and comments above the [section] line
	};
	const Entry *Find(const std::string &section, const std::string &key) const;
	static bool IsBlank(const Entry &entry);
	static void TakeLead(Section &previous, Section &next); // Load: `next`'s comments from the end of `previous`
	Section *FindSection(const std::string &section);
	Section &MakeSection(const std::string &section, size_t index);
	Section &SectionFor(const std::string &section); // made at the end when it's not there
	std::vector<Entry>::iterator InsertPoint(Section &section); // where a new key goes

	std::vector<Section> m_sections;
};
