#pragma once

// A small XML reader for the SIDL UI files: elements, attributes, text,
// comments and the <?xml?> prolog. No DTDs, CDATA or namespaces handling.

#include <map>
#include <memory>
#include <string>
#include <vector>

struct XmlNode
{
	std::string name;
	std::map<std::string, std::string> attributes;
	std::string text; // concatenated character data, entities decoded, trimmed
	std::vector<std::unique_ptr<XmlNode>> children;

	const XmlNode *Child(const std::string &child_name) const;
	std::vector<const XmlNode *> Children(const std::string &child_name) const;
	std::string ChildText(const std::string &child_name, const std::string &fallback = "") const;
	int ChildInt(const std::string &child_name, int fallback) const;
	bool ChildBool(const std::string &child_name, bool fallback) const;
	std::string Attribute(const std::string &attribute_name) const;
};

// Parses a document; returns the root element, or null and an error message.
std::unique_ptr<XmlNode> ParseXml(const std::string &text, std::string &error);
