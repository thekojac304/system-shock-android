# Touch Alpha 4

Touch Alpha 4 adds a shared optional radial selector, touch-only haptics, and a more consistent vector overlay. It preserves the original 4:3 game viewport and draws controls in full surface coordinates, including black bars.

## Controls and defaults

The settings page's **MORE** button opens five independent options. Haptics and all three wheels default **OFF** so an upgrade cannot introduce vibration or defer an existing quick action without the player's choice. Haptic strength defaults to **1** and cycles from 1 to 3. Settings schema 3 loads schema 1 and 2 placements, opacity, sensitivity, invert Y, and stick size, then fills new options with their defaults.

| Wheel | Hold host | Short tap | Hold confirmation |
| --- | --- | --- | --- |
| Weapon | NEXT weapon | Original next weapon | Select an owned weapon slot |
| Consumable | CONS | Original quick consumable | Set active patch type |
| Explosive | BOOM | Original quick explosive | Set active grenade type |

PREV remains available. A hold opens after **320 ms**. A small movement within the host control's slop is allowed. Moving farther before the threshold cancels that gesture. A short tap on an enabled host fires once on release; a disabled host still fires immediately on press. Releasing in the center or outside the wheel cancels. Only one wheel opens at a time; a second wheel-host press is ignored until the first closes.

The wheel uses the original player's seven weapon slots in order (slot indices 0 through 6); empty slots are hidden. Consumable and explosive entries use the game's seven patch and grenade type indices and hide zero-count types. Entries use the original game's short names, active selection, and current counts. A snapshot is taken on open. While open, live counts and active state refresh without moving segments; the game rechecks ownership/count at confirmation.

Weapon confirmation calls `inventory_select_weapon` through a guarded slot wrapper, the same path used by inventory selection and weapon cycling. Consumable and explosive confirmation set the game's corresponding active type and refresh the original inventory MFD. They **do not use or arm an item**. The next CONS or BOOM tap follows the unchanged `use_drug_hotkey` or `arm_grenade_hotkey` path, including their inventory visibility, count, timing, and cursor behavior. Choosing a type also reveals the active inventory list, matching the game's selection UI path.

## Input and rendering

`touch_radial.c` owns angle mapping, center cancellation, finger ownership, availability, and segment refresh for every category. `touch_controls.c` owns the common hold timer and touch suppression. While a wheel is open, its owner cannot look, click, or invoke the underlying tap. A second finger may continue MOVE. FIRE and USE are suppressed; an already held FIRE or USE prevents opening. Mode changes, focus loss, backgrounding, controller changes, surface changes, and global touch cancellation close the wheel without selection and require fresh input.

The overlay keeps a common circular control outline and stroke, stronger pressed and sprint latch fills, distinct LOOK/CURSOR color, amber editor selection, and a label alpha floor at low control opacity. The radial menu uses SDL vector lines and the existing internal bitmap font. No artwork or icon package was added.

## Haptics

Only accepted touch-control edges emit haptic requests. Native decision logic checks the saved option, focused touch context, gameplay availability, and controller suppression. The bridge calls the activity's `touchHaptic(strength, level)` method through SDL's Android JNI accessors. Android uses a short one-shot `VibrationEffect` with bounded amplitude on API 26+, with a short duration fallback on older APIs. Ordinary presses request one light pulse. Sprint and LOOK/CURSOR toggles, wheel opening, and wheel confirmation request one stronger pulse. Held FIRE, movement, looking, and radial dragging never repeat a pulse. Physical controller input has no route to this bridge.

## Validation

`scripts/test-touch.ps1` includes the Alpha 4 suite. `scripts/test-touch-linux.sh` runs the same touch suites with GCC on Linux or in a container. The Alpha 4 tests cover schema migration/persistence, independent settings, tap and hold boundaries, category ownership/selection, unavailable entries, count refresh, center cancellation, suppression, lifecycle/controller cancellation, and haptic request decisions. The existing bridge suite also checks that a confirmed wheel action reaches the game adapter once.

Hardware validation is complete. On-device feedback confirms the visual polish, haptics, all three wheels, hold/tap behavior, and existing Alpha 3 controls feel good, with no known Alpha 4 regression. The checklist below remains a guide for future device passes.

QA artifact: `AndroidProject/app/build/outputs/apk/qa/app-qa.apk`
SHA-256: `8fe22a55718a4b1114ec593a457375cc51c9fbc77d1e72e94c0e1c9599be8a49`

Validation completed on the development machine: all touch suites, both movie decoder optimization variants, project QA preflight, QA build, APK metadata/signature verification, and 16 KB ZIP and ELF alignment verification.

### Hardware checklist

- [ ] 1. Existing Alpha 3 controls unchanged
- [ ] 2. Ordinary button press haptic
- [ ] 3. Sprint toggle haptic
- [ ] 4. LOOK/CURSOR toggle haptic
- [ ] 5. Haptics OFF suppresses pulses
- [ ] 6. All three wheels OFF: legacy controls unchanged
- [ ] 7. Weapon Wheel ON independently
- [ ] 8. Consumable Wheel ON independently
- [ ] 9. Explosive Wheel ON independently
- [ ] 10. All three wheels ON together
- [ ] 11. Short tap still performs the old action
- [ ] 12. Hold opens the correct wheel
- [ ] 13. MOVE continues while a wheel is open
- [ ] 14. Highlight multiple segments
- [ ] 15. Release to confirm
- [ ] 16. Center release cancels
- [ ] 17. Unavailable item cannot be selected
- [ ] 18. Inventory/count changes update availability
- [ ] 19. Controller connects while a wheel is open
- [ ] 20. Background/resume while a wheel is open
- [ ] 21. Settings persist after restart
- [ ] 22. Layout settings remain intact
- [ ] 23. Part O still returns to LOOK correctly
- [ ] 24. Cursor/world/container interactions remain correct
- [ ] 25. Intro tap-to-skip remains correct

### Files changed

- Touch state, selector, visuals, settings and bridge: `AndroidProject/app/jni/touch/touch_controls.{h,c}`, `touch_radial.{h,c}`, `touch_render.c`, `touch_editor.c`, `touch_settings.{h,c}`, `touch_shock_bridge.{h,c}`.
- Android and game adapters: `AndroidProject/app/src/main/java/com/rp5np/systemshock/ShockolateActivity.java`, `AndroidProject/app/jni/CMakeLists.txt`, `SourceSnapshot/src/GameSrc/input.c`, `invent.c`, `Headers/invent.h`.
- Tests and runners: `AndroidProject/app/jni/touch/tests/touch_alpha4_test.c`, `touch_alpha3_test.c`, `touch_bridge_test.c`, `touch_render_test.c`, `stubs/SDL.h`, `scripts/test-touch.ps1`, `scripts/test-touch-linux.sh`.
- This document: `docs/TOUCH_ALPHA_4.md`.
