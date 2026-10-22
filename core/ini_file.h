#pragma once

// A Windows-style .ini file, read and written in place: sections and keys keep their order
// and spelling, and are found case-insensitively (as GetPrivateProfileString does). Keys we
// never touch are written back as they were.

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

private:
	struct Entry
	{
		std::string key, value;
	};
	struct Section
	{
		std::string name;
		std::vector<Entry> entries;
	};
	const Entry *Find(const std::string &section, const std::string &key) const;

	std::vector<Section> m_sections;
};
