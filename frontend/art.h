#pragma once

// Front-end art from the EQ install: eqfeart1.pak .. eqfeart17.pak, searched in
// the order initializeGameResources loads them (eqfeart.pak is not used).

#include "bitmap.h"
#include "pfs.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

class FrontEndArt
{
public:
	// Returns false (and sets error) when no archive could be opened.
	bool Open(const std::string &eq_dir, std::string &error);

	// Case-insensitive. A missing or undecodable bitmap returns a 1x1 magenta placeholder,
	// which draws as nothing with a color key; Missing() lists those names.
	const Image &Get(const std::string &name);
	const std::vector<std::string> &Missing() const { return m_missing; }

private:
	std::vector<std::unique_ptr<PfsArchive>> m_archives;
	std::map<std::string, Image> m_cache;
	std::vector<std::string> m_missing;
};
