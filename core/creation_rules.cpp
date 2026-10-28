#include "creation_rules.h"

#include <cstring>
#include <filesystem>

const uint16_t CreationRules::RaceButtons[RaceCount] = {2, 6, 8, 3, 12, 7, 11, 5, 1, 128, 10, 9, 130, 4};
const uint8_t CreationRules::ClassButtons[ClassCount] = {8, 15, 2, 6, 14, 13, 7, 11, 3, 4, 9, 5, 10, 1, 12};
const uint16_t CreationRules::Deities[DeityCount] = {396, 201, 205, 203, 206, 207, 204, 208, 212, 209, 210, 213, 202, 214,
	215, 216, 211};

namespace
{
	// EQ_Character::BaseAttr: points each class puts into a stat, by class (STR STA AGI DEX WIS INT CHA).
	const int8_t ClassPoints[15][CreationRules::StatCount] = {
		{10, 10,  5,  0,  0,  0,  0}, // WAR
		{ 5,  5,  0,  0, 10,  0,  0}, // CLR
		{10,  5,  0,  0,  5,  0, 10}, // PAL
		{ 5, 10, 10,  0,  5,  0,  0}, // RNG
		{10,  5,  0,  0,  0, 10,  5}, // SHD
		{ 0, 10,  0,  0, 10,  0,  0}, // DRU
		{ 5,  5, 10, 10,  0,  0,  0}, // MNK
		{ 5,  0,  0, 10,  0,  0, 10}, // BRD
		{ 0,  0, 10, 10,  0,  0,  0}, // ROG
		{ 0,  5,  0,  0, 10,  0,  5}, // SHM
		{ 0,  0,  0, 10,  0, 10,  0}, // NEC
		{ 0, 10,  0,  0,  0, 10,  0}, // WIZ
		{ 0, 10,  0,  0,  0, 10,  0}, // MAG
		{ 0,  0,  0,  0,  0, 10, 10}, // ENC
		{ 0, 10,  5,  0, 10,  0,  5}, // BST
	};

	// EQ_Character_ClassMin: the race's starting stats (STR STA AGI DEX WIS INT CHA).
	const uint8_t RaceStats[CreationRules::RaceCount][CreationRules::StatCount] = {
		{ 75,  75,  75,  75,  75,  75,  75}, // Human
		{103,  95,  82,  70,  70,  60,  55}, // Barbarian
		{ 60,  70,  70,  70,  83, 107,  70}, // Erudite
		{ 65,  65,  95,  80,  80,  75,  75}, // Wood Elf
		{ 55,  65,  85,  70,  95,  92,  80}, // High Elf
		{ 60,  65,  90,  75,  83,  99,  60}, // Dark Elf
		{ 70,  70,  90,  85,  60,  75,  75}, // Half Elf
		{ 90,  90,  70,  90,  83,  60,  45}, // Dwarf
		{108, 109,  83,  75,  60,  52,  40}, // Troll
		{130, 122,  70,  70,  67,  60,  37}, // Ogre
		{ 70,  75,  95,  90,  80,  67,  50}, // Halfling
		{ 60,  70,  85,  85,  67,  98,  60}, // Gnome
		{ 70,  70,  90,  85,  80,  75,  55}, // Iksar
		{ 90,  75,  90,  70,  70,  65,  65}, // Vah Shir
	};

	// EQ_Character_CheckClass: the classes each race can be, one character per class:
	//   WAR CLR PAL RNG SHD DRU MNK BRD ROG SHM NEC WIZ MAG ENC BST
	const char *const RaceClasses[CreationRules::RaceCount] = {
		"111111111.1111.", // Human
		"1.......11....1", // Barbarian
		".11.1.....1111.", // Erudite
		"1..1.1.11......", // Wood Elf
		".11........111.", // High Elf
		"11..1...1.1111.", // Dark Elf
		"1.11.1.11......", // Half Elf
		"111.....1......", // Dwarf
		"1...1....1....1", // Troll
		"1...1....1....1", // Ogre
		"1111.1..1......", // Halfling
		"111.1...1.1111.", // Gnome
		"1...1.1..11...1", // Iksar
		"1......111....1", // Vah Shir
	};

	// EQ_Character_CityCanStart: the start cities each race can pick, by city button: Erudin,
	// Qeynos, Halas, Rivervale, Freeport, Neriak, Grobb, Oggok, Kaladim, Kelethin, Felwithe,
	// Ak'Anon (Human and Half Elf have more rules on top; Iksar and Vah Shir have one city each).
	const char *const RaceCities[12] = {
		".1..1.......", // Human
		"..1.........", // Barbarian
		"1...........", // Erudite
		".........1..", // Wood Elf
		"..........1.", // High Elf
		".....1......", // Dark Elf
		".1..1....11.", // Half Elf
		"........1...", // Dwarf
		"......1.....", // Troll
		".......1....", // Ogre
		"...1........", // Halfling
		"...........1", // Gnome
	};

	// The CanWorship functions' tables: the classes that can follow each deity button, one
	// character per class as in RaceClasses. Most races' tables are 14 classes wide, so the exe's
	// lookup for a beastlord reads the next deity's warrior; that's kept here.
	const char *const DeityClasses[CreationRules::RaceCount][CreationRules::DeityCount] = {
		{ // Human
			"1.....111..1111", // Agnostic
			"11..1...1.1111.", // Bertoxxulous
			".......11......", // Brell Serilis
			"..............1", // Cazic Thule
			"11..1...1.11111", // Erollisi Marr
			"1111.1.11..1111", // Bristlebane
			"111....11..1111", // Innoruuk
			"111....1...1111", // Karana
			"111....11..111.", // Mithaniel Marr
			".......1.......", // Prexus
			"......11.......", // Quellious
			".......1...1...", // Rallos Zek
			".......1.......", // Rodcet Nife
			".......1.......", // Solusek Ro
			"...1.1.1.......", // The Tribunal
			".......1......1", // Tunare
			"1......1.......", // Veeshan
		},
		{ // Barbarian
			"1.......1......", // Agnostic
			"...............", // Bertoxxulous
			"........1......", // Brell Serilis
			"...............", // Cazic Thule
			"...............", // Erollisi Marr
			"...............", // Bristlebane
			"...............", // Innoruuk
			"..............1", // Karana
			"...............", // Mithaniel Marr
			"...............", // Prexus
			"...............", // Quellious
			"...............", // Rallos Zek
			"...............", // Rodcet Nife
			"1.......11....1", // Solusek Ro
			"...............", // The Tribunal
			"...............", // Tunare
			"1..............", // Veeshan
		},
		{ // Erudite
			"...........111.", // Agnostic
			"...............", // Bertoxxulous
			"...............", // Brell Serilis
			".1..1.....1....", // Cazic Thule
			"...............", // Erollisi Marr
			"...............", // Bristlebane
			"...............", // Innoruuk
			"...............", // Karana
			"...............", // Mithaniel Marr
			".11........111.", // Prexus
			".11........111.", // Quellious
			"...........1...", // Rallos Zek
			"...............", // Rodcet Nife
			"...............", // Solusek Ro
			"...............", // The Tribunal
			"...............", // Tunare
			"...............", // Veeshan
		},
		{ // Wood Elf
			"1......11......", // Agnostic
			"...............", // Bertoxxulous
			".......11......", // Brell Serilis
			"...............", // Cazic Thule
			"..............1", // Erollisi Marr
			"1......11......", // Bristlebane
			".......1.......", // Innoruuk
			".......1.......", // Karana
			".......1.......", // Mithaniel Marr
			".......1.......", // Prexus
			".......1.......", // Quellious
			".......1.......", // Rallos Zek
			".......1.......", // Rodcet Nife
			".......1......1", // Solusek Ro
			"1..1.1.11......", // The Tribunal
			".......1......1", // Tunare
			"1......1.......", // Veeshan
		},
		{ // High Elf
			"...........111.", // Agnostic
			"...............", // Bertoxxulous
			"...............", // Brell Serilis
			"...............", // Cazic Thule
			"...............", // Erollisi Marr
			"...........111.", // Bristlebane
			"...........111.", // Innoruuk
			"...........111.", // Karana
			"...............", // Mithaniel Marr
			"...............", // Prexus
			"...............", // Quellious
			"...........1...", // Rallos Zek
			"...............", // Rodcet Nife
			"...............", // Solusek Ro
			".11........111.", // The Tribunal
			"...............", // Tunare
			"...............", // Veeshan
		},
		{ // Dark Elf
			"1.......1..111.", // Agnostic
			"...............", // Bertoxxulous
			"........1......", // Brell Serilis
			"..............1", // Cazic Thule
			"11..1...1.1111.", // Erollisi Marr
			"...............", // Bristlebane
			"...............", // Innoruuk
			"...............", // Karana
			"...............", // Mithaniel Marr
			"...............", // Prexus
			"...............", // Quellious
			"...........1...", // Rallos Zek
			"...............", // Rodcet Nife
			"...............", // Solusek Ro
			"...............", // The Tribunal
			"..............1", // Tunare
			"1..............", // Veeshan
		},
		{ // Half Elf
			"1......11.....1", // Agnostic
			"1.......1......", // Bertoxxulous
			".......11......", // Brell Serilis
			"..............1", // Cazic Thule
			"1.............1", // Erollisi Marr
			"1.11.1.11.....1", // Bristlebane
			"1.1....11.....1", // Innoruuk
			"1.1....1......1", // Karana
			"1.1....11.....1", // Mithaniel Marr
			"1......1.......", // Prexus
			".......1.......", // Quellious
			".......1.......", // Rallos Zek
			".......1......1", // Rodcet Nife
			"1......1......1", // Solusek Ro
			"1.11.1.11......", // The Tribunal
			".......1......1", // Tunare
			"1......1.......", // Veeshan
		},
		{ // Dwarf
			"1.......1......", // Agnostic
			"...............", // Bertoxxulous
			"........1......", // Brell Serilis
			"...............", // Cazic Thule
			"...............", // Erollisi Marr
			"...............", // Bristlebane
			"...............", // Innoruuk
			"...............", // Karana
			"...............", // Mithaniel Marr
			"...............", // Prexus
			"...............", // Quellious
			"..............1", // Rallos Zek
			"111.....1......", // Rodcet Nife
			"...............", // Solusek Ro
			"...............", // The Tribunal
			"...............", // Tunare
			"...............", // Veeshan
		},
		{ // Troll
			"1..............", // Agnostic
			"...............", // Bertoxxulous
			"...............", // Brell Serilis
			"1...1....1....1", // Cazic Thule
			"1...1....1....1", // Erollisi Marr
			"...............", // Bristlebane
			"...............", // Innoruuk
			"...............", // Karana
			"...............", // Mithaniel Marr
			"...............", // Prexus
			"...............", // Quellious
			"...............", // Rallos Zek
			"...............", // Rodcet Nife
			"...............", // Solusek Ro
			"...............", // The Tribunal
			"...............", // Tunare
			"1..............", // Veeshan
		},
		{ // Ogre
			"1..............", // Agnostic
			"...............", // Bertoxxulous
			"...............", // Brell Serilis
			"1...1..........", // Cazic Thule
			"...............", // Erollisi Marr
			"...............", // Bristlebane
			"...............", // Innoruuk
			"...............", // Karana
			"...............", // Mithaniel Marr
			"...............", // Prexus
			"...............", // Quellious
			"...............", // Rallos Zek
			"...............", // Rodcet Nife
			"...............", // Solusek Ro
			"...............", // The Tribunal
			"...............", // Tunare
			"1...1....1....1", // Veeshan
		},
		{ // Halfling
			"1.......1......", // Agnostic
			"...............", // Bertoxxulous
			".1......1......", // Brell Serilis
			"...............", // Cazic Thule
			"...............", // Erollisi Marr
			"..11.1.........", // Bristlebane
			"...............", // Innoruuk
			"...............", // Karana
			"...............", // Mithaniel Marr
			"...............", // Prexus
			"...............", // Quellious
			"..............1", // Rallos Zek
			"1.......1......", // Rodcet Nife
			"...............", // Solusek Ro
			"...............", // The Tribunal
			"..............1", // Tunare
			"1..............", // Veeshan
		},
		{ // Gnome
			"1.......1..1111", // Agnostic
			"11..1...1.1111.", // Bertoxxulous
			".1......1......", // Brell Serilis
			"...............", // Cazic Thule
			"...............", // Erollisi Marr
			"...............", // Bristlebane
			"...............", // Innoruuk
			"...............", // Karana
			"...............", // Mithaniel Marr
			"...............", // Prexus
			"...............", // Quellious
			"...........1..1", // Rallos Zek
			"111.....1..111.", // Rodcet Nife
			"...............", // Solusek Ro
			"...............", // The Tribunal
			"..............1", // Tunare
			"1..............", // Veeshan
		},
		{ // Iksar
			"...............", // Agnostic
			"...............", // Bertoxxulous
			"...............", // Brell Serilis
			"111111111111111", // Cazic Thule
			"...............", // Erollisi Marr
			"...............", // Bristlebane
			"...............", // Innoruuk
			"...............", // Karana
			"...............", // Mithaniel Marr
			"...............", // Prexus
			"...............", // Quellious
			"...............", // Rallos Zek
			"...............", // Rodcet Nife
			"...............", // Solusek Ro
			"...............", // The Tribunal
			"...............", // Tunare
			"...............", // Veeshan
		},
		{ // Vah Shir
			"...............", // Agnostic
			"...............", // Bertoxxulous
			"...............", // Brell Serilis
			"...............", // Cazic Thule
			"...............", // Erollisi Marr
			"...............", // Bristlebane
			"...............", // Innoruuk
			"...............", // Karana
			"...............", // Mithaniel Marr
			"...............", // Prexus
			"...............", // Quellious
			"...............", // Rallos Zek
			"...............", // Rodcet Nife
			"...............", // Solusek Ro
			"111111111111111", // The Tribunal
			"...............", // Tunare
			"...............", // Veeshan
		},
	};

	// CreateName's syllables: by race (0-12 by race id, 13 Iksar, 14 Vah Shir), ten choices for
	// each of five parts (start, two middles, end, feminine ending).
	const char *const NameParts[15][10][5] = {
		{
			{"K", "ab", "ar", "tik", "a"},
			{"G", "on", "an", "er", "u"},
			{"J", "ib", "ek", "n", "y"},
			{"V", "ab", "ar", "tik", "a"},
			{"G", "on", "an", "er", "u"},
			{"X", "as", "ek", "n", "y"},
			{"K", "ar", "ar", "tik", "a"},
			{"Z", "ob", "an", "er", "u"},
			{"J", "eb", "ek", "n", "y"},
			{"L", "en", "ob", "ab", "i"},
		},
		{
			{"D", "en", "e", "lron", "a"},
			{"B", "al", "ad", "orn", "y"},
			{"L", "an", "a", "dor", "e"},
			{"G", "en", "el", "en", "i"},
			{"S", "ol", "an", "dil", "la"},
			{"F", "e", "o", "dar", "e"},
			{"R", "on", "ad", "an", "ie"},
			{"A", "ar", "th", "in", "y"},
			{"G", "am", "al", "iel", "ae"},
			{"C", "ae", "la", "eb", "i"},
		},
		{
			{"J", "ar", "ud", "an", "a"},
			{"H", "al", "ad", "in", "u"},
			{"L", "ud", "am", "il", "y"},
			{"B", "en", "gur", "en", "a"},
			{"S", "or", "da", "orf", "i"},
			{"F", "um", "am", "dar", "i"},
			{"T", "in", "da", "orm", "ie"},
			{"K", "il", "ed", "an", "ea"},
			{"G", "an", "el", "or", "a"},
			{"W", "al", "la", "ce", "a"},
		},
		{
			{"A", "b", "er", "ar", "a"},
			{"E", "rr", "ud", "in", "u"},
			{"U", "z", "u", "n", "e"},
			{"Yi", "t", "um", "no", "ya"},
			{"I", "zz", "nus", "ien", "la"},
			{"O", "um", "am", "ar", "ka"},
			{"Fa", "ni", "da", "an", "ki"},
			{"I", "li", "ad", "en", "eth"},
			{"Ni", "bi", "el", "yen", "a"},
			{"Aa", "mm", "la", "zen", "i"},
		},
		{
			{"M", "en", "er", "ian", "a"},
			{"W", "al", "ad", "in", "u"},
			{"L", "an", "we", "il", "e"},
			{"T", "eg", "di", "en", "ya"},
			{"U", "ul", "uv", "ien", "iel"},
			{"A", "lu", "am", "dar", "ye"},
			{"K", "in", "da", "an", "ie"},
			{"I", "il", "ol", "as", "ea"},
			{"N", "ea", "lu", "yen", "ae"},
			{"G", "ae", "la", "wen", "i"},
		},
		{
			{"Q", "ue", "ny", "ar", "a"},
			{"E", "al", "ad", "in", "u"},
			{"L", "an", "am", "il", "e"},
			{"Y", "en", "di", "en", "ya"},
			{"S", "al", "dy", "ien", "iel"},
			{"A", "um", "am", "dar", "ye"},
			{"F", "in", "da", "an", "ie"},
			{"I", "il", "ad", "in", "ea"},
			{"N", "an", "el", "yen", "ae"},
			{"C", "ae", "la", "wen", "i"},
		},
		{
			{"Ut", "eu", "nay", "ar", "aa"},
			{"V", "al", "ad", "ael", "u"},
			{"M", "an", "am", "il", "au"},
			{"U", "en", "di", "en", "ya"},
			{"S", "ael", "day", "r", "ie"},
			{"A", "um", "ael", "dar", "ye"},
			{"T", "in", "da", "an", "iu"},
			{"Br", "il", "uu", "in", "ea"},
			{"N", "an", "el", "yan", "ae"},
			{"K", "ae", "la", "ven", "ia"},
		},
		{
			{"P", "en", "er", "ian", "a"},
			{"W", "al", "ad", "in", "u"},
			{"L", "an", "da", "il", "e"},
			{"T", "em", "id", "en", "ya"},
			{"S", "ul", "uv", "ien", "i"},
			{"R", "u", "am", "dar", "aya"},
			{"K", "in", "b", "an", "y"},
			{"D", "in", "ol", "as", "a"},
			{"M", "e", "lu", "yen", "ai"},
			{"B", "a", "var", "wen", "i"},
		},
		{
			{"H", "in", "dol", "af", "a"},
			{"D", "al", "ad", "in", "u"},
			{"K", "an", "am", "ek", "y"},
			{"H", "in", "dol", "ur", "a"},
			{"D", "al", "ad", "ar", "u"},
			{"K", "ak", "an", "ek", "y"},
			{"A", "in", "dol", "af", "a"},
			{"D", "ar", "th", "in", "u"},
			{"K", "ik", "am", "ek", "y"},
			{"B", "or", "da", "mer", "i"},
		},
		{
			{"G", "u", "r", "uk", "a"},
			{"K", "i", "g", "al", "u"},
			{"B", "a", "k", "ak", "o"},
			{"R", "an", "z", "ek", "u"},
			{"N", "am", "m", "ab", "au"},
			{"G", "um", "l", "az", "a"},
			{"Z", "ak", "b", "an", "u"},
			{"G", "ag", "uk", "uk", "a"},
			{"B", "az", "bk", "ak", "u"},
			{"K", "u", "la", "um", "o"},
		},
		{
			{"O", "g", "ur", "ak", "u"},
			{"T", "ar", "m", "og", "a"},
			{"Z", "uk", "am", "ol", "u"},
			{"B", "ha", "ag", "ok", "a"},
			{"R", "hu", "ar", "gar", "u"},
			{"G", "um", "tu", "uk", "a"},
			{"O", "im", "gr", "an", "u"},
			{"K", "ug", "ob", "im", "o"},
			{"N", "ag", "gul", "buk", "a"},
			{"R", "og", "la", "bok", "i"},
		},
		{
			{"M", "a", "rr", "in", "a"},
			{"B", "e", "tt", "en", "u"},
			{"D", "i", "nn", "il", "e"},
			{"F", "o", "bb", "en", "eth"},
			{"S", "u", "ff", "on", "ry"},
			{"H", "ae", "kk", "as", "dy"},
			{"G", "ai", "pp", "o", "ie"},
			{"T", "ei", "ll", "win", "ea"},
			{"N", "ea", "gg", "an", "y"},
			{"C", "ee", "dd", "un", "i"},
		},
		{
			{"M", "is", "tal", "bik", "a"},
			{"Z", "al", "as", "tin", "u"},
			{"W", "an", "am", "til", "e"},
			{"Y", "en", "di", "ten", "ya"},
			{"S", "al", "ik", "ben", "iel"},
			{"B", "at", "ma", "tik", "ye"},
			{"F", "in", "da", "bus", "ie"},
			{"X", "il", "um", "pus", "ea"},
			{"N", "ab", "li", "ren", "y"},
			{"C", "ur", "la", "kin", "i"},
		},
		{
			{"K", "ab", "ar", "tiz", "a"},
			{"X", "eb", "an", "il", "u"},
			{"J", "ik", "sha", "is", "i"},
			{"C", "atr", "kor", "iz", "a"},
			{"S", "il", "an", "sis", "u"},
			{"R", "as", "th", "le", "y"},
			{"Ch", "ar", "as", "rn", "a"},
			{"Z", "uk", "ra", "ik", "u"},
			{"S", "or", "ek", "ak", "y"},
			{"M", "ak", "ul", "az", "i"},
		},
		{
			{"K", "a", "ra", "sh", "a"},
			{"G", "o", "na", "r", "u"},
			{"J", "i", "ta", "n", "i"},
			{"H", "e", "ri", "yn", "a"},
			{"S", "u", "n", "is", "u"},
			{"R", "a", "k", "n", "ya"},
			{"K", "a", "ra", "th", "a"},
			{"Z", "u", "na", "l", "u"},
			{"T", "e", "ka", "n", "ia"},
			{"L", "a", "ba", "ab", "i"},
		},
	};

	// Race id -> table index (EQ_Character_ClassMin: Iksar and Vah Shir follow the 12 originals).
	int RaceIndex(uint16_t race)
	{
		if (race >= 1 && race <= 12)
			return race - 1;
		if (race == 128)
			return 12;
		if (race == 130)
			return 13;
		return -1;
	}

	bool IsVowel(char c)
	{
		return strchr("aeiouyAEIOUY", c) != nullptr && c != 0;
	}

	// NameFailsOnBasicChecks @00505aa6, without the bad word list.
	bool FailsBasicChecks(const std::string &name, bool strict)
	{
		for (size_t i = 0; i < name.size(); ++i) {
			if (i + 2 < name.size() && name[i] == name[i + 1] && name[i] == name[i + 2])
				return true;
			if (strict && i + 3 < name.size() && !IsVowel(name[i]) && !IsVowel(name[i + 1]) && !IsVowel(name[i + 2]) &&
				!IsVowel(name[i + 3]))
				return true;
		}
		return false;
	}
}

void CreationRules::CheckInstall(const std::string &eq_dir)
{
	std::error_code ec;
	m_paineel = std::filesystem::exists(eq_dir + "/paineel.s3d", ec);
}

bool CreationRules::CanBeClass(uint16_t race, uint8_t class_id) const
{
	int r = RaceIndex(race), c = class_id - 1;
	if (r < 0 || c < 0 || c >= 15)
		return false;
	if (race == 3 && (c == 4 || c == 10) && !m_paineel) // Erudite shadow knights and necromancers start in Paineel
		return false;
	return RaceClasses[r][c] == '1';
}

int CreationRules::RaceBase(uint16_t race, Stat stat) const
{
	int r = RaceIndex(race);
	return r >= 0 ? RaceStats[r][stat] : 75;
}

int CreationRules::ClassBonus(uint8_t class_id, Stat stat) const
{
	int c = class_id - 1;
	return c >= 0 && c < 15 ? ClassPoints[c][stat] : 0;
}

bool CreationRules::CanWorship(uint16_t race, uint8_t class_id, int deity) const
{
	int r = RaceIndex(race), c = class_id - 1;
	if (r < 0 || c < 0 || c >= 15 || deity < 0 || deity >= DeityCount)
		return false;
	const uint32_t type = m_server_type;
	// The per-race exceptions in front of each table lookup.
	switch (race) {
	case 1: // Human
		if (type == 4 && deity == 15 && c == 6)
			return true;
		if (type == 4 && deity == 0)
			return false;
		break;
	case 2: // Barbarian
		if (type == 4 && deity == 7 && (c == 0 || c == 9))
			return true;
		if (type == 4 && deity == 0)
			return false;
		break;
	case 3: // Erudite
		if ((deity == 3 && !m_paineel) || (type == 4 && deity == 0))
			return false;
		break;
	case 4:  // Wood Elf
	case 11: // Halfling
		if (type == 4 && (deity == 0 || deity == 16))
			return false;
		break;
	case 7: // Half Elf
		if (type == 2)
			return deity == 14;
		if (type == 4 && deity == 0)
			return false;
		break;
	case 130: // Vah Shir
		if (type != 4)
			return deity == 0;
		if (deity == 0)
			return false;
		break;
	default:
		if (type == 4 && deity == 0)
			return false;
		break;
	}
	return DeityClasses[r][deity][c] == '1';
}

bool CreationRules::CityCanStart(int city, uint16_t race, int deity, uint8_t class_id) const
{
	int c = class_id - 1;
	const uint32_t type = m_server_type;
	if (race == 128)
		return city == 12;
	if (race == 130)
		return city == 13;
	int r = RaceIndex(race);
	if (r < 0 || city < 0 || city >= 12)
		return false;

	if (race == 1) {
		// Humans: Freeport for the evil and neutral gods' followers, Qeynos for the good ones;
		// druids and rangers only in Qeynos.
		bool freeport = city == 4 || (deity != 6 && deity != 7 && deity != 4 && (c != 6 || deity != 10));
		bool qeynos = city == 1 ||
			(deity != 1 && deity != 8 && deity != 5 && (c != 6 || ((type != 4 || deity != 15) && deity != 0)));
		if (!freeport || !qeynos || (c == 5 && city != 1) || (c == 3 && city != 1))
			return false;
	}
	if (race == 7) {
		// Half Elves.
		bool ok = (type != 2 || city == 9 || city == 10) && (city == 4 || (deity != 4 && deity != 6 && deity != 7)) &&
			(city == 1 || (deity != 1 && deity != 5 && deity != 8)) &&
			(c != 3 || ((city == 1 || city == 9) && (deity == 14 || city != 9))) &&
			(c != 0 || deity != 14 || city == 9) && (c != 8 || deity != 14 || city == 9) &&
			(c != 5 || deity != 14 || city == 9 || city == 1);
		if (!ok)
			return false;
		// Felwithe only for paladins of Tunare, who can only start there.
		bool tunare_paladin = c == 2 && deity == 14;
		if (tunare_paladin != (city == 10))
			return false;
	}
	return RaceCities[r][city] == '1';
}

StartPoint CreationRules::Start(uint16_t race, uint8_t c, int city, uint16_t deity)
{
	// EQ_PC::InitPlayerStart: the spot by race and class (a few by deity or city), then the bind
	// by start zone. The server takes both from OP_CharacterCreate if the zones are on its lists.
	StartPoint s;
	s.zone = 1;
	s.y = 0, s.x = 0, s.z = 9;
	auto at = [&s](uint32_t zone, float y, float x, float z, float heading = 0) {
		s.zone = zone;
		s.y = y, s.x = x, s.z = z, s.heading = heading;
	};
	auto spot = [&s](float y, float x, float z, float heading = 0) { s.y = y, s.x = x, s.z = z, s.heading = heading; };

	switch (race) {
	case 330: // Froglok
		s.zone = 2;
		break;
	case 130: // Vah Shir: sharvahl
		s.zone = 155;
		if (c == 1)
			spot(-143, -535, -260, 128);
		else if (c == 8)
			spot(0, 550, -104, 385);
		else if (c == 9)
			spot(190, -300, -248, 128);
		else if (c == 10)
			spot(55, 100, -260, 256);
		else if (c == 15)
			spot(275, 90, -260);
		break;
	case 11: // Halfling: rivervale
		s.zone = 19;
		if (c == 1)
			spot(125, -157, 28);
		else if (c == 2)
			spot(-227, -381, -18);
		else if (c == 3)
			spot(-228, -309, -9, 384);
		else if (c == 4)
			spot(-125, -320, -5, 430);
		else if (c == 6)
			spot(-104, -347, -10);
		else if (c == 9)
			spot(-34, -192, 2);
		break;
	case 12: // Gnome: akanon
		s.zone = 55;
		if (c == 1)
			spot(900, -461, -30);
		else if (c == 2)
			spot(1190, -550, -39);
		else if (c == 3)
			spot(1281, -559, -40, 292);
		else if (c == 9)
			spot(1203, -599, -53);
		else if (c >= 12 && c <= 14)
			spot(1124, -1008, 32);
		if (deity == 201) // Bertoxxulous
			spot(2060, -588, -181, s.heading);
		break;
	case 4: // Wood Elf: gfaydark
		s.zone = 54;
		if (c == 1)
			spot(370, 304, 76);
		else if (c == 4)
			spot(-437, 501, 117);
		else if (c == 6)
			spot(-698, 224, 76);
		else if (c == 8)
			spot(-231, 244, 76);
		else if (c == 9)
			spot(-501, -210, 160);
		break;
	case 8: // Dwarf: kaladima, warriors in kaladimb
		s.zone = 67;
		if (c == 1)
			at(60, 41, 304, 14);
		else if (c == 2)
			spot(771, 132, 2);
		else if (c == 3)
			spot(1338, 113, 48);
		else if (c == 9)
			spot(546, 218, -33);
		break;
	case 10: // Ogre: oggok
		s.zone = 49;
		if (c == 1)
			spot(-2, 71, -27);
		else if (c == 5)
			spot(333, 9, 5);
		else if (c == 10)
			spot(656, 1006, 80);
		else if (c == 15)
			spot(288, 1074, 80);
		break;
	case 9: // Troll: grobb
		s.zone = 52;
		if (c == 1)
			spot(299, -94, 4);
		else if (c == 5)
			spot(674, -436, -6);
		else if (c == 10)
			spot(57, -459, 54);
		else if (c == 15)
			spot(-49, -454, 74);
		break;
	case 6: // Dark Elf: neriaka, neriakb
		s.zone = 41;
		if (c == 1)
			spot(-28, -1124, -52);
		else if (c == 2)
			at(42, 539, -811, -49);
		else if (c == 5 || c == 11)
			at(42, 1255, -1253, -80);
		else if (c == 9)
			at(42, 667, -1358, -108);
		else if (c == 12)
			spot(165, -885, -38);
		else if (c == 13)
			spot(131, -915, -38);
		else if (c == 14)
			spot(148, -951, -38);
		break;
	case 2: // Barbarian: halas
		s.zone = 29;
		if (c == 1)
			spot(487, -448, 4);
		else if (c == 9)
			spot(186, 171, 29);
		else if (c == 10)
			spot(334, 448, -20);
		else if (c == 15)
			spot(585, 208, 4);
		break;
	case 5: // High Elf: felwithea, felwitheb
		if (c == 2)
			at(61, 14, -459, -1);
		else if (c == 3)
			at(61, 0, -284, 3);
		else if (c >= 12 && c <= 14)
			at(62, 425, -601, 29);
		break;
	case 3: // Erudite: erudnext, erudnint, paineel
		if (c == 2) {
			if (deity == 203) // Cazic Thule
				at(75, 1147, 504, -39);
			else if (deity == 209) // Prexus
				at(24, -1099, -433, 66, 128);
			else if (deity == 210) // Quellious
				at(24, -712, -68, 66);
		} else if (c == 3)
			at(24, -654, -69, 66);
		else if (c == 5)
			at(75, 880, 456, -121);
		else if (c == 11)
			at(75, 739, 840, -25);
		else if (c == 12)
			at(23, 868, 744, 51);
		else if (c == 13)
			at(23, 716, 920, 82);
		else if (c == 14)
			at(23, 552, 745, 51);
		break;
	case 1:   // Human
	case 7: { // Half Elf: Qeynos or Freeport (city 4), Half Elves also Kelethin (9) or Felwithe (10)
		bool kelethin = city == 9 && race == 7;
		switch (c) {
		case 1:
			at(1, 58, -544, 32);
			if (city == 4)
				at(9, -279, -42, -10);
			if (kelethin)
				at(54, 370, 304, 76);
			break;
		case 2:
			if (deity == 212) // Rodcet Nife
				at(2, -162, -259, 4);
			else
				at(1, -186, -441, 4);
			if (city == 4)
				at(9, -1, 349, -9);
			break;
		case 3:
			if (deity == 212)
				at(2, -162, -259, 4);
			else if (deity == 207) // Karana
				at(1, -186, -441, 4);
			if (city == 4)
				at(8, -1, 349, -9);
			if (city == 10)
				at(61, 0, -248, 3);
			break;
		case 4:
			at(3, 146, -36, 4);
			if (kelethin)
				at(54, -437, 501, 117);
			break;
		case 5:
		case 11:
			at(1, -22, -433, -24);
			if (city == 4)
				at(10, -141, -290, -80);
			break;
		case 6:
			at(3, -207, -301, 4);
			if (kelethin)
				at(54, -698, 224, 76);
			break;
		case 7:
			at(2, 342, 384, 4);
			if (city == 4)
				at(9, -196, -790, -24);
			break;
		case 8:
			at(1, 537, -110, 4);
			if (city == 4)
				at(8, -92, 507, 4);
			if (kelethin)
				at(54, -231, 244, 76);
			break;
		case 9:
			at(2, 54, 358, 4);
			if (city == 4)
				at(10, -318, -789, -109);
			if (kelethin)
				at(54, -501, -210, 160);
			break;
		case 12:
		case 13:
		case 14:
			if (deity == 201)
				at(1, -22, -433, -24);
			else
				at(1, 351, -587, 4);
			if (city == 4)
				at(9, 97, -631, -38);
			break;
		default:
			break;
		}
		if (deity == 206) // followers of Innoruuk start in North Freeport
			at(10, -141, -290, -80);
		break;
	}
	case 128: // Iksar: cabeast, wizards in cabwest, the rest fieldofbone
		if (c == 1)
			at(106, -531, -125, 3);
		else if (c == 5)
			at(106, 1029, -188, 72);
		else if (c == 7)
			at(106, 426, -96, 5);
		else if (c == 10)
			at(106, 1017, -189, 17);
		else if (c == 11)
			at(82, 311, 767, 46);
		else if (c == 15)
			at(106, 1032, -189, 44);
		else
			at(78, 0, 0, -7);
		break;
	default:
		break;
	}

	auto bind = [&s](uint32_t zone, float y, float x, float z) {
		s.bind_zone = zone;
		s.bind_y = y, s.bind_x = x, s.bind_z = z;
	};
	switch (s.zone) {
	case 1:
	case 2: bind(2, 646, -131, 5); break;           // qeynos2
	case 8:
	case 9:
	case 10: bind(9, 247, 359, -22); break;         // freportw
	case 19: bind(33, 271, -2126, -3); break;       // misty
	case 23:
	case 24: bind(38, 2230, 67, -44); break;        // tox
	case 29: bind(30, 3139, 629, -57); break;       // everfrost
	case 40:
	case 41:
	case 42: bind(25, 2048, -775, 44); break;       // nektulos
	case 49: bind(47, 1103, 948, 31); break;        // feerrott
	case 52: bind(46, -2408, -339, -14); break;     // innothule
	case 54: bind(54, 192, 64, 35); break;          // gfaydark
	case 55: bind(56, -1668, 636, -106); break;     // steamfont
	case 60:
	case 67: bind(68, 2610, -251, 84); break;       // butcher
	case 61:
	case 62: bind(54, -2040, -2194, 55); break;     // gfaydark
	case 75: bind(75, 800, 200, 0); break;          // paineel
	default: bind(4, 4850, -46, 4); break;          // qeytoqrg
	}
	if (race == 128)
		bind(78, -2394, 3244, 12); // fieldofbone
	else if (race == 130)
		bind(155, -1132, 86, -187); // sharvahl
	return s;
}

std::string CreationRules::RandomName(uint16_t race, uint8_t gender, std::mt19937 &random) const
{
	int index = race == 128 ? 13 : race == 130 ? 14 : race;
	if (index < 0 || index >= 15)
		return "YourNameHere";
	auto roll = [&random](int n) { return static_cast<int>(random() % static_cast<uint32_t>(n)); };

	std::string name;
	for (int attempt = 0; attempt < 50; ++attempt) {
		// Five syllables: always a first and a fourth, the second and third 80% of the time,
		// and the fifth (a feminine ending) only for women.
		name.clear();
		for (int part = 0; part < 5; ++part) {
			int chance = part == 0 || part == 3 ? 100 : part == 4 ? (gender != 0 ? 100 : 0) : 80;
			if (chance == 100 || roll(101) < chance)
				name += NameParts[index][roll(10)][part];
		}
		if (name.size() <= 3 || name.size() >= 16)
			continue;
		// Two consonants with the second doubled ("Kll...") get a vowel between them.
		if (name.size() > 2 && !IsVowel(name[0]) && !IsVowel(name[1]) && name[1] == name[2])
			name.insert(name.begin() + 1, "aeiouy"[roll(6)]);
		if (!FailsBasicChecks(name, m_strict_names))
			break;
	}
	return name.substr(0, 15);
}

bool CreationRules::IsValidName(const std::string &name)
{
	return name.size() > 3 && name.size() < 16 && name.find(' ') == std::string::npos;
}

std::vector<uint32_t> CreationRules::WelcomeText(uint16_t race, uint8_t class_id, int deity, int city)
{
	// GetCharCreateText picks strings by race, class, deity and city through about a thousand
	// lines of branches. This is the same mapping for every combination the rules allow, as
	// race/class defaults with deity and city exceptions (-1 = any); later rows win.
	struct Entry
	{
		uint16_t race;
		uint8_t class_id;
		int8_t deity, city;
		uint16_t ids[5];
	};
	static const Entry table[] = {
		{1, 1, -1, -1, {1955, 1957, 1958}},
		{1, 1, 1, -1, {1955, 1956}},
		{1, 1, 4, -1, {1959, 2046}},
		{1, 1, 6, -1, {1959, 1957, 1960}},
		{1, 1, 7, -1, {1959, 1957, 1960}},
		{1, 1, 0, 4, {1959, 1957, 1960}},
		{1, 1, 16, 4, {1959, 1957, 1960}},
		{1, 2, -1, -1, {1955, 1965}},
		{1, 2, 4, -1, {1959, 1966}},
		{1, 2, 5, -1, {1955, 1963}},
		{1, 2, 6, -1, {1959, 1962}},
		{1, 2, 7, -1, {1959, 1961}},
		{1, 2, 8, -1, {1955, 1964}},
		{1, 3, -1, -1, {1955, 1969}},
		{1, 3, 6, -1, {1959, 1968}},
		{1, 3, 7, -1, {1959, 1967}},
		{1, 3, 8, -1, {1955, 1970}},
		{1, 4, -1, -1, {1971, 1972}},
		{1, 5, -1, -1, {1955, 1974}},
		{1, 5, -1, 4, {1959, 1973}},
		{1, 6, -1, -1, {1971, 1976, 1975, 1978}},
		{1, 6, 14, -1, {1971, 1976, 1977, 1978}},
		{1, 7, -1, -1, {1955, 1979}},
		{1, 7, -1, 4, {1959, 1980}},
		{1, 8, -1, -1, {1955, 1982, 1981}},
		{1, 8, -1, 4, {1959, 1982, 1983}},
		{1, 9, -1, -1, {1955, 1984, 1986}},
		{1, 9, 1, -1, {1955, 1984, 1985}},
		{1, 9, 4, -1, {1959, 1987, 1988}},
		{1, 9, 6, -1, {1959, 1987, 1989}},
		{1, 9, 0, 4, {1959, 1987, 1989}},
		{1, 9, 2, 4, {1959, 1987, 1989}},
		{1, 11, -1, -1, {1955, 2102, 2033, 2104}},
		{1, 11, -1, 4, {1959, 2105, 2104}},
		{1, 12, -1, -1, {1955, 2110, 2113, 2111}},
		{1, 12, 1, -1, {1955, 2106, 2033, 2113, 2109}},
		{1, 12, 4, -1, {1959, 2112, 2113, 2114}},
		{1, 12, 6, -1, {1959, 2115, 2113, 2111}},
		{1, 12, 7, -1, {1959, 2115, 2113, 2111}},
		{1, 12, 0, 4, {1959, 2115, 2113, 2111}},
		{1, 12, 11, 4, {1959, 2115, 2113, 2111}},
		{1, 13, -1, -1, {1955, 2119, 2118, 2111}},
		{1, 13, 1, -1, {1955, 2116, 2033, 2118, 2109}},
		{1, 13, 4, -1, {1959, 2120, 2118, 2114}},
		{1, 13, 6, -1, {1959, 2121, 2118, 2111}},
		{1, 13, 7, -1, {1959, 2121, 2118, 2111}},
		{1, 13, 0, 4, {1959, 2121, 2118, 2111}},
		{1, 14, -1, -1, {1955, 2124, 2123, 2127}},
		{1, 14, 1, -1, {1955, 2122, 2033, 2123, 2109}},
		{1, 14, 4, -1, {1959, 2125, 2123, 2114}},
		{1, 14, 6, -1, {1959, 2126, 2123, 2127}},
		{1, 14, 7, -1, {1959, 2126, 2123, 2127}},
		{1, 14, 0, 4, {1959, 2126, 2123, 2127}},
		{2, 1, -1, -1, {2028, 2027}},
		{2, 9, -1, -1, {2028, 2030}},
		{2, 10, -1, -1, {2028, 2029}},
		{2, 15, -1, -1, {2031}},
		{3, 2, -1, -1, {2075}},
		{3, 2, 9, -1, {2070, 2071, 2078, 2074}},
		{3, 2, 10, -1, {2070, 2076, 2073}},
		{3, 3, -1, -1, {2070, 2077, 2078, 2074}},
		{3, 3, 10, -1, {2070, 2079, 2407}},
		{3, 5, -1, -1, {2068, 2069}},
		{3, 11, -1, -1, {2081, 2069}},
		{3, 12, -1, -1, {2070, 2082, 2083, 2084, 2407}},
		{3, 13, -1, -1, {2070, 2085, 2083, 2086, 2407}},
		{3, 14, -1, -1, {2070, 2087, 2083, 2088, 2407}},
		{4, 1, -1, -1, {2014, 2013}},
		{4, 4, -1, -1, {2014, 2016}},
		{4, 6, -1, -1, {2014, 2017}},
		{4, 8, -1, -1, {2014, 2015}},
		{4, 9, -1, -1, {2014, 2018}},
		{5, 2, -1, -1, {2035, 2042, 2045}},
		{5, 3, -1, -1, {2035, 2044, 2045}},
		{5, 12, -1, -1, {2035, 2039, 2040, 2041}},
		{5, 13, -1, -1, {2035, 2032, 2040, 2034}},
		{5, 14, -1, -1, {2035, 2036, 2040, 2038}},
		{6, 1, -1, -1, {2094, 2095}},
		{6, 2, -1, -1, {2094, 2099}},
		{6, 5, -1, -1, {2094, 2096, 2097}},
		{6, 9, -1, -1, {2094, 2098}},
		{6, 11, -1, -1, {2094, 2100, 2097}},
		{6, 12, -1, -1, {2094, 2101}},
		{6, 13, -1, -1, {2094, 2101}},
		{6, 14, -1, -1, {2094, 2101}},
		{7, 1, -1, -1, {2014, 2013}},
		{7, 3, -1, -1, {1955, 1969}},
		{7, 3, -1, 10, {2035, 2044, 2045}},
		{7, 3, 6, -1, {1959, 1968}},
		{7, 3, 7, -1, {1959, 1967}},
		{7, 3, 8, -1, {1955, 1970}},
		{7, 4, -1, -1, {1971, 1972}},
		{7, 4, -1, 9, {2014, 2016}},
		{7, 6, -1, -1, {1971, 1976, 1975, 1978}},
		{7, 6, -1, 9, {2014, 2017}},
		{7, 6, 14, 1, {1971, 1976, 1977, 1978}},
		{7, 8, -1, -1, {1955, 1982, 1983}},
		{7, 8, -1, 4, {1959, 1982, 1983}},
		{7, 8, -1, 9, {2014, 2015}},
		{7, 9, -1, -1, {1955, 1984, 1986}},
		{7, 9, -1, 4, {1959, 1987, 1989}},
		{7, 9, -1, 9, {2014, 2018}},
		{7, 9, 1, -1, {1955, 1984, 1985}},
		{8, 1, -1, -1, {2089, 2093}},
		{8, 2, -1, -1, {2089, 2090}},
		{8, 3, -1, -1, {2089, 2091}},
		{8, 9, -1, -1, {2089, 2092}},
		{9, 1, -1, -1, {2004, 2003}},
		{9, 5, -1, -1, {2004, 2006}},
		{9, 10, -1, -1, {2004, 2005}},
		{9, 15, -1, -1, {2004, 2007}},
		{10, 1, -1, -1, {2009, 2008}},
		{10, 5, -1, -1, {2009, 2011}},
		{10, 10, -1, -1, {2009, 2010}},
		{10, 15, -1, -1, {2009, 2012}},
		{11, 1, -1, -1, {2020, 2019}},
		{11, 2, -1, -1, {2020, 2023}},
		{11, 3, -1, -1, {2020, 2025, 2026}},
		{11, 4, -1, -1, {2020, 2025, 2024}},
		{11, 6, -1, -1, {2020, 2022}},
		{11, 9, -1, -1, {2020, 2021}},
		{12, 1, -1, -1, {2047, 2056, 2049}},
		{12, 1, 1, -1, {2047, 2054, 2051, 2055, 2053}},
		{12, 2, -1, -1, {2047, 2058, 2049}},
		{12, 2, 1, -1, {2047, 2054, 2051, 2057, 2053}},
		{12, 3, -1, -1, {2047, 2048, 2049}},
		{12, 5, -1, -1, {2047, 2050, 2051, 2052, 2053}},
		{12, 9, -1, -1, {2047, 2060}},
		{12, 9, 1, -1, {2047, 2054, 2051, 2059, 2053}},
		{12, 11, -1, -1, {2047, 2050, 2051, 2065, 2053}},
		{12, 12, -1, -1, {2047, 2062, 2066, 2064}},
		{12, 12, 1, -1, {2047, 2050, 2051, 2066, 2053}},
		{12, 13, -1, -1, {2047, 2062, 2067, 2064}},
		{12, 13, 1, -1, {2047, 2050, 2051, 2067, 2037}},
		{12, 14, -1, -1, {2047, 2062, 2063, 2064}},
		{12, 14, 1, -1, {2047, 2050, 2051, 2063, 2053}},
		{128, 1, -1, -1, {1996, 1995}},
		{128, 5, -1, -1, {1996, 1997}},
		{128, 7, -1, -1, {1998, 2001}},
		{128, 10, -1, -1, {1998, 1999}},
		{128, 11, -1, -1, {1998, 2000}},
		{128, 15, -1, -1, {1998, 2002}},
		{130, 1, -1, -1, {1990}},
		{130, 8, -1, -1, {1993}},
		{130, 9, -1, -1, {1992}},
		{130, 10, -1, -1, {1991}},
		{130, 15, -1, -1, {1994}},
	};
	const Entry *match = nullptr;
	for (const auto &e : table)
		if (e.race == race && e.class_id == class_id && (e.deity < 0 || e.deity == deity) && (e.city < 0 || e.city == city))
			match = &e;
	std::vector<uint32_t> ids;
	if (match)
		for (uint16_t id : match->ids)
			if (id)
				ids.push_back(id);
	return ids;
}
