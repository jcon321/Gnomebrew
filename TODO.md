# TODO

Work after 1.0 that isn't game logic: the client showing and handling what the server sends, with
no rules of its own.

## UI

1. ~~**In-game SIDL windows.**~~ Done: `frontend/window_manager.*` (drag, resize, close, minimize,
   focus order, fading, `UI_<char>_<server>.ini`). Still to do: BGTint isn't drawn, no
   right-click context menu (lock, alpha), and only the chat window's edit box takes typing. The
   resize and drag cursors are the SIDL ones; the usual cursor is still eqmain's `cursor.bmp`
   rather than `A_DefaultCursor`.
2. **Display-only windows**, showing what the server already sends:
   - ~~Player window~~ and ~~target window~~: done (`frontend/game_values.*`, gauges in `sidl.cpp`).
     Still to do: max mana leaves out spell effects (buffs) and `AntiTwinkAdj` for items with a
     recommended level; the XP gauge (EQType 4) needs the experience table.
   - Group window: laid out as `CGroupWnd::UpdateButtons` does (`frontend/game_windows.*`), but
     there's no group state yet (invites, members, leader).
   - Buff window: the buttons are arranged as `CBuffWindow` does and empty slots draw nothing;
     still to do: our buffs from the player profile and `OP_Buff`, their icons (`BuffIcons`
     decals, from the spell file) and names (label EQTypes 45..59).
   - Inventory: name, level, class (its title from 51), deity, HP, stats (base and worn items),
     money, the class picture, item icons in the slots, the Alt. Adv. row from 51, Done, and the
     Skills button opening the skills window (`frontend/game_windows.*`). Still to do: eqgame's own
     formulas for the resists, AC, ATK and weight (blank for now), and for stats spells, AAs and
     drunkenness; the XP and AA gauges (the experience table); picking items and coins up, Destroy,
     Alt. Advance, bags, and icons in the hot button window's weapon slots.
   - Item display: alt+click or the right button held on an inventory slot shows its item
     (`frontend/item_display.*`, eqgame's SetItem text with spells_en.txt's spell names); letting
     the right button go closes it. Still to do: clicking its icon to link the item in chat
     (`CItemDisplayWnd::WndNotification`), FV's always-no-drop item ids (`EQ_Item::IsSpecialNoDrop`),
     open bags, and items in other windows (bags, loot, trade, merchants, links).
   - Skills window: the skills the server says we have (not 254 or 255) and our languages, with
     their ranks, kept up to date by OP_SkillUpdate. Still to do: racial skills the server marks
     255 (eqgame's HasSkill table), selecting rows.
   - Spell gems and the spellbook (pictures only; the gems are all empty)
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
   size, STML links, hold-to-repeat on the scrollbar's arrows, languages scrambled by skill
   (`HearBabel`), and the commands that still say they aren't supported (/who, /consider, /sit,
   socials, /time...).
4. **Cursor and dragging.** EQ's own cursors, item icons that follow the cursor when dragged, and
   hover tooltips.
5. **Options / Escape menu.** Graphics, sound and display settings, mouse sensitivity, the keymap.
6. **A configurable keymap.** `InputEvent::Key` is a hard-coded list of about 18 keys; make it an
   EQ-style keymap with primary and alternate bindings.
7. **Map window.** Draw the line maps from `../Maps` with our position and nearby spawns.
8. **Loading screen** while zoning, with progress.

## Rendering / presentation

9. ~~**Nameplates**~~: done (`frontend/name_sprites.*`, eqgame's name sprites). Still to do:
   their lighting (eqgfx dims them with the scene's light, +0.4), the trader tag in the Bazaar,
   the names eqgame hides in some animations (skeletons, gargoyles and mimics lying or feigning:
   animations 0x10, 0x21, 0x26), `/shownpcnames`, Zeal's raid shownames, and the guild list in packet logs (replays show "Unknown Guild"). An HP
   bar over heads isn't eqgame's; leave it to an option if ever.
10. ~~**Target indicator**~~: done, as eqgame does it: the target's name flashes from white back
   to its color. The client of this era had no target ring and no hover highlighting.
11. **Ground spawns and dropped objects.** Handle `OP_GroundSpawn` so dropped items and forges
    appear.
12. **Spell visuals.** Casting particles and effects from `spells.eff` and the spell-effect `.s3d`
    files, triggered by animation and cast packets.
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

1. Packet replay (16), so the rest can be built offline.
2. The window manager (1).
3. ~~The Player, Target and Chat windows (2, 3), which prove out the whole SIDL pipeline.~~
4. ~~Nameplates and the target indicator (9, 10).~~
