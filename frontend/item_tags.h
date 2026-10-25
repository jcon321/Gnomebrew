#pragma once

// eqgame's item tags: a link in a line of text, to an item or (from the zone) a say link.
//   - CEditWnd::AddItemTag (@005a2920) puts one in an edit box as sprintf("%c%d%06d%s%c", 0x12, 0,
//     id, name, 0x12), at most 10 to the box (item_link_count);
//   - ConvertItemTags (@00502322) makes each one in a line of chat an STML link
//     (CStmlWnd_MakeWndNotificationTag, notification 0x1a) showing the text after the id: the six
//     digits from the tag's third character are the id, and a marker with no other after it goes;
//   - CChatWindow::WndNotification's 0x1a (@00413be9): a click on one sends OP_ItemLinkResponse
//     with the id and the first word of the text (sscanf "%6d%s"). The zone answers an item's with
//     the item, and says a say link's phrase for us (EQMacEmu numbers say links from 0x8000);
//   - CStmlWnd::DrawInRect and CEditWnd::RenderEditWindowText draw them in User_71's color
//     (convertColorIndex 0x146), and the one under the mouse blue (0x0000ff), navy (0x000080)
//     while it's pressed.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ItemTags
{
	constexpr char Marker = 0x12;
	constexpr int MaxPerLine = 10;
	constexpr int ColorType = 0x146; // a chat color type: User_71
	constexpr uint32_t HoverColor = 0x0000ff;
	constexpr uint32_t PressedColor = 0x000080;
}

// A tag where it shows: [begin, end) of the shown text, which is its name.
struct ItemTag
{
	size_t begin = 0, end = 0;
	uint16_t id = 0;
	std::string name;
};

// A run of the shown text, cut where tags begin and end: a tag's (its index) or not (-1).
struct ItemTagPiece
{
	size_t begin = 0, end = 0;
	int tag = -1;
};

std::string MakeItemTag(uint16_t id, const std::string &name);
// The text as it shows, each tag's name in its place, and the tags (when asked for).
std::string ShowItemTags(const std::string &tagged, std::vector<ItemTag> *tags = nullptr);
int CountItemTags(const std::string &tagged);
// [from, to) of the shown text in pieces at the tags' ends.
std::vector<ItemTagPiece> SplitAtItemTags(const std::vector<ItemTag> &tags, size_t from, size_t to);
