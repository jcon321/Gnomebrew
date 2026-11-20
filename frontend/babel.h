#pragma once

// What's said in a language, as eqgame's msg_new_text (@004e25a1) passes it on (English only:
// DAT_00799770 0), for any channel but 6 and 14, unless we're a GM:
//   - SpeakBabel (@00505cfd), by the speaker's skill in it (ChannelMessage_Struct +0x86) and the bad
//     word filter (/filter badword: DAT_00798b24 0, on): with the skill under 100 or the filter on,
//     non-letters run into a word (but an apostrophe, or before a space; a lone digit; and the 16th
//     of a run) become spaces; then word by word (CEverQuest_GrabFirstWord, 15 characters at most;
//     an item tag kept whole), with the filter on a bad word (BadWord @00505b7f: one of 20 within
//     it) becomes random letters; else, a word of no more than 7 letters and a roll (number_percent)
//     over the skill is changed (I and my to me, let's and we'll to we, they'll to them, it's and
//     its to it, went to "did go", the little words dropped, and 20 over the skill also of and is
//     dropped, his to him, best to "more better"), and each letter, 40% of the time, with a second
//     roll over 5 and over the skill + 10, becomes another vowel for a vowel or consonant for a
//     consonant (30 over the skill and under: any letter), or a space inside a word on 99 or 100;
//     the words joined by spaces, the text's last character kept if it isn't a letter or digit,
//     the first letter capitalized;
//   - HearBabel (@005063a9), by our skill in it, when the world says (OP_LogServer's Gibberish,
//     DAT_0079976c) and it's under 100: each letter outside an item tag, 49% of the time, with a
//     roll over 5 and over our skill + 10, changed the same way.

#include <functional>
#include <string>

namespace Babel
{
	using Random = std::function<int()>; // RandNbr
	std::string Speak(const std::string &text, int skill, bool filter_off, const Random &rand);
	std::string Hear(const std::string &text, int skill, bool gibberish, const Random &rand);
	bool BadWord(const std::string &word);
}
