#include "animations.h"

#include <cstring>

namespace render
{
	const char *AnimationCode(int id)
	{
		static const char *const codes[Anim::Count] = {"", "C01", "C02", "C03", "C04", "C05", "C06", "C07", "C08",
			"C09", "C10", "C11", "D01", "D02", "D03", "D04", "D05", "L01", "L02", "L03", "L04", "L05", "L06", "L07",
			"L08", "L09", "O01", "S01", "S02", "S03", "S04", "S05", "P01", "P02", "P03", "P04", "P05", "P06", "P07",
			"T01", "T02", "T03", "T04", "T05", "T06", "T07", "T08", "T09", "S06", "S07", "S08", "S09", "S10", "S11",
			"S12", "S13", "S14", "S15", "S16", "S17", "S18", "S19", "S20", "S21", "S22", "S23", "S24", "S25", "S26",
			"S27", "S28", "P08", "O02", "O03", "P09", "L11", "L12", "S29", "L10"};
		return id >= 0 && id < Anim::Count ? codes[id] : "";
	}

	int FallbackAnimation(int id, uint16_t race)
	{
		// The animation variants (toggleAnimationState) aren't loaded, so this is the rest of it.
		if (id >= 1 && id <= 4)
			return 5;
		if (id >= 6 && id <= 9)
			return 5;
		switch (id) {
		case 10: return 37;
		case 11: return 1;
		case 12: return 13;
		case 13: return 12;
		case 14:
		case 15: return 13;
		case 17: return 18;
		case 18: return 17;
		case 21: return 37;
		case 25: return 37;
		case 26: return 32;
		case 37: return race == 72 || race == 42 ? 18 : 17; // swimming: walking, or running for these two
		case 39: return 40;
		case 42: return 43;
		case 44: return 43;
		case 72: return 26;
		case 75: return 17;
		case 76: return 18;
		case 78: return 74;
		default: return Anim::Idle;
		}
	}

	bool IsStandingAnimation(int id)
	{
		return id == Anim::Idle || id == Anim::Sitting || id == Anim::Stand2 || id == 74;
	}

	bool IsMovementAnimation(int id)
	{
		return id == Anim::Idle || (id > 0 && id < 17) || (id > 41 && id < 48) || id == 254 || id == 17 || id == 75 ||
		       id == Anim::Shuffle;
	}

	std::string AnimationSource(const std::string &code)
	{
		if (code.size() < 3)
			return code;
		auto is = [&](const char *prefix) { return strncmp(code.c_str(), prefix, strlen(prefix)) == 0; };
		auto any = [&](std::initializer_list<const char *> prefixes) {
			for (const char *prefix : prefixes)
				if (is(prefix))
					return true;
			return false;
		};
		std::string gendered = code.substr(2, 1); // the source keeps the model's last letter (M, F)
		if (any({"ERO", "SIR", "HAG", "ABH"}))
			return "ELF";
		if (any({"BRI", "COK"}))
			return "DWM";
		if (any({"DR2", "CCD", "BWD", "PRI"}))
			return "TRK";
		if (any({"DA", "HI", "HA", "HH", "QC", "NG", "FP", "EG", "BR", "HL", "ZO", "FEM", "FEF", "IV", "GHM", "GHF",
				"GF", "GE", "HU", "ER", "BA"}))
			return "EL" + gendered;
		if (any({"GNM", "GNF", "CLM", "CLF", "HO", "RIM", "RIF", "KAM", "KAF", "KA", "GD", "COM", "COF"}))
			return "DW" + gendered;
		if (any({"TRM", "OG", "TRF", "GRM", "GRF", "OKM", "OKF"}))
			return "OGF";
		if (any({"BGM", "TRI", "ENA", "SKE"}))
			return "ELM";
		static const struct
		{
			const char *source;
			std::initializer_list<const char *> models;
		} sources[] = {{"GNN", {"MIN", "CPM", "YAK", "FAN"}}, {"GOB", {"GHU"}}, {"FRO", {"FRG"}},
			{"LIM", {"LIF", "PUM", "TIG", "STC", "MTC"}}, {"CPM", {"CPF"}}, {"SNM", {"SNE"}}, {"FAF", {"PIF"}},
			{"RAT", {"SKU", "ARM"}}, {"DRK", {"GRI", "SPH", "FMO"}}, {"GIA", {"GOL", "GOM"}}, {"WOL", {"WO"}},
			{"WER", {"KOB"}}, {"GAR", {"IMP", "GAM"}}, {"SPI", {"BET", "SPM"}}, {"IKM", {"ICM", "ICN", "ICF", "IKS", "IKF"}},
			{"KGO", {"BGG"}}, {"SRW", {"SSK"}}, {"REA", {"IKH"}}, {"DRA", {"WUR", "GDR"}}, {"RHI", {"BTM"}},
			{"DML", {"SDE"}}, {"SCA", {"TOT"}}, {"SPE", {"SPC"}}, {"FSG", {"STG", "STM"}}, {"VRM", {"VRF"}},
			{"AVI", {"AVM"}}, {"TEN", {"TEM"}}, {"WAL", {"WAM", "WAF"}}, {"SHM", {"SHN", "SHF"}}, {"GMN", {"GMM"}},
			{"AEL", {"WEL", "FEL"}}, {"SDM", {"SDF"}}, {"KEM", {"KES", "VSG", "VSK"}}, {"NYM", {"NYD"}}, {"SPD", {"SPL"}}};
		for (const auto &entry : sources)
			if (any(entry.models))
				return entry.source;
		return code;
	}
}
