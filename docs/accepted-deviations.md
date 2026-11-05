# Accepted deviations

CEFast reads what CSA and producer files actually contain, not only what DS-QMW-TN-0010 (CEF 2.0)
allows. Each row is a deliberate, tested departure; anything not listed must be rejected with a
`parse_error` naming the file, line and record.

| # | Departure from the spec or from CEFLIB | Why | Test |
|---|---|---|---|
| 1 | `INCLUDE` name may be unquoted (`INCLUDE = CL_CH_MISSION.ceh`); the spec writes it quoted | CSA and STAFF `.ceh` files | `header: INCLUDE from the file directory`, `header: INCLUDE from the resolver` |
| 2 | `INCLUDE` also resolves in the including file's directory, before `include_paths` and `$CEFPATH` | CEFLIB only tries the name as given, then `$CEFPATH`; a raw header and its `.ceh` usually sit together | `header: INCLUDE from the file directory`, `header: nested INCLUDE` |
| 3 | `END_OF_RECORD_MARKER` may be unquoted (`END_OF_RECORD_MARKER = $`) | CSA files | `lexer: unquoted marker`, `header: record marker forms` |
| 4 | `ENTRY` values may be unquoted (`ENTRY = Calibrated`, `ENTRY = 090213`) and keep their exact text | CSA files; CEFLIB converts only inside a typed block | `header: untyped entries keep their text`, `header: metadata block entries from a real header` |
| 5 | `VALUE_TYPE = ISO_TIME_RANGE` in a global metadata block; the spec's allowed list omits it | every CSA file declares `MISSION_TIME_SPAN`/`FILE_TIME_SPAN` that way | `header: ISO_TIME_RANGE metadata value type` |
| 6 | `SIZES` may be one quoted list with spaces (`SIZES = "64, 6, 32"`) | CSA PEA/CODIF headers | `header: quoted SIZES with spaces`, `lexer: SIZES quoted with spaces` |
| 7 | `\` line continuation is honoured in every header block; CEFLIB only does it in variable blocks | two CSA files continue a global attribute | `lexer: continuation` |
| 8 | One trailing comma right before a record marker is accepted | CSA `$`/`;` records end with `…,3, $` | `records: trailing comma before the marker` |
| 9 | `CHAR` data tokens may be unquoted | spec requires quotes for text entries; producers do not always quote | `records: unquoted CHAR text is kept, quotes are optional`, `values: CHAR keeps quoted spaces` |
| 10 | `ss` may be `60`; it is added as a plain second (`:60` == next minute) | leap seconds are ignored so that values line up with `datetime64[ns]` | `ISO 8601 time parsing` (test_time) |
| 11 | More than nine fractional-second digits are truncated, not rounded | int64 nanoseconds cannot hold them; CEFLIB keeps ~4 digits | `ISO 8601 time parsing` (test_time) |
| 12 | `FILE_FORMAT_VERSION` must be present and start with `CEF-2`; CEF-1 files are refused | CSA only ever served CEF-2.0, and CEF-1 is not backward compatible (spec Appendix A) | `header: version missing raises`, `header: CEF-1 version raises` |
| 13 | `DATA_UNTIL` is matched as the trimmed beginning of a line, as the spec words it, and the line may carry trailing text | spec §2.3, CEFLIB prefix-matches | `records: data stops at DATA_UNTIL` |
| 14 | `!RECORDS= N` is read only as a capacity hint and verified, never trusted | CSA comment, not part of the format | `records: the records hint is only a hint`, `corpus: every CSA file loads, with the record count of its !RECORDS hint` |

Rejected on purpose, with an error: a field count that differs from the declared variables, an
unparseable number or time, an empty field, an unterminated quote, whitespace or a second token
inside one value, a missing `INCLUDE`, an unclosed or mismatched block, and a non-CEF-2 version.

## CEFLIB differences

CEFLIB 1.8.6 is both the reference reader and a 2000s C library: these are the places where it is
not usable as an oracle as-is. `tests/python/test_vs_ceflib.py` normalises every row below and
compares all 41 corpus files; each file runs in its own subprocess because CEFLIB is a global
singleton with no unload.

| # | CEFLIB behaviour | Evidence | How cefast differs |
|---|---|---|---|
| 1 | Variable names and every variable-attribute value lose their `__<DATASET_ID>` suffix | `C/src/CEF.c:431` (`Prefixe` on the name), `:1345` (on each attribute value) | cefast keeps the name as written; the test compares short names |
| 2 | Synthetic `K001…` variables are created for `DELTA_PLUS`/`DELTA_MINUS` constants | `Allocate_var` → `Create_constant` | cefast keeps them as attributes; the test ignores `K…` variables |
| 3 | `BYTE` variables cannot be read (`cef_var` has no `CEF_BYTE` case) | `PYTHON/src/ceflib.c:521-543` | cefast returns `uint8`; the test skips BYTE (no CSA file uses it) |
| 4 | Times are `float64` milliseconds since 1958-01-01, so sub-millisecond digits are lost | `LIB/src/TIMES.c` (`MILLI_TO_1958`) | cefast returns `datetime64[ns]`; the test allows 1 ms |
| 5 | The number parser accumulates digit weights and is not correctly rounded | `LIB/src/CEF.c` `str_to_double` | cefast uses fast_float; the test allows rtol 1e-6 (FLOAT) / 1e-12 (DOUBLE) |
| 6 | `gattrs()` exposes only `FILE_NAME`, `FILE_FORMAT_VERSION`, `END_OF_RECORD_MARKER`, `DATA_UNTIL` | `PYTHON/src/ceflib.c` | cefast exposes the metadata blocks; the test compares only the two common keys |
| 7 | A `\` continuation inside a metadata block is a fatal error (`Line 301 Unauthorized token : '\ '`), because continuation is only implemented in `Parse_variable` | `C/src/CEF.c:1331` | cefast continues any header line (deviation row 7); `CC_CP_AUX_MAARBLE_KIR_ULF_PC35` is unreadable by CEFLIB and is the one corpus file the test skips |
| 8 | A record ends only when the marker is at end of line | `Read_CEF_record`, `:906-918` | cefast follows the spec: the marker ends a record anywhere outside quotes |
| 9 | One file per process, no unload | global `root` descriptor | cefast is an immutable value returned by `load()` |
