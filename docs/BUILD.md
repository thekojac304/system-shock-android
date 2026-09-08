# Build from source — Windows / PowerShell 7

Use a Git clone for this build path. The helper scripts check Git metadata and tracked changes; a downloaded source ZIP is not an equivalent input.

## Required tools

- Git and PowerShell 7
- JDK 17
- Android SDK Platform 34 and Build Tools 34.0.0
- Android NDK `29.0.14206865`
- CMake 3.22.1 from the Android SDK
- Android platform-tools for device installation and testing only

The included wrapper uses Gradle 8.1.1 with Android Gradle Plugin 8.1.1.

## 1. Check out the source

Clone the repository into a new directory. To use the version 1.0.0 source:

```powershell
git clone https://github.com/raposomiguel50/system-shock-android.git
cd system-shock-android
git checkout --detach v1.0.0
```

Keep the `.git` directory. Run the following commands from the repository root.

## 2. Obtain the pinned dependencies

```powershell
pwsh -File .\scripts\bootstrap-deps.ps1
```

The helper creates the ignored `.deps/` directory and checks out:

- SDL 2.32.10: `5d249570393f7a37e037abf22cd6012a4cc56a71`
- SDL_mixer 2.8.1: `171eb2d420d5643e4ee11514a06e04a41a463bbd`

The build scripts run `scripts/preflight.ps1` automatically. It checks JDK 17, the Android toolchain, dependency revisions and a clean tracked working tree.

## 3. Build the separate QA application

For a device that already has the official game installed, use the QA variant:

```powershell
pwsh -File .\scripts\qa-gate.ps1
```

It builds and verifies `io.github.raposomiguel50.systemshock.qa`. That package can coexist with the stable application and the historical pre-release.

The equivalent individual build and verification steps are:

```powershell
pwsh -File .\scripts\build.ps1 -Variant qa
pwsh -File .\scripts\verify-apk.ps1 -Variant qa
```

Verification checks package, version, label, SDK levels, ARM64 libraries, signing and obvious embedded commercial resources. Device testing remains a separate step.

## 4. Test on the device

With the device connected and authorised through ADB:

```powershell
pwsh -File .\scripts\install-qa.ps1 -GameRes 'C:\path\to\owned\res'
```

Replace the example path with your own compatible game-data folder. The helper checks that the stable and historical pre-release packages remain unchanged.

## Other build variants

**Debug:** `build.ps1 -Variant debug` uses `io.github.raposomiguel50.systemshock`, the same package ID as the official stable application, but a debug signing key.

Use QA for side-by-side testing. Do not uninstall the official application just to make a debug installation succeed; its private data may be removed.

**Release:** `build.ps1 -Variant release` produces a non-debuggable build and requires explicit signing configuration. See [Release process](RELEASE.md).

For `release` and `releaseQa`, APK verification also requires `RP5NP_RELEASE_CERT_SHA256`. Independent builders supply their own signing identity; the official private key is not distributed.

## Release checks

A candidate needs static script checks, dependency setup, preflight, build and APK verification. It then needs the applicable device tests and signed-release checks.

Keep the build logs and executable hashes with the tested source revision. See [Release checklist](V1_RELEASE_GATE.md) and [Source and release identity](INTEGRITY.md).

The `.cxx` and Gradle build directories are disposable, path-bound output. Do not transfer them as a portable build environment.
