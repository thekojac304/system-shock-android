# Touch Alpha 3

Parts A–N are implemented on `touch-controls`. The existing Part O implementation
and its pickup/release tests are preserved. Automated validation passed, and the
user reported successful hardware validation in normal use on 2026-10-02.

## Storage and defaults

Settings are stored in `touch-settings.cfg` under the Android app's private
files directory, obtained from `SDL_AndroidGetInternalStoragePath()`.
For QA this is normally
`/data/user/0/io.github.raposomiguel50.systemshock.qa/files/touch-settings.cfg`.
The stable package has its own private file. No external storage permission is used.
Explicit Save writes a temporary file, flushes/syncs it, then atomically replaces
the preferences file. Normal startup never writes over settings. Android retains
these files across restart, reboot, and an APK update installed over the same package.

The format is bounded, line-oriented `key=value` text, schema **2**:

```text
version=2
opacity=1
sensitivity_x=1
sensitivity_y=1
invert_y=0
fire.x=0.86
fire.y=0.73
fire.scale=1
stick.x=0.20
stick.y=0.77
stick.scale=1
```

Every control has named `.x`, `.y`, and `.scale` keys. Coordinates are fractions
of the full physical surface, including black bars. The data table retains the
Alpha 2.2 positions; the new SET button starts at `(0.16, 0.06)`.

| Setting | Default | Range |
| --- | --- | --- |
| Opacity | 1.00 | 0.25–1.00 |
| Look X multiplier | 1.00 | 0.25–3.00 |
| Look Y multiplier | 1.00 | 0.25–3.00 |
| Invert Y | Off | Off / On |
| Each control's scale | 1.00 | 0.60–1.80 |
| Stick size (`stick.scale`) | 1.00 | 0.60–1.80 |

Stick size has one canonical value: the settings panel's Stick Size and the
editor's MOVE scale both edit `stick.scale`. At 1.00, radius remains **11% of the
rendered game viewport height**, matching Alpha 2.2. The stick continues to spawn
at the touch-down point; its stored position only sets the resting/editor display.

Missing files use defaults. Version 1 retains recognized valid fields (including
the legacy `stick_size` spelling) and fills missing controls from defaults.
Malformed fields fall back or clamp; invalid headers and oversized files use
defaults. Unknown newer schemas are not overwritten. Migration stays in memory
until an explicit Save. Reset affects only touch preferences and remains
cancellable until saved.

## Settings and editor

Tap **SET** near the top left during gameplay or pause. The panel provides Edit
Layout, Opacity, Look X/Y, Invert Y, Stick Size, Reset, Cancel, and Save/Done.
Opening it releases touch-held movement/actions and blocks gameplay touch input.
Physical input sources and original menu handlers retain their existing behavior.

In Edit Layout, tap a control and drag it. One device/finger pair owns the drag;
other touches cannot move a different control. The selected control is highlighted
in amber. The arrows cycle selection, SIZE −/+ changes its scale, and OPACITY −/+
changes the preview. The center toolbar leaves all default controls visible.
MOVE TOOLBAR moves it between the center and top; the toolbar disappears during
a captured drag. This permits editing positions anywhere on the physical surface.

Drawing, gameplay hit testing, and editor selection share computed geometry.
Centers are clamped using the scaled bounds, including after aspect-ratio changes.
LOOK/CURSOR shows the current mode; CURSOR is amber. Sprint displays RUN ON while
latched. Global opacity affects the controls, while panel text and selection stay
readable.

Save/Done persists the draft and closes. A failed save keeps the draft open with
an error. Cancel reverts the entire session. Focus loss, backgrounding, surface
changes, controller suppression, game-context changes, mode reset, and global
touch reset also close the panel and discard unsaved changes. Saved preferences
survive these events; prior held actions never resume.

Opening the touch panel clears Part O's pending return as a modal-context reset.
Outside this modal, the existing LOOK → USE → held object → release return works
unchanged. Manual CURSOR item interactions remain in CURSOR.

## Changed files

New implementation files:

- `AndroidProject/app/jni/touch/touch_settings.h` and `touch_settings.c`: schema,
  defaults, validation, migration, bounded loading, atomic save.
- `AndroidProject/app/jni/touch/touch_editor.c`: shared geometry and editor/panel input.
- `AndroidProject/app/jni/touch/touch_render.c`: overlay and settings/editor drawing,
  extracted from `touch_controls.c` and expanded.

Updated implementation/build files:

- `AndroidProject/app/jni/touch/touch_controls.h` and `touch_controls.c`: settings
  state, modal cancellation, configurable relative look, and panel routing.
- `AndroidProject/app/jni/touch/touch_shock_bridge.c`: private storage and modal
  integration, retaining Part O behavior.
- `AndroidProject/app/jni/CMakeLists.txt`: new Android touch sources.

Tests and tooling:

- `AndroidProject/app/jni/touch/tests/touch_alpha3_test.c`: new persistence,
  migration, corruption, editor, bounds, save failure, sensitivity, stick,
  controller, and reset tests.
- `AndroidProject/app/jni/touch/tests/touch_bridge_test.c`: settings/editor
  lifecycle integration and the preserved Part O tests.
- `AndroidProject/app/jni/touch/tests/touch_render_test.c`: opacity, panel/editor,
  renderer-state restoration, and optional drawing-command capture.
- `AndroidProject/app/jni/touch/tests/touch_input_test.c` and new
  `touch_input_platform_stubs.h`: explicit unused-platform doubles, allowing the
  real input adapter tests to run under Clang as well as GCC. Unexpected platform
  calls fail the test.
- `scripts/test-touch.ps1`: new modules/suite and portable input-test linking.
- `scripts/render-touch-preview.py`: PNG previews from actual recorded drawing commands.
- `docs/TOUCH_ALPHA_3.md`: this report and validation checklist.

This change also includes the prior Part O changes in
`touch_shock_bridge.h`, `SourceSnapshot/src/GameSrc/input.c`, and
`tests/stubs/input.h`; these were not reimplemented.

## Validation

Passed on 2026-10-02:

- All nine touch suites: controls, Alpha 1.1, Alpha 2, surface, bridge (including
  Part O), Alpha 3, cursor targets, real input adapter, and rendering.
- Movie decoder regressions at O0 and O2.
- Project preflight, with the expected dirty-tree allowance for this uncommitted candidate.
- Android QA build and APK package/signature verification.
- 16 KB ZIP alignment and ELF PT_LOAD alignment for every packaged native library.
- Patch whitespace check.
- Visual inspection of actual renderer commands for settings and editor layouts
  against a placeholder background. These previews are not hardware screenshots.

QA APK: `AndroidProject/app/build/outputs/apk/qa/app-qa.apk`

SHA256: `637d5f869544c35080b5af4fc612d5bf4c3d473509fdd37e5c3237cf422930ae`

## Hardware validation checklist

The user confirmed that settings, layout editing, persistence, sensitivity,
Invert Y, stick size, opacity, cursor behavior, and Part O feel correct in normal
hardware use. The checklist below remains available for future regression checks.

1. Confirm default move/look/FIRE/USE feel unchanged, including black-bar touches.
2. Change X sensitivity and check horizontal look.
3. Change Y sensitivity and check vertical look independently.
4. Enable/disable Invert Y.
5. Change stick size; verify radial movement and immediate release.
6. Move FIRE, including into a black bar.
7. Move USE.
8. Move PAUSE.
9. Resize at least two controls and check their visible edges against hit bounds.
10. Lower and raise opacity; check selection and panel readability.
11. Save, restart the app, and confirm every changed preference persists.
12. Reset to Defaults, Save, and confirm only touch settings reset.
13. Edit and Cancel; restart and confirm the saved layout is unchanged.
14. Connect/disconnect a controller; confirm suppression, saved layout, and fresh-touch requirement.
15. Background/resume with both settings and editor open; confirm unsaved drafts revert and actions stay released.
16. Toggle CURSOR after changing layout; verify UI taps, container items, and fullscreen transitions.
17. Part O: LOOK → USE world item → release into inventory → LOOK.
18. Part O: LOOK → USE world item → drop into world → LOOK.
19. Part O: enter CURSOR manually → manipulate an item → remain in CURSOR.

Hardware validation passed in normal use, as reported by the user on 2026-10-02.
