#pragma once

// eqstr_en.txt: eqgame's numbered string table ("EQST0002", "0 <count>", then
// "<id> <text>" lines with %1..%9 placeholders). OP_FormattedMessage sends an
// id plus arguments instead of text.

#include <cstdint>
#include <map>
#include <string>
#include <vector>

class EqStr
{
public:
	bool Load(const std::string &path);
	bool Has(uint32_t id) const { return m_strings.count(id) != 0; }
	// The string with %1..%9 replaced by args (and %Tn, %#id: Expand); "[string id]" plus the args
	// if the id is unknown.
	std::string Format(uint32_t id, const std::vector<std::string> &args) const;

private:
	std::string Expand(const std::string &text, const std::vector<std::string> &args, int depth) const;
	std::map<uint32_t, std::string> m_strings;
};
