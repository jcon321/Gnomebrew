#include "settings_file.h"

#include "ini_file.h"

#include <fstream>

namespace
{
	struct Setting
	{
		const char *section, *key, *value;
		const char *comment; // lines split at \n; empty: none (it goes right under the key above)
	};

	// In the file's order. The [Debug] defaults after RecordPackets are DebugMenu's own.
	const Setting Settings[] = {
		{"Paths", "EQDir", "",
			"The EverQuest folder the art, sounds and zones are read from (eqfeart*.pak, eqtext.txt,\n"
			"uifiles, the .s3d files). Only assets: eqgame.exe and its eqclient.ini are never read.\n"
			"Empty: the current folder if it has eqfeart1.pak, else this exe's. --eqdir overrides it."},

		{"Login", "Server", "127.0.0.1:6000",
			"The login server, host:port (eqhost.txt isn't read). --login overrides it."},
		{"Login", "User", "", "The account name, filled in at the login screen; kept from the last login."},

		{"Display", "Width", "1920",
			"The game resolution: the window's size in pixels, shrunk to fit the screen if it's too big.\n"
			"--size WxH overrides it."},
		{"Display", "Height", "1080", ""},
		{"Display", "Renderer", "d3d11",
			"d3d11: the GPU draws the 3D views and presents the window. software: the CPU rasterizer and\n"
			"GDI, about 60 fps. d3d11 falls back to software when Direct3D 11 or d3dcompiler_47.dll isn't\n"
			"there (my-client.log says why). --renderer overrides it."},
		{"Display", "VSync", "1",
			"1: wait for the monitor's refresh (d3d11). 0: run as fast as it can. The debug menu (F11)\n"
			"toggles it, kept on exit."},

		{"Sound", "Sound", "1", "0: no sound at all (eqclient.ini's Sound=FALSE)."},
		{"Sound", "MusicVolume", "10",
			"The options' music and sound levels, 0 to 20 (10: half, as the real client starts)."},
		{"Sound", "SoundVolume", "10", ""},
		{"Sound", "Music", "midi",
			"Where the XMI music (the zones before Luclin) plays. midi: Windows' MIDI synth, like the\n"
			"real client (the SoundFont without a MIDI out). sf2: our own mix through SoundFont.\n"
			"The MP3 music and all the sounds always play through our own mix."},
		{"Sound", "SoundFont", "synthus2.sf2",
			"The General MIDI bank for Music=sf2: in the EQ folder, or a full path. synthusr.sf2 if it's\n"
			"missing."},

		{"Defaults", "LastCharSel", "0", "The character select slot to start on (0: the first). Kept as you select."},
		{"Defaults", "ShowNamesLevel", "4",
			"/shownames: 0 off, 1 first names, 2 with surnames, 3 with guilds, 4 everything (AA titles\n"
			"too); Zeal's 5 title and first name, 6 title, first and last, 7 first name and guild.\n"
			"/shownames changes it, kept on exit."},
		{"Defaults", "EnableExtendedNameplateDistance", "1",
			"1: names over heads as far out as the zone is drawn (as TAKP's eqgame.dll and Zeal do).\n"
			"0: eqgame's own 70 units."},

		{"Debug", "RecordPackets", "0",
			"1: each zone session's packets go to replays\\<character>-<time>.eqr next to the exe, to\n"
			"play back with --replay."},
		{"Debug", "MenuOpen", "1",
			"1: the debug menu (F11) open, its stat card and radar over the zone. This key and the ones\n"
			"below are the menu's, kept as they change in it."},
		{"Debug", "Folded", "ServerSync,Camera,Packets,Zone",
			"The stat card's sections folded away, any of Performance, Movement, ServerSync, Camera,\n"
			"Network, Packets, Zone, Sound, Render, Target and Nearby."},
		{"Debug", "Wireframe", "0", "1: triangle outlines instead of the scene."},
		{"Debug", "NoFog", "0", "1: no fog, the zone drawn out to its max clip."},
		{"Debug", "CollisionWalls", "0", "1: the collision walls near us outlined."},
		{"Debug", "FloorMarker", "0", "1: a marker on the floor under our feet."},
		{"Debug", "NoOcclusionCulling", "0", "1: draw what the zone's occlusion culling hides too."},
		{"Debug", "NoClipPlane", "0", "1: no far clip plane, and no fog at all."},
		{"Debug", "NoWeatherEffects", "0",
			"1: no rain or snow drawn, and the zone's own fog, sky and light (the weather's sounds go on)."},
		{"Debug", "Vision", "-1",
			"What we see in at night: -1 our own (our race's), 0 none, 1 infravision, 2 ultravision."},
		{"Debug", "AllSound", "1", "0: no sound (the four below are off with it)."},
		{"Debug", "Music", "1", "0: no music."},
		{"Debug", "ZoneAmbience", "1", "0: none of the zone's ambient sounds."},
		{"Debug", "GameSounds", "1", "0: no game sounds (doors, footsteps, creatures)."},
		{"Debug", "WeatherSounds", "1", "0: no weather sounds (rain, wind, thunder)."},
	};

	// At the top of a new file.
	const char *const Header =
		"; my-client's settings. Missing keys are added with their defaults when it starts, and\n"
		"; lines it doesn't change (comments too) are kept as they are.\n"
		";\n"
		"; [TextColors] (optional): the chat's text color n, 1..72 as eqgame's, as User_<n>_Red,\n"
		"; User_<n>_Green and User_<n>_Blue, 0..255. All three, or the built-in color.\n";
}

bool WriteDefaultSettings(const std::string &path, std::string &error)
{
	IniFile ini;
	if (!ini.Load(path)) {
		std::ofstream(path) << Header;
		if (!ini.Load(path)) {
			error = "couldn't write " + path;
			return false;
		}
	}
	bool added = false;
	std::string section;
	for (const Setting &setting : Settings) {
		// A missing section goes in its place among the others.
		if (setting.section != section) {
			ini.AddSection(setting.section, section);
			section = setting.section;
		}
		added |= ini.Add(setting.section, setting.key, setting.value, setting.comment);
	}
	if (added && !ini.Save(path)) {
		error = "couldn't write " + path;
		return false;
	}
	return true;
}
