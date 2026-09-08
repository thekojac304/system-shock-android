# Known issues and development history

## Current compatibility limits

### OPEN-SS-002 — Other Android devices

**Status: open.** The Retroid Pocket 5 is the tested reference device. Other Android 13+ ARM64 devices still need compatibility reports.

Controller maps, system overlays, display handling and app lifecycle behaviour may differ. Touch is an optional pointer, not a validated touch-only control scheme.

[Compatibility matrix](COMPATIBILITY.md) · [Report a device test](https://github.com/raposomiguel50/system-shock-android/issues/new/choose)

## Resolved build and installation issues

### REL-SS-001 — SDL_mixer tag missing after a clean clone

**Status: resolved in the pre-release build workflow.** In v0.1.0-pre.1, dependency setup failed at `git checkout --detach release-2.8.1`.

The helper used `git clone --no-tags`, then requested a tag that had not been fetched. It now fetches the matching tag when the requested reference is absent.

Clean setup reported `BOOTSTRAP_DEPS=PASS`, with SDL at `5d249570393f7a37e037abf22cd6012a4cc56a71` and SDL_mixer at `171eb2d420d5643e4ee11514a06e04a41a463bbd`.

Version v0.1.0-pre.2 superseded the affected pre-release for source reproduction.

### REL-SS-002 — Build attempted with JDK 25

**Status: resolved.** Gradle 8.1.1 failed with `Unsupported class file major version 69` during the first v0.1.0-pre.2 build attempt.

The host exposed JDK 25, while this build requires JDK 17. Preflight now checks the Java major version before building.

Fresh JDK 17 builds and APK verification passed. See [Build requirements](BUILD.md).

### REL-SS-003 — Android launcher resources ignored by Git

**Status: resolved.** A clean checkout referenced `@mipmap/ic_launcher` without the required Android resource directory.

The broad `res/` ignore rule also matched `AndroidProject/app/src/main/res/`. Anchoring the game-data rule to `/res/` allowed Android resources to be tracked.

Fresh build, APK verification and compiled-icon checks passed after the change.

### REL-SS-004 — Game data required a developer-only import path

**Status: resolved and tested on hardware.** The earlier workflow used ADB and `run-as` to place files in app-private storage.

The first-run launcher now uses Android's folder picker. It copies a compatible `res` folder through staging and activates it only after copying succeeds.

A signed, non-debuggable release-QA build passed import and smoke tests on the Retroid Pocket 5. The baseline installation remained unchanged; temporary QA data were removed afterwards.

[Installation](INSTALL.md) · [Import process](GAME_DATA.md)

## Release milestones

### REL-SS-005 — Stable package and signing identity

**Status: established for v1.0.0.** Stable releases use `io.github.raposomiguel50.systemshock` and a dedicated signing identity.

Stable signing-certificate SHA-256:

`806d9cb061de67aa6953cdac573bd917da6aa17625964c2898d23e226bd5323b`

The historical pre-release used `com.rp5np.systemshock` and certificate digest `7419c3aae7efaeea3e0e10945a98164418faf92fa1e55deac2b654c72cb34409`.

These are separate application lines. Stable v1.0.0 does not carry forward the pre-release package or certificate.

Release and release-QA verification require `RP5NP_RELEASE_CERT_SHA256`. Official compatible stable updates retain the stable certificate above.

Keep the private keystore and its credentials outside Git and public artifacts. See [Release identity](INTEGRITY.md) and [Signing configuration](RELEASE.md).

### REL-SS-006 — First public APK pre-release

**Status: published as v0.1.0-pre.3.** This was the first APK-bearing public pre-release.

Its checks covered clean dependency setup, JDK 17 build, APK verification, side-by-side installation, game-data import, smoke testing and manual gameplay acceptance.

The pre-release signing identity belongs to that historical line. Version 1.0.0 starts the separate stable package described above.

### REL-SS-007 — Stable v1.0.0 release

**Status: released; manual and machine gates complete.** A complete playthrough was accepted on the Retroid Pocket 5 before the stable source freeze.

The final checklist records the signed build, APK checks, release identity and public download verification.

[Completed release checklist](V1_RELEASE_GATE.md) · [Published v1.0.0](https://github.com/raposomiguel50/system-shock-android/releases/tag/v1.0.0)

## Historical research outside version 1.0.0

### SCOPE-SS-003 — HD world graphics and truecolor rendering

**Status: closed for the preservation release.** Bicubic scaling introduced colours that the indexed renderer could not represent exactly without quantisation or a different renderer.

Version 1.0.0 uses the original resources. The HD and truecolor experiments remain in the development history.

### SCOPE-SS-004 — Font reconstruction

**Status: closed for the preservation release.** Offline research analysed 36 fonts and 5,696 glyphs. Several experimental stages were rejected.

Version 1.0.0 uses the original game fonts.

[Preservation scope](PRESERVATION_SCOPE.md) · [Development history](DEVELOPMENT_HISTORY.md)
