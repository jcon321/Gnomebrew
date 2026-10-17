#include "eqtext.h"

#include <cctype>
#include <fstream>
#include <iterator>

bool EqText::Load(const std::string &path)
{
	std::ifstream file(path, std::ios::binary);
	if (!file)
		return false;
	std::string data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

	m_strings.clear();
	m_keys.clear();
	size_t pos = 0;
	while (m_strings.size() < 0x200) {
		size_t open = data.find('"', pos);
		if (open == std::string::npos)
			break;

		// The key is the first token on the line holding the opening quote.
		size_t line_start = data.rfind('\n', open);
		line_start = line_start == std::string::npos ? 0 : line_start + 1;
		size_t key_end = line_start;
		while (key_end < open && (std::isalnum(static_cast<unsigned char>(data[key_end])) || data[key_end] == '_'))
			++key_end;
		std::string key = data.substr(line_start, key_end - line_start);

		std::string text;
		size_t i = open + 1;
		for (; i < data.size() && data[i] != '"'; ++i) {
			char c = data[i];
			if (c == '\\' && i + 1 < data.size()) {
				c = data[++i];
				switch (c) {
				case '0': c = '\0'; break;
				case 'a': c = '\a'; break;
				case 'b': c = '\b'; break;
				case 'f': c = '\f'; break;
				case 'n': c = '\n'; break;
				case 'r': c = '\r'; break;
				case 't': c = '\t'; break;
				case 'v': c = '\v'; break;
				default: break; // \" \' \? \\ and anything else: the character itself
				}
			}
			if (text.size() < 0x3fe)
				text.push_back(c);
		}
		if (i >= data.size())
			break; // unterminated

		if (!key.empty() && !m_keys.count(key))
			m_keys[key] = m_strings.size();
		m_strings.push_back(text);
		pos = i + 1;
	}
	return !m_strings.empty();
}

std::string EqText::Get(size_t index) const
{
	return index < m_strings.size() ? m_strings[index] : "#" + std::to_string(index);
}

std::string EqText::Get(const std::string &key) const
{
	auto it = m_keys.find(key);
	return it != m_keys.end() ? m_strings[it->second] : key;
}
