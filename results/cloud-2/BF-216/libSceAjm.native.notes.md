# libSceAjm.native (18 todo: B-S 6, D 12)

**What it is:** the AJM audio-codec engine API: contexts, codec instances, and batches of jobs (initialize, control, run/decode, gapless, resample, statistics) that `sceAjmBatchStart` executes. Also the WVorbis header/setup packet helpers and the MP3/ATRAC9 parsers.

**Existing state** (`core/libs/prx/libSceAjm.native/src/Ajm.cpp` unless noted):
- 22 done: context, module and memory registration, instance destroy and codec type, the batch builders Run, Decode, DecodeSplit, DecodeSingle, GetInfo, GetCodecInfo and GetGaplessDecode, Wait, Cancel and ErrorDump, the ATRAC9 config and MP3 frame parsers, and `sceAjmDecWVorbisCreateHeaderPacket` (`Export.cpp:74`).
- Instances: `Instance` (`:136`) in `g_instances` (`:189`), with LibAtrac9, the vgmstream vibration decoder, and FFmpeg MP3, Opus and M4AAC contexts, plus gapless state and a Catmull-Rom resampler (`:129`, `:634`).
- Batches: each builder packs a `PackedJob` (`:221`) through `Append` (`:234`) and `MakeHeader` (`:254`). `sceAjmBatchStart` (`:1893`) runs every job synchronously through `Execute` (`:1607`). Batch ids are kept per context in `g_batches` (`:194`).
- Tests (`core/libs/prx/libSceAjm.native/CMakeLists.txt`): `guest_ajm_tests` (`core/libs/tests/GuestAjm.cpp`, `:55`, `add_test` `:59`), `guest_ajm_vibration` (`:60`/`:64`), `guest_ajm_wvorbis` (`:65`/`:69`), `guest_ajm_teardown` (`:71`/`:75`), `atrac9_gradient` (`:36`/`:45`) and `atrac9_config_data` (`:47`/`:53`).

**Counts:** A 0, B-S 6, B-M 0, B-L 0, C 0, D 12.

## Functions that already behave but count as todo

progress.py counts a function as todo when it calls a same-file helper that can reach `NotImplemented_nid_no_patch`. `Append` (`:237`) throws for a null buffer list with a nonzero count. Every builder that calls it is todo, including the eight that can never reach that branch: `MakeHeader` (`:254`) leaves both counts 0, and Control (`:1823`) passes a non-null list.
- **B-S (6):** ClearContext, Initialize, SetGaplessDecode, SetResampleParameters, SetResampleParametersEx and GetResampleInfo. Their jobs already run in `Execute` (`:1621-1662`), with the semantics and assumptions the debt records (`:11`, `:14`, `:16`, `:19`).
  - The work is to move the null-list check out of `Append` into `sceAjmBatchJobRunSplit` (`:1827`), the only builder that passes a caller's buffer list. That counts the six as done with no behaviour change.
  - This is B-S only in size. No new semantics are written; the existing debt entries stay.
- **GetStatistics is D, not B-S:** the same change would count it as done, but its job zero-fills a 24-byte sideband (`:1610`) with no source, debt entry or test. Record that before the move, or keep the statistics job throwing.

## The function from the title report: sceAjmBatchJobControl (issue #3410, PPSA26344)

- **PPSA26344's call already runs.** Ghost of Yotei calls it with flags `0x200000006000` (RESET, INITIALIZE and SIDEBAND_GAPLESS_DECODE), 8 gapless bytes plus 8 M4AAC initialize bytes, and an 8-byte result.
  - This call was implemented in 7cc6baf118 ("implement the M4AAC decoder") and merged in b52356acca (#5307, branch `codex/ajm-m4aac-3410`). Debt `:27` records it.
  - `core/libs/tests/GuestAjm.cpp:1249` runs exactly this shape for M4AAC. PPSA25936's flags-0x4000 call (debt `:15`) is covered too.
- **Why it is still todo:** its own four throws (`:1816-1819`), plus the execution throws it feeds (`:1577`, `:1586`). The neighbours that settle its done part:
  - `CONTROL_RESET` → `ClearContext` (`:1551`), as `sceAjmBatchJobClearContext` (`:1802`); debt `:16`.
  - `CONTROL_INITIALIZE` → `InitializeInstance` (`:460`), as `sceAjmBatchJobInitialize` (`:1795`), with fixed sizes from `ControlInitializeSize` (`:1571`); debt `:17`.
  - `SIDEBAND_GAPLESS_DECODE` → the same stores as `sceAjmBatchJobSetGaplessDecode` (`:1806`, `:1632`); debt `:18`.
  - Run-job sidebands (format, stream, gapless) are written by `WriteRunSideband` (`:710`) and `CurrentFormat` (`:694`).
- **What it would still need, each a console measurement or a title call site:**
  - the meaning of control bits beyond 13 and 14 (the tests assert that bit 15 throws, `GuestAjm.cpp:1073`), and of FORMAT/STREAM sidebands on a control job. That is the output-larger-than-8-bytes branch (`:1818`). Writing them through `WriteRunSideband` would be B-S only once a source gives their order;
  - GAPLESS without RESET: RPCSX overwrites both counts and shadPS4 ignores a zero count (debt `:18`);
  - the error for a null input with a nonzero size (`:1819`), and for flags 0;
  - INITIALIZE through control for codec 23, which is explicitly unknown (debt `:13`), and for other codecs;
  - an input other than the gapless block plus the codec's parameters, for example Opus with 8 bytes, which `sceAjmBatchJobInitialize` accepts (`:464`) but control refuses (`GuestAjm.cpp:1133`).
- **Class D.** No public source closes the row: the only sources are PS4 reimplementations that disagree. A report that still stops here on this tree must give the flags and both sideband sizes.

## D families

- **Batch execution (`sceAjmBatchStart`)** and the two partial builders. The remaining branches are unmeasured console behaviour:
  - `RunSplit`: null buffer lists;
  - `InstanceCreate`: unknown M4AAC flag bits (`:1568`);
  - `BatchStart`: codec-info order, codec 23, Opus rates and families, ATRAC9 vibration encodings, control sizes.
- **Unknown or undescribed prototypes:** `BatchGetLog` and `InstanceAvailable` (parameterless, debt `:5`), `InstanceExtend` and `InstanceSwitch` (SharpProspero only, debt `:6`).
- **No source for the content:**
  - `BatchJobEncode`: no encoder codec, parameters or sideband;
  - `StrError`: the console's strings;
  - `GetStatistics`: the statistics layout;
  - `DecWVorbisCreateSetupPacket`: the setup rebuild and its checks. Only the 56-byte parameter is known (`include/Api.hpp:20`).

## Batch proposal

**`refactor(libSceAjm.native): reject null buffer lists only in sceAjmBatchJobRunSplit` (the 6 B-S rows).**
- Semantics: unchanged. Move `Ajm.cpp:237` into `sceAjmBatchJobRunSplit` (`:1827`), before it calls `Append`.
- Decide GetStatistics first: add a debt entry for its zero fill, or keep it throwing. The move would count it as done too.
- Tests: extend `guest_ajm_tests` (`core/libs/tests/GuestAjm.cpp`, `CMakeLists.txt:55`, `add_test` `:59`):
  - the null-list RunSplit still throws and leaves the offset at 0 (`GuestAjm.cpp:1974`);
  - each of the six builders appends with null parameters where allowed and runs as before. The existing cases are at `:157` (Initialize), `:741` (SetGaplessDecode), `:1491` (GetResampleInfo), `:1514`, `:1592`, `:1651`-`:1676` (resample and ClearContext).
- Effect: +6 done, or +7 if GetStatistics moves too.

## Caveats

- **Input line numbers:** for `src/Ajm.cpp` they are 3 lower than the file's. For example, Control is given as `:1811` but is at `:1814`. The generator joins the three backslash-continued lines of `AJM_TRACE` (`:67-70`) before counting. The rows cite the file's own lines.
- **No debt entry:** `sceAjmStrError`, `sceAjmBatchJobEncode` and the GetStatistics zero fill have none.
- **Existing assumptions stay:** the B-S rows keep the assumptions the debt already records, the FMOD-inferred ratio (`:19`) and the assumed ramp rule (`:14`). The refactor neither confirms nor changes them.
