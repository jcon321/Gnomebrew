# TODO

Work after 1.0 that isn't game logic: the client showing and handling what the server sends, with
no rules of its own.

## UI

1. ~~**In-game SIDL windows.**~~ Done: `frontend/window_manager.*` (drag, resize, close, minimize,
   focus order, fading, `UI_<char>_<server>.ini`). Still to do: BGTint isn't drawn, no
   right-click context menu (lock, alpha), and only the chat window's and the social editor's
   edit boxes take typing. The
   resize and drag cursors are the SIDL ones; the usual cursor is still eqmain's `cursor.bmp`
   rather than `A_DefaultCursor`.
2. **Display-only windows**, showing what the server already sends:
   - ~~Player window~~ and ~~target window~~: done (`frontend/game_values.*`, gauges in `sidl.cpp`).
     Targeting ourselves: F1 and clicking our model in the chase view. Still to do: max mana leaves
     out items' worn effects and `AntiTwinkAdj` for items with a recommended level; the XP gauge (EQType 4) needs the experience table.
   - Group window: laid out as `CGroupWnd::UpdateButtons` does (`frontend/game_windows.*`), but
     there's no group state yet (invites, members, leader).
   - Buff window: done (`frontend/buffs.*`, `game_windows.cpp`): our buffs from the profile, those
     landing on us (HitBySpell's FindAffectSlot stacking and overwriting), `OP_Buff`, the 6 s tick
     and its expiry, clicking a beneficial one off, their icons on red or blue, names (tooltips and
     EQTypes 45..59) and the blink in their last ticks. Their effects count in the stats, resists,
     AC, max HP (worked out as `Max_HP` does, not the zone's), max mana and our speed (SoW,
     snares, roots). Still to do: the effects that act as a buff
     lands or fades (levitation, invisibility, see invisible, infravision, illusions, Eye of Zomm,
     mounts), a bard's instrument mastery, alt+click's spell display, and Alt showing every
     buff's tooltip (CBuffWindow::PostDraw).
   - Inventory: name, level, class (its title from 51), deity, HP, stats and resists (base, worn
     items, the next food and drink; `GameValues::Stat`, `Resist`), AC, ATK and weight (`GameValues::Ac`, `Atk`, `Weight`), money, the class picture, item icons in the slots, the Alt. Adv. row from 51, Done, and the
     Skills button opening the skills window (`frontend/game_windows.*`), and items picked up, put
     down, split through the quantity window and put away on the character view (`core/inventory.*`).
     Still to do: for stats, resists, AC and ATK, items' worn effects, ATK's buffs, AAs,
     the Resistant Discipline, drunkenness and berserking; the XP and AA gauges (the experience table); Destroy,
     Alt. Advance, and icons in the hot button window's weapon slots.
   - Coins: done (`Inventory::MoveMoney`, CEverQuest_MoveMoney, OP_MoveCoin): a money button with
     nothing on the cursor asks how many through the quantity window and puts them on the cursor
     (A_DragItem's cell 143 + CursorMoneyType, the count across its bottom), with coins on it takes
     them onto its pile, as the character view does; changed into other coins only at the bank.
     Still to do: coins clicked onto the world or a spawn (DropItemOrMoneyOnPlayer: given to the
     target, or a trade), and Escape leaving them on the cursor as it does an item.
   - Bank window: done (`frontend/bank_window.*`, eqgame's CBankWnd): the right button on a banker
     within 20 (RightClickedOnPlayer, IsOkToTransact) opens it with the banker's welcome, the bank
     slots (2000..2007, their bags as BagBank1..8), the bank's coins on its money buttons (coins
     change into others there) and the inventory with it; Done, Escape, the close box and walking
     more than 20 away (checked every 500 ms) close it with "Come back soon!", the inventory if it
     opened it, the quantity window if it was the bank's, and every bag window. Alt shows every
     slot's tooltip. Still to do: Activate's MakeMeVisible, auto attack off (none yet) and
     SetMusicSelection(3); Deactivate's SavePC.
   - Merchant window: done (`frontend/merchant_window.*`, eqgame's CMerchantWnd): the right button
     on a merchant asks for its shop (OP_ShopRequest), the answer opens it with its items
     (OP_ShopInventoryPacket, OP_MerchantItemPacket, OP_ShopDelItem) in the scrolling slots, a
     click on any slot selects it (a yellow outline) and says its price, Buy and Sell (the quantity
     window for a stack) with eqgame's checks and messages, OP_ShopPlayerBuy and OP_ShopPlayerSell
     and their answers (the price out of the purse, the sale's coins in and the item gone), Done,
     the close box, Escape and walking away (OP_ShopEnd). Checked live against the server for
     opening, the prices and the welcome; buying and selling only through their refusals (the
     test character has no coins or anything worth selling). Still to do: a Bazaar trader's
     (Type 0: OP_TraderShop, OP_TraderBuy), MakeMeVisible, auto attack off on the request and
     SetMusicSelection(6), and the item display of a merchant's item (the slots' alt+click goes
     through the selection now).
   - Loot window: done (`frontend/loot_window.*`, eqgame's CLootWnd): the right button on a corpse
     in reach asks to loot it (CEverQuest_LootCorpse, OP_LootRequest); OP_MoneyOnCorpse's answer
     says why not, or gives its coins and opens the window kneeling, its items
     (OP_LootItemPacket) in LW_LootSlot0..29; a click on one onto the cursor, the right button into
     the inventory (OP_LootItem, a NO DROP item from another's corpse asking first), the slots
     locked until the zone answers; Done, the close box, Escape and walking away end it
     (OP_EndLootRequest), and the zone's answer, or the corpse gone, closes it. Not checked
     against a server yet (no corpse to loot: there's no auto attack). Still to do: /lootnodrop
     (`LootWindow::loot_no_drop`, DAT_00799674) and /autosplit's "as your split" (12071), the
     items in a corpse's bags (shown only to GMs and on player corpses past the eighth), the
     PvP item (Loot_Request_Type 5), MakeMeVisible, and the dead or mounted checks.
   - The right button on a spawn (CEverQuest::RMouseUp within 500 ms, RightClickedOnPlayer): a
     corpse's loot, a banker's bank, a merchant's shop, else /consider of our target. A
     despawning merchant, banker or corpse closes its window. Still to do: our class's guildmaster
     (OP_GMTraining) and the message for another's (eqstr 1467), a trader in the Bazaar (OP_TraderShop), a horse (race 141), and the
     inspect of a player (doInspect).
   - Bag windows: done (`frontend/containers.*`, eqgame's CContainerMgr and CContainerWnd): the right
     button on a bag opens or closes its window, laid out for its slots with Combine and Done
     under them, kept in the layout as BagInv1..8 (BagBank, BagTrade), picking up or swapping out
     a bag closes it, Escape closes them all, Alt shows every slot's tooltip, and Combine sends
     OP_TradeSkillCombine with the class and racial containers' checks, the slots locked until the
     zone answers. Still to do: world containers (forges, ovens: OP_ClickObject,
     OP_ClickObjectAction, OP_ObjectItemPacket, BagWorld1 and its 20 unit distance check in
     CContainerMgr::Process), the bag's open flag (EQ_Container::Open), and the right button on a
     book or note reading it (EQ_Note::SendTextRequestMsg, OP_ReadBook).
   - Equipping: our model's armor, weapons and light follow what we have on (`ZoneSession::UpdateAppearance`).
     Still to do: dual wield
     from the class table rather than the zone's 254/255, the casting check (no moves but from the
     cursor while casting), /showhelm, and a carried light going out underwater (`UpdateLight`'s
     check of the body's environment; it's only not drawn there now).
   - The item on the cursor: clicking the world with it drops it on the ground or gives it to the
     target (`CEverQuest_LMouseUp`, `DropHeldItemOnGround`, `DropItemOrMoneyOnPlayer`), and
     AutoInventory with no room drops it ("There was no place to put that!"); for now it stays on the
     cursor. Holding a slot's item to make an item hot button (`CInvSlot::HandleLButtonHeld`), and
     the right button on a slot (eat, drink, click effects: `CInvSlot::HandleRButtonUp`).
   - Item display: alt+click or the right button held on an inventory slot shows its item
     (`frontend/item_display.*`, eqgame's SetItem text with spells_en.txt's spell names); letting
     the right button go closes it. A link clicked in the chat shows its item (the zone's
     OP_ItemLinkResponse), and clicking the icon links the item in the chat line
     (`CItemDisplayWnd::WndNotification`; so does Shift+click on a slot while typing, our own).
     Still to do: FV's always-no-drop item ids (`EQ_Item::IsSpecialNoDrop`), open bags, items in
     other windows (loot, trade, merchants), and the item+0xdc check WndNotification makes
     before linking (unknown field).
   - Skills window: the skills the server says we have (not 254 or 255) and our languages, with
     their ranks, kept up to date by OP_SkillUpdate. Still to do: racial skills the server marks
     255 (eqgame's HasSkill table), selecting rows.
   - Spellbook and spell gems: done (`frontend/spell_book.*`, eqgame's CSpellBookWnd and
     CCastSpellWnd): the book opens from the gems' book button or /book (casters only, sitting us
     down), pages and page keys, a scroll on the cursor scribed into an empty slot, a book spell
     dragged onto an empty gem and memorized, a gem right clicked to forget it, two slots right
     clicked to swap them, Delete and the confirmation dialog to delete a deletable spell, with the
     scribe and memorize timers and gauges (EQTypes 9, 10) and the zone's answers
     (OP_MemorizeSpell, OP_SwapSpell, OP_DeleteSpell; a replay answers them as the zone would).
     The gems show their icons in their target type's color, dimmed until the spell can be cast.
     Casting from the gems is done (`frontend/spell_cast.*`, EQ_Character::CastSpell): a gem's left
     click or /cast n, CastSpell's checks and their messages (standing, already casting, level,
     line of sight, mana, the target, its range and body type), OP_CastSpell, the casting window
     (EQType 7) and the zone's answers (OP_MemorizeSpell 3's recast and recovery, OP_ManaChange
     ending the cast, OP_TokenText's fizzles and interrupts), with the gems dim while anything's
     cast or recovering; a bard's song stops on a gem click (an empty OP_ManaChange). Still to do:
     - casting: the hotkeys for the gems, stuns and divine aura (OP_Stun), the AA and focus cast time and range modifiers, items'
       click effects (OP_CastSpell's inventory slot) and abilities (gem 9), and a bard being hit
       (StopSpellCast 1);
     - the spell info display: alt+click (or the right button held) on a book slot or a gem shows
       the spell in the item display (CItemDisplayWnd::SetSpell @00425957);
     - spell hot buttons: a gem held onto the cursor (CCursorAttachment type 9) and the book button
       (type 5); a spell on the cursor clicked on a hot button;
     - what the book doesn't do yet: on a mount, turning auto attack off as it opens ("Auto
       attack is off."; there's no auto attack yet), a bard's AA 0x7b doubling memorizing, being
       hit closing it (msg_successful_hit), the spell bar locking other things while it waits
       (DAT_007d0254 also blocks casting);
     - spellsets and memorizing a whole page are later eras (the default UI's "Mem. This Page"
       buttons are zero-sized and eqgame ignores them).
   - Our own stance: /sit, /stand and the Actions window's Sit and Stand (EQPlayer::ChangeStance),
     our model sitting down (P02) and getting up. Sitting, nothing moves us and Space doesn't jump
     (ProcessControls takes the movement keys only standing or crouching), nor does the right
     button turn us (the left still circles the chase camera); forward stands us up (and closes
     the book) and we move off at once, getting up playing out all the same. In first person the
     view goes down and up with our head. Still to do: sitting's idle and fidgets (P07, the sitting
     fidget), kneeling (105), feign death,
     meditating's regen, being hit standing us up (msg_successful_hit), and the zone putting us in
     a stance (stunned, knocked out).
   - Hotbuttons: the Actions window's buttons and socials go on them and come off by holding
     them, and they're kept in `<char>_<server>.ini` (`frontend/game_windows.*`). Still to do:
     items, spells, pet commands and AA abilities on them, whether an ability's ready (checked
     while it isn't), and right-click on anything but a social (picking a combat or ability
     button's skill).
   - Socials: done (defaults, the editor, running them with `/pause`, `[Socials]`). Still to do:
     the edit boxes take typing only at their end (no caret to move, no Tab between them), and
     a social's lines go to InterpretCommand as typed, without eqgame's switch to the main chat
     window's channel while it runs.
   - Compass
   - Selector bar
3. ~~**A real chat window**~~: done (`frontend/chat_window.*`, `chat_text.*`, `chat_commands.cpp`).
   Still to do: more windows and the channel map (eqgame's `CChatManager`: which window each
   channel goes to, set from the right-click menu; there were no tabs in this era), the chat font
   size, hold-to-repeat on the scrollbar's arrows, languages scrambled by skill
   (`HearBabel`), and the commands that still say they aren't supported (/who, /random, /follow,
   /lfg, /smile and the other emotes...).
   Done in `chat_commands.cpp`: /consider (OP_Consider, PrintConsiderAssessment's text and con
   color), /time (Norrath's date as EQWorldData::AdvanceTime carries it on, and Earth's), /played,
   /motd, /afk, /anonymous and /assist (OP_Assist; the answer becomes our target). Still to do:
   - /afk: answering tells with its message, and the chat server's "/afk on silent";
   - /assist: AttackOnAssist turning auto attack on (there's no auto attack yet), and
     allowslashcommand's other checks (/safelock, stance 0x66, the windows that block commands:
     IsNoSlashWnd), also wanted by the other commands that call it;
   - /played: the alarm's time left (no alarm window yet);
   - /consider: under the deity PvP rules (pvpcon 4) NotOnSameDeity lets a GM off, read here as
     our `gm` flag; EQPlayer+0x109 isn't checked to be IsGameMaster.
4. **Cursor and dragging.** EQ's own cursors. (An item on the cursor follows it:
   `DrawCursorAttachment`.) Tooltips: done (`WindowManager::DrawTooltip`, eqgame's DrawWindows and
   DrawTooltipAtPoint): after the mouse rests 500 ms, the control under it shows its tooltip, its
   TooltipReference or what its window sets: an inventory slot's item name (an empty worn slot's
   name), a spell gem's "1) Spell", and Alt held shows every gem's (DrawSpellButtonTooltips).
   The buff window's: the spell's name. Still to do: the key bound to a
   button after its text (SetKeyTooltip, UpdateTooltipText; needs the keymap), the help texts
   FindWnd's other hit codes ask for (GetHelpTextFromStringTable), and the slots of windows we
   don't have yet (trade, give: their own DrawTooltips).
5. **Options / Escape menu.** Graphics, sound and display settings, mouse sensitivity, the keymap.
6. **A configurable keymap.** `InputEvent::Key` is a hard-coded list of about 18 keys; make it an
   EQ-style keymap with primary and alternate bindings.
7. **Map window.** Draw the line maps from `../Maps` with our position and nearby spawns.
8. **Loading screen** while zoning, with progress.

## Rendering / presentation

9. ~~**Nameplates**~~: done (`frontend/name_sprites.*`, eqgame's name sprites), lit as
   renderNameTag does (the world's ambient light +0.4, `NameSpriteLit`). Still to do: the
   character select name sprite isn't lit (its scene's light is our own guess), the trader tag in the Bazaar,
   the names eqgame hides in some animations (skeletons, gargoyles and mimics lying or feigning:
   animations 0x10, 0x21, 0x26), `/shownpcnames`, Zeal's raid shownames, and the guild list in packet logs (replays show "Unknown Guild"). An HP
   bar over heads isn't eqgame's; leave it to an option if ever.
10. ~~**Target indicator**~~: done, as eqgame does it: the target's name flashes from white back
   to its color. The client of this era had no target ring and no hover highlighting.
11. **Ground spawns and dropped objects.** Handle `OP_GroundSpawn` so dropped items and forges
    appear.
12. **Spell visuals.** Done for anyone's casting as the zone shows it (`frontend/spell_casting.*`,
    `frontend/spell_effects.*`, `render/spell_particles.*`): OP_BeginCast's "begins to cast a spell"
    (CEverQuest_StartCasting), the caster's CastingAnim and the effect on the target from OP_Action
    (EQPlayer::TriggerSpellEffect), the landing text and the target's TargetAnim from OP_Damage
    (CEverQuest_ReportSuccessfulHit), and the Luclin particle effects (SpellsNew.eff stages 0 and 2,
    spellsnew.edd emitters, eqgfx's CParticleSystem) on the caster's and target's bones, or the
    older `spells.eff` effects (gequip's sprites circling the target and eqgfx particle clouds) for
    the spells without a SpellsNew.eff record (the debug menu's "Luclin spell effects" off gives every
    spell its old one, as eqgame without SpellsNew.eff), the old sprite flashes' lights and the
    stages' sounds (game sounds; the debug menu's Sound section lists them). Still to do: the
    missiles (stage 1, EQMissile: bolts, their lights and sounds), the options'
    spell particle density and opacity (fixed at eqgame's 1 and 1), the chat filters for PC and NPC
    spells and bard songs, melee's OP_Damage (hit messages, the D02 hit and its sounds), being hit
    standing us up, and OP 0x4265 (destroy a cast's emitters; this server never sends it).
13. **The other camera modes F9 cycles through:** overhead and the two user-set cameras, plus zoom
    and field of view.
14. **Graphics options:** fullscreen or windowed, resolution changes, gamma, anisotropic filtering,
    MSAA, draw distance and level of detail.
15. **Screenshots.**

## Infrastructure / tooling

16. ~~**Packet record and replay.**~~ Done: `[Debug] RecordPackets=1` (GUI) or `--record DIR`
    (console) records; `--replay FILE` plays back in the GUI, console and `frontend_preview`.
17. **A Linux GUI host** (SDL2 with OpenGL or Vulkan), since the front end is already portable.
18. **Text rendering** off GDI (`win32_text`), onto a font renderer that works on both platforms
    (needed for 17).
19. **Connection handling.** Show when the connection is lost (linkdead, timeouts) and handle
    reconnects cleanly.
20. **Speed and stability:** load zones in the background, cache assets, catch crashes and write a
    crash dump, tests for the file and data parsers.

## Order

1. ~~Packet replay (16), so the rest can be built offline.~~
2. ~~The window manager (1).~~
3. ~~The Player, Target and Chat windows (2, 3), which prove out the whole SIDL pipeline.~~
4. ~~Nameplates and the target indicator (9, 10).~~
