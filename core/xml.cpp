#include "xml.h"

#include <cctype>
#include <cstdlib>
#include <cstring>

namespace
{
	std::string Trim(const std::string &s)
	{
		size_t a = 0, b = s.size();
		while (a < b && std::isspace(static_cast<unsigned char>(s[a])))
			++a;
		while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1])))
			--b;
		return s.substr(a, b - a);
	}

	std::string DecodeEntities(const std::string &s)
	{
		std::string out;
		for (size_t i = 0; i < s.size(); ++i) {
			if (s[i] != '&') {
				out.push_back(s[i]);
				continue;
			}
			size_t semi = s.find(';', i);
			if (semi == std::string::npos) {
				out.push_back(s[i]);
				continue;
			}
			std::string entity = s.substr(i + 1, semi - i - 1);
			if (entity == "lt") out.push_back('<');
			else if (entity == "gt") out.push_back('>');
			else if (entity == "amp") out.push_back('&');
			else if (entity == "quot") out.push_back('"');
			else if (entity == "apos") out.push_back('\'');
			else if (!entity.empty() && entity[0] == '#') {
				long code = entity.size() > 1 && (entity[1] == 'x' || entity[1] == 'X') ? strtol(entity.c_str() + 2, nullptr, 16)
				                                                                         : strtol(entity.c_str() + 1, nullptr, 10);
				out.push_back(code > 0 && code < 128 ? static_cast<char>(code) : '?');
			}
			else {
				out += s.substr(i, semi - i + 1);
			}
			i = semi;
		}
		return out;
	}

	class Parser
	{
	public:
		explicit Parser(const std::string &text) : m_s(text) {}

		std::unique_ptr<XmlNode> Document(std::string &error)
		{
			SkipMisc();
			if (m_pos >= m_s.size() || m_s[m_pos] != '<') {
				error = "no root element";
				return nullptr;
			}
			auto root = Element(error);
			return error.empty() ? std::move(root) : nullptr;
		}

	private:
		void SkipSpace()
		{
			while (m_pos < m_s.size() && std::isspace(static_cast<unsigned char>(m_s[m_pos])))
				++m_pos;
		}

		bool StartsWith(const char *prefix) const { return m_s.compare(m_pos, strlen(prefix), prefix) == 0; }

		// Prolog, comments, processing instructions and whitespace between elements.
		void SkipMisc()
		{
			for (;;) {
				SkipSpace();
				if (StartsWith("<?")) {
					size_t end = m_s.find("?>", m_pos);
					m_pos = end == std::string::npos ? m_s.size() : end + 2;
				}
				else if (StartsWith("<!--")) {
					size_t end = m_s.find("-->", m_pos);
					m_pos = end == std::string::npos ? m_s.size() : end + 3;
				}
				else if (StartsWith("<!")) {
					size_t end = m_s.find('>', m_pos);
					m_pos = end == std::string::npos ? m_s.size() : end + 1;
				}
				else
					return;
			}
		}

		std::string Name()
		{
			size_t start = m_pos;
			while (m_pos < m_s.size() && (std::isalnum(static_cast<unsigned char>(m_s[m_pos])) || m_s[m_pos] == '_' ||
			                              m_s[m_pos] == ':' || m_s[m_pos] == '-' || m_s[m_pos] == '.'))
				++m_pos;
			return m_s.substr(start, m_pos - start);
		}

		std::unique_ptr<XmlNode> Element(std::string &error)
		{
			auto node = std::make_unique<XmlNode>();
			++m_pos; // '<'
			node->name = Name();
			if (node->name.empty()) {
				error = "bad element name at offset " + std::to_string(m_pos);
				return nullptr;
			}

			for (;;) {
				SkipSpace();
				if (m_pos >= m_s.size()) {
					error = "unterminated tag <" + node->name + ">";
					return nullptr;
				}
				if (StartsWith("/>")) {
					m_pos += 2;
					return node;
				}
				if (m_s[m_pos] == '>') {
					++m_pos;
					break;
				}
				std::string attribute = Name();
				SkipSpace();
				if (attribute.empty() || m_pos >= m_s.size() || m_s[m_pos] != '=') {
					error = "bad attribute in <" + node->name + ">";
					return nullptr;
				}
				++m_pos;
				SkipSpace();
				char quote = m_pos < m_s.size() ? m_s[m_pos] : 0;
				if (quote != '"' && quote != '\'') {
					error = "unquoted attribute in <" + node->name + ">";
					return nullptr;
				}
				size_t end = m_s.find(quote, m_pos + 1);
				if (end == std::string::npos) {
					error = "unterminated attribute in <" + node->name + ">";
					return nullptr;
				}
				node->attributes[attribute] = DecodeEntities(m_s.substr(m_pos + 1, end - m_pos - 1));
				m_pos = end + 1;
			}

			std::string text;
			for (;;) {
				if (m_pos >= m_s.size()) {
					error = "missing </" + node->name + ">";
					return nullptr;
				}
				if (StartsWith("<!--") || StartsWith("<?")) {
					SkipMisc();
					continue;
				}
				if (StartsWith("</")) {
					m_pos += 2;
					std::string closing = Name();
					SkipSpace();
					if (closing != node->name || m_pos >= m_s.size() || m_s[m_pos] != '>') {
						error = "mismatched </" + closing + "> for <" + node->name + ">";
						return nullptr;
					}
					++m_pos;
					node->text = Trim(DecodeEntities(text));
					return node;
				}
				if (m_s[m_pos] == '<') {
					auto child = Element(error);
					if (!child)
						return nullptr;
					node->children.push_back(std::move(child));
					continue;
				}
				size_t next = m_s.find('<', m_pos);
				if (next == std::string::npos)
					next = m_s.size();
				text += m_s.substr(m_pos, next - m_pos);
				m_pos = next;
			}
		}

		const std::string &m_s;
		size_t m_pos = 0;
	};
}

const XmlNode *XmlNode::Child(const std::string &child_name) const
{
	for (const auto &child : children)
		if (child->name == child_name)
			return child.get();
	return nullptr;
}

std::vector<const XmlNode *> XmlNode::Children(const std::string &child_name) const
{
	std::vector<const XmlNode *> out;
	for (const auto &child : children)
		if (child->name == child_name)
			out.push_back(child.get());
	return out;
}

std::string XmlNode::ChildText(const std::string &child_name, const std::string &fallback) const
{
	const XmlNode *child = Child(child_name);
	return child ? child->text : fallback;
}

int XmlNode::ChildInt(const std::string &child_name, int fallback) const
{
	const XmlNode *child = Child(child_name);
	return child && !child->text.empty() ? atoi(child->text.c_str()) : fallback;
}

bool XmlNode::ChildBool(const std::string &child_name, bool fallback) const
{
	const XmlNode *child = Child(child_name);
	if (!child || child->text.empty())
		return fallback;
	return child->text == "true" || child->text == "TRUE" || child->text == "True" || child->text == "1";
}

std::string XmlNode::Attribute(const std::string &attribute_name) const
{
	auto it = attributes.find(attribute_name);
	return it != attributes.end() ? it->second : std::string();
}

std::unique_ptr<XmlNode> ParseXml(const std::string &text, std::string &error)
{
	error.clear();
	// Skip a UTF-8 BOM.
	if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xef && static_cast<unsigned char>(text[1]) == 0xbb &&
	    static_cast<unsigned char>(text[2]) == 0xbf)
		return Parser(text.substr(3)).Document(error);
	return Parser(text).Document(error);
}
