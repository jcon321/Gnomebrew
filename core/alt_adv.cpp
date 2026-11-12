#include "alt_adv.h"

#include "eqstr.h"
#include "spell_file.h"

#include <algorithm>
#include <iterator>

namespace AltAdv
{
	// AltAdvEntry_initializeAltAdvancement (@004b4c36): every entry starts as {-1, -1, ..., level
	// 51, cost 1, 1 rank, cost_inc 1, reuse 0, spell -1}, then the switch on its id sets its own.
	// Read out of the decompile by running the switch for each id.
	const Ability Abilities[Count] = {
	// {label1, label2, name, desc, min_level, cost, required, required_rank, max_rank, cost_inc, enabled, spell,
	//  category, cost_type, pop_cost_inc, level_step, unknown_2e, reuse, level_inc}
	{1216, -1, 1216, 1216, 51, 0, 0, 0, 1, 1, 0, -1, 0, 0, 0, 0, 0, 10000, 0}, // 0 None
	{-1, -1, 13500, 13501, 51, 1, 0, 0, 5, 1, 1, -1, 1, 0, 0, 0, 0, 0, 0}, // 1 Innate Strength
	{-1, -1, 13502, 13503, 51, 1, 0, 0, 5, 1, 1, -1, 1, 0, 0, 0, 0, 0, 0}, // 2 Innate Stamina
	{-1, -1, 13504, 13505, 51, 1, 0, 0, 5, 1, 1, -1, 1, 0, 0, 0, 0, 0, 0}, // 3 Innate Agility
	{-1, -1, 13506, 13507, 51, 1, 0, 0, 5, 1, 1, -1, 1, 0, 0, 0, 0, 0, 0}, // 4 Innate Dexterity
	{-1, -1, 13508, 13509, 51, 1, 0, 0, 5, 1, 1, -1, 1, 0, 0, 0, 0, 0, 0}, // 5 Innate Intelligence
	{-1, -1, 13510, 13511, 51, 1, 0, 0, 5, 1, 1, -1, 1, 0, 0, 0, 0, 0, 0}, // 6 Innate Wisdom
	{-1, -1, 13512, 13513, 51, 1, 0, 0, 5, 1, 1, -1, 1, 0, 0, 0, 0, 0, 0}, // 7 Innate Charisma
	{-1, -1, 13514, 13515, 51, 1, 0, 0, 5, 1, 1, -1, 1, 0, 0, 0, 0, 0, 0}, // 8 Innate Fire Protection
	{-1, -1, 13516, 13517, 51, 1, 0, 0, 5, 1, 1, -1, 1, 0, 0, 0, 0, 0, 0}, // 9 Innate Cold Protection
	{-1, -1, 13518, 13519, 51, 1, 0, 0, 5, 1, 1, -1, 1, 0, 0, 0, 0, 0, 0}, // 10 Innate Magic Protection
	{-1, -1, 13520, 13521, 51, 1, 0, 0, 5, 1, 1, -1, 1, 0, 0, 0, 0, 0, 0}, // 11 Innate Poison Protection
	{-1, -1, 13522, 13523, 51, 1, 0, 0, 5, 1, 1, -1, 1, 0, 0, 0, 0, 0, 0}, // 12 Innate Disease Protection
	{-1, -1, 13524, 13525, 51, 1, 0, 0, 3, 1, 1, -1, 1, 0, 0, 0, 0, 0, 0}, // 13 Innate Run Speed
	{-1, -1, 13526, 13527, 51, 1, 0, 0, 3, 1, 1, -1, 1, 0, 0, 0, 1, 0, 0}, // 14 Innate Regeneration
	{-1, -1, 13528, 13529, 51, 1, 0, 0, 3, 1, 1, -1, 1, 0, 0, 0, 0, 0, 0}, // 15 Innate Metabolism
	{-1, -1, 13530, 13531, 51, 1, 0, 0, 3, 1, 1, -1, 1, 0, 0, 0, 0, 0, 0}, // 16 Innate Lung Capacity
	{-1, -1, 13532, 13533, 51, 1, 0, 0, 3, 1, 1, -1, 1, 0, 0, 0, 0, 0, 0}, // 17 First Aid
	{-1, -1, 13534, 13535, 55, 1, -1, 0, 3, 2, 1, -1, 2, 0, 0, 0, 0, 0, 0}, // 18 Healing Adept
	{-1, -1, 13536, 13537, 55, 1, -1, 0, 3, 2, 1, -1, 2, 0, 0, 0, 0, 0, 0}, // 19 Healing Gift
	{-1, -1, 13538, 13539, 55, 1, -1, 0, 3, 2, 1, -1, 2, 0, 0, 0, 0, 0, 0}, // 20 Spell Casting Mastery
	{-1, -1, 13540, 13541, 55, 1, -1, 0, 3, 2, 1, -1, 2, 0, 0, 0, 0, 0, 0}, // 21 Spell Casting Reinforcement
	{-1, -1, 13542, 13543, 55, 1, -1, 0, 3, 2, 1, -1, 2, 0, 0, 0, 1, 0, 0}, // 22 Mental Clarity
	{-1, -1, 13544, 13545, 55, 1, -1, 0, 3, 2, 1, -1, 2, 0, 0, 0, 0, 0, 0}, // 23 Spell Casting Fury
	{-1, -1, 13546, 13547, 55, 1, -1, 0, 3, 2, 1, -1, 2, 0, 0, 0, 0, 0, 0}, // 24 Channeling Focus
	{-1, -1, 13548, 13549, 55, 1, -1, 0, 3, 2, 1, -1, 2, 0, 0, 0, 0, 0, 0}, // 25 Spell Casting Subtlety
	{-1, -1, 13550, 13551, 55, 1, -1, 0, 3, 2, 1, -1, 2, 0, 0, 0, 0, 0, 0}, // 26 Spell Casting Expertise
	{-1, -1, 13552, 13553, 55, 1, -1, 0, 3, 2, 1, -1, 2, 0, 0, 0, 0, 0, 0}, // 27 Spell Casting Deftness
	{-1, -1, 13554, 13555, 55, 1, -1, 0, 3, 2, 1, -1, 2, 0, 0, 0, 0, 0, 0}, // 28 Natural Durability
	{-1, -1, 13556, 13557, 55, 1, -1, 0, 3, 2, 1, -1, 2, 0, 0, 0, 0, 0, 0}, // 29 Natural Healing
	{-1, -1, 13558, 13559, 55, 1, -1, 0, 3, 2, 1, -1, 2, 0, 0, 0, 0, 0, 0}, // 30 Combat Fury
	{-1, -1, 13560, 13561, 55, 1, -1, 0, 3, 2, 1, -1, 2, 0, 0, 0, 0, 0, 0}, // 31 Fear Resistance
	{-1, -1, 13562, 13563, 55, 1, -1, 0, 3, 2, 1, -1, 2, 0, 0, 0, 0, 0, 0}, // 32 Finishing Blow
	{-1, -1, 13564, 13565, 55, 1, -1, 0, 3, 2, 1, -1, 2, 0, 0, 0, 0, 0, 0}, // 33 Combat Stability
	{-1, -1, 13566, 13567, 55, 1, -1, 0, 3, 2, 1, -1, 2, 0, 0, 0, 0, 0, 0}, // 34 Combat Agility
	{13811, 13812, 13809, 13810, 59, 9, -2, 0, 1, 1, 1, -1, 3, 0, 0, 0, 0, 4320, 0}, // 35 Mass Group Buff
	{13570, 13571, 13568, 13569, 59, 5, 19, 3, 1, 1, 1, 2738, 3, 0, 0, 0, 0, 129600, 0}, // 36 Divine Resurrection
	{13574, 13575, 13572, 13573, 59, 3, -2, 0, 1, 1, 1, 2739, 3, 0, 0, 0, 0, 7, 0}, // 37 Innate Invis to Undead
	{13578, 13579, 13576, 13577, 59, 5, 19, 3, 1, 1, 1, 2740, 3, 0, 0, 0, 0, 4320, 0}, // 38 Celestial Regeneration
	{13582, 13583, 13580, 13581, 59, 6, -2, 0, 1, 1, 1, 2741, 3, 0, 0, 0, 0, 8640, 0}, // 39 Bestow Divine Aura
	{13586, 13587, 13584, 13585, 59, 1, -2, 0, 3, 3, 1, -1, 3, 0, 0, 0, 0, 4320, 0}, // 40 Turn Undead
	{13590, -1, 13588, 13589, 59, 5, 19, 3, 1, 1, 1, 2742, 3, 0, 0, 0, 0, 4320, 0}, // 41 Purify Soul
	{-1, -1, 13592, 13593, 59, 1, -2, 0, 3, 3, 1, -1, 3, 0, 0, 0, 0, 0, 0}, // 42 Quick Evacuation
	{13596, -1, 13594, 13595, 59, 6, -2, 0, 1, 1, 1, 2771, 3, 0, 0, 0, 0, 4320, 0}, // 43 Exodus
	{-1, -1, 13598, 13599, 59, 1, 23, 3, 3, 3, 1, -1, 3, 0, 0, 0, 0, 0, 0}, // 44 Quick Damage
	{-1, -1, 13600, 13601, 59, 5, -2, 0, 1, 1, 1, -1, 3, 0, 0, 0, 0, 0, 0}, // 45 Enhanced Root
	{13604, 13605, 13602, 13603, 59, 9, -2, 0, 1, 1, 1, -1, 3, 0, 0, 0, 0, 4320, 0}, // 46 Dire Charm
	{13608, 13609, 13606, 13607, 59, 5, 224, 3, 1, 1, 1, 2749, 3, 0, 0, 0, 0, 180, 0}, // 47 Cannibalization
	{-1, -1, 13610, 13611, 59, 1, -2, 0, 3, 3, 1, -1, 3, 0, 0, 0, 0, 0, 0}, // 48 Quick Buff
	{-1, -1, 13612, 13613, 59, 1, -2, 0, 3, 3, 1, -1, 3, 0, 0, 0, 0, 0, 0}, // 49 Alchemy Mastery
	{13616, 13617, 13614, 13615, 59, 5, -2, 0, 1, 1, 1, 2750, 3, 0, 0, 0, 0, 7200, 0}, // 50 Rabid Bear
	{13620, 13621, 13618, 13619, 59, 5, 224, 3, 1, 1, 1, 2751, 3, 0, 0, 0, 1, 8640, 0}, // 51 Mana Burn
	{13624, 13625, 13622, 13623, 59, 9, -2, 0, 1, 1, 1, 2758, 3, 0, 0, 0, 0, 420, 0}, // 52 Improved Familiar
	{13628, 13629, 13626, 13627, 59, 6, -2, 0, 1, 1, 1, 2734, 3, 0, 0, 0, 0, 4320, 0}, // 53 Nexus Gate
	{1216, -1, 1216, 1216, 51, 0, 0, 0, 1, 1, 0, -1, 0, 0, 0, 0, 0, 10000, 0}, // 54 None
	{-1, -1, 13630, 13631, 59, 3, -2, 0, 1, 1, 1, -1, 3, 0, 0, 0, 0, 0, 0}, // 55 Permanent Illusion
	{-1, -1, 13632, 13633, 59, 1, -2, 0, 3, 3, 1, -1, 3, 0, 0, 0, 0, 0, 0}, // 56 Jewel Craft Mastery
	{13636, 13637, 13634, 13635, 59, 5, 224, 3, 1, 1, 1, 2753, 3, 0, 0, 0, 0, 8640, 0}, // 57 Gather Mana
	{13640, 13641, 13638, 13639, 59, 5, -2, 0, 1, 1, 1, 2752, 3, 0, 0, 0, 0, 2160, 0}, // 58 Mend Companion
	{-1, -1, 13642, 13643, 59, 1, -2, 0, 3, 3, 1, -1, 3, 0, 0, 0, 1, 0, 0}, // 59 Quick Summoning
	{13646, 13647, 13644, 13645, 59, 6, -2, 0, 1, 1, 1, 2754, 3, 0, 0, 0, 0, 4320, 0}, // 60 Frenzied Burnout
	{13650, 13651, 13648, 13649, 59, 1, -2, 0, 3, 3, 1, -1, 3, 0, 0, 0, 0, 4320, 0}, // 61 Elemental Form: Fire
	{13654, 13655, 13652, 13653, 59, 1, -2, 0, 3, 3, 1, -1, 3, 0, 0, 0, 0, 4320, 0}, // 62 Elemental Form: Water
	{13658, 13659, 13656, 13657, 59, 1, -2, 0, 3, 3, 1, -1, 3, 0, 0, 0, 0, 4320, 0}, // 63 Elemental Form: Earth
	{13662, 13663, 13660, 13661, 59, 1, -2, 0, 3, 3, 1, -1, 3, 0, 0, 0, 0, 4320, 0}, // 64 Elemental Form: Air
	{-1, -1, 13664, 13665, 59, 3, -2, 0, 1, 1, 1, -1, 3, 0, 0, 0, 0, 0, 0}, // 65 Improved Reclaim Energy
	{13668, 13669, 13666, 13667, 59, 5, 23, 3, 1, 1, 1, -1, 3, 0, 0, 0, 0, 4320, 0}, // 66 Turn Summoned
	{-1, -1, 13670, 13671, 59, 5, -2, 0, 1, 1, 1, -1, 3, 0, 0, 0, 0, 0, 0}, // 67 Elemental Pact
	{13674, 13675, 13672, 13673, 59, 9, -2, 0, 1, 1, 1, 2755, 3, 0, 0, 0, 0, 8640, 0}, // 68 Life Burn
	{13678, 13679, 13676, 13677, 59, 3, -2, 0, 1, 1, 1, 2756, 3, 0, 0, 0, 0, 4320, 0}, // 69 Dead Mesmerization
	{13682, 13683, 13680, 13681, 59, 5, -2, 0, 1, 1, 1, 2757, 3, 0, 0, 0, 0, 4320, 0}, // 70 Fearstorm
	{13686, 13687, 13684, 13685, 59, 3, -2, 0, 1, 1, 1, 2772, 3, 0, 0, 0, 0, 7, 0}, // 71 Flesh to Bone
	{13690, 13691, 13688, 13689, 59, 6, -2, 0, 1, 1, 1, 2764, 3, 0, 0, 0, 0, 4320, 0}, // 72 Call to Corpse
	{13694, 13695, 13692, 13693, 59, 9, -2, 0, 1, 1, 1, 2190, 3, 0, 0, 0, 0, 30, 0}, // 73 Divine Stun
	{-1, -1, 13696, 13697, 59, 5, -2, 0, 1, 1, 1, -1, 3, 0, 0, 0, 0, 0, 0}, // 74 Improved Lay on Hands
	{-1, -1, 13698, 13699, 59, 1, 30, 3, 3, 3, 1, -1, 3, 0, 0, 0, 0, 0, 0}, // 75 Slay Undead
	{13702, 13703, 13700, 13701, 59, 3, -2, 0, 1, 1, 1, 2775, 3, 0, 0, 0, 0, 4320, 0}, // 76 Act of Valor
	{13706, 13707, 13704, 13705, 59, 5, -2, 0, 1, 1, 1, 2874, 3, 0, 0, 0, 0, 1, 0}, // 77 Holy Steed
	{-1, -1, 13708, 13709, 59, 6, 31, 3, 1, 1, 1, -1, 3, 0, 0, 0, 0, 0, 0}, // 78 Fearless
	{-1, -1, 13710, 13711, 59, 6, -2, 0, 1, 1, 1, -1, 3, 0, 0, 0, 0, 0, 0}, // 79 2 Hand Bash
	{13714, 13715, 13712, 13713, 59, 5, -2, 0, 1, 1, 1, 2765, 3, 0, 0, 0, 0, 7, 0}, // 80 Innate Camouflage
	{-1, -1, 13716, 13717, 59, 9, -2, 0, 1, 1, 1, -1, 3, 0, 0, 0, 0, 0, 0}, // 81 Ambidexterity
	{-1, -1, 13718, 13719, 59, 1, -2, 0, 3, 3, 1, -1, 3, 0, 0, 0, 0, 0, 0}, // 82 Archery Mastery
	{-1, -1, 13720, 13721, 59, 1, -2, 0, 3, 3, 0, -1, 3, 0, 0, 0, 0, 0, 0}, // 83 Fletching/Bowyer Mastery
	{-1, -1, 13722, 13723, 59, 9, -2, 0, 1, 1, 1, -1, 3, 0, 0, 0, 0, 0, 0}, // 84 Endless Quiver
	{13726, 13727, 13724, 13725, 59, 5, -2, 0, 1, 1, 1, 2875, 3, 0, 0, 0, 0, 1, 0}, // 85 Unholy Steed
	{13730, 13731, 13728, 13729, 59, 6, -2, 0, 1, 1, 1, 2774, 3, 0, 0, 0, 0, 4320, 0}, // 86 Improved Harm Touch
	{13734, 13735, 13732, 13733, 59, 6, -2, 0, 1, 1, 1, 2766, 3, 0, 0, 0, 0, 4320, 0}, // 87 Leech Touch
	{13738, 13739, 13736, 13737, 60, 3, -2, 0, 1, 1, 0, -1, 3, 0, 0, 0, 0, 7, 0}, // 88 Death Peace
	{-1, -1, 13740, 13741, 59, 1, -2, 0, 3, 3, 1, -1, 3, 0, 0, 0, 0, 0, 0}, // 89 Soul Abrasion
	{-1, -1, 13742, 13743, 59, 1, -2, 0, 3, 3, 1, -1, 3, 0, 0, 0, 0, 0, 0}, // 90 Instrument Mastery
	{-1, -1, 13744, 13745, 59, 1, -2, 0, 3, 3, 1, -1, 3, 0, 0, 0, 0, 0, 0}, // 91 NOT USED
	{-1, -1, 13746, 13747, 59, 1, -2, 0, 3, 3, 1, -1, 3, 0, 0, 0, 0, 0, 0}, // 92 NOT USED
	{-1, -1, 13748, 13749, 59, 1, -2, 0, 3, 3, 1, -1, 3, 0, 0, 0, 0, 0, 0}, // 93 NOT USED
	{-1, -1, 13750, 13751, 59, 1, -2, 0, 3, 3, 1, -1, 3, 0, 0, 0, 0, 0, 0}, // 94 Jam Fest
	{13754, -1, 13752, 13753, 59, 9, -2, 0, 1, 1, 0, -1, 3, 0, 0, 0, 0, 4320, 0}, // 95 NOT USED
	{-1, -1, 13755, 13756, 59, 6, -2, 0, 1, 1, 0, -1, 3, 0, 0, 0, 0, 0, 0}, // 96 Sonic Call
	{-1, -1, 13757, 13758, 59, 1, 17, 3, 3, 3, 1, -1, 3, 0, 0, 0, 0, 0, 0}, // 97 Critical Mend
	{13761, 13762, 13759, 13760, 59, 9, -2, 0, 1, 1, 1, -1, 3, 0, 0, 0, 0, 4320, 0}, // 98 Purify Body
	{-1, -1, 13763, 13764, 59, 1, -2, 0, 3, 3, 0, -1, 3, 0, 0, 0, 0, 0, 0}, // 99 Chain Combo
	{-1, -1, 13765, 13766, 59, 1, -2, 0, 3, 3, 1, -1, 3, 0, 0, 0, 0, 0, 0}, // 100 Rapid Feign
	{-1, -1, 13767, 13768, 59, 1, -2, 0, 3, 3, 1, -1, 3, 0, 0, 0, 0, 0, 0}, // 101 Return Kick
	{13771, -1, 13769, 13770, 59, 9, -2, 0, 1, 1, 1, -1, 3, 0, 0, 0, 0, 4320, 0}, // 102 Escape
	{-1, -1, 13773, 13774, 59, 1, -2, 0, 3, 3, 1, -1, 3, 0, 0, 0, 0, 0, 0}, // 103 Poison Mastery
	{-1, -1, 13775, 13776, 59, 1, -2, 0, 3, 3, 1, -1, 3, 0, 0, 0, 0, 0, 0}, // 104 Double Riposte
	{-1, -1, 13777, 13778, 61, 1, -2, 0, 3, 3, 0, -1, 3, 0, 0, 0, 0, 0, 0}, // 105 Quick Hide
	{-1, -1, 13779, 13780, 59, 3, -2, 0, 1, 1, 0, -1, 3, 0, 0, 0, 0, 0, 0}, // 106 Quick Throw
	{13783, 13784, 13781, 13782, 59, 5, -2, 0, 1, 1, 1, -1, 3, 0, 0, 0, 0, 4320, 0}, // 107 Purge Poison
	{-1, -1, 13785, 13786, 59, 1, 30, 3, 3, 3, 1, -1, 3, 0, 0, 0, 0, 0, 0}, // 108 Flurry
	{13789, -1, 13787, 13788, 59, 5, -2, 0, 1, 1, 1, -1, 3, 0, 0, 0, 0, 600, 0}, // 109 Rampage
	{13793, 13794, 13791, 13792, 59, 5, -2, 0, 1, 1, 1, -1, 3, 0, 0, 0, 0, 900, 0}, // 110 Area Taunt
	{13797, -1, 13795, 13796, 59, 1, 31, 3, 3, 3, 1, -1, 3, 0, 0, 0, 0, 2160, 0}, // 111 Warcry
	{-1, -1, 13799, 13800, 59, 1, 17, 3, 3, 3, 1, -1, 3, 0, 0, 0, 0, 0, 0}, // 112 Bandage Wound
	{-1, -1, 13801, 13802, 59, 8, 21, 3, 1, 1, 1, -1, 3, 0, 0, 0, 0, 0, 0}, // 113 Spell Casting Reinforcement Mastery
	{-1, -1, 13803, 13804, 59, 1, 23, 3, 3, 3, 1, -1, 3, 0, 0, 0, 0, 0, 0}, // 114 Spell Casting Fury Mastery
	{-1, -1, 13805, 13806, 59, 1, -2, 0, 3, 3, 1, -1, 3, 0, 0, 0, 0, 0, 0}, // 115 Extended Notes
	{-1, -1, 13807, 13808, 59, 5, -2, 0, 1, 1, 1, 2767, 3, 0, 0, 0, 0, 0, 0}, // 116 Dragon Punch
	{13815, 13816, 13813, 13814, 59, 5, -2, 0, 1, 1, 1, 2748, 3, 0, 0, 0, 0, 4320, 0}, // 117 Strong Root
	{-1, -1, 13817, 13818, 59, 1, -2, 0, 3, 3, 1, -1, 3, 0, 0, 0, 0, 0, 0}, // 118 Singing Mastery
	{-1, -1, 13819, 13820, 59, 5, -2, 0, 1, 1, 1, -1, 3, 0, 0, 0, 0, 0, 0}, // 119 Body and Mind Rejuvenation
	{-1, -1, 13821, 13822, 59, 5, -2, 0, 1, 1, 1, -1, 3, 0, 0, 0, 0, 0, 0}, // 120 Physical Enhancement
	{-1, -1, 13823, 13824, 59, 1, -2, 0, 3, 3, 1, -1, 3, 0, 0, 0, 0, 0, 0}, // 121 Adv. Trap Negotiation
	{-1, -1, 13825, 13826, 59, 1, -2, 0, 3, 3, 1, -1, 3, 0, 0, 0, 0, 0, 0}, // 122 Acrobatics
	{-1, -1, 13827, 13828, 59, 3, -2, 0, 1, 1, 1, -1, 3, 0, 0, 0, 0, 0, 0}, // 123 Scribble Notes
	{-1, -1, 13829, 13830, 59, 6, -2, 0, 1, 1, 1, -1, 3, 0, 0, 0, 0, 0, 0}, // 124 Chaotic Stab
	{-1, -1, 13831, 13832, 59, 6, -2, 0, 1, 1, 1, -1, 3, 0, 0, 0, 0, 0, 0}, // 125 Pet Discipline
	{3300, 3301, 3298, 3299, 59, 5, -2, 0, 1, 1, 1, 3290, 3, 0, 0, 0, 0, 300, 0}, // 126 Hobble of Spirits
	{3304, 3305, 3302, 3303, 59, 4, -2, 0, 1, 1, 1, 3289, 3, 0, 0, 0, 0, 720, 0}, // 127 Frenzy of Spirit
	{3308, 3309, 3306, 3307, 59, 6, -2, 0, 1, 1, 1, 3291, 3, 0, 0, 0, 0, 900, 0}, // 128 Paragon of Spirit
	{-1, -1, 5546, 5550, 61, 1, 1, 5, 10, 0, 1, -1, 4, 2, 0, 2, 0, 0, 0}, // 129 Advanced Innate Strength
	{-1, -1, 5551, 5552, 61, 1, 2, 5, 10, 0, 1, -1, 4, 2, 0, 2, 0, 0, 0}, // 130 Advanced Innate Stamina
	{-1, -1, 5553, 5554, 61, 1, 3, 5, 10, 0, 1, -1, 4, 2, 0, 2, 0, 0, 0}, // 131 Advanced Innate Agility
	{-1, -1, 5555, 5556, 61, 1, 4, 5, 10, 0, 1, -1, 4, 2, 0, 2, 0, 0, 0}, // 132 Advanced Innate Dexterity
	{-1, -1, 5557, 5558, 61, 1, 5, 5, 10, 0, 1, -1, 4, 2, 0, 2, 0, 0, 0}, // 133 Advanced Innate Intelligence
	{-1, -1, 5559, 5563, 61, 1, 6, 5, 10, 0, 1, -1, 4, 2, 0, 2, 0, 0, 0}, // 134 Advanced Innate Wisdom
	{-1, -1, 5564, 5565, 61, 1, 7, 5, 10, 0, 1, -1, 4, 2, 0, 2, 0, 0, 0}, // 135 Advanced Innate Charisma
	{-1, -1, 5568, 5569, 61, 1, 8, 5, 10, 0, 1, -1, 4, 2, 0, 2, 0, 0, 0}, // 136 Warding of Solusek
	{-1, -1, 5570, 5571, 61, 1, 9, 5, 10, 0, 1, -1, 4, 2, 0, 2, 0, 0, 0}, // 137 Blessing of E'ci
	{-1, -1, 5566, 5567, 61, 1, 10, 5, 10, 0, 1, -1, 4, 2, 0, 2, 0, 0, 0}, // 138 Marr's Protection
	{-1, -1, 5572, 5573, 61, 1, 11, 5, 10, 0, 1, -1, 4, 2, 0, 2, 0, 0, 0}, // 139 Shroud of The Faceless
	{-1, -1, 5574, 5575, 61, 1, 12, 5, 10, 0, 1, -1, 4, 2, 0, 2, 0, 0, 0}, // 140 Bertoxxulous' Gift
	{-1, -1, 3286, 3287, 51, 3, 0, 0, 6, 1, 1, -1, 1, 0, 0, 0, 0, 0, 0}, // 141 New Tanaan Crafting Mastery
	{-1, -1, 5547, 5548, 61, 2, 0, 0, 5, 0, 1, -1, 4, 2, 0, 1, 0, 0, 0}, // 142 Planar Power
	{-1, -1, 5549, 5560, 61, 3, 0, 0, 3, 0, 1, -1, 4, 2, 0, 1, 0, 0, 2}, // 143 Planar Durability
	{-1, -1, 5561, 5562, 61, 3, 0, 0, 5, 0, 1, -1, 4, 2, 0, 1, 0, 0, 0}, // 144 Innate Enlightenment
	{-1, -1, 5576, 5577, 62, 3, 20, 3, 3, 0, 0, -1, 5, 2, 0, 1, 0, 0, 0}, // 145 Advanced Spell Casting Mastery
	{-1, -1, 5578, 5579, 62, 2, 18, 3, 3, 0, 1, -1, 5, 4, 2, 1, 0, 0, 0}, // 146 Advanced Healing Adept
	{-1, -1, 5580, 5581, 62, 2, 19, 3, 3, 0, 1, -1, 5, 4, 1, 1, 0, 0, 0}, // 147 Advanced Healing Gift
	{-1, -1, 5618, 5619, 62, 2, 32, 3, 3, 0, 1, -1, 5, 2, 0, 1, 0, 0, 0}, // 148 Coup de Grace
	{-1, -1, 5620, 5621, 62, 3, 30, 3, 3, 0, 1, -1, 5, 2, 0, 1, 0, 0, 0}, // 149 Fury of the Ages
	{-1, -1, 5624, 5625, 62, 3, 26, 3, 3, 0, 1, -1, 5, 2, 0, 1, 0, 0, 0}, // 150 Mastery of the Past
	{-1, -1, 5636, 5637, 61, 3, 34, 3, 5, 0, 1, -1, 5, 2, 0, 1, 0, 0, 0}, // 151 Lightning Reflexes
	{-1, -1, 5638, 5639, 61, 3, 33, 3, 5, 0, 1, -1, 5, 2, 0, 1, 0, 0, 0}, // 152 Innate Defense
	{3312, 3313, 3310, 3311, 61, 2, 0, 0, 3, 0, 1, -1, 5, 4, 2, 1, 0, 180, 2}, // 153 Radiant Cure
	{-1, -1, 5582, 5583, 63, 2, 39, 1, 3, 0, 1, -1, 5, 2, 0, 1, 0, 0, 0}, // 154 Hastened Divinity
	{-1, -1, 5586, 5587, 63, 2, 40, 1, 3, 0, 1, -1, 5, 2, 0, 1, 0, 0, 0}, // 155 Hastened Turning
	{-1, -1, 5588, 5589, 63, 2, 41, 1, 3, 0, 1, -1, 5, 2, 0, 1, 0, 0, 0}, // 156 Hastened Purification of the Soul
	{-1, -1, 5590, 5591, 63, 2, 57, 1, 3, 0, 1, -1, 5, 2, 0, 1, 0, 0, 0}, // 157 Hastened Gathering
	{-1, -1, 5592, 5593, 63, 2, 50, 1, 3, 0, 1, -1, 5, 2, 0, 1, 0, 0, 0}, // 158 Hastened Rabidity
	{-1, -1, 5594, 5595, 63, 2, 43, 1, 3, 0, 1, -1, 5, 2, 0, 1, 0, 0, 0}, // 159 Hastened Exodus
	{-1, -1, 5596, 5597, 63, 2, 117, 1, 3, 0, 1, -1, 5, 2, 0, 1, 0, 0, 0}, // 160 Hastened Root
	{-1, -1, 5598, 5599, 63, 2, 58, 1, 3, 0, 1, -1, 5, 2, 0, 1, 0, 0, 0}, // 161 Hastened Mending
	{-1, -1, 5600, 5601, 63, 2, 66, 1, 3, 0, 1, -1, 5, 2, 0, 1, 0, 0, 0}, // 162 Hastened Banishment
	{-1, -1, 5608, 5609, 63, 3, 110, 1, 3, 0, 1, -1, 5, 2, 0, 1, 0, 0, 0}, // 163 Hastened Instigation
	{-1, -1, 5610, 5611, 63, 2, 109, 1, 3, 0, 1, -1, 5, 2, 0, 1, 0, 0, 0}, // 164 Furious Rampage
	{-1, -1, 5612, 5613, 63, 2, 98, 1, 3, 0, 1, -1, 5, 2, 0, 1, 0, 0, 0}, // 165 Hastened Purification of the Body
	{-1, -1, 5614, 5615, 63, 2, 102, 1, 3, 0, 1, -1, 5, 2, 0, 1, 0, 0, 0}, // 166 Hasty Exit
	{-1, -1, 5616, 5617, 63, 2, 107, 1, 3, 0, 1, -1, 5, 2, 0, 1, 0, 0, 0}, // 167 Hastened Purification
	{-1, -1, 5622, 5623, 62, 3, 104, 3, 3, 0, 1, -1, 5, 2, 0, 1, 0, 0, 0}, // 168 Flash of Steel
	{5696, 5697, 5532, 5533, 61, 3, 0, 0, 3, 0, 1, -1, 5, 2, 0, 1, 0, 180, 2}, // 169 Divine Arbitration
	{5698, 5699, 5534, 5535, 61, 3, 0, 0, 3, 0, 1, -1, 5, 2, 0, 1, 0, 240, 2}, // 170 Wrath of the Wild
	{5700, 5701, 5536, 5537, 61, 3, 0, 0, 3, 0, 1, -1, 5, 2, 0, 1, 0, 120, 2}, // 171 Virulent Paralysis
	{5702, 5703, 5538, 5539, 62, 2, 0, 0, 1, 0, 1, 3338, 5, 0, 0, 0, 0, 480, 0}, // 172 Harvest of Druzzil
	{5706, 5707, 5542, 5543, 61, 3, 0, 0, 3, 0, 1, -1, 5, 2, 0, 1, 0, 600, 2}, // 173 Eldritch Rune
	{5704, 5705, 5540, 5541, 61, 3, 0, 0, 3, 0, 1, -1, 5, 2, 0, 1, 0, 540, 2}, // 174 Servant of Ro
	{5708, 5709, 5544, 5545, 61, 5, 0, 0, 3, 0, 1, -1, 5, 4, -1, 1, 0, 540, 2}, // 175 Wake the Dead
	{5710, 5711, 5626, 5627, 62, 5, 0, 0, 2, 0, 1, -1, 5, 4, -2, 1, 0, 1, 2}, // 176 Suspended Minion
	{5712, 5713, 5628, 5629, 61, 4, 0, 0, 3, 0, 1, -1, 5, 4, -1, 1, 0, 720, 2}, // 177 Spirit Call
	{-1, -1, 5630, 5631, 63, 3, 38, 1, 2, 0, 1, -1, 5, 4, 3, 1, 0, 0, 0}, // 178 Celestial Renewal
	{-1, -1, 5632, 5633, 64, 6, 52, 1, 1, 1, 1, -1, 5, 0, 0, 0, 0, 0, 0}, // 179 Allegiant Familiar
	{5724, 5725, 5634, 5635, 61, 3, 74, 1, 3, 0, 1, -1, 5, 2, 0, 1, 0, 2160, 2}, // 180 Hand of Piety
	{-1, -1, 5640, 5641, 63, 3, 112, 3, 2, 0, 1, -1, 5, 2, 0, 1, 0, 0, 0}, // 181 Mithaniel's Binding
	{-1, -1, 5642, 5643, 63, 2, 97, 3, 3, 0, 1, -1, 5, 4, 2, 1, 0, 0, 0}, // 182 Mending of the Tranquil
	{-1, -1, 5644, 5645, 63, 2, 108, 3, 3, 0, 1, -1, 5, 4, 2, 1, 0, 0, 0}, // 183 Raging Flurry
	{5714, 5715, 5646, 5647, 61, 3, 0, 0, 3, 0, 1, -1, 5, 2, 0, 1, 0, 900, 2}, // 184 Guardian of the Forest
	{5716, 5717, 5648, 5649, 61, 4, 0, 0, 3, 0, 1, -1, 5, 4, -1, 1, 0, 1320, 2}, // 185 Spirit of the Wood
	{-1, -1, 5650, 5651, 61, 2, 0, 0, 5, 0, 1, -1, 5, 2, 0, 1, 0, 0, 0}, // 186 Bestial Frenzy
	{-1, -1, 5652, 5653, 61, 2, 0, 0, 5, 0, 1, -1, 5, 2, 0, 1, 0, 0, 0}, // 187 Harmonious Attack
	{-1, -1, 5654, 5655, 61, 2, 0, 0, 3, 0, 1, -1, 5, 4, 2, 1, 0, 0, 2}, // 188 Knight's Advantage
	{-1, -1, 5656, 5657, 61, 3, 0, 0, 3, 0, 1, -1, 5, 2, 0, 1, 0, 0, 2}, // 189 Ferocity
	{-1, -1, 5664, 5665, 63, 5, 45, 1, 1, 1, 1, -1, 5, 0, 0, 0, 0, 0, 0}, // 190 Viscid Roots
	{-1, -1, 5666, 5667, 63, 2, 115, 3, 3, 0, 1, -1, 5, 2, 0, 1, 0, 0, 0}, // 191 Sionachie's Crescendo
	{-1, -1, 5668, 5669, 63, 2, 0, 0, 3, 0, 1, -1, 5, 2, 0, 1, 0, 0, 0}, // 192 Ayonae's Tutelage
	{-1, -1, 5674, 5675, 61, 3, 125, 1, 3, 0, 1, -1, 5, 2, 0, 1, 0, 0, 2}, // 193 Feigned Minion
	{-1, -1, 5660, 5661, 61, 2, 0, 0, 3, 0, 1, -1, 5, 4, 2, 1, 0, 0, 2}, // 194 Unfailing Divinity
	{-1, -1, 5658, 5659, 61, 4, 0, 0, 3, 0, 1, -1, 5, 4, -1, 1, 0, 0, 2}, // 195 Animation Empathy
	{-1, -1, 5672, 5673, 63, 2, 73, 1, 3, 0, 1, -1, 5, 2, 0, 1, 0, 0, 0}, // 196 Rush to Judgment
	{-1, -1, 3275, 3276, 61, 2, 120, 1, 3, 0, 1, -1, 5, 4, 2, 1, 0, 0, 2}, // 197 Living Shield
	{-1, -1, 3283, 3284, 61, 3, 87, 1, 3, 0, 1, -1, 5, 2, 0, 1, 0, 0, 2}, // 198 Consumption of the Soul
	{5718, 5719, 5676, 5677, 63, 6, 30, 1, 1, 0, 1, -1, 5, 0, 0, 0, 0, 18, 0}, // 199 Boastful Bellow
	{-1, -1, 5680, 5681, 61, 3, 0, 0, 3, 0, 1, -1, 5, 2, 0, 1, 0, 0, 2}, // 200 Fervent Blessing
	{-1, -1, 5682, 5683, 61, 2, 0, 0, 3, 0, 1, -1, 5, 4, 2, 1, 0, 0, 2}, // 201 Touch of the Wicked
	{-1, -1, 5684, 5685, 61, 2, 0, 0, 3, 0, 1, -1, 5, 4, 2, 1, 0, 0, 2}, // 202 Punishing Blade
	{-1, -1, 5686, 5687, 61, 3, 0, 0, 3, 0, 1, -1, 5, 2, 0, 1, 0, 0, 2}, // 203 Speed of the Knight
	{-1, -1, 5688, 5689, 63, 6, 0, 0, 1, 0, 1, -1, 5, 0, 0, 0, 0, 0, 0}, // 204 Shroud of Stealth
	{-1, -1, 5690, 5691, 61, 1, 0, 0, 5, 0, 1, -1, 5, 2, 0, 1, 0, 0, 0}, // 205 Nimble Evasion
	{-1, -1, 5692, 5693, 61, 2, 0, 0, 5, 0, 1, -1, 5, 2, 0, 1, 0, 0, 0}, // 206 Technique of Master Wu
	{5720, 5721, 5694, 5695, 63, 5, 0, 0, 3, 0, 1, -1, 5, 4, -1, 1, 0, 1320, 0}, // 207 Host of the Elements
	{3290, 3291, 3288, 3289, 61, 3, 0, 0, 3, 0, 1, -1, 5, 4, -1, 1, 0, 900, 2}, // 208 Call of Xuzl
	{-1, -1, 3314, 3315, 61, 3, 0, 0, 3, 0, 1, -1, 5, 2, 0, 1, 0, 0, 0}, // 209 Hastened Stealth
	{-1, -1, 5523, 5728, 61, 1, 0, 0, 3, 0, 1, -1, 5, 4, 1, 1, 0, 0, 2}, // 210 Ingenuity
	{-1, -1, 5584, 5585, 62, 2, 0, 0, 2, 0, 1, -1, 5, 4, 2, 1, 0, 0, 2}, // 211 Fleet of Foot
	{5732, 5733, 5730, 5731, 63, 6, 0, 0, 1, 1, 1, -1, 5, 0, 0, 0, 0, 1, 0}, // 212 Fading Memories
	{-1, -1, 5734, 5735, 61, 2, 0, 0, 3, 0, 1, -1, 5, 4, 1, 1, 0, 0, 2}, // 213 Tactical Mastery
	{-1, -1, 5736, 5737, 61, 1, 0, 0, 3, 0, 1, -1, 5, 4, 1, 1, 0, 0, 2}, // 214 Theft of Life
	{-1, -1, 5738, 5739, 61, 3, 23, 3, 3, 0, 1, -1, 5, 4, 3, 1, 0, 0, 2}, // 215 Fury of Magic
	{-1, -1, 5740, 5741, 61, 2, 114, 1, 3, 0, 1, -1, 5, 4, 2, 1, 0, 0, 2}, // 216 Fury of Magic Mastery
	{5662, 5663, 5670, 5671, 62, 4, 55, 1, 1, 1, 1, -1, 5, 0, 0, 0, 0, 1, 0}, // 217 Project Illusion
	{-1, -1, 5743, 5744, 62, 4, 82, 1, 1, 1, 1, -1, 5, 0, 0, 0, 0, 0, 0}, // 218 Headshot
	{5749, -1, 5747, 5748, 64, 4, 0, 0, 1, 1, 1, 512, 5, 0, 0, 0, 0, 5, 0}, // 219 Entrap
	{-1, -1, 9000, 9001, 61, 2, 86, 1, 3, 0, 1, -1, 5, 4, 2, 1, 0, 0, 2}, // 220 Unholy Touch
	{-1, -1, 9002, 9003, 61, 2, 0, 0, 3, 0, 1, -1, 5, 4, 2, 1, 0, 0, 2}, // 221 Total Domination
	{-1, -1, 5751, 5752, 61, 2, 0, 0, 3, 0, 1, -1, 5, 4, 2, 1, 0, 0, 2}, // 222 Stalwart Endurance
	{-1, -1, 13642, 13643, 59, 1, -2, 0, 3, 3, 1, -1, 3, 0, 0, 0, 0, 0, 0}, // 223 Quick Summoning
	{-1, -1, 13542, 13543, 55, 1, -1, 0, 3, 2, 1, -1, 2, 0, 0, 0, 0, 0, 0}, // 224 Mental Clarity
	{-1, -1, 13526, 13527, 51, 1, 0, 0, 3, 1, 1, -1, 1, 0, 0, 0, 0, 0, 0}, // 225 Innate Regeneration
	{13620, 13621, 13618, 13619, 59, 5, 224, 3, 1, 1, 1, 2751, 3, 0, 0, 0, 0, 8640, 0}, // 226 Mana Burn
	};

	namespace
	{
		// AltAdvManager_Add_MGB_for_Beastlords_fix's lists past the general abilities, by class
		// (1 warrior .. 15 beastlord). Read out of the decompile by running it for every class and id.
		const std::vector<uint8_t> ClassAbilities[15] = {
			{28, 29, 30, 31, 32, 33, 34, 81, 104, 105, 108, 109, 110, 111, 112, 120, 143, 148, 149, 151, 152, 163, 164, 168, 181, 183, 189, 197, 202, 210, 213, 222}, // 1
			{18, 19, 20, 21, 23, 24, 28, 33, 34, 35, 36, 37, 38, 39, 40, 41, 113, 144, 145, 146, 147, 151, 152, 153, 154, 155, 156, 169, 178, 194, 215, 224}, // 2
			{18, 19, 21, 23, 24, 28, 29, 30, 31, 32, 33, 34, 35, 73, 74, 75, 76, 77, 78, 79, 104, 119, 120, 143, 146, 147, 148, 149, 151, 152, 168, 180, 188, 196, 200, 203, 224}, // 3
			{18, 19, 21, 23, 24, 28, 29, 30, 31, 32, 33, 34, 35, 80, 81, 82, 83, 84, 104, 119, 120, 146, 147, 148, 149, 151, 152, 168, 184, 189, 202, 218, 219, 224}, // 4
			{23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 78, 79, 85, 86, 87, 88, 89, 104, 119, 120, 125, 143, 148, 149, 150, 151, 152, 168, 188, 198, 201, 203, 214, 220, 224}, // 5
			{18, 19, 20, 21, 23, 24, 28, 33, 34, 35, 42, 43, 44, 45, 46, 80, 113, 144, 145, 146, 147, 151, 152, 153, 159, 170, 185, 190, 215, 224}, // 6
			{28, 29, 30, 31, 32, 33, 34, 81, 97, 98, 99, 100, 101, 104, 105, 116, 120, 122, 148, 149, 151, 152, 165, 168, 182, 189, 202, 206, 210}, // 7
			{23, 24, 25, 26, 28, 29, 30, 31, 32, 33, 34, 81, 90, 94, 115, 118, 119, 120, 121, 122, 123, 148, 149, 150, 151, 152, 187, 191, 192, 199, 211, 212, 224}, // 8
			{28, 29, 30, 31, 32, 33, 34, 81, 102, 103, 104, 105, 107, 120, 121, 122, 124, 148, 149, 151, 152, 166, 167, 168, 189, 204, 205, 209, 210}, // 9
			{18, 19, 20, 21, 23, 24, 28, 33, 34, 35, 47, 48, 49, 50, 113, 125, 144, 145, 146, 147, 151, 152, 153, 158, 171, 176, 177, 215, 224}, // 10
			{20, 23, 24, 25, 26, 27, 28, 33, 34, 35, 37, 46, 58, 68, 69, 70, 71, 72, 125, 144, 145, 150, 151, 152, 161, 175, 176, 193, 214, 215, 224}, // 11
			{20, 23, 24, 25, 26, 27, 28, 33, 34, 42, 43, 44, 52, 53, 114, 117, 144, 145, 150, 151, 152, 159, 160, 172, 179, 208, 216, 224, 226}, // 12
			{20, 23, 24, 25, 26, 27, 28, 33, 34, 35, 58, 60, 61, 62, 63, 64, 65, 66, 67, 125, 144, 145, 150, 151, 152, 161, 162, 174, 176, 207, 215, 223, 224}, // 13
			{20, 21, 23, 24, 25, 26, 27, 28, 33, 34, 35, 46, 48, 55, 56, 57, 113, 144, 145, 150, 151, 152, 157, 173, 176, 195, 215, 217, 221, 224}, // 14
			{18, 19, 21, 23, 24, 28, 29, 30, 31, 32, 33, 34, 58, 81, 104, 119, 120, 125, 126, 127, 128, 146, 147, 148, 149, 151, 152, 161, 168, 176, 186, 224}, // 15
		};

		std::string Text(const EqStr *eqstr, int32_t id)
		{
			return eqstr && id >= 0 ? eqstr->Format(static_cast<uint32_t>(id), {}) : std::string();
		}
	}

	int Rank(const Character &ch, int id)
	{
		if (!ch.ranks || id < 1 || id > Count - 1)
			return 0;
		return ch.ranks[id];
	}

	bool ClassHas(int class_id, int id)
	{
		// Every class: 1..13, 15..17, the PoP advanced innates and their like (129..142), and 225
		// (Innate Regeneration again; 14 is no one's).
		if (id > 0 && (id < 14 || (id > 14 && id < 18) || (id > 128 && id < 143) || id == 225))
			return true;
		if (class_id < 1 || class_id > 15)
			return false;
		const std::vector<uint8_t> &list = ClassAbilities[class_id - 1];
		return std::find(list.begin(), list.end(), static_cast<uint8_t>(id)) != list.end();
	}

	int PointCost(int rank, int id)
	{
		if (id < 0 || id >= Count)
			return 0;
		const Ability &a = Abilities[id];
		rank = static_cast<int16_t>(rank);
		if (a.max_rank != 1 && a.cost_inc != 1) {
			if (a.category < PopAdvance)
				return (rank + 1) * a.cost_inc;
			switch (a.cost_type) {
			case 1:
				if (rank != 0 && a.level_step != 0) {
					int next = rank + 1;
					if (rank % a.level_step != 0)
						return next / a.level_step + 1;
					return next / a.level_step + 1 + a.pop_cost_inc;
				}
				break;
			case 2:
				break;
			case 3:
				return a.cost + 1 + rank;
			case 4:
				if (rank != 0) {
					if (static_cast<int16_t>(rank + 1) < 2)
						return a.cost;
					return a.cost + a.pop_cost_inc * (static_cast<int16_t>(rank + 1) - 1);
				}
				break;
			default:
				return 0;
			}
		}
		return a.cost;
	}

	int TotalPoints(const Character &ch, int rank, int id)
	{
		if (rank < 1 || !ClassHas(ch.class_id, id))
			return 0;
		int total = 0;
		for (int r = 0; r < rank; ++r)
			total += PointCost(r, id);
		return total;
	}

	int RequiredLevel(const Character &ch, int id)
	{
		if (id < 0 || id >= Count)
			return 0;
		const Ability &a = Abilities[id];
		int level = a.min_level;
		int rank = Rank(ch, id);
		if (a.level_inc == 0) {
			if (a.level_step != 0) {
				int reach = (ch.level - a.min_level + 1) * a.level_step;
				if (rank < reach) {
					level = rank / a.level_step + a.min_level;
					if (level < 0x3e)
						level = 0x3d;
				}
				else {
					level = reach / a.level_step + a.min_level;
					if (rank == a.max_rank)
						--level;
				}
			}
		}
		else {
			level = a.min_level + a.level_inc * rank;
			if (rank == a.max_rank)
				level -= a.level_inc;
		}
		return std::max(level, static_cast<int>(a.min_level));
	}

	namespace
	{
		// The ranks in the general abilities (CheckAbilityRequirements' -1 counts New Tanaan Crafting
		// Mastery, 141, by its points; the archetype abilities' check by its ranks).
		int GeneralPoints(const Character &ch, bool crafting_by_points)
		{
			int total = 0;
			for (int i = 0; i < Count; ++i) {
				int rank = Rank(ch, i);
				if (rank != 0 && Abilities[i].category == General)
					total += crafting_by_points && i == 0x8d ? TotalPoints(ch, rank, i) : rank;
			}
			return total;
		}

		int ArchetypePoints(const Character &ch)
		{
			int total = 0;
			for (int i = 0; i < Count; ++i) {
				int rank = Rank(ch, i);
				if (rank != 0 && Abilities[i].category == Archetype)
					total += TotalPoints(ch, rank, i);
			}
			return total;
		}
	}

	bool MeetsRequirements(const Character &ch, int id)
	{
		if (id < 0 || id >= Count)
			return false;
		const Ability &a = Abilities[id];
		// Body and Mind Rejuvenation (0x77) wants Mental Clarity (0xe0) and Natural Healing (0x1d)
		// at 3 besides.
		if (id == 0x77) {
			if (Rank(ch, 0xe0) != 3 || Rank(ch, 0x1d) != 3)
				return false;
		}
		else if (a.required == 0)
			return true;
		if (a.required == -1)
			return GeneralPoints(ch, true) >= 6;
		if (a.required == -2)
			return ArchetypePoints(ch) >= 12;
		if (a.category < PopAdvance) {
			if (a.category == Archetype) {
				if (GeneralPoints(ch, false) < 6)
					return false;
			}
			else if (a.category == Class) {
				if (ArchetypePoints(ch) < 12)
					return false;
			}
			else
				return false;
		}
		return Rank(ch, a.required) >= a.required_rank;
	}

	bool CanTrain(const Character &ch, int id)
	{
		if (id < 0 || id >= Count || !ch.ranks)
			return false;
		const Ability &a = Abilities[id];
		int rank = Rank(ch, id);
		if (rank == a.max_rank || PointCost(rank, id) > static_cast<int16_t>(ch.unspent))
			return false;
		// The Planes of Power pages want the expansion (Expansions 0x18).
		if (a.category >= PopAdvance && (ch.expansions & 0x18) == 0)
			return false;
		if (!a.enabled || !ClassHas(ch.class_id, id) || ch.level < RequiredLevel(ch, id))
			return false;
		return MeetsRequirements(ch, id);
	}

	uint32_t ReuseTime(const Character &ch, int id)
	{
		if (id < 0 || id >= Count)
			return 0;
		uint32_t reuse = Abilities[id].reuse;
		if (reuse == 0 || !ch.ranks)
			return reuse;
		// A few take a fixed time off a rank of another; the rest 10% a rank of their Hastened one.
		auto off = [&](int other, uint32_t step) {
			int rank = Rank(ch, other);
			return rank >= 1 && rank <= 3 ? reuse - step * rank : reuse;
		};
		int hastened = 0;
		switch (id) {
		case 0x49: return off(0xc4, 7);      // Divine Stun: Rush to Judgment
		case 0x3a: return off(0xa1, 0x21c);  // Mend Companion: Hastened Mending
		case 0x56: case 0x57: return off(0xc9, 0x2d0); // the touches: Touch of the Wicked
		case 0x27: hastened = 0x9a; break;
		case 0x28: hastened = 0x9b; break;
		case 0x29: hastened = 0x9c; break;
		case 0x2b: hastened = 0x9f; break;
		case 0x32: hastened = 0x9e; break;
		case 0x39: hastened = 0x9d; break;
		case 0x42: hastened = 0xa2; break;
		case 0x62: hastened = 0xa5; break;
		case 0x66: hastened = 0xa6; break;
		case 0x6b: hastened = 0xa7; break;
		case 0x6d: hastened = 0xa4; break;
		case 0x6e: hastened = 0xa3; break;
		case 0x75: hastened = 0xa0; break;
		default: return reuse;
		}
		int rank = Rank(ch, hastened);
		if (rank < 1 || rank > 3)
			return reuse;
		uint32_t percent = rank * 10;
		return reuse - (percent * reuse) / 100;
	}

	bool Ready(const Character &ch, int id, int64_t last_used, int64_t now, int64_t *remaining)
	{
		if (id <= 0 || id >= Count)
			return false;
		int64_t reuse = ReuseTime(ch, id);
		if (reuse == 0) {
			if (remaining)
				*remaining = 0;
			return true;
		}
		if (remaining)
			*remaining = reuse - now + last_used;
		return reuse + last_used <= now;
	}

	std::string Requirements(const Character &ch, int id, const EqStr *eqstr)
	{
		if (id <= 0 || id >= Count || !eqstr)
			return "";
		const Ability &a = Abilities[id];
		auto plain = [eqstr](uint32_t sid) { return eqstr->Format(sid, {}); };
		if (id == 0x58)
			return plain(2185);
		if (id == 0x77)
			return plain(2186);
		if (a.required == -1)
			return plain(2187);
		if (a.required == -2)
			return plain(2188);
		std::string level = std::to_string(RequiredLevel(ch, id));
		if (a.required == 0)
			return eqstr->Format(2191, {level});
		std::string other = Text(eqstr, Abilities[a.required].name);
		std::string rank = std::to_string(a.required_rank);
		if (a.category == Class && Abilities[a.required].category == General)
			return eqstr->Format(2189, {other, rank});
		return eqstr->Format(2190, {level, other, rank});
	}

	std::vector<int> SharedTimer(int id)
	{
		if (id >= 0x3d && id <= 0x40)
			return {0x3d, 0x3e, 0x3f, 0x40};
		if (id == 0x56 || id == 0x57)
			return {0x56, 0x57};
		return {id};
	}

	int StatCapBonus(const Character &ch, int index)
	{
		int bonus = 0;
		if (int rank = Rank(ch, 0x8e); rank != 0)
			bonus = std::min(rank, Abilities[0x8e].level_step * (ch.level - 60)) * 5;
		bool mage = ch.class_id > 10 && ch.class_id < 15; // EQ_Character::IsMage
		if (int rank = Rank(ch, 0x90); rank != 0 && (index == 4 || (index == 5 && mage)))
			bonus += std::min(rank, Abilities[0x90].level_step * (ch.level - 60)) * 10;
		return bonus;
	}

	int CapStat(const Character &ch, int value, int index)
	{
		value = static_cast<int16_t>(value);
		if (index >= 7 && index <= 12)
			return std::min(value, 500);
		if (index < 0 || index > 12 || ch.level < 61)
			value = std::min(value, 255);
		else
			value = std::min(value, (ch.level - 9) * 5 + StatCapBonus(ch, index));
		return std::max(value, 1);
	}

	int InnateBonus(const Character &ch, int index)
	{
		// The Innate abilities by index: STR 1, STA 2, AGI 3, DEX 4, WIS 6, INT 5, CHA 7; magic 10,
		// cold 9, fire 8, poison 0xb, disease 0xc. Each one's Advanced is 0x80 more.
		static const int innate[12] = {1, 2, 3, 4, 6, 5, 7, 10, 9, 8, 0xb, 0xc};
		if (index < 0 || index > 11)
			return 0;
		return Rank(ch, innate[index]) * 2 + Rank(ch, innate[index] + 0x80) * 2;
	}

	int MaxHpBonus(const Character &ch, int base)
	{
		double percent = 0;
		if (int rank = Rank(ch, 0x1c); rank != 0) {
			percent = rank == 1 ? 2.0 : rank == 2 ? 5.0 : rank == 3 ? 10.0 : 0.0;
			if (Rank(ch, 0x78) != 0)
				percent += 2.0;
		}
		if (int rank = Rank(ch, 0x8f); rank != 0) {
			rank = std::min(rank, Abilities[0x8f].level_step * (ch.level - 60));
			if (rank != 0) {
				double per = ch.class_id == 1 ? 1.5 : ch.class_id == 3 || ch.class_id == 5 ? 1.25 : 0.0;
				percent += rank * per;
			}
		}
		return static_cast<int>(base * percent * 0.01);
	}

	float RunSpeedCap(const Character &ch)
	{
		switch (Rank(ch, 0xd3)) {
		case 1: return 1.142006f;
		case 2: return 1.192006f;
		case 3: return 1.242006f;
		default: return 1.092006f;
		}
	}

	float InnateRunSpeed(const Character &ch)
	{
		switch (Rank(ch, 0xd)) {
		case 1: return 0.08f;
		case 2: return 0.14f;
		case 3: return 0.21f;
		default: return 0;
		}
	}

	int CastingTimeModifier(const Character &ch, const SpellInfo &spell)
	{
		uint32_t cast = static_cast<uint32_t>(spell.cast_ms);
		// The rank's percentage; the last one set stays when a rank isn't 1..3.
		uint32_t percent = 100;
		auto apply = [&](int rank, uint32_t one, uint32_t two, uint32_t three) {
			if (rank == 1)
				percent = one;
			else if (rank == 2)
				percent = two;
			else if (rank == 3)
				percent = three;
			cast = percent * cast / 100;
		};
		if (spell.good_effect != 0) {
			if (cast > 3999 && spell.duration_formula != 0) {
				if (int rank = Rank(ch, 0x1b); rank != 0)
					apply(rank, 0x5f, 0x55, 0x4b);
				if (int rank = Rank(ch, 0x30); rank != 0)
					apply(rank, 0x5a, 0x4b, 0x32);
			}
		}
		else if (cast > 3999 && spell.duration_formula == 0 && spell.HasSpa(0) && Rank(ch, 0x17) == 3)
			apply(Rank(ch, 0x2c), 0x62, 0x5f, 0x5a);
		if (spell.HasSpa(0x58))
			if (int rank = Rank(ch, 0x2a); rank != 0)
				apply(rank, 0x5a, 0x4b, 0x32);
		if (ch.class_id == 13 && (spell.HasSpa(0x20) || spell.HasSpa(0x21) || spell.id == 0x790 || spell.id == 0x6eb))
			if (int rank = Rank(ch, 0xdf); rank != 0)
				apply(rank, 0x5a, 0x4b, 0x32);
		int c = ch.class_id;
		if (ch.level > 50 && (c == 4 || c == 5 || c == 3 || c == 15) && cast > 2999 && spell.good_effect == 0)
			// On the x87 stack: the float constants (0.01f is 0.0099999998), the product not rounded to
			// a float, then truncated (4000 at level 60 is 2799).
			cast = static_cast<uint32_t>(static_cast<double>(cast) * ((100.0 - (ch.level - 50) * 3.0) * static_cast<double>(0.01f)));
		return static_cast<int>(cast - static_cast<uint32_t>(spell.cast_ms));
	}

	int Title(const Character &ch)
	{
		int general = 0, archetype = 0, class_points = 0;
		for (int i = 0; i < Count; ++i) {
			int points = TotalPoints(ch, Rank(ch, i), i);
			if (Abilities[i].category == General)
				general += points;
			else if (Abilities[i].category == Archetype)
				archetype += points;
			else if (Abilities[i].category == Class)
				class_points += points;
		}
		int title = general > 5 ? 1 : 0;
		if (archetype > 11)
			title = 2;
		if (class_points > 23)
			title = 3;
		return title;
	}
}
