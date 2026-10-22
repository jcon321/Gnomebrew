# TODO

Work after 1.0 that isn't game logic: the client showing and handling what the server sends, with
no rules of its own.

## UI

1. **In-game SIDL windows.** Load the full `EQUI.xml` set and add a window manager: drag, resize,
   close, minimize, focus order, see-through backgrounds, and window positions saved per character
   (the client's `UI_<char>_<server>.ini`).
2. **Display-only windows**, showing what the server already sends:
   - Player window: HP, mana, stamina, XP bar
   - Target window
   - Group window
   - Buff window
   - Inventory and bags, filled from the item packets
   - Spell gems and the spellbook (pictures only)
   - Hotbuttons
   - Compass
   - Selector bar
3. **A real chat window** to replace the current custom one: multiple windows with tabs, channel
   colors and filters, scrollback, input history, and a `/command` parser that works out what to
   send to the server.
4. **Cursor and dragging.** EQ's own cursors, item icons that follow the cursor when dragged, and
   hover tooltips.
5. **Options / Escape menu.** Graphics, sound and display settings, mouse sensitivity, the keymap.
6. **A configurable keymap.** `InputEvent::Key` is a hard-coded list of about 18 keys; make it an
   EQ-style keymap with primary and alternate bindings.
7. **Map window.** Draw the line maps from `../Maps` with our position and nearby spawns.
8. **Loading screen** while zoning, with progress.

## Rendering / presentation

9. **Nameplates** over heads: name, surname, guild, optional HP bar.
10. **Target indicator.** A ring or highlight on the target, and hover highlighting.
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
3. The Player, Target and Chat windows (2, 3), which prove out the whole SIDL pipeline.
4. Nameplates and the target ring (9, 10).
