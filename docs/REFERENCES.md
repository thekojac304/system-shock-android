# Credits and technical sources

## Game and source-port foundation

**System Shock** was developed by Looking Glass Studios and published by Origin Systems in 1994.

This Android port builds on [Shockolate](https://github.com/Interrupt/systemshock), by C. Cuddigan / Interrupt and contributors.

The pinned Shockolate revision is [4cc3d07dfff2d11b6d3a0a9960a51cf4ca253690](https://github.com/Interrupt/systemshock/commit/4cc3d07dfff2d11b6d3a0a9960a51cf4ca253690). It includes [oreo639's GCC 14 build fix, PR #421](https://github.com/Interrupt/systemshock/pull/421).

[System Shock: Enhanced Edition — Nightdive Studios](https://nightdivestudios.com/system-shock-enhanced-edition/). See [Game data](GAME_DATA.md) for the resource sets used by this port.

## Libraries

[SDL](https://github.com/libsdl-org/SDL), by S. Lantinga and contributors, provides platform integration. The pinned SDL revision is [5d249570393f7a37e037abf22cd6012a4cc56a71](https://github.com/libsdl-org/SDL/commit/5d249570393f7a37e037abf22cd6012a4cc56a71).

[SDL_mixer](https://github.com/libsdl-org/SDL_mixer), by S. Lantinga and contributors, provides the audio mixer. The build uses version 2.8.1; exact dependency pins are in [Build from source](BUILD.md).

[libADLMIDI](https://github.com/Wohlstand/libADLMIDI), by V. Novichkov, J. Yliluoma and contributors, provides MIDI synthesis with OPL3 emulation.

## Developer documentation

[SDL2 on Android](https://wiki.libsdl.org/SDL2/README-android) — Android integration guidance.

[Android NDK guides](https://developer.android.com/ndk/guides/) — native Android development tools and documentation.

## Reference hardware

[Retroid Pocket 5 specifications](https://www.goretroid.com/products/retroid-pocket-5).

Device-specific testing for this port is described in [Compatibility](COMPATIBILITY.md).

## Licences

[GNU GPL version 3](https://www.gnu.org/licenses/gpl-3.0.en.html) · [Repository licence](../LICENSE) · [Third-party notices](../THIRD_PARTY_NOTICES.md)
