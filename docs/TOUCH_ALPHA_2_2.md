# Touch Alpha 2.2 hardware notes

## Original mouse semantics

The fullscreen switch is the fullscreen hardware side icon. `screen_init_side_icons`
creates its left/right strip regions below `fullview_region` at z=0, while the
view has z=1. `side_icon_mouse_callback` accepts `MOUSE_LDOWN`, calls `use_ware`,
and ignores release and double-click actions. The fullscreen ware's turnoff
handler requests `GAME_LOOP`. Alpha 2.1 checked only children raised above the
view, so it missed this permanent child and sent a second click into the 3D
view. The classifier now recognizes the original icon region and its actual
icon rectangles before dispatching any mouse button edges.

Searching a container opens the `MFD_GUMP_FUNC` via `object_use`. The MFD gump
draws each item from its bitmap in a slot. `mfd_gump_handler` looks at the item
on the first left down, sets `LAST_DOUBLE` on `UI_MOUSE_LDOUBLE`, and calls
`gump_pickup` on the following left up. Touch now selects object semantics only
on the same drawn item bitmap the handler tests. An inventory row with a `use`
handler similarly expects `UI_MOUSE_LDOUBLE`; ordinary buttons and empty rows
retain single-click behavior. Drags still hold and release one left button.

## Device validation

1. In Cursor mode, tap the fullscreen hardware icon once to enter and once to
   leave fullscreen. Repeat several times. Confirm no world pickup/use occurs
   on the exit tap.
2. Search a nonempty container in normal mode. Tap each drawn item with slight
   finger jitter and confirm one pickup per tap. Drag an item beyond touch slop
   and release; confirm no extra pickup/use on release.
3. Tap inventory objects and plain UI/MFD buttons. Confirm object use and
   ordinary single-click controls each behave as expected.
4. Recheck world pickup, FIRE drag-to-aim, MOVE/LOOK, USE, JUMP, CROUCH,
   weapon cycling, quick actions, Sprint, Target-ID, PAUSE, intro skip, and
   startup-help dismissal.

The QA APK is a local build artifact and is not part of the source commit.
