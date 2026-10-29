#include "eqstr.h"

#include <cctype>
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
	return Expand(it->second, args, 0);
}

std::string EqStr::Expand(const std::string &text, const std::vector<std::string> &args, int depth) const
{
	// orderedStringExpansion (@004f7ea2): %1..%9 the arguments; %Tn the string whose id is
	// argument n, expanded with the same arguments; %#id that string; %% a '%'. Ten deep at most.
	if (++depth > 10)
		return "";
	auto inner = [&](const std::string &number) {
		auto found = m_strings.find(static_cast<uint32_t>(std::strtoul(number.c_str(), nullptr, 10)));
		return found != m_strings.end() ? Expand(found->second, args, depth) : std::string();
	};
	std::string out;
	for (size_t i = 0; i < text.size(); ++i) {
		char next = i + 1 < text.size() ? text[i + 1] : '\0';
		if (text[i] != '%') {
			out.push_back(text[i]);
			continue;
		}
		if (next >= '1' && next <= '9') {
			size_t arg = static_cast<size_t>(next - '1');
			if (arg < args.size())
				out += args[arg];
			++i;
		}
		else if (next == 'T' && i + 2 < text.size() && text[i + 2] >= '1' && text[i + 2] <= '9') {
			size_t arg = static_cast<size_t>(text[i + 2] - '1');
			if (arg < args.size() && !args[arg].empty())
				out += inner(args[arg]);
			i += 2;
		}
		else if (next == '#') {
			size_t end = i + 2;
			while (end < text.size() && std::isalnum(static_cast<unsigned char>(text[end])) && end - (i + 2) < 8)
				++end;
			if (end == i + 2) {
				out += "%#";
				++i;
				continue;
			}
			out += inner(text.substr(i + 2, end - (i + 2)));
			i = end - 1;
		}
		else if (next == '%') {
			out.push_back('%');
			++i;
		}
		else
			out.push_back('%');
	}
	return out;
}
