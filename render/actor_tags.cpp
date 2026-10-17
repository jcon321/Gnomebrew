#include "actor_tags.h"

#include <map>

namespace render
{
	namespace
	{
		// EQPlayer::GetActorTag's race switch (races 13 to 330). Races it doesn't list get
		// EQPlayer_GetPCActorTag's player race codes. Taken once from TAKP's eqgame.exe.
		const std::map<uint16_t, const char *> &RaceTags()
		{
			static const std::map<uint16_t, const char *> tags = {
				{13, "AV"}, {14, "WER"}, {15, "BR"}, {16, "CE"}, {17, "GO"}, {18, "GIA"}, {19, "TRK"}, {20, "VST"},
				{21, "BEH"}, {22, "BET"}, {23, "CP"}, {24, "FIS"}, {25, "FA"}, {26, "FRO"}, {27, "FRG"}, {28, "FUN"},
				{29, "GA"}, {30, "BEH"}, {31, "CUB"}, {32, "GH"}, {33, "GHU"}, {34, "BAT"}, {36, "RAT"}, {37, "SNA"},
				{38, "SP"}, {39, "GNN"}, {40, "GOB"}, {41, "GOR"}, {42, "WO"}, {43, "BEA"}, {44, "FP"}, {45, "DML"},
				{46, "IMP"}, {47, "GRI"}, {48, "KOB"}, {49, "DRA"}, {50, "LI"}, {51, "LIZ"}, {52, "MI"}, {53, "MIN"},
				{54, "ORC"}, {55, "BG"}, {56, "PI"}, {57, "DR"}, {58, "SOL"}, {59, "BGG"}, {60, "SKE"}, {61, "SHA"},
				{62, "TUN"}, {63, "TIG"}, {64, "TRE"}, {65, "DV"}, {66, "RAL"}, {67, "HH"}, {68, "TE"}, {69, "WIL"},
				{70, "ZO"}, {71, "QC"}, {72, "SHIP"}, {73, "LAUNCH"}, {74, "PIR"}, {75, "ELE"}, {76, "PUM"}, {77, "NG"},
				{78, "EG"}, {79, "BIX"}, {80, "REA"}, {81, "RI"}, {82, "SCA"}, {83, "SKU"}, {84, "SNE"}, {85, "SPE"},
				{86, "SPH"}, {87, "ARM"}, {88, "CL"}, {89, "DRK"}, {90, "HL"}, {91, "ALL"}, {92, "GR"}, {93, "OK"}, {94, "KA"},
				{95, "CAZ"}, {96, "COC"}, {97, "DIA"}, {98, "VS"}, {99, "DEN"}, {100, "DER"}, {101, "EFR"}, {102, "FRT"},
				{103, "KED"}, {104, "LEE"}, {105, "SWO"}, {106, "FE"}, {107, "MAM"}, {108, "EYE"}, {109, "WAS"}, {110, "MER"},
				{111, "HAR"}, {112, "GF"}, {113, "DRI"}, {114, "GSP"}, {115, "CLA"}, {116, "SEA"}, {117, "GD"}, {118, "GE"},
				{119, "STC"}, {120, "WOE"}, {121, "GRG"}, {122, "DRU"}, {123, "INN"}, {124, "UNI"}, {125, "PEG"}, {126, "DJI"},
				{127, "IV"}, {129, "SCR"}, {131, "SRW"}, {132, "DLK"}, {133, "LYC"}, {134, "MOS"}, {135, "RHI"}, {136, "XAL"},
				{137, "KGO"}, {138, "YET"}, {139, "IC"}, {140, "FGI"}, {141, "BOAT"}, {144, "BRN"}, {145, "GOO"}, {146, "SSN"},
				{147, "SI"}, {148, "BAC"}, {149, "ISC"}, {150, "ERO"}, {151, "TRI"}, {152, "BER"}, {153, "BRI"}, {154, "FDR"},
				{155, "SSK"}, {156, "VR"}, {157, "WYV"}, {158, "WUR"}, {159, "DEV"}, {160, "IKG"}, {161, "IKS"}, {162, "MEP"},
				{163, "RAP"}, {164, "SGO"}, {165, "SED"}, {166, "IKH"}, {167, "SUC"}, {168, "FMO"}, {169, "BTM"}, {170, "SDE"},
				{171, "DIW"}, {172, "MTC"}, {173, "TOT"}, {174, "SPC"}, {175, "ENA"}, {176, "SBU"}, {177, "WA"}, {178, "RGM"},
				{181, "YAK"}, {182, "FAN"}, {183, "CO"}, {184, "DR2"}, {185, "HAG"}, {186, "HIP"}, {187, "SIR"}, {188, "FSG"},
				{189, "ST"}, {190, "OTM"}, {191, "WLM"}, {192, "CCD"}, {193, "ABH"}, {194, "STU"}, {195, "BWD"}, {196, "GDR"},
				{197, "RON"}, {198, "PRI"}, {199, "SKN"}, {200, "RHP"}, {201, "UNB"}, {202, "GM"}, {203, "VAC"}, {204, "ECS"},
				{205, "KHA"}, {206, "OWB"}, {207, "RNB"}, {208, "VP"}, {209, "EEL"}, {210, "AEL"}, {211, "WEL"}, {212, "FEL"},
				{213, "WET"}, {214, "THO"}, {215, "TEG"}, {216, "HS"}, {217, "SH"}, {218, "FUG"}, {219, "VOL"}, {220, "SGR"},
				{221, "SCH"}, {222, "ZEL"}, {223, "LCR"}, {224, "SD"}, {225, "SNN"}, {226, "SRV"}, {227, "SHR"}, {228, "GAL"},
				{229, "NET"}, {230, "AK"}, {231, "SPR"}, {232, "SOW"}, {233, "GS"}, {234, "KES"}, {235, "MUH"}, {236, "SER"},
				{237, "RE"}, {238, "VSK"}, {239, "VSG"}, {240, "TP"}, {241, "LUJ"}, {242, "NYD"}, {243, "NYM"}, {244, "TRN"},
				{245, "WRF"}, {246, "TMR"}, {247, "SRO"}, {248, "CLG"}, {249, "CLB"}, {250, "SKB"}, {251, "GOJ"}, {253, "DSB"},
				{254, "SRG"}, {255, "BTX"}, {256, "TBU"}, {257, "TRT"}, {258, "VEG"}, {259, "CRO"}, {260, "NBT"}, {261, "SLG"},
				{262, "TRQ"}, {263, "TIN"}, {264, "NMW"}, {265, "MAL"}, {266, "KOP"}, {267, "LEP"}, {268, "BUB"}, {269, "BUU"},
				{270, "PUS"}, {271, "WMP"}, {272, "STR"}, {273, "JUB"}, {274, "BRC"}, {275, "GLC"}, {276, "CWB"}, {277, "NMG"},
				{278, "KAR"}, {279, "BRV"}, {280, "GGL"}, {281, "MOI"}, {282, "HSS"}, {283, "SAR"}, {284, "FEN"}, {285, "TRW"},
				{286, "NPT"}, {287, "NMH"}, {288, "RAZ"}, {289, "VAZ"}, {290, "TAZ"}, {291, "AMP"}, {292, "EMP"}, {293, "FMP"},
				{294, "NMP"}, {295, "ZEB"}, {296, "MAR"}, {297, "UDK"}, {298, "RTH"}, {299, "XEG"}, {300, "GTD"}, {301, "ONT"},
				{302, "CRB"}, {303, "PHX"}, {304, "TMT"}, {305, "PBR"}, {306, "STA"}, {307, "SSA"}, {308, "SKR"}, {309, "SVO"},
				{310, "SMA"}, {311, "STF"}, {312, "SCE"}, {313, "WRW"}, {314, "WRU"}, {315, "KRK"}, {316, "PAF"}, {317, "QZT"},
				{318, "VAL"}, {319, "WRB"}, {320, "EFE"}, {321, "WBU"}, {322, "BKN"}, {323, "AAM"}, {324, "UDF"}, {325, "RZM"},
				{326, "SPD"}, {327, "SPL"}, {328, "ZBC"}, {329, "BTP"}, {330, "FR"},
			};
			return tags;
		}

		// EQPlayer_GetPCActorTag: the playable races; anything else is a human.
		const char *PlayerRaceTag(uint16_t race)
		{
			switch (race) {
			case 2: return "BA";
			case 3: return "ER";
			case 4: return "EL";
			case 5: return "HI";
			case 6: return "DA";
			case 7: return "HA";
			case 8: return "DW";
			case 9: return "TR";
			case 10: return "OG";
			case 11: return "HO";
			case 12: return "GN";
			case 128: return "IK";
			case 130: return "KE";
			case 330: return "FR";
			default: return "HU";
			}
		}

		// EQPlayer_TackOnNeuterChar: the letter some races add for gender 2.
		const char *NeuterLetter(uint16_t race)
		{
			switch (race) {
			case 156: return "M";
			case 17: case 42: case 177: return "L";
			case 29: return "R";
			case 13: case 38: return "I";
			case 183: return "K";
			case 189: return "G";
			case 16: case 68: case 88: case 94: case 139: case 202: case 216: case 217: case 230: case 233: case 237:
			case 240:
				return "N";
			default: return "";
			}
		}
	}

	std::string ActorTag(uint16_t race, uint8_t gender)
	{
		if (race == 252)
			return "MINIPOM200";
		if (race >= 2253 && race <= 2257)
			return "PPOINT";
		auto it = RaceTags().find(race);
		bool listed = it != RaceTags().end() && race >= 13;
		std::string tag = listed ? it->second : PlayerRaceTag(race);
		switch (gender) {
		case 0:
			if (race != 72)
				tag += "M";
			break;
		case 1:
			tag += "F";
			break;
		case 2:
			tag += NeuterLetter(race);
			break;
		default:
			tag += "M";
			break;
		}
		return tag;
	}

	bool IsPlayerRace(uint16_t race)
	{
		return (race >= 1 && race <= 12) || race == 128 || race == 130;
	}
}
