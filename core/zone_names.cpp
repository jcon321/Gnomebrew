#include "zone_names.h"

#include "eqstr.h"

#include <algorithm>
#include <iterator>

namespace
{
	struct ZoneName
	{
		uint32_t id;
		uint32_t string_id;
		const char *long_name;
		const char *short_name;
	};

	// EQWorldData::EQWorldData's AddZone calls, by id.
	constexpr ZoneName Zones[] = {
		{1, 2243, "South Qeynos", "qeynos"},
		{2, 2244, "North Qeynos", "qeynos2"},
		{3, 2245, "Surefall Glade", "qrg"},
		{4, 2246, "Qeynos Hills", "qeytoqrg"},
		{5, 2247, "Highpass Hold", "highpass"},
		{6, 2248, "HighKeep", "highkeep"},
		{8, 2249, "North Freeport", "freportn"},
		{9, 2250, "West Freeport", "freportw"},
		{10, 2251, "East Freeport", "freporte"},
		{11, 2252, "Clan RunnyEye", "runnyeye"},
		{12, 2253, "West Karana", "qey2hh1"},
		{13, 2254, "North Karana", "northkarana"},
		{14, 2255, "South Karana", "southkarana"},
		{15, 2256, "East Karana", "eastkarana"},
		{16, 2257, "Gorge of King Xorbb", "beholder"},
		{17, 2258, "BlackBurrow", "blackburrow"},
		{18, 2259, "Infected Paw", "paw"},
		{19, 2260, "Rivervale", "rivervale"},
		{20, 2261, "Kithicor Forest", "kithicor"},
		{21, 2262, "West Commonlands", "commons"},
		{22, 2263, "East Commonlands", "ecommons"},
		{23, 2264, "Erudin Palace", "erudnint"},
		{24, 2265, "Erudin", "erudnext"},
		{25, 2266, "Nektulos Forest", "nektulos"},
		{26, 2267, "Sunset Home", "cshome"},
		{27, 2268, "Lavastorm Mountains", "lavastorm"},
		{28, 2269, "Nektropos", "nektropos"},
		{29, 2270, "Halas", "halas"},
		{30, 2271, "Everfrost Peaks", "everfrost"},
		{31, 2272, "Solusek's Eye", "soldunga"},
		{32, 2273, "Nagafen's Lair", "soldungb"},
		{33, 2274, "Misty Thicket", "misty"},
		{34, 2275, "North Ro", "nro"},
		{35, 2276, "South Ro", "sro"},
		{36, 2277, "Befallen", "befallen"},
		{37, 2278, "Oasis of Marr", "oasis"},
		{38, 2279, "Toxxulia Forest", "tox"},
		{39, 2280, "The Hole", "hole"},
		{40, 2281, "Neriak Foreign Quarter", "neriaka"},
		{41, 2282, "Neriak Commons", "neriakb"},
		{42, 2283, "Neriak Third Gate", "neriakc"},
		{43, 2284, "Neriak Palace", "neriakd"},
		{44, 2285, "Najena", "najena"},
		{45, 2286, "Qeynos Catacombs", "qcat"},
		{46, 2287, "Innothule Swamp", "innothule"},
		{47, 2288, "The Feerrott", "feerrott"},
		{48, 2289, "Cazic-Thule", "cazicthule"},
		{49, 2290, "Oggok", "oggok"},
		{50, 2291, "Mountains of Rathe", "rathemtn"},
		{51, 2292, "Lake Rathetear", "lakerathe"},
		{52, 2293, "Grobb", "grobb"},
		{53, 2294, "Aviak Village", "aviak"},
		{54, 2295, "Greater Faydark", "gfaydark"},
		{55, 2296, "Ak'Anon", "akanon"},
		{56, 2297, "Steamfont Mountains", "steamfont"},
		{57, 2298, "Lesser Faydark", "lfaydark"},
		{58, 2299, "Clan Crushbone", "crushbone"},
		{59, 2300, "Castle Mistmoore", "mistmoore"},
		{60, 2308, "Kaladim", "kaladima"},
		{61, 2303, "Felwithe", "felwithea"},
		{62, 2303, "Felwithe", "felwitheb"},
		{63, 2304, "Estate of Unrest", "unrest"},
		{64, 2305, "Kedge Keep", "kedge"},
		{65, 2306, "Upper Guk", "guktop"},
		{66, 2307, "Lower Guk", "gukbottom"},
		{67, 2308, "Kaladim", "kaladimb"},
		{68, 2309, "Butcherblock Mountains", "butcher"},
		{69, 2310, "Ocean of Tears", "oot"},
		{70, 2311, "Dagnor's Cauldron", "cauldron"},
		{71, 2312, "Plane of Sky", "airplane"},
		{72, 2313, "Plane of Fear", "fearplane"},
		{73, 2314, "Permafrost Keep", "permafrost"},
		{74, 2315, "Kerra Isle", "kerraridge"},
		{75, 2316, "Paineel", "paineel"},
		{76, 2317, "The Plane of Hate", "hateplane"},
		{77, 2401, "The Arena", "arena"},
		{78, 2319, "The Field of Bone", "fieldofbone"},
		{79, 2320, "Warsliks Wood", "warslikswood"},
		{80, 2321, "Temple of Solusek Ro", "soltemple"},
		{81, 2322, "Temple of Droga", "droga"},
		{82, 2323, "West Cabilis", "cabwest"},
		{83, 2324, "Swamp of No Hope", "swampofnohope"},
		{84, 2325, "Firiona Vie", "firiona"},
		{85, 2326, "Lake of Ill Omen", "lakeofillomen"},
		{86, 2327, "Dreadlands", "dreadlands"},
		{87, 2328, "Burning Woods", "burningwood"},
		{88, 2329, "Kaesora", "kaesora"},
		{89, 2330, "Old Sebilis", "sebilis"},
		{90, 2331, "City of Mist", "citymist"},
		{91, 2332, "Skyfire Mountains", "skyfire"},
		{92, 2333, "Frontier Mountains", "frontiermtns"},
		{93, 2334, "The Overthere", "overthere"},
		{94, 2335, "The Emerald Jungle", "emeraldjungle"},
		{95, 2336, "Trakanon's Teeth", "trakanon"},
		{96, 2337, "Timorous Deep", "timorous"},
		{97, 2338, "Kurn's Tower", "kurn"},
		{98, 2339, "Erud's Crossing", "erudsxing"},
		{100, 2340, "Stonebrunt Mountains", "stonebrunt"},
		{101, 2341, "The Warrens", "warrens"},
		{102, 2342, "Karnor's Castle", "karnor"},
		{103, 2343, "Chardok", "chardok"},
		{104, 2344, "Dalnir", "dalnir"},
		{105, 2345, "Howling Stones", "charasis"},
		{106, 2346, "East Cabilis", "cabeast"},
		{107, 2347, "Mines of Nurga", "nurga"},
		{108, 2348, "Veeshan's Peak", "veeshan"},
		{109, 2349, "Veksar", "veksar"},
		{110, 2350, "Iceclad Ocean", "iceclad"},
		{111, 2351, "Tower of Frozen Shadow", "frozenshadow"},
		{112, 2352, "Velketor's Labyrinth", "velketor"},
		{113, 2353, "Kael Drakkal", "kael"},
		{114, 2354, "Skyshrine", "skyshrine"},
		{115, 2355, "Thurgadin", "thurgadina"},
		{116, 2356, "Eastern Wastes", "eastwastes"},
		{117, 2357, "Cobalt Scar", "cobaltscar"},
		{118, 2358, "Great Divide", "greatdivide"},
		{119, 2359, "The Wakening Land", "wakening"},
		{120, 2360, "Western Wastes", "westwastes"},
		{121, 2361, "Crystal Caverns", "crystal"},
		{123, 2362, "Dragon Necropolis", "necropolis"},
		{124, 2363, "Temple of Veeshan", "templeveeshan"},
		{125, 2364, "Siren's Grotto", "sirens"},
		{126, 2365, "Plane of Mischief", "mischiefplane"},
		{127, 2366, "Plane of Growth", "growthplane"},
		{128, 2367, "Sleeper's Tomb", "sleeper"},
		{129, 2368, "Icewell Keep", "thurgadinb"},
		{130, 2369, "Marauder's Mire", "erudsxing2"},
		{150, 2370, "Shadow Haven", "shadowhaven"},
		{151, 2371, "The Bazaar", "bazaar"},
		{152, 2372, "The Nexus", "nexus"},
		{153, 2373, "Echo Caverns", "echo"},
		{154, 2374, "Acrylia Caverns", "acrylia"},
		{155, 2375, "Shar Vahl", "sharvahl"},
		{156, 2376, "Paludal Caverns", "paludal"},
		{157, 2377, "Fungus Grove", "fungusgrove"},
		{158, 2378, "Vex Thal", "vexthal"},
		{159, 2379, "Sanctus Seru", "sseru"},
		{160, 2380, "Katta Castellum", "katta"},
		{161, 2381, "Netherbian Lair", "netherbian"},
		{162, 2382, "Ssraeshza Temple", "ssratemple"},
		{163, 2383, "Grieg's End", "griegsend"},
		{164, 2384, "The Deep", "thedeep"},
		{165, 2385, "Shadeweaver's Thicket", "shadeweaver"},
		{166, 2386, "Hollowshade Moor", "hollowshade"},
		{167, 2387, "Grimling Forest", "grimling"},
		{168, 2388, "Marus Seru", "mseru"},
		{169, 2389, "Mons Letalis", "letalis"},
		{170, 2390, "The Twilight Sea", "twilight"},
		{171, 2391, "The Grey", "thegrey"},
		{172, 2392, "The Tenebrous Mountains", "tenebrous"},
		{173, 2393, "The Maiden's Eye", "maiden"},
		{174, 2394, "Dawnshroud Peaks", "dawnshroud"},
		{175, 2395, "The Scarlet Desert", "scarlet"},
		{176, 2396, "The Umbral Plains", "umbral"},
		{179, 2397, "Akheva Ruins", "akheva"},
		{180, 2401, "The Arena", "arena2"},
		{181, 2398, "The Jaggedpine Forest", "jaggedpine"},
		{183, 2399, "Tutorial Zone", "tutorial"},
		{184, 2301, "Loading", "load"},
		{185, 2301, "Loading", "load2"},
		{200, 9006, "Ruins of Lxanvom", "codecay"},
		{201, 9007, "Plane of Justice", "pojustice"},
		{202, 9004, "Plane of Knowledge", "poknowledge"},
		{203, 9005, "Plane of Tranquility", "potranquility"},
		{204, 9008, "Plane of Nightmare", "ponightmare"},
		{205, 9010, "Plane of Disease", "podisease"},
		{206, 9011, "Plane of Innovation", "poinnovation"},
		{207, 9012, "Plane of Torment", "potorment"},
		{208, 9013, "Plane of Valor", "povalor"},
		{209, 9014, "Torden, The Bastion of Thunder", "bothunder"},
		{210, 9015, "Plane of Storms", "postorms"},
		{211, 9016, "Halls of Honor", "hohonora"},
		{212, 9018, "Solusek Ro's Tower", "solrotower"},
		{213, 9019, "Plane of War", "powar"},
		{214, 9020, "Drunder, Fortress of Zek", "potactics"},
		{215, 9021, "Eryslai, the Kingdom of Wind", "poair"},
		{216, 9022, "Reef of Coirnav", "powater"},
		{217, 9023, "Doomfire, The Burning Lands", "pofire"},
		{218, 9024, "Vegarlson, The Earthen Badlands", "poeartha"},
		{219, 9026, "Plane of Time", "potimea"},
		{220, 9017, "Temple of Marr", "hohonorb"},
		{221, 9009, "Lair of Terris Thule", "nightmareb"},
		{222, 9025, "Stronghold of the Twelve", "poearthb"},
		{223, 9026, "Plane of Time", "potimeb"},
	};
}

std::string FullZoneName(uint32_t zone_id, const EqStr *strings)
{
	auto it = std::lower_bound(std::begin(Zones), std::end(Zones), zone_id,
		[](const ZoneName &zone, uint32_t id) { return zone.id < id; });
	if (it == std::end(Zones) || it->id != zone_id)
		return "Unknown Zone";
	if (it->string_id != 0 && strings && strings->Has(it->string_id))
		return strings->Format(it->string_id, {}).substr(0, 79); // steve_strncpy(.., 0x4f)
	return it->long_name;
}

std::string ShortZoneName(uint32_t zone_id)
{
	auto it = std::lower_bound(std::begin(Zones), std::end(Zones), zone_id,
		[](const ZoneName &zone, uint32_t id) { return zone.id < id; });
	return it != std::end(Zones) && it->id == zone_id ? it->short_name : "";
}

int ZoneExpansion(uint32_t zone_id)
{
	// The ids AddZone gives each expansion, the original world's being the rest.
	if ((zone_id >= 78 && zone_id <= 97 && zone_id != 80) || (zone_id >= 102 && zone_id <= 109))
		return 1;
	if (zone_id >= 110 && zone_id <= 129 && zone_id != 122)
		return 2;
	if ((zone_id >= 150 && zone_id <= 176) || zone_id == 179)
		return 3;
	if (zone_id >= 200 && zone_id <= 223)
		return 4;
	return 0;
}
