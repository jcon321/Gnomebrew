#pragma once

// Our own window at character select (not eqgame's; CharSelectAnimWindow in my-client.ini): which
// animations the character plays now and then, eqgame's picks for its class (combat only) or those
// and the socials' too (CharSelectEmotes), and a list of every animation its model has, to play one.
// Its XML is ours, drawn with the default UI's templates.

#include "sidl.h"

#include <string>
#include <vector>

class CharSelectAnimWindow
{
public:
	enum class Action
	{
		None,
		ModeChanged, // AllAnimations() changed
		Play,        // play Selected()
	};

	// Adds the window's definitions to the loaded UI's, once.
	static void AddDefinitions(SidlDefinitions &defs);

	bool Build(const SidlDefinitions &defs, std::string &error);
	// At the right edge of a width x height screen, level with the character select window.
	void Place(int width, int height);

	void SetAllAnimations(bool all);
	bool AllAnimations() const { return m_all; }
	// The animations the model has (render::Anim numbers), listed by name; the selection is kept
	// when the new list has it.
	void SetAnimations(const std::vector<int> &ids);
	int Selected() const; // the animation, or -1

	// Mouse events; the window takes only what's over it (or started there).
	Action OnMouse(const InputEvent &event);
	void Draw(Canvas &canvas, TextRenderer &text, SidlTextures &textures, uint64_t now_ms) const;
	const SidlWindow &Window() const { return m_window; }

private:
	SidlWindow m_window;
	std::vector<int> m_ids; // the list's rows
	bool m_all = false;
};
