#pragma once

// Horses, as eqgame rides them: each EQPlayer's ActorInfo has a Mount (the horse it's on) and a
// Rider (who's on it). The zone sends a summoned horse as an NPC of its own (race 216, named
// "<rider>`s_Mount00"), standing where it was summoned; the client puts the rider on it.
//   EQPlayer::MakeRiderMountUp (@0051fcf5), from EQPlayer::EQPlayer for every new NPC and
//     CEverQuest_CreateInitialActors for those there as the zone loads: a horse with no rider, its
//     name over 8 letters with "`s_Mount" in it, is the mount of the spawn named as it is less its
//     last 10 letters (EQPlayer_get_player: stricmp, us too).
//   EQ_Character::ProcessAffects (@004c7448), every 6 s: with a buff of SPA 113 and no Mount, we
//     get on "<our name>`s_Mount" (EQPlayer_get_player_partial_name: trimName'd, the shortest name
//     that starts so).
//   EQPlayer::MountEQPlayer (@0051fd83): off any other horse first (Dismount). Without the Luclin
//     player models (CDisplay+0xa0: InitWorld loaded one other than the Vah Shir's) the horse only
//     takes the rider as its Rider, and that's all. Otherwise a horse takes a rider with an old
//     model (EQPlayer::HasInvalidRiderTexture: its model's +0x3b0 not 1) as its Rider too, and is
//     ChangeHeight'd to the rider's height; then CDisplay_AttachPlayerToPlayerDag puts the rider on
//     its <tag>RIDER_DAG, which fails for a rider with an old model or an untextured horse
//     (EQPlayer::IsUntexturedHorse: race 216 drawn as HUM_ACTORDEF, the horses' global5_chr not
//     loaded, UseLuclinElementals off). Failing, for us: "You must have both the Horse Models and
//     your current Luclin Character Model enabled to ride a horse." (eqstr 5524, color 13) and the
//     SPA 113 buff off (RemovePCAffect, which tells the zone: it takes the horse away). Done, the
//     rider's Mount and the horse's Rider are set.
//   CDisplay::UpdatePlayerVisibility (@004af894) and msg_eq_netplayer (@004dd8de): an untextured
//     horse, and a horse whose Rider has an old model, are never drawn (EQPlayer::ForceInvisible).
//   So with the old models nothing changes for the rider: no horse is seen, the buff stays (unless
//     some Luclin model is on and theirs isn't), and they go on walking as themselves, their own
//     speed and speed buffs (eqspa_movement_rate leaves out the positive ones only with a Mount);
//     the zone counts them as meditating while still (Client's horse id, not the client's), and its
//     horse NPC stays, unseen, where it was summoned.
//   EQPlayer::Dismount (@0051ff5f): the rider off its Mount, its Rider cleared. Its callers:
//     /dismount (@004ff5a6: with a Mount "Dismounting." 13284, the SPA 113 buff off and Dismount;
//     else "You do not have a horse to dismount." 13285), EQPlayer::ChangeStance (@0050be3c: no
//     sitting on a Mount; lying down, feign death, gets off and, for us, takes the buff off),
//     EQPlayer::Dismount2 (@00508152, from ~EQPlayer: a spawn going takes its rider off and gets
//     off its horse) and do_change_form (OP_Illusion: off, the new form, then the horse's
//     MakeRiderMountUp puts it back on, which MountEQPlayer may refuse the new form).
// Riding itself (the rider on the horse's RIDER_DAG, our horse under our controls) is ZoneView's
// (SetMounts) and PlayerMovement's (SetMounted). Not yet: MountEQPlayer onto something not a horse
// (its DagRightPoint), MountEQPlayer's levitation (SetNoGrav on the horse), and a mount following
// its rider's visibility.

#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>

class ZoneSession;
class ZoneView;
struct ZoneSpawn;

class Mounts
{
public:
	struct Host
	{
		std::function<void(uint32_t id, const std::string &fallback, int type)> say; // an eqstr line
		std::function<bool(int spa)> has_affect;    // EQ_Character::GetPCSpellAffect
		std::function<bool(int spa)> remove_affect; // GetPCSpellAffect then RemovePCAffect
	};

	void Attach(ZoneSession &zone, Host host);
	// The zone's models, once loaded (null while there's no zone view).
	void SetView(ZoneView *view) { m_view = view; }
	void Reset(); // a new zone: nobody's on anything

	void SpawnAdded(uint16_t id);   // EQPlayer::EQPlayer's MakeRiderMountUp
	void SpawnRemoved(uint16_t id); // ~EQPlayer: Dismount2, and the Riders that pointed at it cleared
	// Every frame: CreateInitialActors' MakeRiderMountUp once the zone's models are in.
	void Frame();
	// ProcessAffects' SPA 113, on the buffs' 6 s tick.
	void Tick();

	// EQPlayer::MountEQPlayer, EQPlayer::Dismount.
	void Mount(uint16_t rider, uint16_t horse);
	void Dismount(uint16_t rider);
	// EQPlayer::do_change_form on a rider: off its Mount before the new form, then the horse's
	// MakeRiderMountUp after (back on, or not when MountEQPlayer won't take the new form).
	uint16_t ChangeFormOff(uint16_t rider);
	void ChangeFormOn(uint16_t horse);
	// /dismount: false when we have no Mount (eqgame's 13285).
	bool DismountCommand();
	// EQPlayer::ChangeStance's checks: whether `stance` may go on (not sitting on a Mount); lying down
	// (115) gets off, and takes our buff off.
	bool StanceChange(uint16_t id, uint8_t stance);

	uint16_t MountOf(uint16_t rider) const; // ActorInfo's Mount; 0 none
	uint16_t RiderOf(uint16_t horse) const; // ActorInfo's Rider; 0 none
	bool Mounted() const;                   // our own Mount
	const std::map<uint16_t, uint16_t> &Riders() const { return m_mount; } // rider -> horse
	// The horses UpdatePlayerVisibility never draws (see above).
	std::set<uint16_t> Hidden() const;

private:
	const ZoneSpawn *Find(uint16_t id) const; // a spawn, or us
	uint16_t Me() const;
	bool Ready() const; // the zone's models are in
	void MakeRiderMountUp(uint16_t horse);
	bool InvalidRiderTexture(uint16_t rider) const; // EQPlayer::HasInvalidRiderTexture
	bool UntexturedHorse(uint16_t horse) const;     // EQPlayer::IsUntexturedHorse

	ZoneSession *m_zone = nullptr;
	ZoneView *m_view = nullptr;
	Host m_host;
	std::map<uint16_t, uint16_t> m_mount; // rider -> horse
	std::map<uint16_t, uint16_t> m_rider; // horse -> rider
	bool m_initial = false; // CreateInitialActors' pass done
};
