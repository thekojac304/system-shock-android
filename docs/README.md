# Documentation index

Start with the [main README](../README.md). It describes what Touch Edition is and what the downloadable APK contains.
This folder mixes three kinds of document.

## Touch Edition: current

| Document | What |
| --- | --- |
| [FEATURES.md](FEATURES.md) | Every Touch Edition feature, with its status (released, preview, QA builds only, development only) and the evidence for it |
| [TOUCH_ALPHA_3.md](TOUCH_ALPHA_3.md), [TOUCH_ALPHA_4.md](TOUCH_ALPHA_4.md) | Touch settings, the layout editor, the radial wheels and haptics |
| [TOUCH_ALPHA_2_1.md](TOUCH_ALPHA_2_1.md), [TOUCH_ALPHA_2_2.md](TOUCH_ALPHA_2_2.md) | Cursor mode and how taps and drags map onto the original mouse interface |
| [INTRO_DECODER_FIX.md](INTRO_DECODER_FIX.md) | The intro movie crash and its fix |
| [GAME_DATA.md](GAME_DATA.md) | Which System Shock releases' game data work |

## Touch Edition: engineering and QA records

These are the notes written during development, kept as written. Several describe **QA builds** (package suffix
`.qa`, built with `RP5NP_QA_TOOLS`) and features that are **not in the released APK**. [FEATURES.md](FEATURES.md) says
which.

| Prefix | Topic |
| --- | --- |
| `QA1_*`, `QA2_*`, `HORROR_QA_TOUCH1` | Touch QA passes and in-game QA tools |
| `AUDIO1_*`, `AUDIO2_*`, `AUDIO_LIFECYCLE_*` | Replacement soundtrack routing and audio lifecycle tracing |
| `GPU_RENDER*`, `GPU_GROVE1_*` | OpenGL ES 3 world renderer research |
| `HORROR_VIS*`, `HORROR_GENERATED1`, `HORROR_HYBRID*`, `HORROR_AUTHOR*`, `HORROR_VISUAL_FEASIBILITY` | Lighting, atmosphere and post-processing research |
| `DECKLIGHT_*` | Generated deck lighting, per-deck acceptance |
| `MAINTENANCE_ELEVATOR_QA` | An intermittent elevator freeze investigation |

Later audio work (dynamic music, fades, loop points, an audio settings page) is in the
[development-history repository](https://github.com/thekojac304/system-shock-android-development-history), on branch
`backup/workspace-preservation-2026-11-10`.

## Inherited from upstream (System Shock — Android v1.0.0)

These were written by raposomiguel50 for the controller-first upstream port. They still use upstream's package name
`io.github.raposomiguel50.systemshock`, and some say touch is "an optional pointer". That was true upstream; in Touch
Edition, touch is the main input. They remain accurate for controllers, the engine, building and the release process. BUILD.md
has small Touch Edition updates.

[ARCHITECTURE.md](ARCHITECTURE.md), [BUILD.md](BUILD.md), [CI.md](CI.md), [COMPATIBILITY.md](COMPATIBILITY.md),
[CONTROLS.md](CONTROLS.md) (controller reference), [DEVELOPMENT_HISTORY.md](DEVELOPMENT_HISTORY.md),
[DEVELOPMENT_METHOD.md](DEVELOPMENT_METHOD.md), [INSTALL.md](INSTALL.md), [INTEGRITY.md](INTEGRITY.md),
[KNOWLEDGE_BASE.md](KNOWLEDGE_BASE.md), [KNOWN_ISSUES.md](KNOWN_ISSUES.md), [LESSONS_LEARNED.md](LESSONS_LEARNED.md),
[PRESERVATION_SCOPE.md](PRESERVATION_SCOPE.md), [PRESS.md](PRESS.md), [PUBLISH.md](PUBLISH.md),
[REFERENCES.md](REFERENCES.md), [RELEASE.md](RELEASE.md), [RELEASE_NOTES_V1.0.0.md](RELEASE_NOTES_V1.0.0.md),
[STABLE_V1_TRANSITION.md](STABLE_V1_TRANSITION.md), [V1_RELEASE_GATE.md](V1_RELEASE_GATE.md),
[UPSTREAM_README.md](UPSTREAM_README.md) (upstream's original README).
