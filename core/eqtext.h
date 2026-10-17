#pragma once

// eqtext.txt: the front end's string table. eqmain's loadLocalizedTextStrings
// (@10032ff0) collects every "quoted" string in file order, with C escapes,
// and the UI refers to them by index. Most lines are `KEY "text"`, so the key
// before each string is recorded too.

#include <map>
#include <string>
#include <vector>

class EqText
{
public:
	bool Load(const std::string &path);
	size_t Count() const { return m_strings.size(); }

	// Text by index, or by key name. Returns the key itself if it's missing so gaps are visible.
	std::string Get(size_t index) const;
	std::string Get(const std::string &key) const;

private:
	std::vector<std::string> m_strings; // UTF-8
	std::map<std::string, size_t> m_keys;
};
