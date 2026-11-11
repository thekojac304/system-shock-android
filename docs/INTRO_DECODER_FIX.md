# Android intro decoder investigation

Tested 2026-10-01 on the existing `touch-controls` branch. The decoder repair contains no touch-control redesign, gameplay/rendering change, startup-skip fallback, or packaging change.

## Root cause and symbolization

The decoder reads a packed **three-byte Huffman entry as a four-byte native word**:

```cpp
huffword = *((uint32_t*)huffptr) & 0x00FFFFFF;
```

The extension-table lookup in `Draw4x4_InternalAlpha` (original `draw4x4.cpp:133`) reaches the last valid entry. The fourth byte is outside the allocation. Masking the result cannot make the preceding load safe.

The supplied crash BuildId was `303140e878f6be14ad82210762bd6b5f58d8c4b0`. Neither available unstripped build matched it; interpreting that PC with the current symbols would incorrectly name `ComposeFlat24Add`. Instead, the current pre-fix QA APK was installed without clearing data and reproduced the same crash after three seconds of process uptime:

- BuildId: `f56b19676a56cefac4127ac294cf102786f6d7b4`.
- PC: `libmain.so + 0x243064`, symbolized by NDK r29 `llvm-addr2line` to `Draw4x4_InternalAlpha`, original line 133.
- Instruction: `ldr w8, [x8]`, followed by `and w8, w8, #0xffffff`.
- `x8 = 0xb4000072e5617ffd`; fault address `0xb4000072e5618000` (read, `SEGV_ACCERR`). Exactly three readable bytes remain before the protected page.
- The supplied older tombstone has the identical relationship: `x8=...efffeffd`, fault `...effff000`.

The external-data host trace locates the first terminal-entry read at **zero-based frame 22**, block origin **(404, 80)**, in a **600 x 300** frame. The Huffman allocation is **140,688 bytes**, entry **46,895**, byte offset **140,685**, with **3 bytes remaining**. That frame's compressed allocation is **18,283 bytes**. Destination pitch is **600 bytes**, and the block starts at destination offset **48,404**, safely within the 180,000-byte image.

This is a portable out-of-bounds read exposed by Android's guarded secondary allocation layout. It is not pointer truncation or an ARM64 alignment trap. `getconf PAGE_SIZE` reports **4096** on this phone, consistent with its `-4k` kernel. The existing 16 KB APK packaging warning is a separate issue.

## Focused repair

Only production file `SourceSnapshot/src/Libraries/AFILE/Source/draw4x4.cpp` changes:

- Read packed fields explicitly in little-endian order, using their actual widths: 2-byte frame offset, 3-byte Huffman entries, and 2/4/6/8-byte masks.
- The old type-3 mask load also read eight bytes while consuming six. It now reads exactly six. Other mask loads no longer depend on native alignment or aliasing.
- Bound control-bitstream lookahead at the mask-stream offset. A 12-bit flash-table lookup can legitimately need more lookahead bits than the final short code consumes. Zero-extension supplies only those lookahead bits without reading beyond the control stream.

The last point was discovered by putting the full movie's allocations against guard pages: frame 1 has a 68-byte chunk and no mask bytes. On its last block row (`y=296`), only ten bits are buffered for a 12-bit lookup; the old reader fetches byte 68 outside the allocation. This is additional invalid lookahead, not the reported frame-22 crash.

There is no output clamp, enlarged runtime allocation, automatic intro skip, or change to block semantics. The full movie's decoded pixel checksum matches the old decoder both with zero-filled and `0xFF`-filled extra readable padding. Thus the removed lookahead bytes do not contribute to the decoded pixels in the tested movie. This targeted fix does not turn the old decoder API into a fully validated parser for malicious/truncated files.

## Decode path and allocation audit

1. `play_cutscene` opens `res/data/svgaintr.res`, selects resource `0xbd6`, zero-initializes an `Afile`, and calls `AfilePrepareRes`. The resource is copied into an owned `MFILE` before `ResUnlock`; playback does not retain a resource-cache pointer. This owned MOVI is 14,402,996 bytes.
2. `AmovReadHeader` reads the packed 1,024-byte `MovieHeader`, records 600 x 300, 8-bit pixels, and copies the chunk directory into its own allocation. This movie has 1,523 chunks and a 13,312-byte directory allocation. Movie chunks use an 8-byte little-endian bitfield/offset disk layout on these compilers.
3. `AfileOpen` allocates a 180,016-byte work bitmap (180,000 image bytes plus the existing 16-byte write canary), plus 180,000-byte composition and previous-frame buffers. `gr_init_bm` makes the flat-8 work pitch equal to width, 600. `ComposeInit` uses the same pitch. The canary cannot detect reads beyond a separate Huffman allocation.
4. Audio is extracted into a separate `numAudioChunks * 8192` allocation, then `AfileReadReset` rewinds the chunk cursor and clears composition/previous images. Playback feeds that audio separately; the reported fault is on the video decode stack.
5. `cutscene_loop` calls `AfileReadFullFrame`, which dispatches `AmovReadFrame` with the work bitmap. Table chunks replace the static color/Huffman allocations. Compressed Huffman runs expand three-byte tokens into the size declared at the start of the table chunk. `Draw4x4Reset` installs pointers after a Huffman-table update; the compressed table is then freed.
6. Each 4x4 video chunk gets its own exact-length `malloc(len)` buffer. A temporary canvas wraps the work bitmap and is pushed for `Draw4x4`; it is popped before freeing the compressed frame. Both Huffman and color tables remain alive during decoding.
7. `Draw4x4` uses a 160-entry `uint32_t` stack scratch row (640/4), 150 cells per row here, and 75 rows. Alpha decodes Huffman control words, skips, and repeats. Beta consumes masks and color sets, writes four rows per cell using the canvas pitch, and preserves transparent/skipped pixels. The last row begins at y=296; the final pixel is byte 179,999. No rounded-up final destination block is needed.
8. `AfileReadFullFrame` checks the work canary, composes the frame, then copies 180,000 bytes into the caller's persistent movie bitmap. `cutscene_loop` applies the 768-byte palette and scales the completed movie bitmap for presentation. Palette storage is separate from the decoder's indexed color sets.
9. `cutscene_exit` frees the caller bitmap and audio stream/buffer, closes the resource, and frees the outer `Afile`. Existing cleanup does **not** call `AfileFree`, so internal movie/work/compose allocations leak; the static decoder tables also outlive the movie. This is an existing lifecycle defect, not a use-after-free causing the initial crash, and is not changed in this focused patch.

| Assumption audited | Finding |
| --- | --- |
| Pointer truncation, 32-bit address arithmetic, integer-to-pointer casts | Active decoder pointers retain native width. Table indexes use integer offsets added to pointers; the observed valid offset is 140,685. No truncation at the fault. |
| Signed/unsigned overflow and size calculations | Actual dimensions, pitches, table sizes and chunk offsets fit the existing integer types. Host parser checks resource/chunk extents and Huffman run sizes. Runtime malformed-input validation remains limited. |
| Alignment, unaligned word reads, aliasing, endian | Original typed word dereferences on byte streams are unsafe. Exact-width byte reads remove these dependencies in `Draw4x4`. ARM64 fault evidence is a protected-page read, not alignment. |
| Packing | Existing packed 1,024-byte header and 8-byte chunk layout matches this ARM64 little-endian resource. Bitfields remain compiler/endian dependent in the broader loader; no layout change is needed for this crash. |
| Pitch, buffer sizes, dimensions, block count | 600-byte pitch, 180,000-byte image, 150 x 75 cells. Scratch capacity 160 cells. These are sufficient. Widths over 640, malformed skip/repeat codes, invalid color indexes and malformed table runs are not generally checked by the runtime API. |
| Lifetimes and stale scratch/table pointers | Stack scratch lives for the full decode; per-frame input is freed afterward. Actual color updates precede Huffman resets. A color-only replacement without a following reset could leave a stale global pointer on other input; no evidence of that ordering in this movie. Existing exit leaks are separate. |
| Palette | Fixed 768-byte storage and 256-color presentation; not the faulting allocation. |
| Memory pages | No 4 KB rounding in the active decoder. Read crosses a guard page because the access exceeds its allocation. Tests query the OS page size. |
| Word/SIMD final-block overreads | Confirmed 4-for-3 Huffman read; confirmed 8-for-6 mask issue; confirmed terminal bitstream lookahead. Repaired. No handwritten SIMD path in this decoder. |

## Source lineage comparison

The pinned Shockolate source is [Interrupt/systemshock at 4cc3d07](https://github.com/Interrupt/systemshock/tree/4cc3d07dfff2d11b6d3a0a9960a51cf4ca253690). Before this repair, `draw4x4.cpp`, `huffde.cpp`, `amov.c`, and `afile.c` match that revision (line endings normalized). The current upstream master `draw4x4.cpp` fetched during the investigation also matches the pre-fix decoder. Replacing Android's decoder wholesale with those files would preserve the crash.

The material Android difference in `cutsloop.c` is audio ownership: Android pauses/resumes the shared SDL_mixer auxiliary stream and logs its lifecycle; upstream directly pauses the SDL audio device. It does not change the 4x4 buffers or decoder calls. That integration is preserved.

The local source identifies its PowerPC/Mac Night Dive lineage, retains the 1994 AMOV/AFILE and Huffman headers, and describes `draw4x4.cpp` as reverse-engineered. There is only one active `Draw4x4` implementation in this repository. `movie.h` retains an older `Movie` interface with table lengths and a `Draw4x4FreeResources` declaration, but no alternate working decoder/cleanup implementation was found. No missing original Mac implementation was invented or substituted.

## Regression tests

New files:

- `tests/movie/draw4x4_test.cpp`
- `tests/movie/stubs/lg.h`
- `tests/movie/stubs/2d.h`
- `scripts/test-movie.ps1`
- This report.

The test compiles the actual production decoder with only its canvas dependency stubbed. Protected pages catch reads, not just writes. Cases cover terminal root and extension Huffman entries, each mask width, transparency, nontrivial pitch, all 160 scratch cells, repeat/skip, and a short final Huffman code. Each of the root, extension, mask, and short-code cases produces Windows access violation `0xC0000005` against the old decoder at `-O0`; the corrected decoder passes at `-O0` and `-O2`.

```powershell
./scripts/test-movie.ps1 -Compiler C:/path/to/g++.exe
./scripts/test-movie.ps1 -Compiler C:/path/to/g++.exe -Movie C:/owned/res/data/SVGAINTR.RES
```

The optional input may be an extracted MOVI or an uncompressed LG archive. The harness validates container extents and expands table runs, then exercises the real decoder for every video frame with guarded input, Huffman, color and destination allocations. It is not an end-to-end test of the game's resource manager, audio, or compositor. It contains no game assets and does not need them for the synthetic regressions.

Full owned intro result on Windows x64 (both optimization levels) and natively on this phone using an NDK r29 ARM64 executable:

```text
600x300 frames=1106 last_time=110.994 pixel_hash=193a869034748168
```

The hash is FNV-1a over every decoded indexed pixel of every frame. The old decoder, run with eight extra readable bytes initialized first to zero and then to `0xFF`, produces the same hash. Native guard tests pass on the S26 Ultra. All six existing touch test suites pass unchanged.

## QA artifact and evidence

- Package: `io.github.raposomiguel50.systemshock.qa`, version `1.0.0-qa`, ARM64.
- Gradle `:app:assembleQa`: **PASS** during investigation. Preflight used its existing `-SkipGitCleanCheck` switch while the validated changes were deliberately uncommitted. The clean post-commit build and verification are reported separately.
- APK verification: **PASS**.
- Investigation native BuildId: `8c0fe9f2b0d79c27375972c77ee17265fa9ed560`.
- Investigation APK SHA256: `866dc2f5e2ff58e73d32d8d121538f25a1da3265cd9da13c17c487bd794a3656`. The committed touch diagnostic gate may change the subsequent APK hash.

Local ignored evidence is in `dist/intro-investigation/`: original and fresh crash logs, saved pre-fix APK/unstripped library, symbolization/disassembly, upstream comparison, native/host regression logs, pixel comparisons and hardware screenshots/logs. Owned movie data remains ignored; it must not be added to Git. No temporary decoder logging is present in the final APK.

## Final S26 Ultra acceptance

Investigation QA APK installed side by side with the stable app on Samsung Galaxy S26 Ultra, Android 17, ARM64. The movie started, passed the old ~2-second crash point, and played to its natural end. The cutscene audio stream closed at 23:39:45 and the original menu appeared; no native crash appeared in this process log. Observed frames throughout playback showed no obvious movie corruption. `New Game` reached configuration, startup help, and interactive gameplay. Touch movement and right-side look changed the in-game view while the process remained alive. This is a smoke test of Touch Alpha 1.1; the complete manual touch checklist was not repeated. All six existing automated touch test suites passed.

A second launch was interrupted with Escape during the intro. Its audio stream closed after eight seconds and the normal menu appeared without a crash. The repair leaves movie skipping optional; the intro is not automatically skipped. The final full-intro and skip screenshots and process logs are saved in the ignored evidence directory named above.

The existing touch-control implementation was left untouched by this decoder repair.
