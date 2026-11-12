#pragma once

#include <cstdint>
#include <functional>
#include <string>

class WindowManager;
class ZoneSession;
struct ZoneMovement;

// The pet window (eqgame's CPetInfoWnd @0042cbd6, EQUI_PetInfoWindow.xml), /pet (@004f8754) and
// CEverQuest::IssuePetCommand (@00547749).
//   - The window: PetHPGauge is EQType 16 (our ActorInfo's PetID: its name and HP; "" and -3
//     without one). Its seven buttons, GetButton's 1..7: AttackButton, GuardButton, LostButton,
//     SitButton, BackButton, FollowButton, TauntButton.
//   - Update (@0042cd31; on Activate and every 250 ms in OnProcessFrame): with our pet here, the
//     buttons are enabled; without one, disabled. An animation (race 127 with EQPlayer+0x120 set)
//     enables only Animation Empathy's (GetAbility 0xc3): rank 1 Guard and Follow, 2 Attack, 3 Back.
//     SitButton reads eqstr 7518 "sit", or 7519 "stand" while the pet sits (StandingState 'n'),
//     with its first letter in capitals.
//   - WndNotification (@0042cf57): a click does /pet with eqstr 13092 "attack", 7506 "guard here",
//     7504 "get lost", the sit button's own text, 7500 "back off", 7508 "follow me" or 7502
//     "taunt". A button held goes on the cursor (CCursorAttachment type 10: the hot buttons' 8,
//     "Pet <its text>"); a hot button holding one sends the window that notification, enabled or
//     not (CHotButtonWnd::DoHotButton).
//   - /pet: the words (any case, the whole of it) to IssuePetCommand's command: 7500, 7501 back off
//     1; 7502 taunt 11; 7514, 7503, 7509 no taunt 14; 7517 taunt on 13; 7504 get lost 2; 7505 as
//     you were 3; 7506 guard here 5; 7507 guard me 6; 7508 follow me 8; 7510, 7518 sit 9; 7511, 7519
//     stand 10; 7512, 7515, 7516 health 4; 7513 hold 12; 13103 target 15; 7521, 7520 leader 16;
//     3272 feign 17. Else a line starting one of 13092..13101 (attack, kill, ... whack) is attack
//     (7): with a name after it, the nearest spawn starting it (findEntityByPartialName, tried as
//     typed, "#", "a_" and "#a_" before it; spaces as '_'), with none our target (not us). Anything
//     else, or no one found, sends nothing.
//   - IssuePetCommand: 15 targets the first spawn whose PetOwnerSpawnId is ours, saying eqstr
//     13104 "Targeting your pet.", or 13091 "You don't have a pet to command!". Any other is sent
//     to the zone as OP_PetCommands {command, target}; it answers in the pet's words.
class PetWindow
{
public:
	static constexpr const char *Screen = "PetInfoWindow";
	static constexpr int Buttons = 7; // GetButton's 1..7

	struct Host
	{
		std::function<std::string(uint32_t, const std::string &)> eqstr; // eqstr_en.txt, else the fallback
		std::function<void(const std::string &)> chat;                    // print_chat_dsp_chat_2
		std::function<uint16_t()> target;                                 // pinstTarget, 0 none
		std::function<void(uint16_t)> set_target;
		std::function<ZoneMovement()> position;                           // where we are
		std::function<int(int)> ability;                                  // EQ_Character::GetAbility: an AA's rank
	};

	void Attach(WindowManager &windows, ZoneSession &zone, Host host);
	void Frame(uint64_t now_ms); // OnProcessFrame; Update as it comes up (Activate), then every 250 ms
	void Update();               // CPetInfoWnd::Update
	// WndNotification's click (1) on one of its buttons.
	bool Click(const std::string &window, const std::string &control);
	// A hot button's: the notification for GetButton(index), enabled or not.
	void Press(int index);
	void Command(const std::string &text); // /pet

	// GetButton: the ScreenID of button 1..7; nullptr for another. And back: 0 for none.
	static const char *ButtonId(int index);
	static int ButtonIndex(const std::string &control);

private:
	void Notify(int index);                        // WndNotification 1
	void Issue(uint32_t command, uint16_t target); // IssuePetCommand
	uint16_t FindByPartialName(const std::string &name) const; // EQPlayer::findEntityByPartialName
	std::string Word(uint32_t id) const;

	WindowManager *m_windows = nullptr;
	ZoneSession *m_zone = nullptr;
	Host m_host;
	uint64_t m_updated_ms = 0; // 0x138
	bool m_shown = false;      // up at the last frame
};
