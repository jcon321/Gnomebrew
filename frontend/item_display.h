#pragma once

// The item display window's text (CItemDisplayWnd::SetItem, @00423640): STML, a line per <BR>.
// A common item:
//   - LORE: and its lore text, when the world auto identifies (OP_LogServer's @00799764);
//   - CreateEquipmentStatusString (@004257ce): MAGIC ITEM, LORE ITEM or PENDING LORE (the lore's
//     '*' or '~'), ARTIFACT or SUMMONED (its next '#' or '&'), NO DROP (CanDrop's 0x29a: a no drop
//     item or a bag holding one) and NO RENT;
//   - Slot: the slots it's worn in, uppercased, the second ear, wrist and finger left out;
//   - charges (an expendable's, or one with IsStackable 2..4 and MaxCharges);
//   - a scroll's level needed (for our class), its spell's skill and mana;
//   - a weapon's skill (EQPlayer_SkillUsed by its ItemType) and delay; a compass's direction (by
//     our heading); an instrument's kind;
//   - AC, and a weapon's DMG with Dmg Bonus (DisplayPCBonus2HDam), each brought down by
//     EQ_Character::AntiTwinkAdj (@004bdbab) below its recommended level or skill and shown red
//     with the full value after it;
//   - elemental and bane damage, a skill mod, a faction modifier;
//   - the stats, HP and mana, and the saves (CreateModString @004256b7: "%+d", or the adjusted
//     value red and the full one after), lines broken as eqgame breaks them;
//   - the recommended and required levels, the effect (its spell and how it goes off, with the
//     item's casting time) and the focus effect;
//   - a food or drink's size (GetItemLevelDescription, by its duration), the weight, range and
//     size, the classes (GetClassThreeLetterCode, ALL for all 15), the races (14) and deities.
// A container: open or closed, the status, the weight and its weight reduction, the capacity and
// the largest size it holds. A book or note: open or closed (unrolled or rolled up), the status,
// the weight and size.
// What we don't have: on FV's rules (@0079975c) everything drops but for a list of item ids
// (EQ_Item::IsSpecialNoDrop) that isn't here, so nothing says NO DROP; bags are always closed
// (no bag windows yet).

#include <string>

class GameValues;
struct SpellInfo;
struct ZoneItem;

std::string ItemDisplayText(const GameValues &values, const ZoneItem &item);
// CItemDisplayWnd::SetSpell (@00425957): a spell's title ("Spell:  " and its name, eqstr 3101) and,
// with `details` (the book and the gems; the buff window passes none), its text: CANNOT USE (3104)
// when our class never gets it, else its level for our class (506), its skill (3105), the mana
// for a casting skill's spell (3006), and but for a bard its cast time (3107, "%4.1f" seconds).
std::string SpellDisplayTitle(const GameValues &values, const SpellInfo &spell);
std::string SpellDisplayText(const GameValues &values, const SpellInfo &spell, bool details);
// CEverQuest_GetRaceDesc2 (@0052db3a): a race's name, "UNKNOWN RACE" past its table.
std::string RaceDescription(const GameValues &values, int race);
