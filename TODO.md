# TODO

Work after 1.0 that isn't game logic: the client showing and handling what the server sends, with
no rules of its own.

## UI

1. ~~**In-game SIDL windows.**~~ Done: `frontend/window_manager.*` (drag, resize, close, minimize,
   focus order, fading, `UI_<char>_<server>.ini`). Still to do: BGTint isn't drawn, no
   right-click context menu (lock, alpha), no resize cursors, and only the chat window's edit box
   takes typing.
2. **Display-only windows**, showing what the server already sends:
   - ~~Player window~~ and ~~target window~~: done (`frontend/game_values.*`, gauges in `sidl.cpp`).
     Still to do: max mana leaves out spell effects (buffs) and `AntiTwinkAdj` for items with a
     recommended level; the XP gauge (EQType 4) needs the experience table.
   - Group window
   - Buff window
   - Inventory and bags, filled from the item packets
   - Spell gems and the spellbook (pictures only)
   - Hotbuttons
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
