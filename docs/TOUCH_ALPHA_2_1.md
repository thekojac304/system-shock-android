# Touch Alpha 2.1 hardware notes

## Cursor interaction

The original `view3d_mouse_handler` treats a single left click as a look/identify
action. Its `UI_MOUSE_LDOUBLE` branch calls `view3d_dclick`, which picks the
rendered object at the cursor and calls `use_object_in_3d`. The touch cursor
previously sent one left down/up pair per tap. Repeated taps could accidentally
complete the original double-click sequence, explaining the intermittent pickup.

A short cursor tap now sends one native left down/up pair for UI regions and two
pairs for the active 3D view. Both routes use the existing mouse queue and UI
double-click handler. The bridge maps surface coordinates to game coordinates,
then `touch_world_view_at` converts those to original UI coordinates and checks
`_current_view`. Raised, visible child regions in fullscreen (MFD, inventory,
and other controls) remain UI targets. Drags still start only after touch slop,
hold the original left button, and release it without a use event. No object
pick geometry or distance rules changed.

## Sprint

The touch Sprint control toggles a touch-owned Shift modifier on each completed
tap. The button uses its pressed appearance while latched. Cancellation, mode
change, focus loss, backgrounding, surface change, gameplay loss, and controller
suppression clear it. Physical keyboard Shift and controller L3 keep their own
state when touch Sprint clears.

## Target Identifier

R3 and the touch ID button dispatch `RP5NP_CONTROLLER_ACTION_TARGET_ID`, which
calls the original `use_ware(WARE_HARD, HARDWARE_TARGET)`. This toggles the
Target Identifier hardware. `use_ware` returns without activation when the
player has not acquired it, and can reject activation when power is insufficient
or a hardware check fails. The input mapping therefore can work without a
visible effect before the hardware is acquired and usable. No Target-ID behavior
was changed in Alpha 2.1.

## Hardware check

1. Tap Sprint once, lift the finger, then move with the floating stick; confirm
   sprint remains active and the button remains highlighted. Tap again to stop.
2. Latch Sprint and test Cursor mode, background/resume, focus loss, rotation or
   surface recreation, and controller connection; confirm it clears each time.
   Hold physical Shift or L3 during a touch reset and confirm that source stays on.
3. In Cursor mode, tap a UI/MFD/inventory control once; confirm ordinary single
   click behavior. Drag an item past slop and release; confirm drag without use.
4. Tap a pickup/use object in the 3D view once. Repeat with rapid taps and small
   finger jitter; confirm one intended use per tap. Check a raised fullscreen MFD.
5. Recheck FIRE and drag-to-aim, movement/look, USE, jump, crouch, weapons,
   quick consumable/explosive, pause, intro skip, and startup-help dismissal.
6. Test Target ID only after acquiring the hardware and with sufficient energy.

If world pickup is still difficult on device after this semantic fix, investigate
touch assistance separately. Do not enlarge world-object hitboxes as part of
this change.
