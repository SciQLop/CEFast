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
