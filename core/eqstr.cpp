#include "eqstr.h"

#include <cstdlib>
#include <fstream>

bool EqStr::Load(const std::string &path)
{
	std::ifstream file(path, std::ios::binary);
	if (!file)
		return false;
	m_strings.clear();
	std::string line;
	int line_number = 0;
	while (std::getline(file, line)) {
		if (!line.empty() && line.back() == '\r')
			line.pop_back();
		if (++line_number <= 2)
			continue; // "EQST0002" and "0 <count>"
		size_t space = line.find(' ');
		if (space == std::string::npos || space == 0)
			continue;
		char *end = nullptr;
		unsigned long id = strtoul(line.c_str(), &end, 10);
		if (end != line.c_str() + space)
			continue;
		m_strings[static_cast<uint32_t>(id)] = line.substr(space + 1);
	}
	return !m_strings.empty();
}

std::string EqStr::Format(uint32_t id, const std::vector<std::string> &args) const
{
	auto it = m_strings.find(id);
	if (it == m_strings.end()) {
		std::string out = "[string " + std::to_string(id) + "]";
		for (const auto &arg : args)
			out += " " + arg;
		return out;
	}
	std::string out;
	const std::string &text = it->second;
	for (size_t i = 0; i < text.size(); ++i) {
		if (text[i] == '%' && i + 1 < text.size() && text[i + 1] >= '1' && text[i + 1] <= '9') {
			size_t arg = static_cast<size_t>(text[i + 1] - '1');
			if (arg < args.size())
				out += args[arg];
			++i;
		}
		else
			out.push_back(text[i]);
	}
	return out;
}
