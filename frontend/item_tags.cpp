#include "item_tags.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

std::string MakeItemTag(uint16_t id, const std::string &name)
{
	char body[16];
	std::snprintf(body, sizeof(body), "%c%d%06d", ItemTags::Marker, 0, id);
	return body + name + ItemTags::Marker;
}

std::string ShowItemTags(const std::string &tagged, std::vector<ItemTag> *tags)
{
	std::string shown;
	if (tags)
		tags->clear();
	size_t i = 0;
	while (i < tagged.size()) {
		if (tagged[i] != ItemTags::Marker) {
			shown.push_back(tagged[i++]);
			continue;
		}
		// The closing marker is looked for from the third character on; without one, this one goes.
		size_t close = tagged.find(ItemTags::Marker, i + 2);
		if (close == std::string::npos) {
			++i;
			continue;
		}
		ItemTag tag;
		tag.id = static_cast<uint16_t>(std::strtol(tagged.substr(i + 2, std::min<size_t>(6, close - i - 2)).c_str(), nullptr, 10));
		tag.name = close > i + 8 ? tagged.substr(i + 8, close - i - 8) : "";
		tag.begin = shown.size();
		shown += tag.name;
		tag.end = shown.size();
		if (tags)
			tags->push_back(std::move(tag));
		i = close + 1;
	}
	return shown;
}

int CountItemTags(const std::string &tagged)
{
	std::vector<ItemTag> tags;
	ShowItemTags(tagged, &tags);
	return static_cast<int>(tags.size());
}

std::vector<ItemTagPiece> SplitAtItemTags(const std::vector<ItemTag> &tags, size_t from, size_t to)
{
	std::vector<ItemTagPiece> pieces;
	size_t at = from;
	for (size_t t = 0; t < tags.size() && at < to; ++t) {
		const ItemTag &tag = tags[t];
		if (tag.end <= at || tag.begin == tag.end)
			continue;
		if (tag.begin >= to)
			break;
		if (tag.begin > at)
			pieces.push_back({at, tag.begin, -1});
		at = std::max(at, tag.begin);
		size_t end = std::min(tag.end, to);
		pieces.push_back({at, end, static_cast<int>(t)});
		at = end;
	}
	if (at < to)
		pieces.push_back({at, to, -1});
	return pieces;
}
