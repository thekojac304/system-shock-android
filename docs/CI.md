# Continuous integration

GitHub Actions builds and checks the public QA package. The private stable signing key is not used by this workflow.

## What runs?

The Windows workflow checks out the source, installs JDK 17 and prepares the pinned Android toolchain.

It then validates PowerShell scripts, fetches the pinned SDL dependencies, builds the QA APK and runs the APK verifier.

The workflow uploads a short-lived QA artifact for inspection. Its official checkout, Java setup and artifact actions use Node.js 24-compatible generations.

## What does a successful run cover?

A pass records completion of the scripted build and verifier in the hosted environment.

The checks cover package ID, version, label, ARM64 libraries, debuggable state, signature properties and obvious embedded commercial-data paths.

Hardware behaviour is tested separately. The stable release has an accepted complete playthrough on the Retroid Pocket 5.

## How is the official APK produced?

The stable package is `io.github.raposomiguel50.systemshock`. The historical pre-release package is `com.rp5np.systemshock`.

The official signed APK is produced from a clean local checkout with the private release-signing environment:

```powershell
pwsh -File .\scripts\v1-final-gate.ps1
```

The public workflow does not produce that official signed release.

## Where are the results?

The release evidence identifies the successful Actions run and the exact source commit. The final checklist also records the signed local build and public artifact checks.

[Release checklist](V1_RELEASE_GATE.md) · [Signing process](RELEASE.md) · [Source and artifact identity](INTEGRITY.md)
