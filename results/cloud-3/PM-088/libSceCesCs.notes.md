# libSceCesCs classification notes

- **What it is:** character encoding conversion between Unicode forms and legacy single- and multi-byte charsets.
  - **Done (38):** UTF-8/16/32 scalar conversions in Unicode.cpp; CP1250-1252 single-byte profiles; CP932 Shift_JIS; CP51932 EUC-JP (with JIS X 0212); CP949 EUC-KR/UHC; CP950 Big5; MBCS context string conversions to and from UTF-8 and UCS-2. Existing tables: JisX0208.hpp (CP932 flavour), JisX0212.hpp, Cp949.hpp, Cp950.hpp.
  - **Todo (249):** declared in PendingExports.cpp.
- **Counts:** 0 A, 225 B-S, 8 B-M, 2 B-L, 0 C, 14 D.
- **Families:**
  - **Scalar conversions (137, B-S):** to and from {Big5, EucKr, Uhc, EucJp, SJis, Sbc, Mbc, EucCn, Gb} and {UCS-2, UTF-16/BE/LE, UTF-32/BE/LE, UTF-8}. They reuse `decodeMbcs`/`encodeMbcs`/`decodeCp949`/`decodeCp950`/`sbcToUnicode` and the Unicode.cpp unit helpers, which first move out of their anonymous namespaces into a shared header. Prototypes come from the Orbital db_types file, as for the done exports.
  - **Context string forms (9, B-S):** UTF-16/32 string forms plus `sceCesMbcsUcsContextInitCopy`, over the `toMbcs`/`fromMbcs` templates.
  - **Single-byte profiles (52 `RefersUcsProfile*`, B-S), each a public table:** ISO/IEC 8859-1..16 (unicode.org ISO8859); Windows CP1253-1258 and CP874 (MICSFT/WINDOWS); DOS CP437-869 (MICSFT/PC; CP858 = CP850 plus the euro); KOI8-R (RFC 1489) and KOI8-U (RFC 2319); the ISO 646 variants BS 4730, GB/T 1988, KS X 1003 and JIS X 0201 Roman with their `Tilde0x7e` forms; US-ASCII and JIS X 0201 katakana. The if-chained `sbcToUnicode` should become table-indexed first.
  - **New DBCS tables (B-M):** GB2312 (unicode.org GB2312.TXT), GBK (CP936.TXT), GB18030-2000/2005 (four-byte BMP range table plus linear supplementary planes, and the 2005 0xA8BC/U+1E3F swap), and the standard JIS X 0208 mapping for the X0208 EUC-JP and Shift_JIS profiles (a delta from JIS0208.TXT against the CP932-flavoured table). EUC-JIS-2004 and Shift_JIS-2004 (B-L) need the JIS X 0213:2004 table (x0213.org; some cells map to two code points). All of these are derivable from public mapping files.
  - **ISO-2022-JP (22):** RFC 1468 and Microsoft CP50221/50222. The stateful decoder and encoder are B-M, the wrappers and profile tags B-S, and `Iso2022JpUcsProfileSetSbcs` D (with the other SetSbcs setters). The shift state lives in the caller's context, whose console size is unknown (as already recorded for `sceCesMbcsUcsContextInit`).
  - **JIS/SJIS code arithmetic (B-S):** JIS X 0208/0213 Annex 1 conversions and SJIS get/put.
- **D (14):** the seven `*UcsProfileSetSbcs` setters, Gb `Set4ByteCharRange`/`SetUdaMapping`, `MbcsUcsSetMbcsReplacementChar(U)Code`, `JisGetLevel`, and `JiscsToUcs`/`UcsToJiscs`. Their argument values, accepted profiles or result encodings are not in the tree or in a public source.
- **No new dependency:** tables are generated offline from the public unicode.org/RFC/x0213.org files into headers like Cp949.hpp. ICU or iconv would only be a table source, not a runtime dependency.

## Batch proposals

1. **ISO 8859 profiles + Sbc scalar conversions (29):** `RefersUcsProfileIso8859_*` (15), `SbcTo{Ucs2,Utf16*,Utf32*}` (7), `{Ucs2,Utf16*,Utf32*}ToSbc` (7). Tests: extend GuestCes.cpp (`guest_ces` and `guest_ces_module`) with a per-table hash of all 256 decodings (`RequireTable` style from GuestCesDbcs.cpp), round trips, `UNASSIGNED_CODE` for the unassigned 8859-3/6/7/8/11 cells, BE/LE byte order and `DST_BUFFER_END`.
2. **Windows/DOS/KOI8 code-page profiles (25):** Cp1253-1258, Cp437/737/775/850/852/855/857/858/860-866/869/874, Koi8R, Koi8U. Tests: the same GuestCes table hashes computed from the unicode.org/RFC files, plus euro and box-drawing spot checks.
3. **Big5 and UHC scalar conversions (30):** `Big5To`/`UhcTo` UCS-2/UTF-16(BE/LE)/UTF-32(BE/LE), and `Ucs2`/`Utf16*`/`Utf32*`/`Utf8` `ToBig5`/`ToUhc`. Tests: GuestCesDbcs.cpp (`guest_ces_dbcs` and its module variant) round-trips every CP950/CP949 cell through each form, plus surrogate input yielding `UNASSIGNED_CODE` and the Big5 duplicate-cell choices.
