# libSceVorbisDec (3 todo: D 3)

**What it is:** libvorbisfile with a `sceVorbisDec` prefix (open, read, seek, tell, info, comment, bitrate), plus three Sony additions: a decode priority getter and setter, and a last-decode-error query.

**Existing state** (`core/libs/prx/libSceVorbisDec/Export.cpp`):
- 31 done. Each wraps the matching `ov_*` call of bundled libvorbis 1.3.7 / libogg 1.3.6 (`docs/dev/debt/libSceVorbisDec.md:5`).
- Host-side state: a `Handle` per guest `OggVorbis_File` address (`:29`), kept in a leaked `Table` (`:42-50`).
- Test: `guest_vorbis_dec_tests` (`core/libs/tests/GuestVorbisDec.cpp`), `core/libs/CMakeLists.txt:934`, `add_test` `:939`.

**Counts:** A 0, B-S 0, B-M 0, B-L 0, C 0, D 3.

## D families

All three are `(void)` declarations with unknown signatures, named from the NID database (debt `:6-8`, `Export.cpp:203`, `:208`, `:293`).
- They are not libvorbisfile functions, so the done exports' public source does not cover them.
- **Priority (2):** values and effect unknown. Decoding here is synchronous on the caller's thread.
- **GetLastDecodeError (1):** whether it is per handle or global, and the error encoding, are unknown.

## Batch proposal

None.

## Caveats

None.
