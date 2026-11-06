#pragma once

// Wire opcodes. Login: server/loginserver/login_util/login_opcodes_oldver.conf.
// World: server/utils/patches/patch_Mac.conf, cross-checked with
// WorldAuthenticate (eqgame.exe @0054c9ac).

#include <cstdint>

namespace LoginOp
{
	constexpr uint16_t SessionReady = 0x0059;
	constexpr uint16_t LoginPC = 0x0001;        // 40 bytes DES-CBC {user[20], pass[20]}
	constexpr uint16_t ClientError = 0x0002;    // server -> client, text
	constexpr uint16_t LoginDisconnect = 0x0005;
	constexpr uint16_t LoginAccepted = 0x0004;  // SessionId_Struct, "LS#<id>"
	constexpr uint16_t ServerListRequest = 0x0046; // request and response share the opcode
	constexpr uint16_t PlayEverquest = 0x0047;  // request: world IP string. response: [0]=?, [1..10]=key
	constexpr uint16_t LoginUnknown1 = 0x0048;
	constexpr uint16_t LoginUnknown2 = 0x004a;
	constexpr uint16_t ServerName = 0x0049;
	constexpr uint16_t LoginBanner = 0x0052;
	constexpr uint16_t LoginComplete = 0x0088;
}

namespace WorldOp
{
	constexpr uint16_t SendLoginInfo = 0x1858;  // 200 bytes, "LS#id\0key\0", zoning @188
	constexpr uint16_t ApproveWorld = 0x1007;   // client treats this as "access granted"
	constexpr uint16_t BadLogin = 0x1005;       // client: "Invalid Login or Password"
	constexpr uint16_t LogServer = 0x41c3;
	constexpr uint16_t EncryptKey = 0x41e5;
	constexpr uint16_t MOTD = 0x41dd;
	constexpr uint16_t ExpansionInfo = 0x41d8;
	constexpr uint16_t SendCharInfo = 0x4047;   // CharacterSelect_Struct, 1620 bytes
	constexpr uint16_t EnterWorld = 0x8001;     // name\0, both directions
	constexpr uint16_t ZoneServerInfo = 0x8004; // ip[128], uint16 port
	constexpr uint16_t ZoneUnavail = 0x8005;
	constexpr uint16_t SetChatServer = 0x8009;
	constexpr uint16_t GuildsList = 0x4192;
	constexpr uint16_t WorldLogout = 0x4023;
	constexpr uint16_t DeleteCharacter = 0x405a; // name\0, server replies with OP_SendCharInfo
	constexpr uint16_t ApproveName = 0x408b;     // name[64], race, class, deity (76 bytes) -> 1 byte approval
	constexpr uint16_t CharacterCreate = 0x4049; // CharCreate_Struct (8452 bytes) -> OP_SendCharInfo, or OP_ApproveName 0
}

namespace ZoneOp
{
	// utils/patches/patch_Mac.conf
	constexpr uint16_t DataRate = 0x41e8; // float; ZoneAuthenticate sends it first
	constexpr uint16_t ZoneEntry = 0x4028; // client: {crc32(name), name[64]}; server: ServerZoneEntry_Struct
	constexpr uint16_t PlayerProfile = 0x4036; // deflated + EncryptProfilePacket
	constexpr uint16_t ReqNewZone = 0x405d; // name\\0
	constexpr uint16_t NewZone = 0x405b; // Mac::structs::NewZone_Struct, 572 bytes
	constexpr uint16_t ReqClientSpawn = 0x400a;
	constexpr uint16_t SendExpZonein = 0x40d8; // both directions, twice
	constexpr uint16_t ClientUpdate = 0x40f3; // SpawnPositionUpdate_Struct, 15 bytes
	constexpr uint16_t ZoneSpawns = 0x415f; // Spawn_Structs, deflated + EncryptZoneSpawnPacket; also carries new spawns
	constexpr uint16_t NewSpawn = 0x426b;
	constexpr uint16_t DeleteSpawn = 0x4029; // u16 spawn id
	constexpr uint16_t MobUpdate = 0x409f; // u32 count + SpawnPositionUpdate_Structs
	constexpr uint16_t HPUpdate = 0x40b2; // SpawnHPUpdate_Struct
	constexpr uint16_t ManaChange = 0x417f;
	constexpr uint16_t SpawnAppearance = 0x40f5; // SpawnAppearance_Struct; type 0x10 = our spawn id
	constexpr uint16_t TimeOfDay = 0x40f2;
	constexpr uint16_t Weather = 0x4136;
	constexpr uint16_t ChannelMessage = 0x4107; // ChannelMessage_Struct + text
	constexpr uint16_t SpecialMesg = 0x4180; // u32 type + text
	constexpr uint16_t FormattedMessage = 0x4236; // eqstr id + args
	constexpr uint16_t Camp = 0x4207;
	constexpr uint16_t Jump = 0x4020;          // no body; DoMainLoop sends it as the jump starts
	constexpr uint16_t Logout = 0x4150;
	constexpr uint16_t LogoutReply = 0x4159;
	constexpr uint16_t ZoneInAvatarSet = 0x406f;
	constexpr uint16_t ExpUpdate = 0x4199;   // {u32 exp}: how far into the level, 0..330
	constexpr uint16_t AAExpUpdate = 0x4223; // AltAdvStats_Struct {i32 exp (0..330), i16 unspent, i8 percentage, i8}
	constexpr uint16_t CharInventory = 0x41f6;
	constexpr uint16_t ItemPacket = 0x4164;
	constexpr uint16_t SpawnDoor = 0x41f7;     // u16 count, then the Door_Structs (44 bytes each) deflated
	constexpr uint16_t MoveDoor = 0x408e;      // {u8 door id, u8 action}: EQSwitch::ChangeState's state
	constexpr uint16_t ClickDoor = 0x408d;     // ClickDoor_Struct, 8 bytes (EQSwitch::UseSwitch)
	constexpr uint16_t GroundSpawn = 0x402c;       // Object_Struct (224 bytes): something on the ground; ours, a drop
	constexpr uint16_t ClickObject = 0x402b;       // ClickObject_Struct {u32 drop id, u32 who}: a pickup; the zone's, it's gone
	constexpr uint16_t ClickObjectAction = 0x40d7; // ClickObjectAction_Struct: a tradeskill container opened; ours, closed
	constexpr uint16_t ObjectItemPacket = 0x40fb;  // Item_Struct: one in an open tradeskill container (slot 0..9)
	constexpr uint16_t ClearObject = 0x4205;       // the open tradeskill container's items gone
	constexpr uint16_t SendZonepoints = 0x40b4; // u32 count + ZonePoint_Entry (24 bytes) each: the teleport table
	constexpr uint16_t ZoneChange = 0x40a3;     // ZoneChange_Struct, 76 bytes, both directions
	constexpr uint16_t Stamina = 0x4157;
	constexpr uint16_t MoneyUpdate = 0x4008;
	constexpr uint16_t TradeMoneyUpdate = 0x413d; // TradeMoney_Struct {u16, u8 type (0 copper .. 3 platinum), u8, i32 amount}: coins added
	constexpr uint16_t Emote = 0x4015;
	constexpr uint16_t RandomReq = 0x41e7;   // {u32 low, u32 high}: /random
	constexpr uint16_t RandomReply = 0x4016; // RandomReply_Struct {u32 low, u32 high, u32 result, char name[64]}
	// Death_Struct (20 bytes): {u16 spawn, u16 killer, u16 corpse, u8 level, u8, i16 spell @8, u8 attack
	// skill @10, u8, i32 damage @12, u8 is_pc, u8[3]}: CEverQuest::ReportDeath
	constexpr uint16_t Death = 0x404a;
	// {u32 on}: our auto attack (EverQuestinfo's __Attack) changed; DoPassageOfTime sends both, 2 first.
	constexpr uint16_t AutoAttack = 0x4151;
	constexpr uint16_t AutoAttack2 = 0x4161;
	// EQ_Character::UseSkill's: CombatAbility_Struct {u16 target, u16, u32 attack (100), u32 skill}
	// (DoMissileHit); Taunt {u16 target}; Disarm_Struct {i16 us, i16 target, i16 skill, u8 0xff, u8};
	// the rest empty.
	constexpr uint16_t CombatAbility = 0x4160;
	// UpdateFilters_Struct (68 bytes, 17 i32s): the /filter switches (CEverQuest_send_update_filters).
	constexpr uint16_t SetServerFilter = 0x41ff;
	constexpr uint16_t Taunt = 0x413b;
	constexpr uint16_t Disarm = 0x40aa;
	constexpr uint16_t Forage = 0x4094;
	constexpr uint16_t Mend = 0x419d;
	constexpr uint16_t SenseHeading = 0x4187;
	constexpr uint16_t SenseTraps = 0x4188;
	constexpr uint16_t DisarmTraps = 0x41f3;
	constexpr uint16_t InstillDoubt = 0x419c; // intimidation
	constexpr uint16_t Animation = 0x40a1;
	constexpr uint16_t WearChange = 0x4092;
	constexpr uint16_t LevelUpdate = 0x4198;
	constexpr uint16_t SkillUpdate = 0x4189;  // SkillUpdate_Struct {u32 skill, u32 value}: "You have become better at..."
	constexpr uint16_t SkillUpdate2 = 0x41be; // SkillUpdate2_Struct {u16 spawn, u16, i32 skill, i32 value}: the same, silently
	constexpr uint16_t BookPacket = 0x4165;      // Item_Struct: a book or note we now have, in its equipSlot
	constexpr uint16_t ContainerPacket = 0x4166; // Item_Struct: a bag
	constexpr uint16_t SummonedItem = 0x4178;    // Item_Struct: an item on the cursor
	constexpr uint16_t MoveItem = 0x412c;        // MoveItem_Struct {i32 from, to, qty}: to -1 deletes it
	// Trading: see ZoneSession::TradeRequest.
	constexpr uint16_t TradeRequest = 0x40d1;     // BeginTrade_Struct {u16 target, u16 source}
	constexpr uint16_t TradeRequestAck = 0x40e6;  // the same: yes to a trade
	constexpr uint16_t TradeRefused = 0x40d6;     // RefuseTrade_Struct {u16 target, u16 source, u8 reason, u8}
	constexpr uint16_t TradeAcceptClick = 0x40da; // ReadyTrade_Struct {u16 the other side, u8 flag, u8}
	constexpr uint16_t CancelTrade = 0x40db;      // CancelTrade_Struct {u16 target, u16 source}
	constexpr uint16_t FinishTrade = 0x40dc;      // empty: the trade's done
	constexpr uint16_t TradeCoins = 0x40e4;       // TradeMoney_Struct: the other side's coins
	constexpr uint16_t TradeItemPacket = 0x40df;  // TradeItemHeader_Struct {u16 from, i16 slot, u8 type, item}
	constexpr uint16_t TradeReset = 0x4010;       // empty
	// Groups: see ZoneSession::GroupInvite.
	constexpr uint16_t GroupInvite = 0x203e;       // GroupInvite_Struct {invitee[64], inviter[64], group[64], u8 pvp}: both ways
	constexpr uint16_t GroupInvite2 = 0x4040;      // the same, from a group's leader (the zone hands it on as 0x203e)
	constexpr uint16_t GroupFollow = 0x203d;       // GroupGeneric_Struct {inviter[64], invitee[64]}: yes; the zone's, to the inviter
	constexpr uint16_t GroupCancelInvite = 0x4041; // GroupCancel_Struct {inviter[64], invitee[64], u8 why}
	constexpr uint16_t GroupUpdate = 0x2026;       // GroupJoin_Struct, GroupUpdate_Struct or GroupGeneric_Struct2 by its action
	constexpr uint16_t GroupDisband = 0x4044;      // GroupGeneric_Struct {name[64], name[64]}
	// Looting: see ZoneSession::LootRequest.
	constexpr uint16_t LootRequest = 0x404e;    // ours: {u16 corpse}
	constexpr uint16_t MoneyOnCorpse = 0x4050;  // moneyOnCorpseStruct {u8 response, u8[3], u32 platinum, gold, silver, copper}
	constexpr uint16_t LootItemPacket = 0x4052; // Item_Struct: one of the corpse's, its equipSlot the corpse slot
	constexpr uint16_t LootItem = 0x40a0;       // LootingItem_Struct {u16 corpse, u16 us, u16 slot, u16, u32 auto}: both ways
	constexpr uint16_t EndLootRequest = 0x404f; // {u16 corpse}: ours, done; the zone's, the looting's over
	constexpr uint16_t LootComplete = 0x4145;   // empty: the looting's over
	// Merchants (Merchant_Click_Struct, Merchant_Sell_Struct, Merchant_Purchase_Struct): see ZoneSession::ShopRequest.
	constexpr uint16_t ShopRequest = 0x400b;
	constexpr uint16_t ShopInventoryPacket = 0x400c; // a count, a byte, deflated {u16 item class, Item_Struct} records
	// Item_Struct: an item put in one of our slots (the zone's ItemPacketTrade: bought, traded for,
	// given); eqgame's msg_snd_ieq, as OP_ItemPacket.
	constexpr uint16_t MerchantItemPacket = 0x4031;
	constexpr uint16_t ShopPlayerBuy = 0x4035;
	constexpr uint16_t ShopPlayerSell = 0x4027;
	constexpr uint16_t ShopEnd = 0x4037;             // ours: {u16 merchant, u16 us}
	constexpr uint16_t ShopEndConfirm = 0x4146;
	constexpr uint16_t ShopDelItem = 0x4038;         // Merchant_DelItem_Struct {u16 npc, u16, u8 slot, u8 type}
	constexpr uint16_t MoveCoin = 0x412d;          // MoveCoin_Struct {i32 from, to, from coin, to coin, amount}: ours
	constexpr uint16_t TradeSkillCombine = 0x4105; // Combine_Struct (32 bytes) ours; the zone's empty, done
	constexpr uint16_t ItemLinkResponse = 0x4264; // ours: ItemViewRequest_Struct {u16 id, char name[64]}, a link clicked; the zone's: its Item_Struct
	constexpr uint16_t TargetMouse = 0x4162;   // ClientTarget_Struct {u16 id}: we clicked a spawn (0 clears it)
	constexpr uint16_t TargetCommand = 0x41fe; // ClientTarget_Struct: /target asks; the zone answers with the target it set
	constexpr uint16_t GuildsList = 0x4192;    // GuildsList_Struct, as the world sends it: the guilds changed
	constexpr uint16_t LFGCommand = 0x41f0;    // LFG_Appearance_Struct {i16 spawn, i16, i32 on}: someone's LFG flag
	// MemorizeSpell_Struct {u32 slot, u32 spell, u32 scribing}: 0 scribed into a book slot, 1 memorized
	// into a gem, 2 forgotten from one; both ways. The zone's -1 only unlocks the spell bar.
	constexpr uint16_t MemorizeSpell = 0x4182;
	constexpr uint16_t SwapSpell = 0x41ce;   // SwapSpell_Struct {u32 from, to}: two book slots; the zone sends it back
	constexpr uint16_t DeleteSpell = 0x424a; // DeleteSpell_Struct {i16 slot, u16, u8 success, u8[3]}: both ways
	constexpr uint16_t BeginCast = 0x40a9;   // BeginCast_Struct {u16 caster, u16 spell, u32 cast time ms}: CEverQuest_StartCasting
	// Action_Struct (36 bytes): {u16 target, u16 source, u16 level, u16, i32 instrument_mod, float force,
	// heading, pitch, u8 type (231 a spell), ..., u16 spell @30, u8, u8 flags @33 (4: it landed)}: EQPlayer::IWasHit
	constexpr uint16_t Action = 0x4046;
	// Damage_Struct (24 bytes): {u16 target, u16 source, u8 type @4 (231 a spell), u8, i16 spell @6,
	// i32 damage @8, float force, heading, pitch}: msg_successful_hit's messages
	constexpr uint16_t Damage = 0x4058;
	// Arrow_Struct (112 bytes): a missile, EQMissile's own first 0x70 bytes (EQMissile::EQMissile from a
	// packet): {u32 type, u32, u32, float y @12, x, z, heading @24, pitch, speed @32, velocity y, x, z,
	// heading rate @48, pitch rate, climb @56, u32, u16 source @64, u16 target, u16 level, u16 offense,
	// u32 object @72 (a spell when the kind's 9), u8 light @76, u8, u8 aimed @78, u8 kind @79 (an item
	// type, 9 a spell), u8 skill, char model[16] @81}. Ours goes out as an archer's or thrower's shot.
	constexpr uint16_t Projectile = 0x4045;
	// CastSpell_Struct (12 bytes): {u16 gem (or 10, an item), u16 spell, u16 inventory slot (0xffff
	// none), u16 target, u32 spell_crc (the zone doesn't look at it)}: EQ_Character::CastSpell. Empty,
	// to the zone: stop the cast (StopSpellCast).
	constexpr uint16_t CastSpell = 0x417e;
	// SpellBuffFade_Struct (20 bytes): {u16 spawn, SpellBuff_Struct (10), u16 slot, u16 0, u32 how}.
	// The zone's: a buff of ours changed (how 0: the one with its spell and type takes it; 3: the
	// one in its slot does) or gone (1: the one with its spell and type, as if it wore off). Ours:
	// one we took off (RemovePCAffectex, NotifyPCAffectChange); the zone takes off its spell.
	constexpr uint16_t Buff = 0x4132;
	constexpr uint16_t TokenText = 0x4235; // TokenText_Struct {i16 eqstr id, i16 color, i16 from world}
	// Consider_Struct (24 bytes): {u16 us, u16 target, u32 faction (1 ally .. 9 scowls), u32 level
	// (its con color), i32 hp, i32 max hp, u8 pvpcon, u8[3]}. Ours has the two ids; the zone fills it in.
	constexpr uint16_t Consider = 0x4137;
	constexpr uint16_t ConsiderCorpse = 0x4234; // the same, about a corpse; the zone answers in text
	constexpr uint16_t Assist = 0x4200; // EntityId_Struct {i16 id}: ours who to assist; the zone's their target
}

struct OpcodeEntry
{
	uint16_t value;
	const char *name;
};

inline const OpcodeEntry LoginOpcodes[] = {
	{LoginOp::SessionReady, "OP_SessionReady"},
	{LoginOp::LoginPC, "OP_LoginPC"},
	{LoginOp::ClientError, "OP_ClientError"},
	{LoginOp::LoginDisconnect, "OP_LoginDisconnect"},
	{LoginOp::LoginAccepted, "OP_LoginAccepted"},
	{LoginOp::ServerListRequest, "OP_ServerList"},
	{LoginOp::PlayEverquest, "OP_PlayEverquest"},
	{LoginOp::LoginUnknown1, "OP_LoginUnknown1"},
	{LoginOp::LoginUnknown2, "OP_LoginUnknown2"},
	{LoginOp::ServerName, "OP_ServerName"},
	{LoginOp::LoginBanner, "OP_LoginBanner"},
	{LoginOp::LoginComplete, "OP_LoginComplete"},
	{0, nullptr},
};

inline const OpcodeEntry WorldOpcodes[] = {
	{WorldOp::SendLoginInfo, "OP_SendLoginInfo"},
	{WorldOp::ApproveWorld, "OP_ApproveWorld"},
	{WorldOp::BadLogin, "OP_BadLogin"},
	{WorldOp::LogServer, "OP_LogServer"},
	{WorldOp::EncryptKey, "OP_EncryptKey"},
	{WorldOp::MOTD, "OP_MOTD"},
	{WorldOp::ExpansionInfo, "OP_ExpansionInfo"},
	{WorldOp::SendCharInfo, "OP_SendCharInfo"},
	{WorldOp::EnterWorld, "OP_EnterWorld"},
	{WorldOp::ZoneServerInfo, "OP_ZoneServerInfo"},
	{WorldOp::ZoneUnavail, "OP_ZoneUnavail"},
	{WorldOp::SetChatServer, "OP_SetChatServer"},
	{WorldOp::GuildsList, "OP_GuildsList"},
	{WorldOp::WorldLogout, "OP_WorldLogout"},
	{WorldOp::DeleteCharacter, "OP_DeleteCharacter"},
	{WorldOp::ApproveName, "OP_ApproveName"},
	{WorldOp::CharacterCreate, "OP_CharacterCreate"},
	{0, nullptr},
};

inline const OpcodeEntry ZoneOpcodes[] = {
	{ZoneOp::DataRate, "OP_DataRate"},
	{ZoneOp::ZoneEntry, "OP_ZoneEntry"},
	{ZoneOp::PlayerProfile, "OP_PlayerProfile"},
	{ZoneOp::ReqNewZone, "OP_ReqNewZone"},
	{ZoneOp::NewZone, "OP_NewZone"},
	{ZoneOp::ReqClientSpawn, "OP_ReqClientSpawn"},
	{ZoneOp::SendExpZonein, "OP_SendExpZonein"},
	{ZoneOp::ClientUpdate, "OP_ClientUpdate"},
	{ZoneOp::ZoneSpawns, "OP_ZoneSpawns"},
	{ZoneOp::NewSpawn, "OP_NewSpawn"},
	{ZoneOp::DeleteSpawn, "OP_DeleteSpawn"},
	{ZoneOp::MobUpdate, "OP_MobUpdate"},
	{ZoneOp::HPUpdate, "OP_HPUpdate"},
	{ZoneOp::ManaChange, "OP_ManaChange"},
	{ZoneOp::SpawnAppearance, "OP_SpawnAppearance"},
	{ZoneOp::TimeOfDay, "OP_TimeOfDay"},
	{ZoneOp::Weather, "OP_Weather"},
	{ZoneOp::ChannelMessage, "OP_ChannelMessage"},
	{ZoneOp::SpecialMesg, "OP_SpecialMesg"},
	{ZoneOp::FormattedMessage, "OP_FormattedMessage"},
	{ZoneOp::Camp, "OP_Camp"},
	{ZoneOp::Jump, "OP_Jump"},
	{ZoneOp::Logout, "OP_Logout"},
	{ZoneOp::LogoutReply, "OP_LogoutReply"},
	{ZoneOp::ZoneInAvatarSet, "OP_ZoneInAvatarSet"},
	{ZoneOp::ExpUpdate, "OP_ExpUpdate"},
	{ZoneOp::AAExpUpdate, "OP_AAExpUpdate"},
	{ZoneOp::CharInventory, "OP_CharInventory"},
	{ZoneOp::ItemPacket, "OP_ItemPacket"},
	{ZoneOp::SpawnDoor, "OP_SpawnDoor"},
	{ZoneOp::MoveDoor, "OP_MoveDoor"},
	{ZoneOp::ClickDoor, "OP_ClickDoor"},
	{ZoneOp::GroundSpawn, "OP_GroundSpawn"},
	{ZoneOp::ClickObject, "OP_ClickObject"},
	{ZoneOp::ClickObjectAction, "OP_ClickObjectAction"},
	{ZoneOp::ObjectItemPacket, "OP_ObjectItemPacket"},
	{ZoneOp::ClearObject, "OP_ClearObject"},
	{ZoneOp::SendZonepoints, "OP_SendZonepoints"},
	{ZoneOp::ZoneChange, "OP_ZoneChange"},
	{ZoneOp::Stamina, "OP_Stamina"},
	{ZoneOp::MoneyUpdate, "OP_MoneyUpdate"},
	{ZoneOp::TradeMoneyUpdate, "OP_TradeMoneyUpdate"},
	{ZoneOp::Emote, "OP_Emote"},
	{ZoneOp::RandomReq, "OP_RandomReq"},
	{ZoneOp::RandomReply, "OP_RandomReply"},
	{ZoneOp::Death, "OP_Death"},
	{ZoneOp::AutoAttack, "OP_AutoAttack"},
	{ZoneOp::AutoAttack2, "OP_AutoAttack2"},
	{ZoneOp::CombatAbility, "OP_CombatAbility"},
	{ZoneOp::SetServerFilter, "OP_SetServerFilter"},
	{ZoneOp::Taunt, "OP_Taunt"},
	{ZoneOp::Disarm, "OP_Disarm"},
	{ZoneOp::Forage, "OP_Forage"},
	{ZoneOp::Mend, "OP_Mend"},
	{ZoneOp::SenseHeading, "OP_SenseHeading"},
	{ZoneOp::SenseTraps, "OP_SenseTraps"},
	{ZoneOp::DisarmTraps, "OP_DisarmTraps"},
	{ZoneOp::InstillDoubt, "OP_InstillDoubt"},
	{ZoneOp::Animation, "OP_Animation"},
	{ZoneOp::WearChange, "OP_WearChange"},
	{ZoneOp::LevelUpdate, "OP_LevelUpdate"},
	{ZoneOp::SkillUpdate, "OP_SkillUpdate"},
	{ZoneOp::SkillUpdate2, "OP_SkillUpdate2"},
	{ZoneOp::BookPacket, "OP_BookPacket"},
	{ZoneOp::ContainerPacket, "OP_ContainerPacket"},
	{ZoneOp::SummonedItem, "OP_SummonedItem"},
	{ZoneOp::ItemLinkResponse, "OP_ItemLinkResponse"},
	{ZoneOp::MoveItem, "OP_MoveItem"},
	{ZoneOp::TradeSkillCombine, "OP_TradeSkillCombine"},
	{ZoneOp::MoveCoin, "OP_MoveCoin"},
	{ZoneOp::TradeRequest, "OP_TradeRequest"},
	{ZoneOp::TradeRequestAck, "OP_TradeRequestAck"},
	{ZoneOp::TradeRefused, "OP_TradeRefused"},
	{ZoneOp::TradeAcceptClick, "OP_TradeAcceptClick"},
	{ZoneOp::CancelTrade, "OP_CancelTrade"},
	{ZoneOp::FinishTrade, "OP_FinishTrade"},
	{ZoneOp::TradeCoins, "OP_TradeCoins"},
	{ZoneOp::TradeItemPacket, "OP_TradeItemPacket"},
	{ZoneOp::TradeReset, "OP_TradeReset"},
	{ZoneOp::GroupInvite, "OP_GroupInvite"},
	{ZoneOp::GroupInvite2, "OP_GroupInvite2"},
	{ZoneOp::GroupFollow, "OP_GroupFollow"},
	{ZoneOp::GroupCancelInvite, "OP_GroupCancelInvite"},
	{ZoneOp::GroupUpdate, "OP_GroupUpdate"},
	{ZoneOp::GroupDisband, "OP_GroupDisband"},
	{ZoneOp::LootRequest, "OP_LootRequest"},
	{ZoneOp::MoneyOnCorpse, "OP_MoneyOnCorpse"},
	{ZoneOp::LootItemPacket, "OP_LootItemPacket"},
	{ZoneOp::LootItem, "OP_LootItem"},
	{ZoneOp::EndLootRequest, "OP_EndLootRequest"},
	{ZoneOp::LootComplete, "OP_LootComplete"},
	{ZoneOp::ShopRequest, "OP_ShopRequest"},
	{ZoneOp::ShopInventoryPacket, "OP_ShopInventoryPacket"},
	{ZoneOp::MerchantItemPacket, "OP_MerchantItemPacket"},
	{ZoneOp::ShopPlayerBuy, "OP_ShopPlayerBuy"},
	{ZoneOp::ShopPlayerSell, "OP_ShopPlayerSell"},
	{ZoneOp::ShopEnd, "OP_ShopEnd"},
	{ZoneOp::ShopEndConfirm, "OP_ShopEndConfirm"},
	{ZoneOp::ShopDelItem, "OP_ShopDelItem"},
	{ZoneOp::TargetMouse, "OP_TargetMouse"},
	{ZoneOp::TargetCommand, "OP_TargetCommand"},
	{ZoneOp::GuildsList, "OP_GuildsList"},
	{ZoneOp::LFGCommand, "OP_LFGCommand"},
	{ZoneOp::MemorizeSpell, "OP_MemorizeSpell"},
	{ZoneOp::SwapSpell, "OP_SwapSpell"},
	{ZoneOp::DeleteSpell, "OP_DeleteSpell"},
	{ZoneOp::BeginCast, "OP_BeginCast"},
	{ZoneOp::Action, "OP_Action"},
	{ZoneOp::Damage, "OP_Damage"},
	{ZoneOp::Projectile, "OP_Projectile"},
	{ZoneOp::CastSpell, "OP_CastSpell"},
	{ZoneOp::Buff, "OP_Buff"},
	{ZoneOp::TokenText, "OP_TokenText"},
	{ZoneOp::Consider, "OP_Consider"},
	{ZoneOp::ConsiderCorpse, "OP_ConsiderCorpse"},
	{ZoneOp::Assist, "OP_Assist"},
	{0, nullptr},
};
