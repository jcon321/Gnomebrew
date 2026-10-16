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
	{0, nullptr},
};
