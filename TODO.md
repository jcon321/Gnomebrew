# TODO

Work after 1.0 that isn't game logic: the client showing and handling what the server sends, with
no rules of its own.

## UI

1. ~~**In-game SIDL windows.**~~ Done: `frontend/window_manager.*` (drag, resize, close, minimize,
   focus order, fading, `UI_<char>_<server>.ini`, BGTint and BGType drawn, the window menu and
   the color picker: `CContextMenuManager`, `CColorPickerWnd`). Still to do:
   - The windows' own menus over the default one: a window can have its own (`AddMenu`,
     `SetWindowMenu`, checked and disabled items, `menu_opening` to fill it; the chat windows' is
     done, see 3). Still to do: `CPlayerWnd::CreateLocalMenu` (@0042ef32) and
     `CGroupWnd::CreateLocalMenu` (@0041f30d) put a checked "Pet Display" (eqstr 3126) and a
     separator (`CContextMenu::AddSeparator`, `CheckMenuItem`) over "Window" (6643).
   - Lock for windows that can't be locked (CXWnd 0x13, always allowed here).
   - Edit boxes (`CEditWnd`, @005a1ed0 on): the keys go to one of them (a chat window's, the
     social editor's, the quantity window's, the color picker's, the friends window's and the text
     entry's: `FrontEnd`'s focus group). A single-line box's caret and selection are done
     (`frontend/edit_box.*`: HandleKeyboardMsg's Backspace, Delete, Shift+Delete and Shift with
     the arrows, Home and End; HandleLButtonDown, Shift+click and the drag; ReplaceSelection with
     FilterInputStr's markers kept out; the selection drawn inverse and the caret blinking).
     A multi-line box's caret, selection and scrolling are done too (`WrapTextToWidth`'s lines,
     `SelectableCharFromPoint` by row, Shift+Home and Shift+End on a line, `GetCaretPt`'s line-end
     caret, `CalculateScrollRange` and `EnsureCaretVisible`), and `SetEditable(false)` (the book's
     pages). `processClipboardText` is voice dictation: eqgame has no paste. Still to do: masked
     text (`GetMaskedText`), the IME, and the windows with multi-line boxes to type in that aren't
     built yet (the guild MOTD, the bug and feedback windows). The inspect window's bio and the
     player notes (29) are.
     FilterInputStr's AlphaNumericOnly (`edit_filter`: 1 and 2 letters, 3 digits, the Bazaar search's
     price boxes, 4 letters and digits) is done.
   - STML boxes (`CStmlWnd`; ours take `<BR>`, `<P>`, `<c "#RRGGBB">`, `&lt;`, `&gt;`, `&amp;`,
     `<a href="FILE:///...">` links and `<body>`'s TEXT, LINK, MLINK and ALINK): tables
     (`ParseHtmlTable` @00584540), the other kinds of link (`HandleStmlAction`'s 3, 5 and 6:
     `_BLANK` targets open a new STML window, `WNDNOTIFY`, `SOVREPORT`), VLINK's visited links,
     `<title>` (`ParseHtmlTitle` @00584220), the named colors and alignment of `ParseColorValue`
     (@00581650) and `ParseAlignmentValue`, the other entities (`InitializeHtmlEntityMappings`
     @00581b20). Nothing we show needs them.
   - Lists (`CListWnd`): a column's separator dragged on the lists that allow it (+0x13c: the AA
     window's and the raid window's; `SidlWindow::Press`, `SeparatorRect`, ShiftColumnSeparator's
     10 least, the resize cursor) is done. `LoadIniListWnd` and `StoreIniListWnd` only ever run
     for the Bazaar search window's ItemList (`<window>` section, `ItemList_ColWidth_<n>`), whose
     +0x13c nothing sets, so no widths are ever kept. Still to do: `DrawColumnSeparators` (each
     separator's 2-pixel line through the header, CListWnd+0x174's animation, not traced), `Sort`
     and `Compare` as the list's own (ours sort in each window), and, for a window that needs them
     (none of ours does: only the file dialog's list is multi-select, style 0x20000, and no list
     has an editable column, flag 1), `ToggleSel` and `ExtendSel` with Shift and Ctrl
     (HandleListItemClick @0057bff0) and editing a cell in place (`CloseAndUpdateEditWindow`
     @0057c530).
   - ~~UI skins~~: done (`LoadSidlSkin` in `frontend/sidl.*`, `FrontEnd::UseSkin`, `ReloadUI`,
     `DeactivateMainUI`, `BuildZoneWindows(reload, use_ini)`): `CDisplay::InitNewUI`'s skin, each
     file not in `uifiles/<skin>` from `uifiles/default` (`_XMLRead`, `getTextureRenderContext`), the
     default skin with "Error loading skin" when it doesn't load; character select's from
     my-client.ini's `[Defaults] UISkin`, the character's from the UI ini's `[Main] UISkin` as the
     world's entered; `CDisplay::ReloadUI` (@004a1465) from DoPassageOfTime after /loadskin, with
     `DAT_0062a78c`'s Use INI and eqstr 3146 from `IsAllValid` (`SidlWindow::CheckChildren`, the
     windows' `GetChildItem`). Still to do: `IsAllValid` is checked over our windows' set-up
     lookups (their `Attach`), which are near but not the same as eqgame's constructors'
     `GetChildItem` calls, and not over the chat windows'; InitNewUI's `EQUI_GUIDE.xml` for a GM
     when it's there (`pinstCEverQuest[0x89].field_0x57a`); a default skin that doesn't load keeps
     the skin we have, where InitNewUI says "Error in your GUI XML files" and exits;
     `ProcessGameLoop` reloads the UI at every main loop start (`ReloadUI(1)` unless a merchant's
     up), ours only as the world's entered.
   - The resize and drag cursors are the SIDL ones; the usual cursor is still eqmain's
     `cursor.bmp` rather than `A_DefaultCursor` (`CXWndManager::GetCursorToDisplay` @005a0690).
2. **Display-only windows**, showing what the server already sends:
   - ~~Player window~~ and ~~target window~~: done (`frontend/game_values.*`, gauges in `sidl.cpp`).
     Targeting ourselves: F1 and clicking our model in the chase view. The XP and AA gauges
     (EQTypes 4, 5) are OP_ExpUpdate's and OP_AAExpUpdate's 0..330 * 3.03031.
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
     player (`eqclient --group-invite`, `--group-accept`; `frontend_preview ... group`). The raid's
     branches (5087; doInvite's why 4 and 5; msg_wldgroup 7 and 9's CRaid::ResetFromDeath) ask
     `frontend/raid.*`. CEverQuest_Disband goes as a camp ends (ProcessGameLoop's 0xca). Still to do:
     the group window's menu (`CGroupWnd::CreateLocalMenu`: its pet gauges toggle, 0x32), and
     AcceptGroupInvite's OP_GroupFollow2 (0x4042) for an invitation that came as
     OP_GroupInvite2 (the zone never hands one on as that; we always send OP_GroupFollow).
   - Buff window: done (`frontend/buffs.*`, `game_windows.cpp`): our buffs from the profile, those
     landing on us (HitBySpell's FindAffectSlot stacking and overwriting), `OP_Buff`, the 6 s tick
     and its expiry, clicking a beneficial one off, their icons on red or blue, names (tooltips and
     EQTypes 45..59) and the blink in their last ticks. Their effects count in the stats, resists,
     AC, max HP (worked out as `Max_HP` does, not the zone's), max mana and our speed (SoW,
     snares, roots). What their effects do as they land and fade (`Buffs::effects`,
     `FrontEnd::BuffEffect`, HitBySpell's loop and RemovePCAffectex) is done for levitation (SPA
     57: Levitate's SpawnAppearance 0x13, no gravity and no jumps), invisibility (12, 28, 29:
     UpdateMyVisibleStatus), seeing invisible (13: CanISeeInvis in `InvisibleToMe`, a target lost
     as it fades), infravision and ultravision (65, 66: Vision1 and 2 over the race's), the lens
     (87: SetViewAngle), Bind Sight (73: SetViewActorByName, the camera from the target's eyes;
     1275..1277; no target 214 and the buff cleared), Locate Corpse (77: eqspa_locate_corpse),
     blindness (20 and 34: ActorInfo+0x18d, RealRender_World's ClearScreen over the world; 34's
     stays to the next zone), fear and charm by another (23, 22:
     `EQPlayer::HandoverControlToZoneserver`'s 1461; the zone's OP_FreezeClientControl does the
     rest) and the Eye of Zomm (67: ProcessAffects finds "Eye_of_<name>" each tick,
     `ZoneSession::PlayerByPartialName`, and it's pinstCharSpawn, walked by the keys with a
     PlayerMovement of its own, `ZoneView::Steer`, its OP_ClientUpdate sent with its id, and the
     view actor; RemovePCAffectex and ~EQPlayer of the eye hand us back), and
     HandleSpecialPCAffects(1) as we're made in a zone (the zone-only buffs, 67, 31, 58, 22, 23, 73
     and 64, taken off, an enchanter's illusion kept with Permanent Illusion; 20 and 34 blind us
     again). A spell with no duration landing on us does HitBySpell's own branches: Bind Affinity
     (25: `EQWorldData::IsNoBindZone`, `InSpecialBindSpot`, FV's rules and the zone's time type;
     ZoneBound set and SavePC, or 105 / 259), Gate (26: 98 in 100, our bind, MoveToZone 6, else
     260), Shadow Step (42: `CDisplay::IsValidTeleport` over 10001 random tries, else 12399) and
     the teleports (83, 88, 145: the spell's zone and base values, the evacuations to "same" at
     the safe point, Decession only from our group, Succor's 1 in 100; MoveToZone 7). Still to
     do: TurnInfravisionEffectOff's spawn ambient reset (ours redoes the glow each frame); a
     bard's instrument mastery; levitation on our horse (SetNoGrav on the Mount); the eye's
     `headEnvironmentChange` (the underwater sound and view follow pinstCharSpawn's head in
     eqgame, ours follow us) and RemovePCAffectex handing back a boat's helm too (ours only lets
     go of an eye); `GetEnvironment`'s other region types (ours: water, lava and the like);
     HitBySpell's other no-duration branches: SPA 0x51 (0x412a with the target's place), 0x33
     (the nearest trap switch), 0x34..0x35 (FindClosest tracking), 0x3d and 0x3e (the cursor
     item's lore and its stats, eqstr 0x30a2..0x30a5), and the illusion's change of form here
     (`eqspa_change_form` @004ca8b2, through `EQPlayer::do_change_form` @005074fa; races 142 and
     143 failing its own `GetNearestActorTag`, `ZoneView::NearestActorTag`, land no form; the
     zone's OP_Illusion does the form now). The new ones are checked on replay
     (`frontend_preview ... spells`), none live. Alt+click or the right button held on a buff shows its spell's name and icon
     in the item display (HandleSpellInfoDisplay), and Alt over the window shows every buff's
     tooltip (CBuffWindow::PostDraw, `FrontEnd::DrawBuffTooltips`).
   - Inventory: name, level, class (its title from 51), deity, HP, stats and resists (base, worn
     items, the next food and drink; `GameValues::Stat`, `Resist`), AC, ATK and weight (`GameValues::Ac`, `Atk`, `Weight`), money, the class picture, item icons in the slots, the Alt. Adv. row from 51, Done, and the
     Skills button opening the skills window (`frontend/game_windows.*`), and items picked up, put
     down, split through the quantity window and put away on the character view (`core/inventory.*`).
     The worn items' effects (`GameValues::SpellTotal`, TotalSpellAffects with the items), ATK's
     buffs and worn ATK, the Resistant Discipline (`OP_DisciplineChange`) and drunkenness (STR, STA,
     DEX, WIS, INT, to-hit and defense) count, and lore duplicates go (`ZoneSession::CheckDupLoreItems`,
     `DelLoreItemDup`) after the inventory, `OP_MerchantItemPacket` and a theft. Still to do: Alt.
     Advance, icons in the hot button window's weapon slots; `msg_trade_final_ieq`'s (0x413e) and
     `msgStartIcon`'s and `msgStartInotes`' lore checks if their packets ever come (EQMacEmu sends
     a trade's items as `OP_MerchantItemPacket` and the start as `OP_CharInventory`).
   - Coins: done (`Inventory::MoveMoney`, CEverQuest_MoveMoney, OP_MoveCoin): a money button with
     nothing on the cursor asks how many through the quantity window and puts them on the cursor
     (A_DragItem's cell 143 + CursorMoneyType, the count across its bottom), with coins on it takes
     them onto its pile, as the character view does; changed into other coins only at the bank.
     Escape leaves them on the cursor, as it does an item (RemoveAttachment takes off only a
     button or a spell). Clicked onto a spawn they ask for a trade (below), and the inventory's
     Destroy takes them (`CInventoryWnd_DestroyHeld`, `EQ_PC::DestroyHeldItemOrMoney`). Still to
     do: coins clicked onto the ground (11). A quest's coins (`OP_Reward`) are done (37).
   - Bank window: done (`frontend/bank_window.*`, eqgame's CBankWnd): the right button on a banker
     within 20 (RightClickedOnPlayer, IsOkToTransact) opens it with the banker's welcome, the bank
     slots (2000..2007, their bags as BagBank1..8), the bank's coins on its money buttons (coins
     change into others there) and the inventory with it; Done, Escape, the close box and walking
     more than 20 away (checked every 500 ms) close it with "Come back soon!", the inventory if it
     opened it, the quantity window if it was the bank's, and every bag window. Alt shows every
     slot's tooltip. Activate makes us visible (MakeMeVisible) and plays the bank's music
     (SetMusicSelection 3) until Deactivate, which saves (SavePC).
   - Merchant window: done (`frontend/merchant_window.*`, eqgame's CMerchantWnd): the right button
     on a merchant asks for its shop (OP_ShopRequest), the answer opens it with its items
     (OP_ShopInventoryPacket, OP_ShopDelItem) in the scrolling slots, a
     click on any slot selects it (a yellow outline) and says its price, Buy and Sell (the quantity
     window for a stack) with eqgame's checks and messages, OP_ShopPlayerBuy and OP_ShopPlayerSell
     and their answers (the price out of the purse, the sale's coins in and the item gone), Done,
     the close box, Escape and walking away (OP_ShopEnd). Checked live against the server for
     opening, the prices and the welcome; buying and selling only through their refusals (the
     test character has no coins or anything worth selling). The zone ending the session
     (OP_ShopEndConfirm, HandleWorldMessage's 0x4146) runs Deactivate; eqgame's runs again when that
     answers our own OP_ShopEnd (a second SavePC, the target cleared again), ours not. A Bazaar trader's
     shop (Type 0) is done (see 33). Still to do: the item display of a
     merchant's item (the slots' alt+click goes through the selection now). The right button makes
     us visible (MakeMeVisible) and the window plays the merchant's music (SetMusicSelection 6).
   - Loot window: done (`frontend/loot_window.*`, eqgame's CLootWnd): the right button on a corpse
     in reach asks to loot it (CEverQuest_LootCorpse, OP_LootRequest); OP_MoneyOnCorpse's answer
     says why not, or gives its coins and opens the window kneeling, its items
     (OP_LootItemPacket) in LW_LootSlot0..29; a click on one onto the cursor, the right button into
     the inventory (OP_LootItem, a NO DROP item from another's corpse asking first), the slots
     locked until the zone answers; Done, the close box, Escape and walking away end it
     (OP_EndLootRequest), and the zone's answer, or the corpse gone, closes it. Not checked
     against a server yet (the test character hasn't killed anything of its own to loot). /loot
     (@004fb5ae) loots our target (eqstr 13201 with none). /lootnodrop sets whether a
     NO DROP item asks first (`LootNoDrop`). Dead (DeathTimer), camping and deity 20000 don't ask;
     asking makes us visible (MakeMeVisible), and on a boat (`AttachedEntity`, `ZoneView::Vehicle`)
     or while the zone's answer to anything is waited on (`ZoneAnswerWaiting`: `DAT_007d0258`,
     `DAT_007d0254`) it doesn't ask. A bag on a player's corpse is one loot slot: the zone sends only
     the top-level items (PvP looting, Loot_Request_Type 3 and 4, sends the bags' items too, each as
     its own slot) and moves a bag's contents along with it when it's looted; PvP's one set item
     (type 5) comes as any other, at slot 22. Every loot item, bags too, comes as
     OP_LootItemPacket (0x4052), which TAKP's eqgame.dll hands to `msg_send_corpse_eq_equip`;
     eqgame's container and note paths (0x4053 `msg_send_corpse_con` @004df3ab /
     `CLootWnd::ProcessLootContainer` @00426a15, 0x4056 `msg_send_corpse_notes` @004df1f5 /
     `processLootNotes` @004268e3) never run under EQMacEmu.
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
     the
     bags in a trade window (BagTrade1..16 open, but their contents aren't checked) and
     OP_TradeReset. The zone's cancel runs Deactivate, which cancels again and says "You have
     cancelled the trade." (1450) after the zone's line, as msg_cancel_trade does.
   - The right button on a spawn (CEverQuest::RMouseUp within 500 ms, RightClickedOnPlayer): a
     corpse's loot, a banker's bank, a merchant's shop, else /consider of our target. A
     despawning merchant, banker or corpse closes its window. Our class's guildmaster asks the zone
     to train us (OP_GMTraining) and another's says eqstr 1467. A trader in the Bazaar asks for its shop
     (OP_TraderShop, see 33), and a race 141 boat for its helm (39). A merchant, banker, trader
     and our guildmaster turn auto attack off (1466). Still to do: the inspect of a player
     after the /consider (`CEverQuest_doInspect`, see 29), `_consider`'s TargetType refusals
     (0xb, 0x3c, 0x41, 0x43) after it, and Use centerscreen doing the same from the keyboard (6).
   - Guildmaster training: done (`frontend/train_window.*`, CTrainWnd): msg_req_guildmaster's
     SetGMData and Activate, UpdateSkill's rows and CostToTrain's prices, the headings' sorts,
     Train through BitchCanTrain (`frontend/skills.*`: setskill with New Tanaan Crafting Mastery,
     GetSkillLimit2, EQ_PC_GetKunarkThreshold, EQ_PC_checkLang) and OP_GMTrainSkill,
     OP_GMEndTraining on leaving, the coin pictures as list headings, and OP_LevelUpdate's 5
     practices.
   - Bag windows: done (`frontend/containers.*`, eqgame's CContainerMgr and CContainerWnd): the right
     button on a bag opens or closes its window, laid out for its slots with Combine and Done
     under them, kept in the layout as BagInv1..8 (BagBank, BagTrade), picking up or swapping out
     a bag closes it, Escape closes them all, Alt shows every slot's tooltip, and Combine sends
     OP_TradeSkillCombine with the class and racial containers' checks, the slots locked until the
     zone answers. World containers (a forge clicked on the ground, msg_chest_lock): BagWorld1,
     named by type (OpenWorldContainer), their items from OP_ObjectItemPacket at 4000 + n,
     OP_ClearObject emptying them, Combine as slot 1000, closed by Done, Escape or walking off 20
     (CContainerMgr::Process) with OP_ClickObjectAction, and not closeable with a NO DROP item in
     (CheckCloseable). A bag's open flag (EQ_Container::Open) is its window's, which the item
     display reads. Still to do: CanGoInBag's checks for a world container's slots.
   - Equipping: our model's armor, weapons and light follow what we have on
     (`ZoneSession::UpdateAppearance`), a flame out with our body in water (`UpdateLight`,
     `SetBodyInWater`), dual wield from the class table (`CanItemEquipInSlot`'s `HasSkill(0x16)`),
     and no slot clicks while casting but a bard's song (`CInvSlot::HandleLButtonUp`). /showhelm is
     Zeal's, not eqgame's: out of the base client. Still to do: the console client's `--move auto`
     passes no dual wield (it doesn't link the skill table); none of these checked live.
   - The item on the cursor: clicking a spawn with it (or coins) gives it to them through a trade
     (`DropItemOrMoneyOnPlayer`, below), clicking the ground drops it (item 11), and AutoInventory
     with no room drops it (AutoGenInventory: "There was no place to put that!", eqstr 12937; a
     NO DROP item stays on the cursor). Holding a slot's item to make an item hot button (`CInvSlot::HandleLButtonHeld`), and
     the right button on a slot: done for food, drink and alcohol (`CInvSlot_DoDrinkEatPoison`:
     the messages, hunger and thirst to 6000, OP_Consume and OP_DeleteCharge with the stack taken
     down here; checked live) and items' click effects (`SpellCast::CastItem`, OP_CastSpell gem 10
     with the slot; not checked live, the test characters have none). Eating and drinking for us
     when low (`ProcessHungerandThirst`, `AutoEat`: every 46 s, a monk's 92 s, OP_Consume with -1,
     and the hunger, thirst and out-of-food messages, Innate Metabolism's longer wait): done,
     checked live ("You and your mount..." on a horse); a Bazaar trader neither eats nor is told.
     Poisons (item type 42, `CEverQues_ApplyPoison` @00545c08 and DoPassageOfTime's wait,
     `FrontEnd::ApplyPoison`, `OP_ApplyPoison` and its answer @004e1778): done, not checked live
     (no test character has a poison). Still to do: Alcohol Tolerance's skill-up
     (EQ_PC_CheckSkillImprove 66) and what drunkenness does. A charge or stack count the zone
     takes (`OP_DeleteCharge` from the zone) is done (37). Notes and books are read
     (EQ_Note::SendTextRequestMsg's OP_ReadBook; CNoteWnd's SetNote, checked live; CBookWnd's
     pages, buttons and keys in `frontend/book_window.*`, checked through the preview with a
     zone's book text). Still to do
     for books: check the lines' fit against the TAKP client on Windows (the preview's box font is
     wider than Arial), DrawFormattedText's own wrapping in place of measuring the line whole, and
     keyboard focus (the window takes Left and Right while it's on top, not while it has the focus).
   - Item display: alt+click or the right button held on an inventory slot shows its item
     (`frontend/item_display.*`, eqgame's SetItem text with spells_en.txt's spell names); letting
     the right button go closes it. A link clicked in the chat shows its item (the zone's
     OP_ItemLinkResponse), and clicking the icon links the item in the chat line
     (`CItemDisplayWnd::WndNotification`; so does Shift+click on a slot while typing, our own).
     Any slot's item shows (loot, trade, merchant, bag and world container slots: checked on
     replay, `frontend_preview ... itemdisplay`), a bag says OPEN while its window's up, and FV's
     always-no-drop ids are `Inventory::IsSpecialNoDrop` (in `CanDrop`, here and everywhere).
     Still to do: the item+0xdc check WndNotification makes before linking (unknown field).
   - Skills window: the skills HasSkill gives us and our languages, listed as CSkillsWnd::Activate
     lists them, with their ranks (UpdateSkill: 254's "LEVEL: %1", 255 as HasSkill's value), kept
     up to date by OP_SkillUpdate. A press on a row selects it (CListWnd's own), none selected as
     it opens (UpdateAll's DeleteAll). Still to do: checking a racial skill live (the test
     characters are human).
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
     cast or recovering; a bard's song stops on a gem click (an empty OP_ManaChange). Fizzles and
     channeling are the zone's (eqgame's `SpellFizzled` and `CastingRequirementsMet` run only
     offline, see README Spells); others' fizzles, interrupts and regains are told
     (OP_SpellTextMessage, OP_SpellTextMessageStringID, by the spell filters); our own interrupt
     (StopSpellCast 1, msg_stat_change's SpawnAppearance 0x12 of 0 on us) tells us and the zone;
     divine aura (174) and being stunned (175, OP_Stun) stop a cast. The cast time has the AAs' and
     the focus's modifiers and the range the focus's (`frontend/spell_focus.*`:
     GetFocusCastingTimeModifier, GetFocusRangeModifier, GetFocusItem, GetFocusEffect,
     DoesSpellMatchFocusFilters; "Your %1 shimmers briefly.", 12444), as has the spell info
     display's "Your Cast Time" (3106). Lay on Hands and Harm Touch cast as gem 9
     (`SpellCast::CastAbility`, UseSkill's 'k' and 'i', their recast by
     AltAdvManager_AdjustSpellRecastTimeByClassAbility). A bard's song interrupted by a hit is
     only `ApplyDamage`'s, which needs an attacker and so never runs online (the zone's). Still to
     do:
     - what the book doesn't do yet: Activate's auto attack off (12371) when ChangeStance's sit is
       refused on a horse (eqgame turns it off anyway), a bard's AA 0x7b doubling
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
     or crouching moves us. Being stunned (`OP_Stun`) and knocked out are done (24). Our model's
     idle and fidget (`CDisplay::ProcessAnimation`: P01 and O01 standing, P07 and O03 once sat
     down, a fidget once a second one time in four, none with auto attack on, which goes for
     every spawn), the 6 s tick's mana (`EQ_Character::DoMeditation`, `IsCharacterMeditating`:
     `FrontEnd::DoMeditation`), Duck (ExecuteCmd 10: `ZoneView::Duck`, `StandUp`; crouched at half
     the walk speed, a jump standing us up, a wind-up backed out of leaving us ducked) and the
     body's tilt (`CDisplay_SetPlayerPitchType`'s types and eqgfx's
     `updateActorOrientationAfterCollision`: `SurfacePitchType`, `SurfaceTilt`, for us and every
     spawn) are done. Still to do: a spawn created lying (stance 115 from the start) stands until
     its stance changes (`SpawnActor`'s transition only plays on a change). The zone freezing us
     (a charm's `OP_FreezeClientControl`, `OP_UnfreezeClientControl` and `OP_ReturnClientControl`)
     is done (37).
   - Hotbuttons: the Actions window's buttons and socials go on them and come off by holding
     them, and they're kept in `<char>_<server>.ini` (`frontend/game_windows.*`). Clicked or by
     key (Hot1..10) they do what their button does: Attack, Range, the combat skills and the
     abilities (CHotButtonWnd::DoHotButton), AA abilities (28) and the pet window's buttons ("Pet
     <text>", 30), and they're checked while they aren't ready (OnProcessFrame: Attack and Range by
     __RangeAttackReady, a skill by its timer, an AA ability by IsAbilityReady). Items and spells
     go on them too: an inventory slot held (CInvSlot::HandleLButtonHeld, type 8: its InvSlot
     shown over the button, used as a right click on the slot, DoHotButtonRightClick), a gem held
     (CCastSpellWnd's 0x16, type 9: its gem shown, a gem click, a right click forgets it) and the
     gems' book button (type 5, 10: the book). DoHotButton refuses without control of ourselves
     (stance 102) or while the zone's answer is waited on (12084). A combat skill's or ability's
     right click opens the skill select window for its button (SelectSkillForCombatBtn,
     SelectSkillForAbilityBtn), and a spell's gem held shows its spell (WndNotification's 0x17,
     0x19). Still to do: the cursor's count for an item hot button (CCursorAttachment::Activate's
     fifth argument).
   - Socials: done (defaults, the editor, running them with `/pause`, `[Socials]`). Still to do:
     the edit boxes take typing only at their end (no caret to move, no Tab between them). (A
     social's lines, like a hot button's, go by the main chat window's channel and language.)
   - ~~Compass~~: done (`frontend/compass.*`, eqgame's CCompassWnd): the strips slide by our
     heading, and the needle wanders with a Sense Heading under 200, and Toggle Compass (Alt+D)
     opens and closes it, and using Sense Heading points it true for 5 s (CCompassWnd::SenseHeading).
   - ~~Selector bar~~: done (`SelectorToggles` in `frontend/frontend.cpp`): its buttons open and
     close their windows.
3. ~~**A real chat window**~~: done (`frontend/chat_window.*`, `chat_text.*`, `chat_commands.cpp`).
   Still to do:
   - More chat windows and the channel map are done (`frontend/chat_manager.*`, `chat_menus.cpp`:
     `CChatManager` @004100e2, `CreateChatWindow`, `FreeChatWindow` on a close, the channel map
     and `ProcessChatMessage` by `GetChatWindowIndexFromChannelID`, the active and locked windows,
     each window's default channel, tell target and language, [ChatManager] in the UI ini; the
     right-click menu, `InitContextMenu` @0041179c, `UpdateContextMenus` @00412f9b and
     `UpdateTellMenus` @00413439, with its picks from `CChatWindow::WndNotification`; Rename
     Window through `CTextEntryWnd`, `frontend/text_entry.*`). Still to do:
     - The chat channel items (Filters' and Channel's Chat Channel, ids 0x70.. and 0xcf..) are
       disabled: they list the chat server's channels (`ChannelManager`), which we don't have.
     - The language menu's ids and /language number the languages we know by their place in that
       list, and the window keeps that place; what's sent is that place's language
       (`CEverQuest_GetCurrentLanguage`), as eqgame does.
   - The chat font size is done (`ChatWindow::SetFont`: `CChatWindow::SetChatFont`'s 0..6, from
     /chatfontsize @004fdb25 on the active window, 13270 otherwise; [ChatManager]
     ChatWindow<n>_FontStyle in the UI ini; the main window starts at 3),
     and Page up and down (ExecuteCmd 0x55, 0x56: `ScrollChatWindowUp` and `Down`, a page of the
     output's height less 8). Still to do: scrolling by pixels (5).
   - Languages are done (`frontend/babel.*`: `SpeakBabel` by the speaker's skill at +0x86 and the
     bad word filter, `BadWord`'s list, then `HearBabel` by ours when OP_LogServer's Gibberish
     is set, for channels but 6 and 14, not as a GM; /language @004fd44a; what we send in
     `CEverQuest_GetCurrentLanguage`'s language with our skill less Drunkness / 4). Still to do:
     the other two SpeakBabel callers in HandleWorldMessage (language 0, skill 100: the bad word
     filter on other text) and `msg_npc_say_text`'s HearBabel (@004e07b9) on an NPC's line.
   - `CEverquest_InterpretCmd` (@0054572f) looks a word up twice, first by each command's
     localized name (the table's second pointer, null in the exe as it ships) and then its own:
     nothing to do in English. A line starting with `;` is /chat's (35), where ours says it.
   - The MOTD as we enter is done (`WorldSession::TakeEnteringMotd`: `DoMainLoop`'s 12359 after
     "You have entered %1.", once after character select). Still to do: before it, 12358 "You are
     no longer in a PvP immunity zone." (color 13) when `DAT_00799758` is 4 (the server type),
     under level 6, outside a newbie zone (`EQWorldData::IsNewbieZone` @00523f15, by
     `IsSpecialZone`'s zone flags, which aren't ours); a second `OP_MOTD` (EQMacEmu sends one
     with every zone) emptying /motd's text, as eqgame's 0x41dd does (ours keeps it); and
     "You have entered %1." itself through eqstr 12357.
   - `%R` in a string (`orderedStringExpansion` @004f7ea2): the grammar rules of
     `GrammarRulesClass::Resolve` (@0054bf5c; English's `RuleEn1`..`RuleEn4` @0054c04c on, and the
     Korean, French and German ones), which `EqStr::Format` leaves as it is.
   - The commands that still say they aren't supported (35).
   /who (CEverQuest::Who: this zone's players filtered by class, race, levels, GM, LFG, guild or
   name, and our corpses; `all` through OP_WhoAllRequest and msg_who_response's lines), /whotarget,
   the Who key and button, and /follow (FollowPlayerAI's turning, running, walking and stopping,
   TurnOffAutoFollow on Move back and the turn keys) are done, and /who all friends (29). Still
   to do: the
   spawn list's order (pinstSpawnList; ours by id), and /follow on a horse or of a rider
   (ProcessControls hands FollowPlayerAI's Following to pinstCharSpawn, the horse; not checked
   riding) and its line of sight from them to us (CastRay is ours,
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
   - allowslashcommand's other checks (the windows that block commands:
     IsNoSlashWnd and eqstr 12084), for /assist, /attack and the other commands that call it;
   - /played: the alarm's time left (no alarm window yet);
   - /consider: under the deity PvP rules (pvpcon 4) NotOnSameDeity lets a GM off, read here as
     our `gm` flag; EQPlayer+0x109 isn't checked to be IsGameMaster.
   - socials: the 60 unit check is our 3D SpawnDistance (eqgame's CDisplay::PlayerDistance with
     its last argument 0, not checked to be the same), DAT_007985f1 (a flag that stops them
     all) isn't known.
4. **Cursor and dragging.** EQ's own cursors. (An item on the cursor follows it:
   `DrawCursorAttachment`.) Tooltips: done (`WindowManager::DrawTooltip`, eqgame's DrawWindows and
   DrawTooltipAtPoint): after the mouse rests 500 ms, the control under it shows its tooltip, its
   TooltipReference or what its window sets: an inventory slot's item name (an empty worn slot's
   name), a spell gem's "1) Spell", and Alt held shows every gem's (DrawSpellButtonTooltips).
   The buff window's: the spell's name. The key bound to a button after its text
   (`CXWnd::SetKeyTooltip`, `FrontEnd::KeyMapUpdated`) is done. Still to do: the help texts
   FindWnd's other hit codes ask for (GetHelpTextFromStringTable), and the slots of windows we
   don't have yet (the world container's).
5. **Options window.** The Keyboard page is done (`frontend/options_window.*`, COptionsWnd's
   keyboard page; see 6), with a combo box (CComboWnd) in `frontend/sidl.*`, and the Colors page
   (ColorPageWndNotification, SaveColors, RestoreDefaultColors, into my-client.ini's
   `[TextColors]`; /usercolor (@004fe3a2) sets, resets and lists them from the chat line), the
   Filters page (SyncChatPage, ChatPageWndNotification: the /filter switches, sent and kept), and
   the General page (SyncGeneralPage, GeneralPageWndNotification: Fast Item Destroy, Autosplit,
   Anonymous, Roleplaying, Allow trading with, Receive Guild Invites (msg_invite_guild reads it),
   Fast Drop, the music and sound volumes, kept in my-client.ini; Load UI Skin, 34). The Display
   page (SyncDisplayPage, DisplayPageWndNotification, DisplayPageOnProcessFrame: names, sky, particles,
   clip plane, the windows' global fades) is done; still to do there: Video Modes (CVideoModesWnd)
   and Level of Detail's effect (s3dSetDynamicLOD: models are always drawn whole); Gamma does
   nothing, as in eqgame windowed. Still to do: the Mouse page, which shows empty
   (`COptionsWnd::MousePageWndNotification` @0042c2d6 and InitKeyboardAssignments' mouse
   settings; its sensitivity and invert, which `procMouse` reads,
   are fixed at 4 and off); the reverb's drugged room (23) while drunk at 40 and
   over (DoPassageOfTime's drunkenness wearing off isn't ours); the Keyboard page's
   temporary red rows (commands not yet done) go once every command is done.
   A combo box's list that's down takes any press (CXWndManager's 0x34): off it, the list goes
   and the press goes no further. Its list draws the choice on the select color and the row under
   the mouse in the hover color (style 0x10000), CListWnd's or CComboWnd::SetColors' (white, yellow
   and gray on every options page's combo box). Still to do: its scrollbar when ListHeight cuts it
   short (InsertChoice sets CListWnd style 1; no combo box in the default options window gets
   there), and SetColors' text color for the box itself (CComboWnd+0x68; white either way here).
   Scrollbars
   step by pixels in eqgame, not rows: a press on an arrow is HandleVScroll 0 or 2
   (adjustScrollPosition @005744d0: 2 pixels; CListWnd's @0057c780 the same, then its update), and
   CXWndManager::ProcessWindowTransitions (@005a07d0) calls it again (1 or 3) every frame while the
   arrow's still pressed under the mouse (field 0x5c 10, 0xd), the gutter a page a frame (5, 6;
   0xb, 0xc) until the thumb reaches the cursor. Ours keep `scroll_pos` in rows (lists, STML
   boxes, the chat, scrolling pages) and step a row a press with no repeat; holding needs pixel
   positions first. Lists draw the
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
   own character after it. Zoom in and out held move the chase camera 2 units a frame unit
   (ProcessControls; no nearer than our ModelHeightOffset, DoCamAi's 2..200), and Look up and down
   held pitch faster each frame, to 12 EQ units a frame unit. Toggle UI Display (0x15, __ScreenMode
   2 and 3) hides the windows and the cursor (DoMainLoop's DrawWindows and DrawCursor), the hidden
   windows still taking the mouse and the world only in mouse look, until it's pressed again or a
   zone is entered; the mouse look check is our right button held (no Toggle mouse look yet). Still to do:
   - ExecuteCmd's (@0054050c) commands not done yet (the page's "No"):
     - 7, Toggle mouse look: `DAT_00798571` flipped (on from the start; the Mouse page's check
       box and `loadOptions` set it too), eqstr 12372 "Mouse control deactivated." or 12373
       "Mouse control activated.  Use right mouse button.". `CEverQuest_RMouseDown` copies it to
       `__MouseLook`, so off, the right button no longer turns us; ours always does.
     - 14, Use centerscreen: with the chat line closed, the middle of the screen clicked:
       `CEverQuest_ClickedPlayer` there is `LeftClickedOnPlayer` and, with no target or an NPC's
       (Type 1), `RightClickedOnPlayer` too; nobody there, `CEverQuest_ClickedSwitch`.
     - 17, Center view: in view 0 our pitch to 0 (the horse's or boat's when it's the char spawn),
       in view 1 `_DAT_007996b4` to -100, another view's own pitch to 0.001; the held look keys'
       state cleared.
     - 30, Toggle netstat: `__NetStatusToggle`, drawn by `DrawNetStatus` (@0054d3ae) and
       `DrawPacketLossBar` (@0054d1a8) from `connection_get_perf_stats`.
     - 75 and 76, the clip plane: `DAT_00798b28` down or up one in 0..20, the far clip
       `_DAT_00799660` that * 0.2 held to 0.1..4, saved (the Display page's slider is done).
     - 77, voice input (`InitializeVoiceRecognition` @0055785c; with /voice, /microphone and
       /vrdelay, see 35: kept out unless asked).
     - 78, screenshots (15).
     - 81..84 and 85, 86: the chat line's cursor left, right, to its start and end, and the chat
       text a page up and down (`CChatWindow::ScrollChatWindowUp` @00414ad9, `Down` @00414b17).
     - 104, the help window (`CHelpWnd_SetFile` then Activate, or its close), 106, the video modes
       window (`CVideoModesWnd::Activate`), and 110, the music player window (34 for all three).
     - Old UI only, nothing to do: 62 Backdrop, 89 Lock windows (`[Defaults] LockWindows`, eqstr
       12376 and 12363) and 96 Toggle chat window (`HideChatWin`) return at once in the new UI.
   - ExecuteCmd's numbers past the page: 119 the petition queue window (36), 209 a line to our
     target (not traced), 210 the face picker (/facepick, 23), and 198, 199 and 205, which only
     store the key's state. (200 Escape, 202 quit and 206 the chat line's Enter are ours by
     other roads.)
   - Send tell for a GM helping a petitioner (`/tell <petitioner> `, CPetitionQWnd), and
     ExecuteCmd 0x81 and 0x82 (`;` and `/` command modes; not on the keyboard page).
   - Target myself's pet: eqgame takes the first in pinstSpawnList's order; ours the lowest id.
   - Cycle nearest target (Zeal's) doesn't look at riders (Target nearest NPC passes over a horse
     with a Rider and hands a horse's target to it). Both don't know whether we see invisible (EQ_Character::CanISeeInvis: SPA 13 and the races'), so
     an invisible spawn is never picked, nor a GM's VisibilityState.
   - Look up and down held (ProcessControls, DAT_0079974c and DAT_0079974e) pitch to EQ's 128
     either way, but our view stops at -80 and 60 degrees (ZoneView::Pitch); and the pitch going
     back to level by 10 a frame in first person with neither held (DAT_00798b14's option).
   - CDisplay_KeyMapUpdated: the buttons' key tooltips (CXWnd::SetKeyTooltip,
     CActionsWnd::KeyMapUpdated_UpdateActionWindowKeyTooltips, CSelectorWnd::KeyMapUpdated).
   - Character select's explore mode takes only the movement, Jump and Toggle camera view
     commands from the keymap (ProcessNewUICharacterSelectKeys runs ProcessMovementKeys).
7. ~~**Map window.**~~ Not eqgame's: the in-game map is Zeal's, so it stays out of the base client
   for now.
8. ~~**Loading screen**~~: done (`frontend/loading_screen.*`, eqgame's `EQ_LoadingS`,
   `CEverQuest_DoLoadScreenProgressBar`): the newest expansion's picture (`eqpload.bmp`), the bar,
   its lines ("Loading Character Equipment Files...") and the jokes, up whenever a zone loads (ours: entering the world and every
   zone line), the bar following `InitWorld`'s steps for the global files
   (`render::CharacterModels::Open`) and then the zone's (`ZoneScene::Load`), no input meanwhile.
   Still to do: eqgame shows it from the moment character select ends (`DoLoadScreen(1)` before
   `WorldAuthenticate`), ours once the world gives the zone's address; and the startup one after
   server select (`CDisplay::InitDDraw`'s `DoLoadScreen(0)` with `InitWorld`'s global files, which
   we load with each zone instead).
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
    - The face picker in the zone: /facepick (@004fffcb) is ExecuteCmd 210, which opens `CFacePick`
      only with the Luclin expansion (`Expansions & 0xc`, or PCData+0x1362) and on a Luclin model
      (the actor's +0x3b0), not while it's already up (`DAT_007cf258`). Its Accept sends 0x4225
      with the face, hair and beard, and says eqstr 0x2efc; `Deactivate` (@0041a3e6) puts the old
      look back.
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
    takes off our invisibility and pushes us (IWasHit); our death (`frontend/death.cpp`:
    CEverQuest_ReportDeath_ClearOutRentables, ClearOutRentables, IDied) takes our temporary items
    (NoRent 0, a temporary bag with its contents), closes the loot window (DoorLoading_), puts the
    camera in the overhead view (1) and lays us down (D05) to the death music, and nothing moves
    us; 2 s on, "Returning to home point, please wait..." (eqstr 539), PrepareLocalPCForRepop (in a
    group our invitation dropped, the raid's reset, a corpse's looting ended), OP_ZoneChange to the
    bind zone with reason 10 and CDisplay_SwitchToDefaultCameraMode (RealRender_World's
    DeathTimer, MoveToZone); the zone refusing that, MoveToZone_2's "Your Home Zone is unavailable,
    going back to Character Selection Screen!" and OP_Logout at once. While the DeathTimer runs a
    merchant, banker, our guildmaster and /consider don't answer the right button
    (EQ_3DView_MouseUp). DoPassageOfTime's KnockedOut at 0 HP or under (lying down, StunnedState
    3, the overhead view, eqstr 12877) and GetConscious over 0 (12876, the default view, up as from
    kneeling); OP_GMKill naming us ("struck down by the gods", 1391: wave 0x8f, HP -10000, the
    DeathTimer back to 0). Our own death (SendDeathMessage, `ZoneSession::SendDeath`): under -10
    HP with no DeathTimer (DoPassageOfTime: skill 0x17, no killer), or a fall that takes us there
    (ApplyDamage: skill 0xfc, in place of OP_Damage), OP_Death to the zone and ReportDeath here.
    A spawn becomes its corpse (Type 2, `ZoneSession::BecomeCorpse`) after its death is told, as
    IDied does; our death lets go of the group's spawns until they come in again, the group
    changes or the next zone (`Group::ReleaseSpawns`). Checked live (an NPC's Cazic Touch) and
    through its replay (`frontend_preview ... death`).
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
      bow or anything to throw. Still to do: AimAtTarget's pitch and our own missile drawn
      (EQMissile);
    - DoAttack's other StunnedStates (2..4 and 7 say nothing; 1, stunned, is done), the
      ActorInfo +0xc8c+0x74 and DAT_007985f0 refusals, and DAT_00798560 (CanIHitTarget always
      passing);
    - the tracking window: done (`frontend/tracking.*`, `CTrackingWnd`, /trackplayers, /tracksort,
      /trackfilter, DoPassageOfTime's direction lines). Not checked live (no ranger to test with);
      a selection isn't kept across
      a refresh (UpdateTrackingList's callers all pass 0, so neither is eqgame's);
    - the AA abilities (105 Harm Touch and 107 Lay on Hands: CastSpell gem 9 of spell 88 and 87, the
      profile's timers at +0xc7a and +0xc7e, with AdjustSpellRecastTimeByClassAbility's 12 min a rank
      of Fervent Blessing or Touch of the Wicked off; 111 Slam as bash) and the innate abilities'
      names on the Actions window's buttons and in the skill picker (`CEverQuest_GetInnateDesc`
      @00530118, from `CActionsWnd::UpdateButtonText` and `CSkillsSelectWnd::UpdateSkill`: eqstr
      6604 Slam, 6605 Lay Hands, 6606 Harm Touch, 6609 Awareness, 6610 Bash Door, 6615 Breathe
      Fire, 6616 Harmony, 6617 Infravision, 6618 Lore, 6619 No Bash, 6620 Regeneration, 6621
      Surprise, 6622 Ultravision), UseSkill's horse check by
      ActorInfo's Mount (ours is the SPA 113 buff, which a Mount always has), stealing under the PvP deity rules (DAT_006ec740 4: NotOnSameDeity, the level range, 12412,
      12413), fishing's splash particles (DoParticleEffect 2) and the pole
      shown in hand as the cast starts (UpdateItemSlot), and someone picking our pockets (msg_steal
      0xff: 12897 and the answer);
    - AllowedToAttack's server PvP types and the water;
    - /filter's client side: the chat channels and the bad word filter
      (msg_new_text's `SpeakBabel`, see 3; `bad_word_class` @005054e4), and the old UI's hit modes
      (only when DAT_008092d8, the new UI, is off; the new UI's are the chat menu's Hit Modes, done)
      (/hitsmode @00500373: `HandleMyHitsMode` @00500f86, `HandleOtherHitsMeMode`,
      `HandleOtherHitsOtherMode` and the three misses ones from `DoPassageOfTime`, each with its
      `[Defaults]` Mode, X, Y, Count, FontSize, ShowBG, BGWidth, ShowHeader and AlwaysDraw; eqstr
      2130's short form, the number only; /clearhits (@00500dba), `do_clearhits` @00500dbf, and
      `DrawHits` @00501483);
    - fatigue's own changes (`EQ_PC::ProcessFatigue` @004d0424, from `DoPassageOfTime`): up 1 out of
      food or drink, running and moving (DAT_0079856d), or swimming (water types 5..10); down 10
      when it hasn't changed since the last call and we're fed and watered; 0 under DAT_00798560
      or DAT_00798620. Ours only goes up by swings and comes down by the zone's OP_Stamina;
    - berserking: OP_BerserkStart and OP_BerserkEnd (0x41a9, 0x41aa; ActorInfo+0x42c) set
      `ZoneSession::Berserk`, which STR (`EQ_Character::Str`: level / 10 + 9 under 56 drunk) and ATK
      (`compute_tohit`: level * 2 / 5 while Drunkness / 2 is 20 or under) count. Past that,
      compute_tohit takes a value of the drunkenness the decompile loses (an `__ftol`), not ours;
    - the blood of our crushing, slashing, piercing, backstab and bash hits (SpurtBloodOnDag);
    - DoPassageOfTime's HP tick (`FrontEnd::HitPointsTick`): the locale's (DAT_00799770 1) regen
      of 6, 10 sitting, under level 21 is left out (English is 0); a monk's 2 past level 50 reads
      ActorInfo+0x18e, which nothing in eqgame sets but to 0; the mez broken by a DoT turns the
      stun's sound room off with `SetGenericEnvironment` (eqgame's `_StunRelated` isn't read);
      the dead group member's gauge isn't checked against a grouped recording;
    - MoveToZone_2's other refusals: its test before the logout reads `StunnedState == 4 ||
      param_4 || strlen(@00546057)(Name)`, which can't be right for a zone line (every refusal
      would log out); ours logs out only going home dead;
    - being hit on a mount (DeacSpellScreen, ChangeStance);
    - disarm's answer (`msg_disarm` @004dfc2a, `Abilities::ZoneEvent`'s `DisarmAnswered`) says
      12890 and 12891; its `EQ_PC_CheckSkillImprove` 16 on a success isn't ours (the zone's
      `OP_SkillUpdate` is). Its status 0xff is us being disarmed, which the client rolls
      itself (their skill less ours with the weapon + 10, held to 5..20, against 1..100; the
      primary, secondary or range item to a free slot or a bag it fits, 0x41b2 with our name when
      the cursor's full, `UpdateItemSlot`, `UpdateLight`, eqstr 12889, and the answer sent back):
      EQMacEmu disarms on its own side and never sends that;
    - `EQ_Character::GetSkillBaseDamage` (@004bdc9c, with `CheckRaceFactionBonus` and
      `ElementResistDmg`) fills the fist's damage for DoAttack's bare-handed swings: the damage is
      the zone's, so nothing of ours reads it;
    - TotalSpellAffects adds the worn items' effects only for attack speed; the other SPAs
      (`Buffs::Total`) leave them out.

25. ~~**What eqgame tells the zone that we don't.**~~ Done (`ZoneSession::SetRunMode`, `SavePC`,
    `ReportEnvDamage`, `SafeFallSuccess`, `TargetGroupBuff`; `FrontEnd::SavePC`, `TakeFallDamage`):
    OP_SetRunMode as we walk or run (`DoPassageOfTime`'s `__RunWalkState`), OP_Save (`SavePC`'s
    callers: every 181 six-second ticks, closing the bank, merchant and loot windows, a finished
    trade, /anonymous, /roleplay and the options' Anonymous and Roleplaying; not while the zone's
    answer to something is waited on), a fall's OP_EnvDamage and OP_Damage
    (`TakeFallDamage` with what we carry and safe fall, whose OP_SafeFallSuccess goes too), and
    /targetgroupbuff and DoMainLoop's OP_TGB. Still to do:
    - OP_Save carries the profile as the zone sent it, not eqgame's current copy (EQMacEmu reads
      none of it); and `SavePC`'s other callers: `MoveToZone_2`'s `SavePCForce` and OP_DeleteSpawn
      as we zone, `msg_stat_change`'s anonymous state for us, the bind in `ProcessConfirmation` and
      `HitBySpell`.
    - ApplyDamage's own half for a fall: the HP taken here before the zone's update and divine
      aura. (Its stun, and Stalwart
      Endurance against it, need an attacker, which no caller passes.)
    - The other environment damage the client reports: lava and the rest (`ProcessEnvironment`
      @004c0e32). Drowning (`ProcessAirSupply`, 34) is done.
    - OP_ClientError.
    - The checksums `ProcessGameLoop` sends the world as the game starts (and
      `CEverQuest_HandleWorldMessage_do_quit` again): `SendSpellChecksum` (@004f2cf6, 0x4135, a
      CRC over `spells_us.txt`) and `SendExeChecksum` (@004f2ed0, 0x4139, over eqgame.exe's own
      file). EQMacEmu's world checks one only when it comes (`Client::HandleChecksumPacket`: three
      known values, else the connection closed), so ours sends neither. The exe's can't be worked
      out here (we never read eqgame.exe): ask before sending a constant.
    - Camping's own checks and messages (40).
26. ~~**Guilds.**~~ Done (`frontend/guild.*`, `ZoneSession`'s guild packets): ours (EQ_Character's
    GuildId and GuildStatus, the profile's @4946 and @4967) and each spawn's guild and rank;
    /guildinvite, /guildremove, /guilddelete (the confirmation dialog, 0x17), /guildstatus,
    /guildleader, /guildwar, /guildpeace and /guildmotd as eqgame checks and says them; an invite
    or promotion asked (msg_invite_guild, category 0, Receive Guild Invites, the 3/4/5 and rank + 10
    answers), war and peace asked of our leader (msg_guild_war, msg_guild_peace, 8 and 9) and
    refused (handleRequestDenial), a member's rank (msg_guild_list), our guild disbanded, a guild
    added (save_guilds2), the MOTD (asked for on entering the game, printed), the players' unknown
    guilds asked for (OP_GetGuildsList), and SpawnAppearance 22 and 23's messages. Checked live: the
    commands, the MOTD and /guilddelete. Still to do:
    - The guilds file eqgame keeps (`guilds2.dat`: `load_guilds`, `save_guilds`,
      `GuildFileChecksum`, the CRC sent with OP_GuildsList). EQMacEmu's world sends the list
      whatever the checksum, so ours sends 0 and keeps none.
    - Not checked live: an invite from another player (needs a second character in the guild),
      war and peace (EQMacEmu ignores OP_GuildWar and OP_GuildPeace), 0x411e and 0x4127 (it never
      sends them).
27. ~~**Raids.**~~ Done (`frontend/raid.*`, `frontend/raid_window.*`, `ZoneSession::RaidCommand` and
    the zone's `OP_RaidInvite` and `OP_RaidUpdate`): `CRaid`'s state (@0049cf43),
    `HandleC2SRaidMessage` and `HandleS2CRaidMessage` with their messages, the raid commands as
    eqgame checks them (Planes of Power or a test server), the raid window (`CRaidWnd`: the grouped
    and ungrouped lists, ranks, class colors, the selection and its highlight, the buttons,
    /raidwindow and Toggle Raid Window) and the raid options window (`CRaidOptionsWnd`: the loot type
    combo, the looter list, the class color buttons through the color picker). Checked live with a
    second player (`eqclient --raid-invite`, `--raid-accept`; `frontend_preview ... raid`), the chat
    window's raid default channel (/channel rsay), and leaving the raid as a camp ends. Still to do:
    - Not checked live: renames (2), group moves (3), 7, 12, 13 and the separator rows' look (the
      window shows six rows; EQMacEmu puts each member in their own group).
28. **Alternate advancement.** Done so far: the table and `AltAdvManager`'s rules (`core/alt_adv.*`:
    `AltAdvEntry_initializeAltAdvancement`, `Add_MGB_for_Beastlords_fix`, `CalculateAbilityPointCost`,
    `TotalPointsInSkill`, `GetAbilityRequiredLevel`, `CheckAbilityRequirements`, `CanTrainAbility`,
    `GetCalculatedTimer`, `IsAbilityReady`, `GetAbilityReqs`, `GetAbilityTitle` for our own title),
    the ranks from the profile and `OP_RespondAA`, the timers from the zone's `OP_AAAction`, the AA
    window (`frontend/aa_window.*`, `CAAWnd`: `Init`, `Update`, `UpdateSelected`, `UpdateTimer`,
    `OnProcessFrame`, `WndNotification`'s "-", "+", Train, Done, rows and tabs), /alternateadv, Toggle
    Alt. Adv. window and the inventory's Alt. Adv. button; the window's Hotkey and AA hot buttons
    (`CCursorAttachment::Activate` type 4, 0x78 + id; `CHotButtonWnd::OnProcessFrame`'s words and
    checked state; `DoHotButton` and `DoPassageOfTime`'s "/alt activate"; `DoHotButtonRightClick`'s
    `CAAWnd::Activate` and `ShowAbility`); the passive abilities where the system is ours: the stats
    and resists and their caps (`CalculateMaxStatAA2`, `CalculateMaxStatAA`), max mana by them, max
    HP (`CalculateMaxHPAA`), the run speed (Innate Run Speed, Fleet of Foot), Innate Metabolism, the
    trap, feign and hide timers, Nimble Evasion, 2 Hand Bash, New Tanaan Crafting Mastery,
    Acrobatics, Endless Quiver, the spell display's `GetAACastingTimeModifier` and Permanent
    Illusion's duration, `DoMeditation`'s 0x77 and 0xe0, and Jam Fest (0x5e: a bard's own songs
    count 1, 3 or 5 levels more in `CalcAffectChange`, `Buffs::OurValue`) and Permanent Illusion
    in a decaying illusion's value (its 10000 ticks, only a beneficial SPA 0x3a spell's). Nothing to do on the client: the "archery and throwing rolls"
    (`CheckArcherySuccess` 0xba..0xbd, `CheckThrowingSkill` 0x51) are DoAttack 13 and 14's double
    attack and dual wield rolls, which send nothing; a bard's instrument and singing mastery
    (`CalculateBardSongMod`) is `CastingRequirementsMet`'s, offline only (the zone sends the
    song's modifier in `OP_Action`); Viscid Roots (0xbe) in `HitBySpell` only goes to
    `EQ_Character_SetLocalVar`, an empty stub. Still to do:
    - the passive abilities whose systems aren't ours yet: the book's 0x7b, the pet window's
      (`CPetInfoWnd::Update`), Stalwart Endurance and the ability recast (above);
    - Jam Fest not checked live (no bard test character);
    - not checked live: the PoP pages trainable (61 and up with the Planes of Power), a GM's "(ID#n)".
29. ~~**Friends, ignore, inspect and notes.**~~ Done (`frontend/friends.*`, `frontend/inspect_window.*`,
    `frontend/player_notes.*`): `CFriendsWnd`, /friends, /ignore, /who all friend (OP_FriendsWho),
    `[Friends]` and `[Ignored]` in the character's ini, `CEverQuest_BeingIgnored`; /inspect,
    `CEverQuest_doInspect` from `RightClickedOnPlayer`, `msg_req_inspect`'s answer, the inspect window
    (`msg_inspect`: their worn items and their inspect text, read only; ours editable when we
    inspect ourselves, `AcceptInspectText` keeping it as `[InspectText] Text`;
    `CheckInspectionDistance`, `PlayerBeingDeleted`) and `[Defaults] ShowInspectMessage`; `CPlayerNotesWnd` and /note with
    `notes.txt`. Still to do:
    - 0x4243's ignore check (EQMacEmu never sends it).
    - Inspecting live against two clients: only checked with injected packets in the preview.
    - `RightClickedOnPlayer`'s TargetType returns (0xb, 0x41, 0x43, 0x3c, races under 0x8cd) before
      /consider and the inspect.
    - `CursorItemSet` for the inspect: ours checks only an item on the cursor, not a button or coins.
30. ~~**Pets.**~~ Done (`frontend/pet_window.*`, `ZoneSession::PetCommand`, `MyPetId`): the owner's
    ActorInfo `PetID` (`EQPlayer::EQPlayer`, `~EQPlayer`, `OP_Charm`'s `updatePetTargetInfo`),
    SpawnAppearance 25 and 27, the pet window (`CPetInfoWnd`: `Update`, `WndNotification`,
    `GetButton`; Animation Empathy's buttons, Sit/Stand), /pet (@004f8754, `IssuePetCommand`,
    `findEntityByPartialName`, `OP_PetCommands`), a pet button held onto a hot button and pressed
    there, labels 68 and 69. Checked live (`eqclient --pet`) and on replay (`frontend_preview ...
    pet`). Still to do:
    - `IssuePetCommand` 15 (/pet target) and `findEntityByPartialName`'s ties take the first in
      pinstSpawnList's order; ours the lowest id. 15 also sets `DAT_007f9574`+0xe64 to 1 (not
      known what it is).
    - The player window's menu toggle for its pet gauge (`CPlayerWnd::WndNotification` flips
      +0x144, which `UpdatePlayerWindowState` needs to show it; on from the constructor). Ours
      always shows it with a pet.
31. ~~**Death's dialogs and corpses.**~~ Done (`frontend/death.cpp`): `msg_resurrect` (@004e1f5f,
    `OP_RezzRequest` to us, eqstr 10001 or 10000; unanswered with the dialog up) answered by
    `OP_RezzAnswer`, `msg_resurrect_complete` (@004e212b: buffs cleared, under 100% no mana, a
    third of our HP and Resurrection Effects (756, 757) in slot 0, then to the corpse or MoveToZone reason
    9); `msg_sacrifice` and `msg_translocate` (@004e1afe, @004e1c3e: the questions, No at once
    with the dialog up, `OP_Sacrifice` and `OP_Translocate` from ProcessYes and ProcessNo, and a
    translocation's move, to the bind point for 1334, 1422 and 3243); /consent; /corpse
    (@004f5eeb, `OP_GMSummon`) and /hidecorpses (@004fb5eb). Corpse notes need nothing: TAKP's
    eqgame.dll hands every `OP_LootItemPacket` item to `msg_send_corpse_eq_equip`. Checked live
    against EQMacEmu and through the replay (`frontend_preview ... rez`). Still to do:
    - `OP_CorpseDrag` from the zone (0x4114: "%1 has permission to drag corpse %2.", eqstr 1405);
      EQMacEmu never sends it.
32. ~~**Duels.**~~ Done (`frontend/duel.*`): /duel (@004fca55, `OP_RequestDuel`, `OP_DuelAccept`),
    /decline (@004fc966), `msg_duel` (@004e31b7, `OP_DuelResponse`, the ignore check),
    HandleWorldMessage's 0x40d0 and 0x4029 branches, ReportDeath's (12100, 12101, 0x415e) and
    AllowedToAttack's melee duel branch. Checked live with Claudebot (`frontend_preview ... duel`).
    Still to do: AllowedToAttack's spell branch in a duel (a detrimental spell's `field88_0x87` with
    the two `IsPlayerKill`s differing turns it down); our spells don't ask AllowedToAttack yet.
33. ~~**The Bazaar.**~~ Done: the bazaar window (`frontend/bazaar_window.*`, eqgame's CBazaarWnd:
    /trader, the satchels' items, prices in `BZR_<name>_<server>.ini`, Set Price, Clear, Add,
    Remove, Start under the stalls (`IsSpecialBazaarSpot`), End, List, `OP_Trader`'s answers and
    `bzrlog_*.txt`), the search window (`frontend/bazaar_search.*`, CBazaarSearchWnd: /bazaar, the
    combos, Find, the results, sorting by a heading, Request Item and the right button on a
    result, Welcome), a trader's shop in the merchant window (`OP_TraderShop`, `OP_TraderBuy`,
    `OP_Trader` 4), `OP_BecomeTrader` and SpawnAppearance 31 (`ZoneSpawn::trader`), the trader tag
    and color on names, /who's traders, the Bazaar's welcome on entering, the toggle keys and
    Escape. Checked live with Claudebot (`frontend_preview ... bazaar`). EQMacEmu builds
    `OP_MoneyUpdate` for a sale but never sends it: the trader's coins come as `OP_TradeMoneyUpdate`.
    Still to do:
    - RightClickedOnPlayer's "with a customer" (1468) reads an ActorInfo field nothing in eqgame
      sets; we always ask.
    - The search list's column widths kept in the UI ini (`LoadIniListWnd`, `StoreIniListWnd`), and
      the price boxes taking digits only (`AlphaNumericOnly` 3).
    - Every InvSlot's picture tinted 0xc0c0c0 (white under the mouse: `InitializeInventorySlotWindow`);
      ours are untinted but the bazaar window's.
    - Not checked: a trader's stack's count in the merchant window's slot (ours shows 1), and
      Request Item and the right button on a result live.
    - `msg_snd_trader_eq` (0x4217) is the old UI's; IsOkToTransact's deity 20000 and mount checks.
34. **The smaller windows**, each opened by its command or key:
    - ~~Breath~~: done (`FrontEnd::BreathPassageOfTime`, `CBreathWnd`, `CanIBreathe`,
      `MaxAirSupply`, `ProcessAirSupply` and drowning's `ApplyDamage`; `AirSupply` from each
      profile). Still to do: checking it in water live.
    - ~~Alarm~~: done (`frontend/alarm_window.*`: `CAlarmWnd`'s Activate, WndNotification,
      SaveAlarmWindowVisibilitySetting and ProcessAlarmTrigger; /played's 6684). Still to do: the
      time left window (`CTimeLeftWnd` @00437c5b, `updateTimerDisplay` @00437e49), made only when
      `DAT_00807dcc` (eqmain.dll's login block + 0x5a8) is above 0: find where eqmain fills it
      first. The in-game confirmation dialog has an owner per window; `TellInGame` takes it over
      from `AskInGame`'s, Destroy's and the spellbook's questions, not the loot or ground items'.
    - ~~Help~~: done (/help; the help window, `frontend/help_window.*`: `CHelpWnd_SetFile`,
      `CStmlWnd_LoadPage`, the box's history, Back and Contents, links). Still to do: its page keys
      (vtbl 0x68, `GameInstance_handleKeyboardInput` @0041f731: Page Up and Page Down, with only
      the first of its three flags set, scroll `Display` by 98 pixels; ours scroll by rows).
    - Music player (`MusicPlayerWnd` @004285e3: `SongSelector`, `SongTimer`, `Volume`, and Add,
      Delete, Clear, Play, Pause, Stop, Back, Next, Random and Save buttons;
      `HandleMusicPlayerWndNotification` @0042898d; its file picker `CFileSelectionWnd` @0041af9a)
      on Toggle Music Player Window (110), over `MusicManager`'s play list (`loadMusicPlaylist`
      @005504da for `.m3u`, `ExportPlaylistToM3U` @0055041f, `SetPlaylistShuffleMode`,
      `RemovePlaylistItem`, `SkipToNextTrack`); /mp3 (@004fa1cc: add, play, pause, stop, count,
      list, clear, shuffle, ordered; eqstr 7531..7550) and /music (@004ff4f8: on or off into
      `[Defaults] PlayMusic` for the next run, eqstr 6677 and 6678, else 6679).
    - ~~Load UI skin~~: done (`frontend/loadskin_window.*`: `CLoadskinWnd`'s constructor, Activate,
      `UpdateSkinList` and `HandleWindowNotification`, from the options' button; /loadskin
      @004f8655, `FrontEnd::LoadSkin`; the reload is 1's).
    - Video modes (`CVideoModesWnd` @0043b198: `VIDW_VideoResolutionsList`, the bit depth and
      refresh rate combo boxes, `VIDW_AcceptButton`; `Init` @0043b408 lists the adapter's modes;
      Accept changes the mode, asks eqstr 6682 for 15 s and goes back without a yes,
      `CVideoModesWnd_RestoreOldMode` @0043bbe2; kept as `[VideoMode]` Width, Height,
      BitsPerPixel and RefreshRate) from the options' button and Toggle video mode window (106).
      Goes with 14.
    - ~~Bug report and feedback~~: done (`frontend/bug_report.*`: `CBugReportWnd`'s
      `ResetPositionAndUIState` and `WndNotification`, `OP_Bug`; `CFeedbackWnd`, `OP_Feedback`;
      both checked live). Still to do: the target box with a target checked live (only the
      no-target branch was).
    - ~~/petition and /report~~: done (`send_petition`, `UserPetitions`, `OP_Petition`; once a
      zone from `OP_SendExpZonein`, `collectUIElementsText`'s rows in view, `OP_Report`).
    - ~~The text entry window~~: done (`frontend/text_entry.*`, `CTextEntryWnd`); the soulmark
      window's use of it waits on 36.
    - The gems game (`CGemsGameWnd` @0041cd06, /gems @004fa556: a falling-blocks game on the
      spell gem art, with its high scores file; 31 functions to `UpdateGemsGameWindowUI`
      @0041e6f2).
35. **The other slash commands** that still say they aren't supported, not under another item.
    The table is at 0x609af8: 277 entries of {label eqstr, name, localized name, the handler, the
    GM level, the /help kind}. Ghidra missed the handlers only the table points at; they're read from the
    exe's disassembly. Done so far: /split (`CEverQuest_DoSplit`; it doesn't close the windows
    DoSplit puts away, `DAT_007914a4` and `DAT_007952f4`), /yell with `msg_yell`'s directions,
    /loot, /charinfo (`FullZoneName` of the bind), /ttell (@004f839a: the target's name to its
    apostrophe, eqstr 13090 for a name with '#' or '_' but an NPC corpse's; its `send_tell` also
    stops on `DAT_007985f1`, as the socials do, not known), /movelog (@004fba78: "yes" sends
    MSG_MOVELOG, 0x41e4, which EQMacEmu ignores), /stopsong (@004f5c74: a bard's StopSpellCast2),
    /shield (@004fd688, `OP_Shielding`), /quit and /exit (@004fa8c5: the connection closed, the
    game ended), /log (@004fbad0, `eqlog`; [Defaults] Log), /keys (@004f8ec7, `OP_Key`),
    /dismount (@004ff5a6, `Mounts::DismountCommand`), and /adriver and /sdriver (@004f69f5: eqstr
    12379 only), /reverb, /resetwindows, /popmail and /alias (@0043777e, a bare `ret`: nothing
    said), /rtarget (@004ff62d: /target of the name typed or of the last teller, eqstr 13286 with
    no tells), /inspect (29) and /particledensity (@004ff0b3: `CDisplay::SetPCloudDensity` and
    the exe's two literal lines), /discipline (@004f4fb5, `frontend/disciplines.*`; checked on
    replay, `frontend_preview ... slash`, not live: the test characters are under 30) and /surname
    (@004f3ddb, `CEverQuest_Surname`; the same), /safelock (@004ff689; it leaves the window's
    close box alone, where `EverQuest_wndproc`'s WM_CLOSE is /quit, refused while locked or frozen) and
    /hotbutton (@004ff36a, `RegisterHotButton`), /mousespeed (@004f9e87; the cursor scaled by the
    host, not checked on Windows), /dynamiclights (@004fda22),
    /showspelleffects (@004f9f4d), /viewport (@004ffb00; the GPU renderer's not checked on Windows) and /showgrass
    (@004faa05; its zones don't load yet, 42).
    Still to do:
    - /facepick (23), /hitsmode and /clearhits (3), /mp3, /music and /gems (34),
      /shownpcnames (9).
    - The chat channels, all through `CEverQuest::ChatServerMessage` (@00524de9) to the chat
      server the world names (`OP_SetChatServer`; `ChatServerConnect` @00524cae,
      `ChatServerGiveTime` @00524885 from `DoMainLoop`, `CshOnMessage` @00524b2c,
      `ChannelServerApi` @0049c3cf, `ChannelManager_ProcessChannelCommand` @0049c6c7): /chat
      (@004f6439, and a line starting with `;`; usage eqstr 13065), /join (@00500106), /leave,
      /leaveall, /list, /announce, /1../10 (`_SendToChannel` @00500266, after DoPercentConvert),
      and /autojoin (@0050008a: the names into `[Defaults] ChannelAutoJoin`, "Set channels to be
      joined at login.", or three usage lines). EQMacEmu has no chat server for this client:
      waits on one.
    - The client settings, each a `[Defaults]` key in eqgame (ours go in my-client.ini):
      - /invwinlabels (@004fdc50, `InvWinLabels`, `DAT_007cf20c`): on, off or flipped, "Inventory
        Window Labels *ON*" or "*OFF*"; the labels are the old UI's.
      - /mixahead (@004fa100, `SoundMixAhead` 1..100, `EqSoundManager::SetMixAhead`, eqstr 7528
        and 7529), /reversesound (@004fb9a2: normal or reverse, 13214..13218) and /audiodebug
        (@004f60f6: 13051 on, 13052 off): Miles' own; ask before giving them a meaning in our mix.
      - /dopropertinting (@004fffdd, `DoProperTinting`) and /newui (@004ffa57, `NewUI`: eqstr
        15012 and 15013, the old UI the next run): ours is the new UI only.
      - /wincolor (@004fddcd): the old UI's windows (`WriteWindowColor` @004fdcfb); nothing to do.
      - /toggletell is a GM's (36).
    - Kept out unless asked: /mcicontrol (@004ff739, `mciSendStringA` with the line, the error
      printed), /webupdate (@004f3951, 0x423c with the text), /edit (@004fa6fc, `EQ_Guide::Edit`,
      the old UI's), and voice recognition: /voice (@004f6041: on, off or flipped for the next
      run, eqstr 13049 and 13050), /microphone (@004f60e0: a window message, 0x4c8) and /vrdelay
      (@004f6150, 13053..13058).
36. **GM tools**, for a GM account: /goto, /summon, /zone, /kill, /kick, /hideme, /becomenpc,
    /searchcorpse, /delcorpse, /emotezone, /servers, /find, /lastname, /approval and the
    `OP_GM*` packets; /private (@004f9062, GM level 1: the text through `send_private`
    @005368d9) and /broadcast (@004f416c, level 4: `send_broadcast` @005384f1); /toggletell
    (@004fa597, level 3: 0x41de, eqstr 13172..13174); the petition queue (`CPetitionQWnd`
    @0042d1ca, `msg_pq_update` @004dce71, `handlePetitionRequest` @004e42cf for
    `OP_PetitionCheckout`, /viewpetition, /deletepetition, ExecuteCmd 119); soulmarks
    (`CSoulmarkWnd` @00433258, /inquire, /praise, /warn, `OP_SoulMarkAdd`, `OP_SoulMarkList`,
    `OP_SoulMarkUpdate`); what the zone tells a GM or does to us for one (`OP_GMFind` 0x4069:
    eqstr 2134 with the zone and place, or 2135; `OP_GMKick` 0x406d: "Thou hast been expelled by
    the gods!", 1392, and the game ended; `OP_GMGoto` 0x406e and `OP_GMZoneRequest` 0x414f: put at
    the place given; 0x40d2, a level set: `LevelUpAffects` @004eff2d with "You are now level
    %1!", 12932; `msg_ability_change` @004e1a0c, 12915 and 12916); /height (@004f47b8, GM level
    4): the target's (or our) height to the number typed, said (eqstr 0x32a4) and sent as 0x40f5
    {spawn id, 29 (size), the height}, or with `DAT_00798560` or `DAT_00798620` said (0x32a3) and
    changed here (`EQPlayer::ChangeHeight`).
37. **What the zone sends that isn't handled yet.** eqgame's dispatchers
    (`CEverQuest::HandleWorldMessage` @004e829f and its second half `FUN_004ebe1c`) take 249
    opcodes; `ZoneSession` takes some 137. Done from earlier lists: `OP_ManaUpdate`,
    `OP_PlaySound`, `OP_DespawnDoor`, `OP_CancelSneakHide` (`CEverQuest_CancelSneakHide`),
    `OP_ShopEndConfirm` (the merchant window's Deactivate), `OP_BerserkStart` and `OP_BerserkEnd`
    (under 24), `OP_CorpsePosition` (`CEverQuest_HandleWorldMessage_FloorRelated_`: the spawn put
    there, stopped, and dropped to the floor), `OP_GMLastName` and `OP_GMNameChange` (the spawn's,
    and our profile's, surname or name, and the name over their head; they also set
    `EQ_Main`+0xe64, `DAT_007f9574`, not known), `OP_Charm` (30), `OP_SpellTextMessageStringID`
    (12), the boats' packets (39), `OP_RequestClientZoneChange` (`DoTeleportB`, refused while
    stance 102 or linkdead) and `OP_Illusion` (`do_change_form`: races 142 and 143 the nearest
    zone object's actor by `GetNearestActorTag`, else nothing and on us the SPA 0x3a buff off with
    1371; a rider off and back on; the spell emitters onto the new actor's root),
    `OP_DeleteCharge` from the zone (@004dd106: the count off the slot's charges, the item gone
    with its last, 1390 for nothing there), `OP_Reward` (0x4080: each coin's line with "from" the
    giver, the purse, wave 0x8d), `OP_LevelUpdate` as eqgame takes it (only a level above ours,
    our spawn's level, wave 0x8b), `OP_Disarm`'s answer (24), `OP_ResetSkill`
    (`UpdateAbilityTimer`: mend's and hide's ready times, another skill's last use) and the
    charm's `OP_FreezeClientControl`, `OP_UnfreezeClientControl` and `OP_ReturnClientControl`
    (stance 102 and back, 1394). All six checked on replay (`frontend_preview ... packets`), none
    live. Still to do:
    - Left over from those:
      - `OP_LevelUpdate`: the highest level reached (PCData+0x1360, not in our profile) raised
        with it and `CSkillsSelectWnd::Refresh`. A level lost isn't shown until the next
        profile, as in eqgame (it drops the packet).
      - The control packets: what reads StunnedState 2 isn't traced (ours keeps `m_frozen` and
        reads it nowhere; stance 102 does the work), `OP_ReturnClientControl` letting the waits go
        (`DAT_007d0254`, `DAT_007d0258`; ours are each window's own), `DAT_007cf1e4` (not known),
        and 0x40be (the freeze with `DAT_007cf1e4` 1; EQMacEmu doesn't send it). Not checked:
        the stance our model shows frozen, and a charm live.
      - `OP_Reward`'s giver is looked up among the spawns by id; eqgame's `trimName` of it is our
        `DisplayName`.
    - Sent by EQMacEmu, handled by eqgame, dropped here (found by matching the packets the
      server builds against `ZoneSession`'s cases):
      - `OP_MultiLineMsg` (0x4014): the text a line at a time (`ParseNextLine`), color 10 in the
        old UI; the new UI's path (color 0x145 at `LAB_004ecdca`) isn't traced.
      - `OP_SetTitle` (0x40d4, MSG_GRANT_TITLE, @004e45eb): eqgame reads a name at 0, a title at
        0x80 and a flag at 0xc2, and copies the title into our profile; EQMacEmu sends 8 bytes
        {is suffix, spawn id} for an AA title, which means nothing to it. Our titles come from
        the AAs (28); nothing to do unless the server's packet changes.
      - `OP_MoneyUpdate` (0x4008): in eqgame, coins picked up off the ground (`EQMoneyList`, 11);
        EQMacEmu builds one for a trader's sale and never sends it (33).
      - A GM's: `OP_GMFind`, `OP_GMKick`, `OP_GMZoneRequest`, `OP_PetitionCheckout`,
        `OP_PetitionRefresh`, `OP_SoulMarkUpdate` (36).
      - Built by EQMacEmu with no case in eqgame's dispatchers: `OP_GuildRemove` (0x4119),
        `OP_TradeReset` (0x4010), `OP_ZoneInAvatarSet` (0x406f). Nothing to do; check where the
        server sends them before believing that.
    - `OP_Illusion`'s leftovers:
      - `GetNearestActorTag`'s list is `s3dGetWorldVisibleActorList`'s, the zone regions the
        place's region can see (its PVS); ours takes every region in reach.
      - `SetHeights` for races 142 and 143: `ModelHeightOffset` 0 (0.1), so the server's z is the
        object's foot; ours keeps the size's center (size / 5 * 3.125) for where we stand and
        what we send. The eye (`CameraHeightOffset` 6) and the object's scale 1 are done.
      - `do_change_form`'s old (`spells.eff`) clouds and sprites on the spawn: whether eqgfx
        keeps them on the deleted actor isn't known; ours stay on the spawn.
      - A dwarf's, halfling's or gnome's z + 1 going to another zone (`DoTeleportB`) isn't sent
        anywhere and isn't ours.
    - Not sent by EQMacEmu (no `OP_` name in `patch_Mac.conf`; their original names from
      `../my-eqmac-server/server/utils/structs/opcodes.h`, byte-swapped): 85 more opcodes eqgame's
      dispatchers handle. EQMacEmu does most of their jobs through other packets (MSG_GSAY,
      MSG_SHOUT, MSG_CURHP, MSG_BUFF, MSG_ZONE_SKY); the rest are Sony's GM, housing, message
      board and limbo features. Worth a look if the server ever sends them: MSG_SPELLWORNOFF
      (0x4265), MSG_SPELLACTIVATEPARTICLES (0x4266, 0x4267), MSG_TRAP_LOCATION (0x41f4),
      MSG_NPC_SAY_TEXT (0x425e, `msg_npc_say_text` @004e07b9: an NPC's say or shout with its
      language through `HearBabel`, eqstr 1032, 1034, 1410, 1420), MSG_ABILITY_CHANGE (0x41bf),
      MSG_SND_CORPSE_CON (0x4053) and its notes (2), MSG_DUST_CORPSE (0x404c), MSG_ADD_MLIST
      (0x4007, coins on the ground), MSG_INVITE_OK (0x4042, group follow 2, under 2), the
      inventory's `msg_del_inventory` (@004e7ab9), `msg_mod_inventory` (@004e7d13),
      `msg_delete_item` (@004e3c33) and its bank and coin kin, and 0x40d2 (a GM's level set, 36).
40. ~~**Camping.**~~ Done (`FrontEnd::StartCamp`, `CampPassageOfTime`, `ZoneSession::Camp` and
    `CancelCamp`): `CEverQuest_Camp` (@00530c7b)'s checks (frozen, mesmerized 12368, not sitting
    12291, mounted 12292), /camp again calling it off (12290), `ChangeStance` calling it off, and
    `DoPassageOfTime`'s interruption (12683) and countdown (12684). Still to do:
    - `OP_Camp`'s 4 bytes of `DAT_007f95bc` when that's set (not known; ours sends none), and
      `CEverQuest_Camp`'s HP remembered (`DAT_0079875c`) and `DAT_00798744`, whose readers aren't
      traced.
43. **The front end's other screens** (eqmain.dll's `new_dll_main` @10025300). The start
    screen's ACCOUNT, CREDITS and OPTIONS and server select's NEWS say "not implemented"
    (`SISATEXT_NOT_IMPLEMENTED`), and server select's CHAT button isn't drawn:
    - Credits (`displayCreditsScreen` @10021eb0, `processNextCreditLine` @10021df0, the folder's
      `credits.txt`).
    - Options (`handleModelPreferencesOptionsScreen` @1001e620 and
      `ShowGraphicsAndMemorySettingsDialog` @1001fc80: the Luclin models by race and sex, the
      texture and memory settings, which are my-client.ini keys now, 21).
    - News (`displayAccountNewsScreen` @10019f90, `processNewsItems` @1001a1b0,
      `checkAndDisplayNewsUpdates` @10026af0; the login server's `OP_LoginBanner` is the only
      news EQMacEmu has).
    - The login server's chat (`handleChatInterface` @1001bc40, `attemptChatLogin` @1001bb30):
      EQMacEmu's login server has none.
    - The splash and the intro (`displaySplashScreen` @10021820, `playIntroVideo` @10025db0 with
      `processSmackVideoFrame`, `displayLicenseAgreements` @100267a0), the adapter and language
      pickers (`displayAdapterSelectionScreen` @1001c8d0, `displayLanguageSelectionScreen`
      @1001cba0).
    - Sony's account screens (registration, billing, the expansion upgrades, the PIN and password
      changes, voting): nothing to do against an emulator.

## Rendering / presentation

9. ~~**Nameplates**~~: done (`frontend/name_sprites.*`, eqgame's name sprites), lit as
   renderNameTag does (the world's ambient light +0.4, `NameSpriteLit`), on the animated head point
   (DagHeadPoint, renderSceneHierarchy's lift) and depth tested pixel by pixel in the 3D pass
   (`ZoneView::DrawNameSprites`). A Bazaar trader's "Trader " (5503) in the Bazaar, in place of an
   AA title, and its 0x40ffff (SetNameSpriteState, SetNameSpriteTint) are done. Still to do:
   the names eqgame hides in some animations (skeletons, gargoyles and mimics lying or feigning:
   animations 0x10, 0x21, 0x26), `/shownpcnames` (@004ff9d9, GM level 10: "off" is
   `CDisplay::TurnNPCNameSpritesOff` @004b14a4, anything else `TurnNPCNameSpritesOn` @004b14da, said
   as "SHOWNPCNAMES *OFF*" or "*ON*"), and the guild list in packet logs (replays show "Unknown
   Guild"). Zeal's raid shownames isn't the base client's. An HP
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
    the caster's animation and a spell's stage 1 on them, the old spells.eff one too: its sprite
    going with the missile and its emitters as `DoParticleEffect` places them for a missile), and
    a missile whose model is a gequip blit-sprite actor (`GENC00`, `GENW00`) drawn as that sprite
    (`SpellEffects::DrawActorSprite`). Our own arrows and throws come back from the zone as
    OP_Projectile (EQMacEmu sends them to the shooter too); DoAttack's own EQMissile is only for a
    shot with no target, which `CanIHitTarget` stops online. Still to do: the
    static kind 9 missiles' checks on who walks into them (MoveMissile's sentinel and trap
    spells), `DoParticleEffectNew` (@004e8208) and
    `t3dParticleSystemDestroyTaggedEmitters` (@004e81ef) for OP 0x4265 to 0x4267 (destroy or start
    a cast's emitters; this server never sends them).
13. ~~**The other camera modes F9 cycles through**~~: done (`ZoneView::CameraSetup`, `ToggleView`,
    `CameraKeys`, `MouseLookCamera`; `FrontEnd::LoadCameras`, `SaveCameras`): `CDisplay_ToggleView`'s
    five views with `CDisplay_default_camera`'s table, kept from zone to zone and the user cameras in
    `[Camera]`; `DoCamAi`'s overhead and user cameras; `ProcessControls`' zoom and six camera keys;
    `CEverQuest_procMouse` in those views; `SetViewAngle`'s lens per view (F9, the drunk's,
    `PrepareForRepop`), widened past 4:3 (`render::LensFov`). Still to do:
    - Our chase camera isn't eqgame's view 2 (`DoCamAi`: the distance plus `ModelHeightOffset`
      behind along our heading, the eye plus the height, the table's pitch; `procMouse`'s up and
      down moving its height and looking at us; the camera keys on it; `HandleMouseWheelZoom`'s 3 a
      notch to 100, and its 2 off view 2's bearing), and we start in it, where eqgame starts in view 0.
    - `s3dCollideSphereWithWorld` for views 1, 3 and 4 (ours: a segment from our center, 1 short of
      what it hits), and `DoCamAi`'s `GetEnvironment` of the camera's region (`DAT_0063b7c8`).
    - The mouse's sensitivity and invert (`_DAT_0079858a` from `DAT_00798b0c`, `DAT_007985e8`: the
      Mouse page) are fixed at 4 and off.
    - SPA 87 (magnification) as it lands or fades (under 2).
14. **Graphics options:** fullscreen or windowed, resolution changes, gamma, anisotropic filtering,
    MSAA, draw distance and level of detail. eqgame's side of it: `CDisplay::ChangeVideoMode`
    (@004acc0f, with `CDisplay_SupportsVidMode` @004acac4 and `UpdateCameraAfterModeSwitch`
    @004acb64), the video modes window (34), /viewport's rectangle read again for the
    new size, and
    `s3dSetDynamicLOD` (5).
15. **Screenshots.** ExecuteCmd 78 (Take a screenshot) says eqstr 12374 "Taking a screenshot..."
    and `CDisplay::RealRender_World` then calls `CaptureScreenshot` (@004aa6d1), which writes the
    frame as `EQ%06d.bmp`, the next number free, in the game's folder.
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
    - `CanIFitHere` (done, `PlayerMovement::CanIFitHere`) for a ridden horse: its room is three
      times its `ModelHeightOffset` (ours skips the check mounted), and an actor's own height
      (EQPlayer+0xf8) when it has one.
    - `DoFloorCheck` with no floor below keeps the last one and we fall; we stay put instead.
    - MovePlayer's other branch for a horse we ride (no lift; our horse moves as we do, lifted, its
      sphere twice ours; the boats' part is 39), and `MovementSpeedZ` held to -1 when the move didn't
      change our height.
    - Walking into a switch (`EQPlayer::TouchingSwitch` @0050b7ac, from MovePlayer; doors are only
      clicked now): type 50 a pressure plate (a PC, once, until `GetSwitchStateChangeTime` puts it
      back; `UseSwitch` with our id), 57 a teleporter (a PC, every 2 s, our cursor item, the
      destination from `GetTeleportTableEntryByID` only if `DoesZoneFileExist`), 35 every 1 s on
      a 49% roll (`UseSwitch` with id 0), 115 and up hurting (`GetSwitchDamage` through
      `IWasHit`, damage type 0xfd, every 1000 ms, 400 for a fast switch); a switch in state 4
      does nothing.
    - `CDisplay::MoveLocalPlayerToSafeCoords` (@004b459c): `DoMainLoop`'s way out when the
      vehicle still can't be set ("Still can't set vehicle"), a floor height found or the safe
      point.
    - Ladders: done (`PlayerMovement::GetOnLadder`, `ZoneView::ClickLadder`, `CDisplay::GetOnActor`
      @00504202 and the climb's state 6 in process_physics and MovePlayer). Still to do: check it
      against the TAKP client. As ported, the push GetOnActor gives us (along the heading, at the
      ladder) runs into the ladder's own mesh, which ends the climb at once and drops us down it,
      fall damage and all; whether eqgfx's collision takes the ladder actor (t3dCollideSphere) isn't
      known. Ours picks a ladder by its scaled bounding sphere, not its mesh (`t3dGetClickedActor`),
      and plays no climbing animation (`ProcessAnimation`'s 23 by MovementSpeedZ while climbing).
    - `MoveToZone_2`'s (@005460b5, see 24) other lines: under `EQWorldData::GetMinLevel`
      (@00523f42) of the zone, eqstr 12385 "Your will is not strong enough to leave the material
      plane."; a zone with no index 12386 "That zone is currently down, moving you to safe point
      within your current zone."; -1 from its check 5051 "This zone is not ready to receive
      visitors yet...". What each does to the move after saying so isn't traced.

38. ~~**Mounts.**~~ Done (`frontend/mounts.*`, `ZoneView::SetMounts`, `PlayerMovement::SetMounted`,
    `SpawnActor::Ride` and `Drive`): the zone's horse (race 216, `<rider>`s_Mount00`) taken by its
    rider (`MakeRiderMountUp` @0051fcf5, ProcessAffects' SPA 113 retry), `MountEQPlayer`
    (@0051fd83) with the old models' quirk (no Luclin player model: the horse only gets its Rider;
    an old model's or an untextured horse's attach fails, with eqstr 5524 and the buff off for us)
    and the horses UpdatePlayerVisibility never draws; riding: the rider on the horse's
    `RIDER_DAG` playing P09 or L10, another's horse moved by its rider's updates
    (`ProcessUpdateStats`), ours by our movement (`SetAccel`'s acceleration, turns of 8, no jumps,
    snares only, the camera at 0.85), the buff off as its head changes water; `Dismount` from
    /dismount, feign death, sitting refused, a horse or rider going (`Dismount2`), and an illusion
    (`do_change_form`: off, then the horse's `MakeRiderMountUp`, refused for a form with an old
    model); the mount's
    hunger messages, fall damage, spell book and targeting. Checked through replays of our horse
    (each model setting) and another player's. Still to do:
    - Not checked against the real client: how high the rider sits (the rider's model root on the
      `RIDER_DAG`, scale 1) and the camera there (our eye 0.85 up from under the saddle).
    - MountEQPlayer's levitation (`SetNoGrav` on the horse of a levitating rider; no levitation
      yet), onto something not a horse (its `DagRightPoint`), and a boat's `AttachedEntity` branch.
    - `UpdatePlayerVisibility`: a ridden horse follows its rider's visibility (we draw the invisible).
    - `EQPlayer::ChangeHeight` of a rider (a `/height` or size change) carrying on to its horse.
    - Being hit on a horse (`ReportSuccessfulHit`'s `DeacSpellScreen` and `ChangeStance`) and the
      right button on one (24, 2), `PlayAnimationTrack2`'s horse pitch (64) and its riding-skill
      `EQ_PC_CheckSkillImprove` while the horse moves, and the horse's swim jump (the swim skill's
      speed counts for it as for us).
    - `IsCharacterMeditating` for the client's own regen display (the zone's is what counts).
39. ~~**Boats.**~~ Done (`frontend/vehicle.h`, `ZoneView::UpdateSpawns`, `UpdateBoatShapes`,
    `PlaceOnVehicle`, `SteerBoat`, `PlayerMovement::Vehicle` and `Carry`, `SpawnActor::Carry`,
    `ZoneScene::SetActor`, `CharacterModels::BuildStatic`, `FrontEnd::VehiclePassageOfTime` and
    `FindMyVehicle`): the boats' mesh actors (`SHIP`, `LAUNCH`, `GSP`, `BOAT`) drawn and solid
    (`CDisplay_IsCollisionValid`), the one under our feet our `AttachedEntity` (`DoFloorCheck`),
    `vehicle_premove` and `vehicle_postmove` carrying us and the spawns on it, `OP_BoardBoat` and
    `OP_LeaveBoat` and the profile's boat (`DoPassageOfTime`), zoning in onto it (`DoMainLoop`,
    `IFoundMyVehicle`'s `MSG_LOCATE_VEHICLE`, `MoveLocalPlayerToSafeCoords`), `msg_vehicle_reset`,
    and a race 141 boat's helm (`OP_ControlBoat` from the right button and `ProcessControls`, the
    zone's yes and 1407, steering it with `MovePlayer`'s race 141 branch, its `OP_ClientUpdate` with
    its id). Checked on replay (`frontend_preview ... boats`): a Lake Rathetear rowboat stood on,
    steered and turned, and a Stormbreaker (`#npctypespawn 770`) zoned onto. Still to do:
    - Not checked live: a ship sailing its grid with us on it (none were up on the test server),
      the trip's `MovePC` and coming in on the next zone's boat, and the zone's answer to taking a
      helm.
    - The chase camera goes through a boat (`SegmentHit` leaves the solid spawns out); whether
      `DoCamAi`'s collision meets a boat's actor isn't known.
    - `SetHeights`' race 141 levitation (`SetNoGrav(2)`, `LevitationSize` 4, `LevitationMovementModifier`
      0.005: a bob) and `process_physics`' levitating branch for it (sinking 0.1 a tick down to
      0.4 over `FindZoneTopZ`); the carry leaves out a boat's pitch (`ViewActorPitch`).
    - `SavePC` and `SavePCForce` keeping `ZoneEnter` in the boat's frame on one (`vehicle_premove`
      around it) and the boat's name in the profile: our `OP_Save` sends the profile's bytes as the
      zone sent them, position and boat included.
    - `IFoundMyVehicle`'s answer 0x40a2 (`CEverQuest_IHaveTicket`, `MoveToZone` with
      `vehicle_xfr`), and its blocking wait (ours plays on for the 60 s).
    - `msg_vehicle_reset` and `MoveLocalPlayerToSafeCoords` also move `pinstCharSpawn` when it's
      attached or has a Rider (the boat we steer, our horse); ours move only us.
    - `RealRender_World` runs `pinstCharSpawn`'s `AttachedEntity` first and holds its z (all but race
      141); the spawns on a boat out of our reach (no shape there) aren't carried.
    - `CreatePlayerActor` falls back to `HUM_ACTORDEF` (`HUF_ACTORDEF` for a woman) when the actor
      isn't there; ours to race 75's (a gender 0 launch, `LAUNCHM`, shows as an elemental). Check
      where the race 75 fallback came from before changing it.
41. **Doors and switches.** Clicking, moving, sounds, and keys and lockpicks on the cursor
    (`CEverQuest_ClickedSwitch` @00547034, `FrontEnd::WorldClick`), walking onto one
    (`EQPlayer::TouchingSwitch` from the floor check's `CDisplay_IsCollisionValid`: type 35's
    chance, pressure plates, teleporters by `msg_teleport_index`'s `<zone>_tp.txt`, traps' IWasHit;
    `ZoneDoors::Touching`, `FrontEnd::SwitchTouched`), the invisible switches in the scene (54 and
    57 floors) and the particle switches (53: by the hour, `UpdateDoorStateBasedOnWorldTime`, and
    their spells.eff effect at the switch) are done (`frontend/zone_doors.*`). The plate checked on
    replay (`frontend_preview ... switches`), none live. Still to do:
    - `CDisplay_IsCollisionValid`'s floors for the visible doors: it makes 0..7 and 60..77 (lifts)
      no floor for `GetPlayerFloorHeight`, and the traps (115 up) none; ours stand on every visible
      door. Lifts carry us as they are; check how eqgame keeps us on one before changing it.
    - Riding a horse, nothing's touched (MovePlayer moves the horse); not checked whether eqgame's
      MovePlayer for us runs too then.
    - `GetTPCoordsFromIndex` (@0052459f), what reads the table's coordinates.
    - The particle switches at character select (23's `CRATEG`), and
      `EQSwitch::InitializeSwitchColors` (@005214f2, from the door loading code).
    - Type 56's 0x410f (MSG_REQ_HOUSELOCK) is Sony's housing: nothing to do.
42. **Scenery and model odds and ends**, found in the decompile and not ours:
    - The newer WLD version (0x1000c800): `WldFile::Load` takes only the older one, so dawnshroud,
      grimling, hollowshade, jaggedpine, letalis, maiden, mseru, nightmareb, podisease, pofire,
      ponightmare, postorms, potranquility, povalor, powar, scarlet, shadeweaver, tenebrous,
      thegrey, twilight and umbral don't load ("Couldn't read <zone>.wld"). Their 0x36
      meshes keep the older header and vertices; the rest isn't traced. Grass (done, `render/grass.*`) is
      only in some of these zones: check it there then.
    - `CDisplay::ReplaceSpecialCloakMaterials` (@004a0d05, from `CDisplay_SwapMaterial`): a
      model's materials named `CLK??06`..`CLK??09` swapped for the one named `CLK` + the actor's
      tag + the rest. `ReplaceCloakMaterials` is ours (robes); check this one against a robed
      Luclin model before porting.
    - `EQPlayer::SetToRandomRace` (@005200da, from `CreatePlayerActor`): a race 20 spawn in zone
      163 (Grieg's End) is given a race at random.
    - `CDisplay::SetUserRender` (@004afb6e, from `InitDDraw` and `StartWorldDisplay`) and
      `SetCameraFogMode` (@004aca90): not read yet.
    - `CDisplay::renderDoll` (@0049f61b, from `RealRender_World`) draws the old UI's inventory
      model; nothing to do in the new UI.

## Infrastructure / tooling

16. ~~**Packet record and replay.**~~ Done: `[Debug] RecordPackets=1` (GUI) or `--record DIR`
    (console) records; `--replay FILE` plays back in the GUI, console and `frontend_preview`.
17. **A Linux GUI host** (SDL2 with OpenGL or Vulkan), since the front end is already portable.
18. **Text rendering** off GDI (`win32_text`), onto a font renderer that works on both platforms
    (needed for 17).
19. ~~**Connection handling**~~: done (`FrontEnd::ShowLostConnection`; the sessions' `Error` reasons):
    `CEverQuest_DisplayScreen`'s "YOU HAVE BEEN DISCONNECTED." (12353) for 3 s on a lost zone or
    world connection past server select, "THE SERVER IS NOT RESPONDING." (12354) for 2 s when the
    waits for the zone's address (180 s, 120 s zoning) or the profile and our spawn (180 s) run out,
    then the game ends, as eqgame's does (it has no reconnect). Still to do: `ProcessGameLoop`'s
    "Couldn't connect to network, creating dummy" when the first `WorldAuthenticate` fails (ours:
    eqmain's dialog, then the start screen).
20. **Speed and stability:** load zones in the background, cache assets, catch crashes and write a
    crash dump, tests for the file and data parsers.
44. **What stays out**, so the decompile's function list (8128 in eqgame.exe, 5431 past the
    unwind stubs) isn't combed for it again. Ask before taking any of it up:
    - The old UI (`DAT_008092d8` 0, /newui): `EQ_Main` and its 17 subclasses (`draw`,
      `ProcessButton`, `doHotButton`, `DoMerchant`, `DoLoot`...), `EQ_Bag`, `EQ_Book`,
      `EQ_CharCreateS`, `EQ_CharSelS`, `EQ_CreateBS`, `EQ_DragItem`, `EQ_Guide`, `EQ_ItemData`,
      `EQ_3DView`'s drawing, `EQ_TextIO`, `EQ_Overlay`, `EQ_HSlider`, `EQ_Requestor`,
      `EQ_Columns` (the old UI's gems game), and every `DAT_008092d8 == 0` branch of the commands
      and packets (/wincolor, /invwinlabels, Backdrop, Lock windows, Toggle chat window,
      `msg_snd_trader_eq`, /note's `notes.txt` line).
    - Playing offline (`GameState` 1 or `DAT_00798620`): `SpellFizzled`,
      `CastingRequirementsMet` with `HasEnoughItemsForSpell` and `ExpendItemCharge`,
      `EQSwitch::SwitchWasOpened`, and what the server works out for itself at creation and on a
      level (`EQ_Character::InitMyLanguages` @004ba03a, `InitializeCharacterSkills` @004bce1d,
      `setSkillsByLevel` @004effec).
    - Sony's services: the chat server until a server has one (35), the message board
      (`EQBBoard` @0049c140), housing (`EQ_Main::ClearHouseBank`, MSG_REQ_HOUSELOCK), /webupdate,
      the client checks (`fillEvilProcessNames` @0055890c, `initializeClientValidationString`
      @005588b7, the exe checksum of 25), account screens (43).
    - Voice recognition (`InitializeVoiceRecognition` @0055785c, /voice, /microphone,
      /vrdelay), the IME (`CIMEManager`, `CEditWnd`'s composition handlers) and the other
      locales' text (the Korean, French and German grammar rules, `ConvertNameToLocal`).
    - Windows and Miles plumbing we replace rather than port: `EverQuest_wndproc`, DirectInput
      (`initializeDirectInputKeyboard`, `GetMouseInputData`), `rdp_*` and `NetworkConnection`
      (`third_party/rdplib`), `SoundManager`'s Miles calls, `CXStr`, the XML and SIDL
      loaders' internals (`XMLDocument`, `CParseTokenXML`, `UIElement` and the generated
      `*Control` and `UI*` classes), the C runtime.

## Order

1. ~~Packet replay (16), so the rest can be built offline.~~
2. ~~The window manager (1).~~
3. ~~The Player, Target and Chat windows (2, 3), which prove out the whole SIDL pipeline.~~
4. ~~Nameplates and the target indicator (9, 10).~~
5. ~~What the zone already sends and we drop, since it leaves our state wrong: `OP_DeleteCharge`
   and `OP_Reward` (37), then disarm's answer (24). The poison's answer (2).~~
6. ~~Camping's checks, countdown and calling off (40). The MOTD as we enter (3).~~
7. ~~Keys and lockpicks on doors (41), then ladders (22).~~
8. ~~Inspecting and the player notes window (29), the tracking window (24), breath and drowning
   (34).~~
9. ~~The stale Keyboard page rows (5), and the do-nothing commands that say "not supported" (35).~~
