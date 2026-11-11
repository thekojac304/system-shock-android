# Touch Edition feature inventory

This is a complete list of what Touch Edition adds to upstream System Shock — Android v1.0.0, with the evidence for
each item. It was checked against:

- the source of 1.1.0-beta.2;
- the build configuration;
- the text strings inside the published APK's native library;
- the development history, and the QA notes in [docs/](.).

(Audit of 2026-11-11.)

**Status key**

| Status | Meaning |
| --- | --- |
| **Released** | In the downloadable 1.1.0-beta.2 APK and available to normal users |
| **Preview** | In the APK, but limited in scope |
| **QA builds only** | Implemented and tested in internal QA builds; compiled out of the release APK |
| **In APK, not usable** | The code is in the APK, but a normal user can't turn it on |
| **Development only** | Uncommitted development work, preserved on the `backup/workspace-preservation-2026-11-10` branch of the [development-history repository](https://github.com/thekojac304/system-shock-android-development-history). Not on `main` here and not in the APK |
| **Research** | Experimental tooling, or work that was set aside |
| **Upstream** | Provided by Shockolate or upstream and unchanged here |

Commit links point to the
[development-history repository](https://github.com/thekojac304/system-shock-android-development-history).

## How the release APK was checked

- **Release build, not a QA build:** the APK's package is `io.github.thekojac304.systemshock`, version `1.1.0-beta.2`,
  code `10101`. The `qa` and `releaseQa` build types add `.qa` / `.releaseqa` to the package name, so the APK is a
  `release` build.
- **QA code compiled out:** only the `qa` build type turns on `RP5NP_QA_TOOLS`, which compiles `qa_tools.c` and the GPU
  renderer (`GpuWorld.c`, `GpuGenerated.c`, `GpuTextureCache.c`). The APK's `libmain.so` contains none of their text
  strings: no shader source, and none of the QA tools' messages.
- **Released features are present:** strings of the touch layer are in the APK. Examples are `TOUCH SETTINGS`,
  `EDIT LAYOUT`, `WEAPON WHEEL ON` and `HAPTIC STRENGTH`.
- **Some QA labels appear but are unreachable:** strings such as `GOD MODE ON` are in shared label tables, but they are
  dead code. The switches that enable them are only set when `RP5NP_QA_TOOLS` is on.
- **Source matches:** tag `v1.1.0-beta.2` in this repository has the same tree as the private release commit, apart
  from one device identifier redacted in seven QA notes.

## Controls and input

| Feature | Status | Evidence |
| --- | --- | --- |
| Full-screen touch overlay, including the black bars; hidden while a controller is connected | Released | [`303551a`](https://github.com/thekojac304/system-shock-android-development-history/commit/303551a) Touch Alpha 1.1; `AndroidProject/app/jni/touch/`. Tested with the beta.2 APK on a Galaxy S26 Ultra |
| Floating move stick, swipe to look, LOOK X / LOOK Y sensitivity, INVERT Y | Released | `touch_controls.c`, `touch_settings.c`; [TOUCH_ALPHA_3.md](TOUCH_ALPHA_3.md) |
| FIRE (drag to aim), USE, JUMP, CROUCH, RUN sprint latch, ID, previous/next weapon, CONS, BOOM, pause, SET | Released | `touch_settings.c` defaults. The buttons are new; the game actions behind them (quick patch, target ID…) are upstream's |
| LOOK / CURSOR mode with the game's click and double-click rules; recognizes container items, inventory rows and the fullscreen icon; returns to LOOK after a world pickup | Released | [`10c1c3b`](https://github.com/thekojac304/system-shock-android-development-history/commit/10c1c3b); [TOUCH_ALPHA_2_1.md](TOUCH_ALPHA_2_1.md), [TOUCH_ALPHA_2_2.md](TOUCH_ALPHA_2_2.md). Upstream had touch only as a plain pointer |
| Reliable single-tap menus; drags not taken as clicks | Released | `UI/event.c`, `wrapper.c`, `setup.c`; [QA2_COORDINATED_QA.txt](QA2_COORDINATED_QA.txt) |
| Weapon, consumable and explosive radial wheels (hold 320 ms; off by default) | Released | [`d5b8bb9`](https://github.com/thekojac304/system-shock-android-development-history/commit/d5b8bb9) Touch Alpha 4; `touch_radial.c`; [TOUCH_ALPHA_4.md](TOUCH_ALPHA_4.md) |
| AMMO button: tap to reload, hold for the ammo wheel | Released | [`fa57a8b`](https://github.com/thekojac304/system-shock-android-development-history/commit/fa57a8b); [QA2_COORDINATED_QA.txt](QA2_COORDINATED_QA.txt) |
| Haptics (off by default, strength 1–3) | Released | [`d5b8bb9`](https://github.com/thekojac304/system-shock-android-development-history/commit/d5b8bb9); `ShockolateActivity.java` `touchHaptic()` |
| Layout editor: move and resize each control, opacity, stick size, toolbar position; saved in settings schema 3, with older schemas migrated | Released | [`e5e2a9d`](https://github.com/thekojac304/system-shock-android-development-history/commit/e5e2a9d) Touch Alpha 3; `touch_editor.c`, `touch_settings.c` |
| Tap to skip the intro and cutscenes; tap to dismiss the start-up help | Released | `touch_shock_bridge.c`, `olh.c`. Tested with the beta.2 APK |
| MFD minigames no longer read touch as a right-click | Released | `mfdgames.c` ([`fa57a8b`](https://github.com/thekojac304/system-shock-android-development-history/commit/fa57a8b)) |
| Touch assist: snapping for small side icons, inventory tabs and MFD buttons | Development only | `touch_assist.h`; hardware retest pending |
| Controller support, Android keyboard input, 4:3 presentation | Upstream | upstream v1.0.0 ([CONTROLS.md](CONTROLS.md)) |

## Visuals and rendering

| Feature | Status | Evidence |
| --- | --- | --- |
| Visuals option in the game's Video settings: ORIGINAL / VIS1 CONS / VIS1B HORROR. A regrade of the indexed lighting on the Medical deck's healing-suite area only, falling back to Original elsewhere | Preview | `wrapper.c` `visuals_button_*`, `horror_visuals.c`, `horror_profile.c`; [HORROR_VIS1_QA.txt](HORROR_VIS1_QA.txt), [HORROR_VIS1B_QA.txt](HORROR_VIS1B_QA.txt). Not covered by the beta.2 device test |
| OpenGL ES 3 world renderer, falling back to the software renderer | QA builds only | [`8fddd2f`](https://github.com/thekojac304/system-shock-android-development-history/commit/8fddd2f), [`bcc842c`](https://github.com/thekojac304/system-shock-android-development-history/commit/bcc842c), [`463e5f1`](https://github.com/thekojac304/system-shock-android-development-history/commit/463e5f1), [`b19e5bb`](https://github.com/thekojac304/system-shock-android-development-history/commit/b19e5bb), [`7f195f7`](https://github.com/thekojac304/system-shock-android-development-history/commit/7f195f7); [GPU_RENDER1B_QA.txt](GPU_RENDER1B_QA.txt), [GPU_RENDER2_3_4_QA.txt](GPU_RENDER2_3_4_QA.txt). No full parity |
| Generated deck lighting (DECKLIGHT): 8 of 10 decks fully lit, Maintenance and Security with post-processing only | QA builds only | [`7c5505e`](https://github.com/thekojac304/system-shock-android-development-history/commit/7c5505e), [`25fd353`](https://github.com/thekojac304/system-shock-android-development-history/commit/25fd353), [`bc6495f`](https://github.com/thekojac304/system-shock-android-development-history/commit/bc6495f), [`ffffad8`](https://github.com/thekojac304/system-shock-android-development-history/commit/ffffad8), [`c143769`](https://github.com/thekojac304/system-shock-android-development-history/commit/c143769); [DECKLIGHT_CLOSE2_QA.txt](DECKLIGHT_CLOSE2_QA.txt). Profiles are built from the player's own game data during a QA build |
| Atmosphere post-processing: vignette, grain, haze and height fog, per-deck mood, fixture flicker sync | QA builds only | [`b56fb78`](https://github.com/thekojac304/system-shock-android-development-history/commit/b56fb78), [`b465f34`](https://github.com/thekojac304/system-shock-android-development-history/commit/b465f34), [`33a9d97`](https://github.com/thekojac304/system-shock-android-development-history/commit/33a9d97); `HORROR_HYBRID1…5` notes. The `world-post*` settings in the APK have no effect without the GPU renderer |
| Offline lighting-inference tooling (AUTHOR1–1E) | Research | [`497d47d`](https://github.com/thekojac304/system-shock-android-development-history/commit/497d47d) … [`ff62699`](https://github.com/thekojac304/system-shock-android-development-history/commit/ff62699), which records a failed gate; `scripts/horror_*.py` |

## Audio

| Feature | Status | Evidence |
| --- | --- | --- |
| Per-deck replacement soundtrack from user-supplied MP3s, with fallback to stock music | In APK, not usable | `ExternalMusic.c`, `musicai.c`, `Prefs.c` (`soundtrack = original`). No in-app setting and no music supplied; the QA installer needs a debuggable build. [AUDIO2_SOUNDTRACK_QA.txt](AUDIO2_SOUNDTRACK_QA.txt) |
| Dynamic exploration/combat music | Development only | `AUDIO3_SOUNDTRACK_QA.txt`, `tests/audio/dynamic_music_test.c` |
| Fades between tracks and resume at the saved position | Development only | `AUDIO31_RESUME_QA.txt`; the 1000 ms fade still needs a hardware check |
| Per-track loop points (22 tracks) | Development only, experimental | `AUDIO4_*`, `scripts/audio4/` |
| Audio settings page (master, music, effects and ambience volumes; soundtrack; dynamic music) with a 30 dB volume curve | Development only | `AUDIO5_MOBILE_POLISH1.md`; hardware retest pending |
| Enhanced ambience (randomized per-deck layers) | Research | `AmbientDirector.c`; no content exists |
| Audio lifecycle and transition tracing | QA builds only | [AUDIO_LIFECYCLE_QA.txt](AUDIO_LIFECYCLE_QA.txt). Diagnostics, not a fix |
| SDL_mixer audio at 48 kHz, cutscene and audio-log mixing, pause and resume | Upstream | `SDLSound.c` |

## Compatibility, stability and packaging

| Feature | Status | Evidence |
| --- | --- | --- |
| Intro movie decoder overread fix | Released | [`59c343e`](https://github.com/thekojac304/system-shock-android-development-history/commit/59c343e); [INTRO_DECODER_FIX.md](INTRO_DECODER_FIX.md); `tests/movie/draw4x4_test.cpp`. Intro plays in the beta.2 test |
| 16 KB native-library alignment | Released | [`64d4dc0`](https://github.com/thekojac304/system-shock-android-development-history/commit/64d4dc0); `scripts/verify-apk.ps1` |
| Launcher crash after the first import on a phone held upright | Released (beta.2) | [`8245002`](https://github.com/thekojac304/system-shock-android-development-history/commit/8245002); reproduced and confirmed fixed on a Galaxy S26 Ultra |
| Own package, label and signing key; installs beside upstream | Released | [`6d5a39e`](https://github.com/thekojac304/system-shock-android-development-history/commit/6d5a39e), [`8245002`](https://github.com/thekojac304/system-shock-android-development-history/commit/8245002) |
| QA tools: god mode, full loadout, deck teleport, visual-compare buttons | QA builds only | [`fa57a8b`](https://github.com/thekojac304/system-shock-android-development-history/commit/fa57a8b), [`0ef5854`](https://github.com/thekojac304/system-shock-android-development-history/commit/0ef5854); [QA1_ANDROID_QA.txt](QA1_ANDROID_QA.txt) |
| Game-data import and launcher | Upstream (plus the beta.2 crash fix) | `LauncherActivity.java` |
| Save and load | Upstream (no functional change) | `saveload.c` changes are QA-only traces |
| Performance | No release changes | Timing measurements exist only in QA builds |

## Open issues

- Save slots can't be named from touch.
- A Maintenance elevator freeze was seen once in QA builds, wasn't reproduced on retest, and has no known cause
  ([MAINTENANCE_ELEVATOR_QA.txt](MAINTENANCE_ELEVATOR_QA.txt)).
- Sound effects can arrive slightly late after the app returns from the background. The cause isn't known
  ([AUDIO_LIFECYCLE_QA.txt](AUDIO_LIFECYCLE_QA.txt)).
- The GPU renderer still needs translucency and ordering work for Maintenance, and has weak coverage on Security.
- The Visuals preview still needs art review and testing in enemy encounters.
