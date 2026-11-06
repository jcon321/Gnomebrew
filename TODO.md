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
     Targeting ourselves: F1 and clicking our model in the chase view. The XP and AA gauges
     (EQTypes 4, 5) are OP_ExpUpdate's and OP_AAExpUpdate's 0..330 * 3.03031. Still to do: max mana
     leaves out items' worn effects and `AntiTwinkAdj` for items with a recommended level.
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
     the Resistant Discipline, drunkenness and berserking; Destroy,
     Alt. Advance, and icons in the hot button window's weapon slots.
   - Coins: done (`Inventory::MoveMoney`, CEverQuest_MoveMoney, OP_MoveCoin): a money button with
     nothing on the cursor asks how many through the quantity window and puts them on the cursor
     (A_DragItem's cell 143 + CursorMoneyType, the count across its bottom), with coins on it takes
     them onto its pile, as the character view does; changed into other coins only at the bank.
     Escape leaves them on the cursor, as it does an item (RemoveAttachment takes off only a
     button or a spell). Still to do: coins clicked onto the world or a spawn
     (DropItemOrMoneyOnPlayer: given to the target, or a trade).
   - Bank window: done (`frontend/bank_window.*`, eqgame's CBankWnd): the right button on a banker
     within 20 (RightClickedOnPlayer, IsOkToTransact) opens it with the banker's welcome, the bank
     slots (2000..2007, their bags as BagBank1..8), the bank's coins on its money buttons (coins
     change into others there) and the inventory with it; Done, Escape, the close box and walking
     more than 20 away (checked every 500 ms) close it with "Come back soon!", the inventory if it
     opened it, the quantity window if it was the bank's, and every bag window. Alt shows every
     slot's tooltip. Still to do: Activate's MakeMeVisible and SetMusicSelection(3);
     Deactivate's SavePC.
   - Merchant window: done (`frontend/merchant_window.*`, eqgame's CMerchantWnd): the right button
     on a merchant asks for its shop (OP_ShopRequest), the answer opens it with its items
     (OP_ShopInventoryPacket, OP_ShopDelItem) in the scrolling slots, a
     click on any slot selects it (a yellow outline) and says its price, Buy and Sell (the quantity
     window for a stack) with eqgame's checks and messages, OP_ShopPlayerBuy and OP_ShopPlayerSell
     and their answers (the price out of the purse, the sale's coins in and the item gone), Done,
     the close box, Escape and walking away (OP_ShopEnd). Checked live against the server for
     opening, the prices and the welcome; buying and selling only through their refusals (the
     test character has no coins or anything worth selling). Still to do: a Bazaar trader's
     (Type 0: OP_TraderShop, OP_TraderBuy), MakeMeVisible and SetMusicSelection(6), and the item display of a merchant's item (the slots' alt+click goes
     through the selection now).
   - Loot window: done (`frontend/loot_window.*`, eqgame's CLootWnd): the right button on a corpse
     in reach asks to loot it (CEverQuest_LootCorpse, OP_LootRequest); OP_MoneyOnCorpse's answer
     says why not, or gives its coins and opens the window kneeling, its items
     (OP_LootItemPacket) in LW_LootSlot0..29; a click on one onto the cursor, the right button into
     the inventory (OP_LootItem, a NO DROP item from another's corpse asking first), the slots
     locked until the zone answers; Done, the close box, Escape and walking away end it
     (OP_EndLootRequest), and the zone's answer, or the corpse gone, closes it. Not checked
     against a server yet (the test character hasn't killed anything of its own to loot). /lootnodrop sets whether a
     NO DROP item asks first (`LootNoDrop`). Still to do: /autosplit's "as your split" (12071), the
     items in a corpse's bags (shown only to GMs and on player corpses past the eighth), the
     PvP item (Loot_Request_Type 5), MakeMeVisible, and the dead or mounted checks.
   - Trade and give windows: done (`frontend/trade_window.*`, eqgame's CTradeWnd and CGiveWnd): an
     item or coins on the cursor clicked onto a player or NPC in reach asks to trade
     (DropItemOrMoneyOnPlayer, OP_TradeRequest), the go-ahead opens the trade window with it in
     (the give window for an NPC), someone asking opens it with OP_TradeRequestAck (or turns
     them down busy), their refusal is said; items onto the same stack or an empty slot, coins
     onto our money buttons, their items (OP_TradeItemPacket) and coins (OP_TradeCoins) shown,
     both names red until accepted (green), anything changing taking the accepts back; Trade or
     Give (OP_TradeAcceptClick), OP_FinishTrade closing it, Cancel, Escape, the close box and
     walking off cancelling (OP_CancelTrade). The give window checked through the replay (an
     NPC's answers as the zone gives them); a trade with another player not yet. Still to do:
     /trade's setting (DAT_00798ad8: nobody 'b', group only 'c'), the
     bags in a trade window (BagTrade1..16 open, but their contents aren't checked), OP_TradeReset,
     and Deactivate's second OP_CancelTrade and "You have cancelled the trade." after the zone's
     cancel (ours leaves them out).
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
     zone answers. World containers (a forge clicked on the ground, msg_chest_lock): BagWorld1,
     named by type (OpenWorldContainer), their items from OP_ObjectItemPacket at 4000 + n,
     OP_ClearObject emptying them, Combine as slot 1000, closed by Done, Escape or walking off 20
     (CContainerMgr::Process) with OP_ClickObjectAction, and not closeable with a NO DROP item in
     (CheckCloseable). Still to do: CanGoInBag's checks for a world container's slots, the bag's
     open flag (EQ_Container::Open), and the right button on a book or note reading it
     (EQ_Note::SendTextRequestMsg, OP_ReadBook).
   - Equipping: our model's armor, weapons and light follow what we have on (`ZoneSession::UpdateAppearance`).
     Still to do: dual wield
     from the class table rather than the zone's 254/255, the casting check (no moves but from the
     cursor while casting), /showhelm, and a carried light going out underwater (`UpdateLight`'s
     check of the body's environment; it's only not drawn there now).
   - The item on the cursor: clicking a spawn with it (or coins) gives it to them through a trade
     (`DropItemOrMoneyOnPlayer`, below), clicking the ground drops it (item 11), and AutoInventory
     with no room drops it (AutoGenInventory: "There was no place to put that!", eqstr 12937; a
     NO DROP item stays on the cursor). Holding a slot's item to make an item hot button (`CInvSlot::HandleLButtonHeld`), and
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
     their ranks, kept up to date by OP_SkillUpdate. Still to do: the racial skills the server marks
     255 (`Skills::HasSkill` has eqgame's table now; the window doesn't use it), selecting rows.
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
     - casting: stuns and divine aura (OP_Stun), the AA and focus cast time and range modifiers, items'
       click effects (OP_CastSpell's inventory slot) and abilities (gem 9), and a bard being hit
       (StopSpellCast 1);
     - the spell info display: alt+click (or the right button held) on a book slot or a gem shows
       the spell in the item display (CItemDisplayWnd::SetSpell @00425957);
     - spell hot buttons: a gem held onto the cursor (CCursorAttachment type 9) and the book button
       (type 5); a spell on the cursor clicked on a hot button;
     - what the book doesn't do yet: on a mount (Activate's own branch), a bard's AA 0x7b doubling
       memorizing, the spell bar locking other things while it waits (DAT_007d0254 also blocks
       casting);
     - spellsets and memorizing a whole page are later eras (the default UI's "Mem. This Page"
       buttons are zero-sized and eqgame ignores them).
   - Our own stance: /sit, /stand and the Actions window's Sit and Stand (EQPlayer::ChangeStance),
     our model sitting down (P02) and getting up. Sitting, nothing moves us and Space doesn't jump
     (ProcessControls takes the movement keys only standing or crouching), nor does the right
     button turn us (the left still circles the chase camera); forward stands us up (and closes
     the book) and we move off at once, getting up playing out all the same. In first person the
     view goes down and up with our head. Still to do: sitting's idle and fidgets (P07, the sitting
     fidget), kneeling (105), feign death,
     meditating's regen, and the zone putting us in a stance (stunned, knocked out).
   - Hotbuttons: the Actions window's buttons and socials go on them and come off by holding
     them, and they're kept in `<char>_<server>.ini` (`frontend/game_windows.*`). Still to do:
     items, spells, pet commands and AA abilities on them, whether an ability's ready (checked
     while it isn't), and right-click on anything but a social.
   - Socials: done (defaults, the editor, running them with `/pause`, `[Socials]`). Still to do:
     the edit boxes take typing only at their end (no caret to move, no Tab between them), and
     a social's lines go to InterpretCommand as typed, without eqgame's switch to the main chat
     window's channel while it runs.
   - ~~Compass~~: done (`frontend/compass.*`, eqgame's CCompassWnd): the strips slide by our
     heading, and the needle wanders with a Sense Heading under 200, and Toggle Compass (Alt+D)
     opens and closes it, and using Sense Heading points it true for 5 s (CCompassWnd::SenseHeading).
   - ~~Selector bar~~: done (`SelectorToggles` in `frontend/frontend.cpp`): its buttons open and
     close their windows.
3. ~~**A real chat window**~~: done (`frontend/chat_window.*`, `chat_text.*`, `chat_commands.cpp`).
   Still to do: more windows and the channel map (eqgame's `CChatManager`: which window each
   channel goes to, set from the right-click menu; there were no tabs in this era), the chat font
   size, hold-to-repeat on the scrollbar's arrows, languages scrambled by skill
   (`HearBabel`), and the commands that still say they aren't supported (/who, /follow, /lfg...).
   DoPercentConvert (@00538110) isn't ported: %t (the target's name), %m (our pet's), %s, %o, %p
   (the target's he/him/his) and %r (its race) in what's said, told, shouted, emoted and in socials.
   Done in `chat_commands.cpp`: /consider (OP_Consider, PrintConsiderAssessment's text and con
   color), /time (Norrath's date as EQWorldData::AdvanceTime carries it on, and Earth's), /played,
   /motd, /afk, /anonymous, /assist (OP_Assist; the answer becomes our target), /random
   (OP_RandomReq and OP_RandomReply), /lootnodrop, /fastdrop, and the 88 socials
   (CEverQuest_send_social: OP_Emote, OP_Animation and our echo). Still to do:
   - /afk: answering tells with its message, and the chat server's "/afk on silent";
   - allowslashcommand's other checks (/safelock's eqstr 12095, the windows that block commands:
     IsNoSlashWnd and eqstr 12084), for /assist, /attack and the other commands that call it;
   - /played: the alarm's time left (no alarm window yet);
   - /consider: under the deity PvP rules (pvpcon 4) NotOnSameDeity lets a GM off, read here as
     our `gm` flag; EQPlayer+0x109 isn't checked to be IsGameMaster.
   - /random: leaving out the rolls of someone we ignore (CEverQuest_BeingIgnored; no /ignore yet).
   - socials: the 60 unit check is our 3D SpawnDistance (eqgame's CDisplay::PlayerDistance with
     its last argument 0, not checked to be the same), DAT_007985f1 (a flag that stops them
     all) isn't known, and OP_Emote goes out with 0 in front where send_social puts 0xffff.
4. **Cursor and dragging.** EQ's own cursors. (An item on the cursor follows it:
   `DrawCursorAttachment`.) Tooltips: done (`WindowManager::DrawTooltip`, eqgame's DrawWindows and
   DrawTooltipAtPoint): after the mouse rests 500 ms, the control under it shows its tooltip, its
   TooltipReference or what its window sets: an inventory slot's item name (an empty worn slot's
   name), a spell gem's "1) Spell", and Alt held shows every gem's (DrawSpellButtonTooltips).
   The buff window's: the spell's name. Still to do: the key bound to a
   button after its text (SetKeyTooltip, UpdateTooltipText, from CDisplay_KeyMapUpdated), the help texts
   FindWnd's other hit codes ask for (GetHelpTextFromStringTable), and the slots of windows we
   don't have yet (the world container's).
5. **Options window.** The Keyboard page is done (`frontend/options_window.*`, COptionsWnd's
   keyboard page; see 6), with a combo box (CComboWnd) in `frontend/sidl.*`. Still to do: the
   General, Display, Mouse, Filters and Colors pages, which show empty (SyncGeneralPage,
   SyncDisplayPage, InitKeyboardAssignments' mouse settings, SyncChatPage, the color page and
   SaveColors); the Keyboard page's temporary Implemented column and the wider window it needs come
   out once every command is done. A combo box's list doesn't go when the click is outside its
   window, and has no scrollbar when ListHeight cuts it short (CListWnd's style 1).
6. ~~**A configurable keymap**~~: done (`frontend/key_map.*`, eqgame's two maps,
   `default_key_bindings` with W A S D, `ProcessMovementKeys`, `ProcessKeyUp`, ExecuteCmd's
   typed-character rule, `[KeyMaps]` in my-client.ini as `_writeKeyMapToIni` writes eqclient.ini's;
   `InputEvent::scan` carries the DirectInput code). Still to do:
   - ExecuteCmd's commands not done yet (the page's "No"): Toggle mouse look, Duck, Use centerscreen, Center view, Zoom in and out held
     (ProcessControls' camera distance at 2 units a frame unit), Toggle UI Display (ScreenMode 3),
     Target party member 1..5 and a second press of Target myself taking the pet
     (setTargetBasedOnIndex), Target nearest PC and NPC (CDisplay::GetNearestPlayerInView, 500
     units), Toggle netstat, Send tell, Reply, Cycle reply and Send tell to target (the chat line
     opened with `/tell `, `/tell <last> `, `/rtarget <last> `; eqstr 12364 and 12365 with no
     tells), Toggle last two targets, Who, Invite/follow, Disband, the clip plane, voice input,
     screenshots, the chat line's cursor and page up and down (CChatWindow::ScrollChatWindowUp),
     Command mode say and emote (`'` and `:`), the AA, bazaar, bazaar search, raid and music player
     windows, and the six camera moves (ExecuteCmd 0x6f..0x74, ProcessControls).
   - Look up and down tilt 5 degrees a press with Windows' key repeat; ProcessControls speeds the
     pitch up while the key's held (DAT_0079974c up to 12 a frame unit, to 128).
   - CDisplay_KeyMapUpdated: the buttons' key tooltips (CXWnd::SetKeyTooltip,
     CActionsWnd::KeyMapUpdated_UpdateActionWindowKeyTooltips, CSelectorWnd::KeyMapUpdated).
   - Character select's explore mode takes only the movement, Jump and Toggle camera view
     commands from the keymap (ProcessNewUICharacterSelectKeys runs ProcessMovementKeys).
7. **Map window.** Draw the line maps from `../Maps` with our position and nearby spawns.
8. **Loading screen** while zoning, with progress.
23. ~~**Character select**~~: done (`frontend/frontend.cpp`, `frontend/charselect_view.*`, eqgame's
    CCharacterSelect and CEverQuest::DoCharacterSelection @0053b9cf): explore mode
    (`EnterExplorationMode`, `LeaveExplorationMode`, `ExploreModeWindow`, the other characters
    round us, walking through `PlayerMovement`, F9's first person and chase camera, a click on one
    selecting it, Enter and Esc), Delete's eqstr 10019 in the confirmation dialog with the buttons
    off while it's up and the option file deleted, Up and Down wrapping round, eqstr 1947 on empty slots, eqstr
    1954 back from an unavailable zone, "Newbie" with nobody selected, the user light and the
    room's hour lighting the scene with the race's infravision or ultravision, load2's switches,
    and the old load zone's places. Explore mode's others hold their torches' flames, and the
    wheel zooms its camera (ours). Character creation's messages are the confirmation dialog, the
    face picker cycles a Luclin model's hair, beard, eyes and woad (`CFacePick::CycleThroughFHEB`),
    sent in `OP_CharacterCreate`, and the random features are rolled for the new race (ours).
    Still to do:
    - Explore mode's animations are only standing, walking, running, jumping and falling (not all of
      `ProcessAnimation`: swimming, crouching, the shuffle turning on the spot), and there are no
      footsteps or clicking a door (`CEverQuest_ClickedSwitch`); what
      `CExploreModeWnd::Activate` does to EXW_ExploreModeWindow3 (`CXWnd::_`) isn't known.
    - load2's particle effects: its switches of type 53 (`CRATEG`, the parameter the effect), which
      `ZoneDoors` doesn't do.
    - The face picker in the zone (`/face`: CFacePick's Accept sends 0x4225 with the face, hair and
      beard, and says eqstr 0x2efc).
24. **Auto attack and melee.** Done (`frontend/combat.*`, `FrontEnd::ToggleAutoAttack`): auto attack
    (`__Attack`) goes on and off by the Autoattack key, the Actions window's Attack button
    (CombatPageWndNotification) and /attack (ExecuteCmd 8's checks and eqstr 12368..12371), off
    sitting or otherwise not standing (DoPassageOfTime), opening a bank or asking a merchant (1466),
    entering a zone, our target dying or going and our death; on with AttackOnAssist; DoPassageOfTime
    sends OP_AutoAttack2 and OP_AutoAttack as it changes. The combat music follows it
    (/combatmusic, `CombatMusic`); the player window's red lasso blinks (UpdatePlayerWindowState); a
    trade asked meanwhile is busy (msg_req_trade). The zone's swings of ours play on our model with
    their sound. OP_Damage that isn't a spell's: ReportSuccessfulHit's hits, misses and defences,
    the skill's or race's verb, the environment's damage, their colors, the victim's D02 (or the
    environment's animation) and get-hit, flesh-hit and block sounds, the attacker becoming our
    target, a hit on us closing the book, standing us up and taking its HP at once. OP_Death:
    ReportDeath's killing blow and death lines, closing a dead partner's trade, merchant or bank.
    Checked live against a sewer rat in South Qeynos (`eqclient --attack`) and through its replay.
    Skills (`frontend/abilities.*`, `frontend/skills.*`): InitializeSkillData's table (taken from the
    exe by running its decompile), HasSkill, the Actions window's ability and combat skill buttons
    ([Abilities], [CombatSkills]) and SkillsSelectWindow to pick them, /doability, the ability
    timers and the combat timer (hastened), UseSkill's combat skills through DoAttack 100 and
    CanIHitTarget to OP_CombatAbility (checked live: a kick), and forage, sense heading, mend, taunt,
    intimidation, disarm, disarm traps and sense traps. /filter and /serverfilter
    (`frontend/filters.*`, [Filters]) sent to the zone as OP_SetServerFilter. A melee hit on us
    takes off our invisibility and pushes us (IWasHit); our death lays us down (D05) to the death
    music, and nothing moves us.
    Still to do:
    - range attacks: the Range button and the hot button's (CombatPageWndNotification's
      `__RangeAttackReady`, DoAttack 0xb: the bow's delay and a quiver's haste, too close (12698),
      too far above or below (12699), the bow's and arrow's range in CanIHitTarget, HandleAmmo,
      DoMissileHit's OP_CombatAbility {target, 11, skill}, OP_DeleteCharge 0x4147 or OP_MoveItem
      for what's used up, the bow in the hand while shooting), and throwing; the zone's
      RangedAttack wants the ammo it's told of;
    - ProcessControls' own swings while auto attack's on (DoAttack 0xd primary, 0xe the off hand,
      on `__RangeAttackReady` and DAT_007cd845): their timers, the fatigue, CanIHitTarget's says
      ("too far away") as well as the zone's;
    - UseSkill's sneak, hide (DAT_00798780 and its breaking on moving, 12682), feign death (stance
      115), bind wound, begging, pick pockets, tracking (CTrackingWnd), fishing; the AA abilities
      (105, 107, 111) and the timers' AA reductions (GetAbility 0x79, 100), being on a horse
      (12393), stunned (125), AllowedToAttack's duels, the server's PvP types and the water;
    - /filter's client side: the spell lines by PC spells, NPC spells and bard songs
      (ReportSuccessfulHit's spell branch, StartCasting), the chat channels and the bad word filter
      (msg_new_text), and the hit modes (/hitsmode, CChatManager's MyHitsMode and the rest: eqstr
      2130's short form, the number only);
    - the blood of our crushing, slashing, piercing, backstab and bash hits (SpurtBloodOnDag);
    - ReportDeath's duel ending (eqstr 12100, 12101), a group member's HP, the tracking window; our
      death's first person view (still standing height), KnockedOut's "You have been knocked
      unconscious!" (12877) and ClearOutRentables' temporary items;
    - being hit on a mount (DeacSpellScreen, ChangeStance), and the guildmaster window turning auto
      attack off (pinstActiveGMaster) once there is one;
    - the haste ModifyAttackSpeed divides by is `Buffs::Total(11)`, a sum of our buffs' SPA 11:
      TotalSpellAffects' own SPA 11 branch (@004c6b6d) takes the best spell haste, the best worn
      item's (110 at most under level 26), the worst slow, overhaste (SPA 98, over level 49) and
      bard haste (SPA 119), capped by level; worn and over haste aren't ours yet.

## Rendering / presentation

9. ~~**Nameplates**~~: done (`frontend/name_sprites.*`, eqgame's name sprites), lit as
   renderNameTag does (the world's ambient light +0.4, `NameSpriteLit`). Still to do: the trader tag in the Bazaar,
   the names eqgame hides in some animations (skeletons, gargoyles and mimics lying or feigning:
   animations 0x10, 0x21, 0x26), `/shownpcnames`, Zeal's raid shownames, and the guild list in packet logs (replays show "Unknown Guild"). An HP
   bar over heads isn't eqgame's; leave it to an option if ever.
10. ~~**Target indicator**~~: done, as eqgame does it: the target's name flashes from white back
   to its color. The client of this era had no target ring and no hover highlighting.
11. ~~**Ground spawns and dropped objects**~~: done (`frontend/ground_items.*`, `ZoneView`'s ground
    items, `CharacterModels::BuildItem`): what's on the ground (OP_GroundSpawn's EQItemList) drawn
    from its item model on the floor under it (IT63 for a missing one, CreateActor's sizes for the
    tradeskill containers, its particle clouds), a click on it within 20 picking it up
    (OP_ClickObject) or opening a tradeskill container (OP_ClickObjectAction), an item on the cursor
    clicked onto the ground dropped (the confirmation dialog, DropHeldItemOnGround), and what the
    zone takes away (its OP_ClickObject). Checked live: the zone's ground spawns and forges drawn, a
    drop, the forge opened and closed. /fastdrop sets whether a drop asks first (`FastDrop`). Still
    to do: coins dropped on the ground (DropHeldMoneyOnGround), CreateActor's models from
    the zone's _obj.s3d and the `_chr` files (only gequip's are found; the server's data has
    none that would show), `RED_BLOB_ACTORDEF` when even IT63 is missing (a sprite), and
    CreateActor's light argument (whether a lit item on the ground lights things).
    Clicks follow GetClickedActor (the nearest model triangle under the cursor) for spawns, our
    model and ground items; doors are still asked after them, not in the same nearest-first test.
12. **Spell visuals.** Done for anyone's casting as the zone shows it (`frontend/spell_casting.*`,
    `frontend/spell_effects.*`, `render/spell_particles.*`): OP_BeginCast's "begins to cast a spell"
    (CEverQuest_StartCasting), the caster's CastingAnim and the effect on the target from OP_Action
    (EQPlayer::TriggerSpellEffect), the landing text and the target's TargetAnim from OP_Damage
    (CEverQuest_ReportSuccessfulHit), and the Luclin particle effects (SpellsNew.eff stages 0 and 2,
    spellsnew.edd emitters, eqgfx's CParticleSystem) on the caster's and target's bones, or the
    older `spells.eff` effects (gequip's sprites circling the target and eqgfx particle clouds) for
    the spells without a SpellsNew.eff record (the debug menu's "Luclin spell effects" off gives every
    spell its old one, as eqgame without SpellsNew.eff), the old sprite flashes' lights and the
    stages' sounds (game sounds; the debug menu's Sound section lists them), and missiles
    (OP_Projectile, `frontend/missiles.*`: bolts, arrows and thrown weapons with their models, lights,
    the caster's animation and a spell's stage 1 on them). Still to do: a missile's old (spells.eff)
    stage 1, blit-sprite actors as a missile's model (`GENC00_ACTORDEF`: only item meshes are built;
    the server sends `IT68`), the static kind 9 missiles' checks on who walks into them (MoveMissile's
    sentinel and trap spells), our own archery and throwing making their missile (DoAttack), the options'
    spell particle density and opacity (fixed at eqgame's 1 and 1), the chat filters for PC and NPC
    spells and bard songs, and OP 0x4265 (destroy a cast's emitters; this server never sends it).
13. **The other camera modes F9 cycles through:** overhead and the two user-set cameras, plus zoom
    and field of view.
14. **Graphics options:** fullscreen or windowed, resolution changes, gamma, anisotropic filtering,
    MSAA, draw distance and level of detail.
15. **Screenshots.**
21. **Luclin player models.** Done (`render/character_model.*`): `UseLuclin*` in my-client.ini
    picks them per race and sex (off by default), loaded in `CDisplay::InitWorld`'s order and rules;
    their variant tracks (by what they hold, `SetAnimVariation`) and source models, the `_LAYER` textures, faces, eyes
    (`CDisplay_SetEyeMaterial`), the Vah Shir's fur by face, robes, and the helm, tunic, robe,
    shoulders, gauntlets, belt, boots, hair and beard as item models on their point bones
    (`CDisplay_SetDefaultITAttachments`, `SetFHEB`) playing the body's tracks
    (`UpdateAttachmentAnimations`). They never take the old models' `Global<n>_amr` Velious
    textures, even with `LoadVeliousArmorsWithLuclin`; they wear what those stand in for
    (`BodyMaterial`). Unset hair (eye color 0xff) gets `SetDefaultFacialFeaturesByFace`'s defaults
    from `CreatePlayerActor`. Still to do:
    - `PlayAnimationTrack2`'s Luclin speeds: idles, sitting and standing (and a woman's 17) at
      half speed, socials at three quarters, at the normal speed.
    - A body slot's material of 200 and up on a Luclin model (`CDisplay::HandleMaterialEx`: IT<material>
      on the slot's point through `CDisplay_UpdateItemSlot2`, plate under it).
    - Turning the models on and off in game (an options page); only my-client.ini, read at start.
    - `AllLuclinPcModelsOff` as InitWorld forces it under 129 MB, and the expansion flags
      (`DAT_007cf1f0`/`f4`) `ShouldLoadNewPcModel` checks: we take them as set.
    - `frontend_preview` doesn't read the keys (`scene_preview --luclin` does).
    - Zone `_chr` files are searched before the global ones (ours); eqgame keeps the first loaded,
      the globals.

22. **Player collision.** Done as `CDisplay::MovePlayer` does it (`frontend/player_movement.*`,
    `ZoneScene::SphereHit`): the actor's sphere (`CreatePlayerActor`'s radius) lifted by the step
    height sweeps the move and slides once more along the surface, then `GetPlayerFloorHeight`'s
    sphere drop and `PutPlayerOnFloor`. Still to do:
    - `CanIFitHere`: the move is undone where floor to ceiling (`GetCeilingHeight`) is under twice
      the `ModelHeightOffset` + 0.5, and a duck stands back up when there's room again.
    - `DoFloorCheck` with no floor below keeps the last one and we fall; we stay put instead.
    - MovePlayer's other branch (mounts, boats and races 72/73/114/141, no lift, the vehicle
      moves), and `MovementSpeedZ` held to -1 when the move didn't change our height.

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
