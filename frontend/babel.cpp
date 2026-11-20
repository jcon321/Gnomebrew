#include "babel.h"

#include "item_tags.h"

#include <cctype>
#include <cstring>

namespace
{
	bool Alpha(char c) { return std::isalpha(static_cast<unsigned char>(c)) != 0; } // ctype & 0x103
	bool Digit(char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; } // ctype & 4
	bool Vowel(char c) { return std::strchr("aeiouyAEIOUY", c) != nullptr && c != 0; } // NameFailsOnBasicChecks2
	int Percent(const Babel::Random &rand) { return rand() % 100 + 1; }               // number_percent

	bool Same(const std::string &a, const char *b)
	{
		if (a.size() != std::strlen(b))
			return false;
		for (size_t i = 0; i < a.size(); ++i)
			if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
				return false;
		return true;
	}

	// A letter changed by a roll `roll` over the skill: SpeakBabel's and HearBabel's.
	char Garble(char c, int roll, int skill, const Babel::Random &rand)
	{
		if (roll - skill < 31) {
			if (!Vowel(c)) {
				char out;
				do
					out = static_cast<char>(rand() % 25 + 'a');
				while (Vowel(out));
				return out;
			}
			return "aeiouy"[rand() % 6]; // CreateName3
		}
		return static_cast<char>(rand() % 26 + '`');
	}

	// CEverQuest_GrabFirstWord: past the spaces, a word up to a space (or a quote it starts with),
	// the end or an item tag's marker; returns where the next starts.
	size_t GrabFirstWord(const std::string &s, size_t at, std::string &word)
	{
		word.clear();
		while (at < s.size() && std::isspace(static_cast<unsigned char>(s[at])))
			++at;
		char end = ' ';
		if (at < s.size() && (s[at] == '\'' || s[at] == '"'))
			end = s[at++];
		while (at < s.size()) {
			char c = s[at];
			if (c == end) {
				++at;
				break;
			}
			if (c == ItemTags::Marker)
				break;
			word.push_back(c);
			++at;
		}
		while (at < s.size() && std::isspace(static_cast<unsigned char>(s[at])))
			++at;
		return at;
	}
}

bool Babel::BadWord(const std::string &word)
{
	// bad_word_class("word")'s twenty, anywhere in the word, lower cased; not a word of 32 or more.
	static const char *const Words[] = {"fuck", "shit", "piss", "cock", "penis", "pussy", "fag", "cunt", "kunt",
		"nigger", "niger", "asshole", "tits", "nigga", "jesus!", "christ!", "damn", "dong", "bitch", "dickhead"};
	if (word.size() >= 32)
		return false;
	std::string lower = word;
	for (char &c : lower)
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	for (const char *bad : Words)
		if (lower.find(bad) != std::string::npos)
			return true;
	return false;
}

std::string Babel::Speak(const std::string &text, int skill, bool filter_off, const Random &rand)
{
	if (skill >= 100 && filter_off)
		return text;
	int n = static_cast<int>(text.size());
	// Non-letters that would run into a word become spaces.
	std::string spaced = text;
	int run = 0;
	for (int i = 1; i < n - 1; ++i) {
		char c = text[i], prev = text[i - 1], next = text[i + 1];
		bool space = !Alpha(c) && c != ItemTags::Marker &&
		             (c == ' ' || run == 15 || (c != '\'' && next != ' ' && !Alpha(c) && !Digit(c)) ||
		                 (Digit(c) && prev != ' ' && next != ' ' && !Digit(prev) && !Digit(next)));
		if (space) {
			run = 0;
			spaced[i] = ' ';
		}
		else {
			++run;
			spaced[i] = c;
		}
	}
	std::string out;
	size_t at = 0;
	while (at < spaced.size()) {
		if (spaced[at] == ItemTags::Marker) {
			size_t close = spaced.find(ItemTags::Marker, at + 1);
			if (close != std::string::npos) {
				out += spaced.substr(at, close - at + 1) + " ";
				at = close + 1;
			}
			else
				spaced[at] = 'Q';
			continue;
		}
		std::string grabbed;
		size_t next = GrabFirstWord(spaced, at, grabbed);
		std::string word;
		if (grabbed.empty()) {
			word = spaced.substr(at, 15);
			next = at + word.size();
		}
		else
			word = grabbed.substr(0, 15);
		at = next;
		if (!filter_off && !word.empty() && BadWord(word)) {
			for (char &c : word)
				c = static_cast<char>(rand() % 26 + '`');
		}
		else {
			int roll = Percent(rand);
			if (roll > skill && word.size() <= 7) {
				if (Same(word, "I") || Same(word, "I'") || Same(word, "my"))
					word = "me";
				else if (Same(word, "let's") || Same(word, "we'll"))
					word = "we";
				else if (Same(word, "they'll"))
					word = "them";
				else if (Same(word, "it's") || Same(word, "its"))
					word = "it";
				else if (Same(word, "went"))
					word = "did go";
				else {
					bool little = false;
					for (const char *w : {"a", "am", "an", "to", "or", "do", "the", "and", "are", "was", "has", "were", "that"})
						little |= Same(word, w);
					if (little)
						word.clear();
					else if (roll - skill > 20) {
						if (Same(word, "of") || Same(word, "is"))
							word.clear();
						else if (Same(word, "his"))
							word = "him";
						else if (Same(word, "best"))
							word = "more better";
					}
				}
			}
			for (size_t i = 0; i < word.size(); ++i) {
				if (!Alpha(word[i]) || Percent(rand) <= 40)
					continue;
				int b = Percent(rand);
				if (b <= 5 || b <= skill + 10)
					continue;
				bool inside = b >= 99 && i >= 1 && word[i - 1] != ' ' && i + 1 < word.size() && word[i + 1] != ' ';
				word[i] = inside ? ' ' : Garble(word[i], b, skill, rand);
			}
		}
		if (word.empty())
			continue;
		out += word;
		if (out.back() != ' ')
			out += ' ';
	}
	if (n - 1 > 0 && !out.empty()) {
		char last = text[n - 1];
		if (!Alpha(last) && !Digit(last) && out.size() >= 2)
			out[out.size() - 2] = last;
		out.pop_back();
		if (!out.empty() && out[0] >= 'a' && out[0] <= 'z')
			out[0] = static_cast<char>(out[0] - 0x20);
	}
	return out;
}

std::string Babel::Hear(const std::string &text, int skill, bool gibberish, const Random &rand)
{
	if (skill >= 100 || !gibberish)
		return text;
	std::string out;
	bool tag = false;
	for (size_t i = 0; i < text.size(); ++i) {
		char c = text[i];
		if (c == ItemTags::Marker)
			tag = !tag;
		if (!Alpha(c) || tag || Percent(rand) < 51) {
			out.push_back(c);
			continue;
		}
		int b = Percent(rand);
		if (b < 6 || b <= skill + 10) {
			out.push_back(c);
			continue;
		}
		bool edge = b < 99 || out.empty() || out.back() == ' ' || (i + 1 < text.size() && text[i + 1] == ' ') ||
		            i + 1 == text.size();
		out.push_back(edge ? Garble(c, b, skill, rand) : ' ');
	}
	return out;
}
