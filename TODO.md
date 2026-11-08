# TODO

Work after 1.0 that isn't game logic: the client showing and handling what the server sends, with
no rules of its own.

## UI

1. ~~**In-game SIDL windows.**~~ Done: `frontend/window_manager.*` (drag, resize, close, minimize,
   focus order, fading, `UI_<char>_<server>.ini`, BGTint and BGType drawn, the window menu and
   the color picker: `CContextMenuManager`, `CColorPickerWnd`). Still to do: the windows' own
   menus over the default one (`CPlayerWnd::CreateLocalMenu`, `CGroupWnd`'s, the chat window's
   `CChatManager::InitContextMenu`), Lock for windows that can't be locked (CXWnd 0x13, always
   allowed here), and only the chat window's, the social editor's, the quantity window's and the
   color picker's edit boxes take typing. The
   resize and drag cursors are the SIDL ones; the usual cursor is still eqmain's `cursor.bmp`
   rather than `A_DefaultCursor`.
2. **Display-only windows**, showing what the server already sends:
   - ~~Player window~~ and ~~target window~~: done (`frontend/game_values.*`, gauges in `sidl.cpp`).
     Targeting ourselves: F1 and clicking our model in the chase view. The XP and AA gauges
     (EQTypes 4, 5) are OP_ExpUpdate's and OP_AAExpUpdate's 0..330 * 3.03031. Still to do: max mana
     leaves out items' worn effects and `AntiTwinkAdj` for items with a recommended level.
   - Group: done (`frontend/group.*`, the zone's OP_GroupInvite, OP_GroupCancelInvite,
     OP_GroupFollow and OP_GroupUpdate in `ZoneSession`): msg_wldgroup's actions,
     CheckForGroupChanges on each profile, CEverQuest_doInvite and doUnInvite, CEverQuest_Invite,
     AcceptGroupInvite and CEverQuest_Disband with their messages, from /invite, /disband, the group
     window's and Actions window's buttons and the Invite/follow and Disband keys; the group window
     (`CGroupWnd::UpdateButtons`: Follow and Decline while invited, Invite and Disband enabled as
     eqgame enables them) with the members' names and HP gauges (EQTypes 11..15; out of the zone,
     -2) and their pets' (17..21); a gauge clicked targets by its EQType (setTargetBasedOnIndex),
     Target party member 1..5 a member and then their pet, and the leader's Disband removes the
     member selected (by those or a click on them in the world). /makeleader (@004f397d,
     OP_GroupUpdate 8) and /lfg (@004f9518, OP_LFGCommand; off on joining, sent again on entering a
     zone). Checked live with a second
     player (`eqclient --group-invite`, `--group-accept`; `frontend_preview ... group`). Still to
     do: the raid's branches (CRaid: IsInRaid, IsRaidGroupLeader, eqstr 5087, 5086, 5088's why 4
     and 5), the group window's menu (`CGroupWnd::CreateLocalMenu`: its pet gauges toggle, 0x32),
     CEverQuest_Disband sent as a camp ends (ProcessGameLoop's 0xca; our session's already
     closed), and AcceptGroupInvite's OP_GroupFollow2 (0x4042) for an invitation that came as
     OP_GroupInvite2 (the zone never hands one on as that; we always send OP_GroupFollow).
   - Buff window: done (`frontend/buffs.*`, `game_windows.cpp`): our buffs from the profile, those
     landing on us (HitBySpell's FindAffectSlot stacking and overwriting), `OP_Buff`, the 6 s tick
     and its expiry, clicking a beneficial one off, their icons on red or blue, names (tooltips and
     EQTypes 45..59) and the blink in their last ticks. Their effects count in the stats, resists,
     AC, max HP (worked out as `Max_HP` does, not the zone's), max mana and our speed (SoW,
     snares, roots). Still to do: the effects that act as a buff
     lands or fades (levitation, invisibility, see invisible, infravision, illusions, Eye of Zomm,
     mounts) and a bard's instrument mastery. Alt+click or the right button held on a buff shows its
     spell's name and icon in the item display (HandleSpellInfoDisplay), and Alt over the window
     shows every buff's tooltip (CBuffWindow::PostDraw, `FrontEnd::DrawBuffTooltips`).
   - Inventory: name, level, class (its title from 51), deity, HP, stats and resists (base, worn
     items, the next food and drink; `GameValues::Stat`, `Resist`), AC, ATK and weight (`GameValues::Ac`, `Atk`, `Weight`), money, the class picture, item icons in the slots, the Alt. Adv. row from 51, Done, and the
     Skills button opening the skills window (`frontend/game_windows.*`), and items picked up, put
     down, split through the quantity window and put away on the character view (`core/inventory.*`).
     Still to do: for stats, resists, AC and ATK, items' worn effects, ATK's buffs, AAs,
     the Resistant Discipline, drunkenness and berserking; Alt. Advance, icons in the hot button
     window's weapon slots, and FastItemDestroy for Destroy (the Options General page's checkbox;
     Destroy always asks until there is one).
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
     slot's tooltip. Activate makes us visible (MakeMeVisible) and plays the bank's music
     (SetMusicSelection 3) until Deactivate. Still to do: Deactivate's SavePC.
   - Merchant window: done (`frontend/merchant_window.*`, eqgame's CMerchantWnd): the right button
     on a merchant asks for its shop (OP_ShopRequest), the answer opens it with its items
     (OP_ShopInventoryPacket, OP_ShopDelItem) in the scrolling slots, a
     click on any slot selects it (a yellow outline) and says its price, Buy and Sell (the quantity
     window for a stack) with eqgame's checks and messages, OP_ShopPlayerBuy and OP_ShopPlayerSell
     and their answers (the price out of the purse, the sale's coins in and the item gone), Done,
     the close box, Escape and walking away (OP_ShopEnd). Checked live against the server for
     opening, the prices and the welcome; buying and selling only through their refusals (the
     test character has no coins or anything worth selling). Still to do: a Bazaar trader's
     (Type 0: OP_TraderShop, OP_TraderBuy), Deactivate's SavePC, and the item display of a
     merchant's item (the slots' alt+click goes through the selection now). The right button makes
     us visible (MakeMeVisible) and the window plays the merchant's music (SetMusicSelection 6).
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
     bags in a trade window (BagTrade1..16 open, but their contents aren't checked) and
     OP_TradeReset. The zone's cancel runs Deactivate, which cancels again and says "You have
     cancelled the trade." (1450) after the zone's line, as msg_cancel_trade does.
   - The right button on a spawn (CEverQuest::RMouseUp within 500 ms, RightClickedOnPlayer): a
     corpse's loot, a banker's bank, a merchant's shop, else /consider of our target. A
     despawning merchant, banker or corpse closes its window. Our class's guildmaster asks the zone
     to train us (OP_GMTraining) and another's says eqstr 1467. Still to do: a trader in the Bazaar
     (OP_TraderShop), a horse (race 141), the inspect of a player (doInspect), and `_consider`'s
     TargetType refusals (0xb, 0x3c, 0x41, 0x43) after it.
   - Guildmaster training: done (`frontend/train_window.*`, CTrainWnd): msg_req_guildmaster's
     SetGMData and Activate, UpdateSkill's rows and CostToTrain's prices, the headings' sorts,
     Train through BitchCanTrain (`frontend/skills.*`: setskill, GetSkillLimit2,
     EQ_PC_GetKunarkThreshold, EQ_PC_checkLang) and OP_GMTrainSkill, OP_GMEndTraining on leaving,
     the coin pictures as list headings, and OP_LevelUpdate's 5 practices. Still to do:
     setskill's Trade Skill Mastery (GetAbility 0x8d, taken as 0: no AAs yet).
   - Bag windows: done (`frontend/containers.*`, eqgame's CContainerMgr and CContainerWnd): the right
     button on a bag opens or closes its window, laid out for its slots with Combine and Done
     under them, kept in the layout as BagInv1..8 (BagBank, BagTrade), picking up or swapping out
     a bag closes it, Escape closes them all, Alt shows every slot's tooltip, and Combine sends
     OP_TradeSkillCombine with the class and racial containers' checks, the slots locked until the
     zone answers. World containers (a forge clicked on the ground, msg_chest_lock): BagWorld1,
     named by type (OpenWorldContainer), their items from OP_ObjectItemPacket at 4000 + n,
     OP_ClearObject emptying them, Combine as slot 1000, closed by Done, Escape or walking off 20
     (CContainerMgr::Process) with OP_ClickObjectAction, and not closeable with a NO DROP item in
     (CheckCloseable). Still to do: CanGoInBag's checks for a world container's slots, and the bag's
     open flag (EQ_Container::Open).
   - Equipping: our model's armor, weapons and light follow what we have on (`ZoneSession::UpdateAppearance`).
     Still to do: dual wield
     from the class table rather than the zone's 254/255, the casting check (no moves but from the
     cursor while casting), /showhelm, and a carried light going out underwater (`UpdateLight`'s
     check of the body's environment; it's only not drawn there now).
   - The item on the cursor: clicking a spawn with it (or coins) gives it to them through a trade
     (`DropItemOrMoneyOnPlayer`, below), clicking the ground drops it (item 11), and AutoInventory
     with no room drops it (AutoGenInventory: "There was no place to put that!", eqstr 12937; a
     NO DROP item stays on the cursor). Holding a slot's item to make an item hot button (`CInvSlot::HandleLButtonHeld`), and
     the right button on a slot: done for food, drink and alcohol (`CInvSlot_DoDrinkEatPoison`:
     the messages, hunger and thirst to 6000, OP_Consume and OP_DeleteCharge with the stack taken
     down here; checked live) and items' click effects (`SpellCast::CastItem`, OP_CastSpell gem 10
     with the slot; not checked live, the test characters have none). Still to do: poisons
     (CEverQues_ApplyPoison), Alcohol Tolerance's skill-up (EQ_PC_CheckSkillImprove 66), what
     drunkenness does. Notes and books are read (EQ_Note::SendTextRequestMsg's OP_ReadBook;
     CNoteWnd's SetNote, checked live; CBookWnd's pages, buttons and keys in
     `frontend/book_window.*`, checked through the preview with a zone's book text). Still to do
     for books: check the lines' fit against the TAKP client on Windows (the preview's box font is
     wider than Arial), DrawFormattedText's own wrapping in place of measuring the line whole, and
     keyboard focus (the window takes Left and Right while it's on top, not while it has the focus).
   - Item display: alt+click or the right button held on an inventory slot shows its item
     (`frontend/item_display.*`, eqgame's SetItem text with spells_en.txt's spell names); letting
     the right button go closes it. A link clicked in the chat shows its item (the zone's
     OP_ItemLinkResponse), and clicking the icon links the item in the chat line
     (`CItemDisplayWnd::WndNotification`; so does Shift+click on a slot while typing, our own).
     Still to do: FV's always-no-drop item ids (`EQ_Item::IsSpecialNoDrop`), open bags, items in
     other windows (loot, trade, merchants), and the item+0xdc check WndNotification makes
     before linking (unknown field).
   - Skills window: the skills HasSkill gives us and our languages, listed as CSkillsWnd::Activate
     lists them, with their ranks (UpdateSkill: 254's "LEVEL: %1", 255 as HasSkill's value), kept
     up to date by OP_SkillUpdate. Still to do: selecting rows (CListWnd's selection), and checking
     a racial skill live (the test characters are human).
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
     - casting: stuns and divine aura (OP_Stun), the AA and focus cast time and range modifiers,
       abilities (gem 9), and a bard being hit (StopSpellCast 1);
     - the spell info display (done: `ToggleSpellDisplay`, CItemDisplayWnd::SetSpell, from a book
       slot, a gem or a buff): "Your Cast Time" (eqstr 3106) waits on the AA and focus casting
       time modifiers, and the hot button's call (CHotButtonWnd::WndNotification @00420462, with a
       third argument other than -1) on spell hot buttons;
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
     view goes down and up with our head. Kneeling (105: P05, up by P05 backwards) and lying (115,
     feign death: D05 held, up by P02 backwards) are ChangeStance's too, and nothing but standing
     or crouching moves us. Still to do: sitting's idle and fidgets (P07, the sitting fidget),
     meditating's regen, and the zone putting us in a stance (stunned, knocked out).
   - Hotbuttons: the Actions window's buttons and socials go on them and come off by holding
     them, and they're kept in `<char>_<server>.ini` (`frontend/game_windows.*`). Clicked or by
     key (Hot1..10) they do what their button does: Attack, Range, the combat skills and the
     abilities (CHotButtonWnd::DoHotButton), and they're checked while they aren't ready
     (OnProcessFrame: Attack and Range by __RangeAttackReady, a skill by its timer). Still to do:
     items, spells, pet commands and AA abilities on them, DoHotButton's refusal while stunned or
     DAT_007d0254 (string 0x2f34), and right-click on anything but a social.
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
   (`HearBabel`), and the commands that still say they aren't supported (/lfg, /rtarget...).
   /who (CEverQuest::Who: this zone's players filtered by class, race, levels, GM, LFG, guild or
   name, and our corpses; `all` through OP_WhoAllRequest and msg_who_response's lines), /whotarget,
   the Who key and button, and /follow (FollowPlayerAI's turning, running, walking and stopping,
   TurnOffAutoFollow on Move back and the turn keys) are done. Still to do: /who all friends
   (OP_FriendsWho with a friends list; no /friends yet), traders (ActorInfo's IsTrader), the
   spawn list's order (pinstSpawnList; ours by id), and /follow on a mount or of a rider
   (pinstCharSpawn, ActorInfo's Mount) and its line of sight from them to us (CastRay is ours,
   from us). DoPercentConvert (@00538110, `FrontEnd::PercentConvert`): %t, %m, %s, %o, %p and %r
   at a line's start or after a space or apostrophe, eleven at most (then '@'), on every line
   printed (`ChatOut`, as print_chat_dsp_chat) and on what's said, told, sent to a channel, emoted
   and sent by socials (SendNewText, CEverQuest::Emote, send_social). Still to do: stripName's
   ConvertNameToLocal (another server's names), and the lines we print through `AddZoneLog`.
   Done in `chat_commands.cpp`: /consider (OP_Consider, PrintConsiderAssessment's text and con
   color), /time (Norrath's date as EQWorldData::AdvanceTime carries it on, and Earth's), /played,
   /motd, /afk, /anonymous, /assist (OP_Assist; the answer becomes our target), /random
   (OP_RandomReq and OP_RandomReply), /lootnodrop, /fastdrop, and the 88 socials
   (CEverQuest_send_social: OP_Emote, OP_Animation and our echo). Still to do:
   - /afk: the chat server's "/afk on silent" (CEverQuest::ChatServerMessage), and the language
     and skill of its answer to a tell (CEverQuest_GetCurrentLanguage; ours common at 100, as all
     our tells);
   - allowslashcommand's other checks (/safelock's eqstr 12095, the windows that block commands:
     IsNoSlashWnd and eqstr 12084), for /assist, /attack and the other commands that call it;
   - /played: the alarm's time left (no alarm window yet);
   - /consider: under the deity PvP rules (pvpcon 4) NotOnSameDeity lets a GM off, read here as
     our `gm` flag; EQPlayer+0x109 isn't checked to be IsGameMaster.
   - /random: leaving out the rolls of someone we ignore (CEverQuest_BeingIgnored; no /ignore yet).
   - socials: the 60 unit check is our 3D SpawnDistance (eqgame's CDisplay::PlayerDistance with
     its last argument 0, not checked to be the same), DAT_007985f1 (a flag that stops them
     all) isn't known.
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
   keyboard page; see 6), with a combo box (CComboWnd) in `frontend/sidl.*`, and the Colors page
   (ColorPageWndNotification, SaveColors, RestoreDefaultColors, into my-client.ini's
   `[TextColors]`; /usercolor (@004fe3a2) sets, resets and lists them from the chat line). Still
   to do: the General, Display, Mouse and Filters pages, which show empty (SyncGeneralPage,
   SyncDisplayPage, InitKeyboardAssignments' mouse settings, SyncChatPage); the Keyboard page's
   temporary Implemented column and the wider window it needs come out once every command is done. A combo box's list doesn't go when the click is outside its
   window, and has no scrollbar when ListHeight cuts it short (CListWnd's style 1). Lists draw the
   row under the cursor yellow and the selected one on their selection color (CListWnd::DrawItem,
   DrawLine; red, or SetColors' gray in the train, skills and skills select windows); still to do:
   IsLineEnabled for lists other than the context menus', a multiselect list's own selected rows,
   and the mouse-over notification (0x14) DrawItem sends the list's parent.
6. ~~**A configurable keymap**~~: done (`frontend/key_map.*`, eqgame's two maps,
   `default_key_bindings` with W A S D, `ProcessMovementKeys`, `ProcessKeyUp`, ExecuteCmd's
   typed-character rule, `[KeyMaps]` in my-client.ini as `_writeKeyMapToIni` writes eqclient.ini's;
   `InputEvent::scan` carries the DirectInput code). Target nearest PC and NPC
   (CDisplay::GetNearestPlayerInView, 500 units from the camera, `FrontEnd::NearestInView` over
   `ZoneView::SpawnsInView`), our Cycle nearest target on Tab
   (Zeal's CycleTarget, command 213), Toggle last two targets (ExecuteCmd 63) on no key, Cycle
   reply forward and back (ExecuteCmd 60, 61, CycleNamesInTellList, AddNameToTellList's 50
   names). Target myself a second time takes our pet (ExecuteCmd 22: the first spawn of ours
   not a temporary pet, Spawn_Struct's temporaryPet). Send tell, Reply, Send tell to target and
   Command mode say and emote (ExecuteCmd 13, 59, 100, 87, 88; `FrontEnd::OpenChatLine`) open the
   chat line with `/tell `, `/tell <name> ` (`;tell` for a name with a dot), `/rtarget <name> `
   (Cycle reply's name; eqstr 12364 and 12365 with no tells), `'` and `:`, without the key's
   own character after it. Still to do:
   - ExecuteCmd's commands not done yet (the page's "No"): Toggle mouse look, Duck, Use centerscreen, Center view, Zoom in and out held
     (ProcessControls' camera distance at 2 units a frame unit), Toggle UI Display (ScreenMode 3),
     Toggle netstat, the clip plane, voice input,
     screenshots, the chat line's cursor and page up and down (CChatWindow::ScrollChatWindowUp),
     the AA, bazaar, bazaar search, raid and music player
     windows, and the six camera moves (ExecuteCmd 0x6f..0x74, ProcessControls).
   - Send tell for a GM helping a petitioner (`/tell <petitioner> `, CPetitionQWnd), and
     ExecuteCmd 0x81 and 0x82 (`;` and `/` command modes; not on the keyboard page).
   - Target myself's pet: eqgame takes the first in pinstSpawnList's order; ours the lowest id.
   - Target nearest and Cycle nearest target don't know a spawn's rider (ActorInfo's Rider): an
     NPC with a rider (a mount) isn't passed over, a horse (race 216) doesn't give its rider. They
     don't know whether we see invisible (EQ_Character::CanISeeInvis: SPA 13 and the races'), so
     an invisible spawn is never picked, nor a GM's VisibilityState.
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
    music, and nothing moves us; 2 s on, "Returning to home point, please wait..." (eqstr 539) and
    OP_ZoneChange to the bind zone with reason 10 (RealRender_World's DeathTimer, MoveToZone).
    Our own swings (`frontend/melee_attack.cpp`): ProcessControls swings each hand while auto
    attack's on and we have control, on `__RangeAttackReady` and DAT_007cd845 (DoPassageOfTime
    readies the secondary by its weapon, hand to hand empty, else the primary's delay), through
    DoAttack 13 and 14: its hastened delay (a monk's and beastlord's fists by level), the stance
    messages, CanIHitTarget's ("too far away", "cannot see"), and the fatigue a swing or shot costs
    by the weapon's weight; any attack ends invisibility and hiding (eqstr 12682) and puts the
    weapons back in hand after shooting. Haste is TotalSpellAffects' SPA 11 branch
    (`Buffs::AttackSpeed`): the worst slow, else the best spell and worn haste (110 under level 26)
    less the doubled 100, overhaste, bard haste, capped by level. UseSkill's sneak (OP_Sneak, the
    zone's SpawnAppearance 15, InitSneakMod's speed), hide (OP_Hide, VisibilityState through
    UpdateMyVisibleStatus, ended by moving, attacking or using it again), feign death (stance 115,
    OP_FeignDeath), bind wound (kneeling, OP_Bind_Wound and its answers 1432..1436), begging and
    pick pockets (one timer, msg_beg and msg_steal's coins and stolen item), fishing (pole and bait
    checks, the cast's animation, DoFishingEvent's hook and splash sound, moving calls it off) and
    tracking's OP_Track (`frontend/abilities.*`). Checked live: the swings' messages, sneak, and
    bind wound's, begging's and fishing's refusals; hide, feign death and pick pockets not (the
    test rogue is level 1).
    Still to do:
    - range attacks: done (`frontend/ranged_attack.cpp`): the Range button and a hot button
      holding it when `__RangeAttackReady` (DoPassageOfTime: the primary's delay, or the range
      item's after a shot, hastened, a bow's by a quiver too), DoAttack 11's checks (standing, too
      close 12698, too far above or below 12699, CanIHitTarget with the bow's and arrow's reach),
      HandleAmmo (12875; a stack of throwing weapons split to the ammo slot), DoMissileHit's turn
      to face them and OP_CombatAbility {target, 11, skill}, the ammo used (OP_DeleteCharge, the
      last of a stack taken from the general slots or bags) or the weapon thrown (OP_MoveItem to
      -1), and the bow held while shooting. Not checked live: none of the test characters has a
      bow or anything to throw. Still to do: AimAtTarget's pitch, the Endless Quiver AA (GetAbility
      0x54) and our own missile drawn (EQMissile);
    - DoAttack's stunned states (StunnedState 1 says eqstr 125; 2..4 and 7 nothing), the
      ActorInfo +0xc8c+0x74 and DAT_007985f0 refusals, and DAT_00798560 (CanIHitTarget always
      passing);
    - the tracking window (CTrackingWnd, TrackingWnd: GenerateTrackingList within the skill's range,
      DoTrackSort and DoTrackFilter with /tracksort, /trackfilter and /trackplayers, the colors of
      GetTrackingColorIndex, the Track button setting DAT_007989e2 53 for 6 minutes, eqstr 12040)
      and DoPassageOfTime's direction lines every 2 s and its end (12674..12681) and the lost target (12499);
      UseSkill only sends OP_Track;
    - the AA abilities (105 Harm Touch and 107 Lay on Hands: CastSpell gem 9 of spell 88 and 87, the
      profile's timers at +0xc7a and +0xc7e; 111 Slam as bash), the timers' AA reductions
      (GetAbility 0x79 for sense and disarm traps, 100 feign death, 0xd1 hide), the hiding AA
      (CheckHidingSkillBonus), a real mount (ActorInfo's Mount; only the SPA 113 buff here),
      stealing under the PvP deity rules (DAT_006ec740 4: NotOnSameDeity, the level range, 12412,
      12413), being stunned (12407), fishing's splash particles (DoParticleEffect 2) and the pole
      shown in hand as the cast starts (UpdateItemSlot), and someone picking our pockets (msg_steal
      0xff: 12897 and the answer);
    - AllowedToAttack's duels, the server's PvP types and the water;
    - /filter's client side: the spell lines by PC spells, NPC spells and bard songs
      (ReportSuccessfulHit's spell branch, StartCasting), the chat channels and the bad word filter
      (msg_new_text), and the hit modes (/hitsmode, CChatManager's MyHitsMode and the rest: eqstr
      2130's short form, the number only);
    - the blood of our crushing, slashing, piercing, backstab and bash hits (SpurtBloodOnDag);
    - ReportDeath's duel ending (eqstr 12100, 12101), a group member's HP, the tracking window; our
      death's first person view (still standing height), KnockedOut's "You have been knocked
      unconscious!" (12877) and ClearOutRentables' temporary items;
    - going home dead: CDisplay_SwitchToDefaultCameraMode, PrepareLocalPCForRepop's group and raid
      reset (SetInvited, CRaid::ResetFromDeath), MoveToZone_2's "Your Home Zone is unavailable"
      fallback when the zone refuses (0x4150), EQ_3DView_MouseUp's clicks while the DeathTimer runs,
      and HandleWorldMessage's "struck down by the gods" (1391) resetting it; the return checked
      live only through a recording's replay;
    - being hit on a mount (DeacSpellScreen, ChangeStance), and the guildmaster window turning auto
      attack off (pinstActiveGMaster) once there is one;
    - TotalSpellAffects adds the worn items' effects only for attack speed; the other SPAs
      (`Buffs::Total`) leave them out.

## Rendering / presentation

9. ~~**Nameplates**~~: done (`frontend/name_sprites.*`, eqgame's name sprites), lit as
   renderNameTag does (the world's ambient light +0.4, `NameSpriteLit`), on the animated head point
   (DagHeadPoint, renderSceneHierarchy's lift) and depth tested pixel by pixel in the 3D pass
   (`ZoneView::DrawNameSprites`). Still to do: the trader tag in the Bazaar,
   the names eqgame hides in some animations (skeletons, gargoyles and mimics lying or feigning:
   animations 0x10, 0x21, 0x26), `/shownpcnames`, Zeal's raid shownames, and the guild list in packet logs (replays show "Unknown Guild"). An HP
   bar over heads isn't eqgame's; leave it to an option if ever. Character select's name is still
   drawn on the 2D canvas (`DrawNameSprite`) at the model's top, not the head point. A 1-2 pixel
   size difference from the TAKP client was reported; the size math (texels x 0.05, focal / depth,
   20 units nearest) and font 5 (Arial 20, `MakeFont`) match eqgfx's, so the cause isn't found:
   compare against a screenshot (the zone's 90 degree field of view against eqgfx's lens is a
   candidate).
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
    to do: coins dropped on the ground (DropHeldMoneyOnGround: OP 0x4007 with an EQMoneyList,
    which EQMacEmu has no handler for, so it waits on the server), CreateActor's models from
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
