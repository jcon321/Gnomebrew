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
	constexpr uint16_t ExpUpdate = 0x4199;
	constexpr uint16_t CharInventory = 0x41f6;
	constexpr uint16_t ItemPacket = 0x4164;
	constexpr uint16_t SpawnDoor = 0x41f7;     // u16 count, then the Door_Structs (44 bytes each) deflated
	constexpr uint16_t MoveDoor = 0x408e;      // {u8 door id, u8 action}: EQSwitch::ChangeState's state
	constexpr uint16_t ClickDoor = 0x408d;     // ClickDoor_Struct, 8 bytes (EQSwitch::UseSwitch)
	constexpr uint16_t GroundSpawn = 0x402c;
	constexpr uint16_t SendZonepoints = 0x40b4; // u32 count + ZonePoint_Entry (24 bytes) each: the teleport table
	constexpr uint16_t ZoneChange = 0x40a3;     // ZoneChange_Struct, 76 bytes, both directions
	constexpr uint16_t Stamina = 0x4157;
	constexpr uint16_t MoneyUpdate = 0x4008;
	constexpr uint16_t Emote = 0x4015;
	constexpr uint16_t Death = 0x404a;
	constexpr uint16_t Animation = 0x40a1;
	constexpr uint16_t WearChange = 0x4092;
	constexpr uint16_t LevelUpdate = 0x4198;
	constexpr uint16_t SkillUpdate = 0x4189;
	constexpr uint16_t BookPacket = 0x4165;      // Item_Struct: a book or note we now have, in its equipSlot
	constexpr uint16_t ContainerPacket = 0x4166; // Item_Struct: a bag
	constexpr uint16_t SummonedItem = 0x4178;    // Item_Struct: an item on the cursor
	constexpr uint16_t MoveItem = 0x412c;        // MoveItem_Struct {i32 from, to, qty}: to -1 deletes it
	constexpr uint16_t TargetMouse = 0x4162;   // ClientTarget_Struct {u16 id}: we clicked a spawn (0 clears it)
	constexpr uint16_t TargetCommand = 0x41fe; // ClientTarget_Struct: /target asks; the zone answers with the target it set
	constexpr uint16_t GuildsList = 0x4192;    // GuildsList_Struct, as the world sends it: the guilds changed
	constexpr uint16_t LFGCommand = 0x41f0;    // LFG_Appearance_Struct {i16 spawn, i16, i32 on}: someone's LFG flag
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
	{ZoneOp::CharInventory, "OP_CharInventory"},
	{ZoneOp::ItemPacket, "OP_ItemPacket"},
	{ZoneOp::SpawnDoor, "OP_SpawnDoor"},
	{ZoneOp::MoveDoor, "OP_MoveDoor"},
	{ZoneOp::ClickDoor, "OP_ClickDoor"},
	{ZoneOp::GroundSpawn, "OP_GroundSpawn"},
	{ZoneOp::SendZonepoints, "OP_SendZonepoints"},
	{ZoneOp::ZoneChange, "OP_ZoneChange"},
	{ZoneOp::Stamina, "OP_Stamina"},
	{ZoneOp::MoneyUpdate, "OP_MoneyUpdate"},
	{ZoneOp::Emote, "OP_Emote"},
	{ZoneOp::Death, "OP_Death"},
	{ZoneOp::Animation, "OP_Animation"},
	{ZoneOp::WearChange, "OP_WearChange"},
	{ZoneOp::LevelUpdate, "OP_LevelUpdate"},
	{ZoneOp::SkillUpdate, "OP_SkillUpdate"},
	{ZoneOp::BookPacket, "OP_BookPacket"},
	{ZoneOp::ContainerPacket, "OP_ContainerPacket"},
	{ZoneOp::SummonedItem, "OP_SummonedItem"},
	{ZoneOp::MoveItem, "OP_MoveItem"},
	{ZoneOp::TargetMouse, "OP_TargetMouse"},
	{ZoneOp::TargetCommand, "OP_TargetCommand"},
	{ZoneOp::GuildsList, "OP_GuildsList"},
	{ZoneOp::LFGCommand, "OP_LFGCommand"},
	{0, nullptr},
};
