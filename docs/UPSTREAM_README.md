# System Shock — Android

An unofficial Android/ARM64 port of System Shock, based on [Shockolate](https://github.com/Interrupt/systemshock). The Retroid Pocket 5 is the tested reference device.

**Version 1.0.0 is available.** Bring compatible game data from your own copy of System Shock. The APK does not include it.

[Download the APK](https://github.com/raposomiguel50/system-shock-android/releases/download/v1.0.0/SystemShock-Android-v1.0.0-arm64-v8a.apk) · [Release notes](https://github.com/raposomiguel50/system-shock-android/releases/tag/v1.0.0) · [Project website](https://raposomiguel50.github.io/projects/system-shock-android/)

## What does the port provide?

**Handheld controls.** Use the right stick to look around. Press View/Select to switch it to a fine cursor for the original mouse-driven interface.

**Touch and text entry.** Touch can also control the pointer. Android's on-screen keyboard handles text fields.

**Original-style presentation.** The game runs at 1024×768 in 4:3 without stretching. Version 1.0.0 keeps the original graphics, fonts, sound and gameplay content.

**Game-data import.** Select your compatible `res` folder on first launch. The importer copies the files into private app storage before starting the game.

[Full controls](docs/CONTROLS.md) · [Preservation scope](docs/PRESERVATION_SCOPE.md)

## Install and play

You need Android 13 or later, an ARM64 device and compatible game data. The controls are designed for a physical controller; touch is an optional pointer.

1. Download and install the v1.0.0 APK linked above.
2. Place your compatible System Shock `res` folder on the device.
3. Open **System Shock — Android** and choose **Select res folder**.
4. Select the folder containing both `data` and `sound`, then approve access.

The game starts when the import finishes. You do not need ADB or developer tools.

Keep the original game-data copy separately. Uninstalling the app may remove its private storage.

[Installation guide](docs/INSTALL.md) · [Compatible game data](docs/GAME_DATA.md)

## Compatibility

The Retroid Pocket 5 has an accepted complete playthrough and first-run importer test. Other Android 13+ ARM64 devices still need their own compatibility reports.

[Device compatibility](docs/COMPATIBILITY.md) · [Report a test or problem](https://github.com/raposomiguel50/system-shock-android/issues/new/choose)

## Learn from the work

The engineering notes cover controller input, text entry, audio timing and Android integration. They also retain rejected experiments and their outcomes.

[Engineering knowledge](docs/KNOWLEDGE_BASE.md) · [Architecture](docs/ARCHITECTURE.md) · [Development history](docs/DEVELOPMENT_HISTORY.md)

## Build it yourself

The Windows/PowerShell 7 guide lists the toolchain and pinned dependencies. Use the QA build for testing and your own signing key for independent release builds.

[Build guide](docs/BUILD.md) · [Signing and release process](docs/RELEASE.md) · [Release checks](docs/V1_RELEASE_GATE.md)

## Version and file identity

Stable v1.0.0 uses package `io.github.raposomiguel50.systemshock`, version code `10000` and the `arm64-v8a` architecture.

The historical pre-release uses `com.rp5np.systemshock`. It is a separate installation, with separate app storage and signing identity.

The official release includes the APK checksum, JSON manifest and final test report. Exact source and certificate identifiers are in [Source and release integrity](docs/INTEGRITY.md).

## Credits and contributions

The port builds on Shockolate. [Credits and technical sources](docs/REFERENCES.md) identify the upstream projects and dependencies.

I set the goals, make project decisions and test the reference hardware. ChatGPT assists with programming, analysis, automation and documentation.

[Development method](docs/DEVELOPMENT_METHOD.md) · [Contribute](CONTRIBUTING.md) · [External coverage](docs/PRESS.md) · [ModDB](https://www.moddb.com/mods/system-shock-android)

The source-port code uses the repository's [GPL-3.0-or-later licence](LICENSE). See [third-party notices](THIRD_PARTY_NOTICES.md) for dependency attribution.
