# Does CSA still serve CEF-1?

Probe date 2026-11-05. Corpus kept in `~/.cache/cefast-csa-probe/corpus/<DATASET>/CSA_Download_*/<DATASET>/*.cef`.

## Verdict

**No.** Every CEF file the CSA serves today is `FILE_FORMAT_VERSION = "CEF-2.0"`, and CSA never served
CEF-1: the archive was built on CEF-2 from its inception.

Evidence:
- **41 files / 39 datasets** downloaded via `csa-sl-tap/data?...&DELIVERY_FORMAT=CEF`, spanning 2000-2026,
  all four Cluster spacecraft, Double Star 1/2, Cluster-Context/ground-based, and every non-standard
  data type (CAA_Event_Data, JSOC, Quality/Caveats, Auxiliary, CSDS Prime/Summary, blank data_type,
  unreleased datasets, oldest files ingested in 2005): **41/41 = CEF-2.0**, zero exceptions.
- Every file starts with `!---- CSA MERGED CEF FILE (<timestamp>) ----` and carries
  `GENERATION_DATE` = the download time: the service **regenerates the header and inlines `INCLUDE`s at
  download time**. Even a hypothetical legacy-stored file would be normalised. There is no file-level
  retrieval (`RETRIEVAL_TYPE` has no `FILE` constant), so stored bytes are not exposed.
- Docs: CEF-1 is Issues 1.2-1.5 of DS-QMW-TN-0010 (2001-05-25 to 2004-04-13, "Modified for Cluster
  Active Archive"); CEF-2 is Issue 2 (2009-10-19). Appendix A: "CEF-2.x is NOT backward compatible with
  CEF-1.0"; `FILE FORMAT VERSION` "required from CEF-2.0 onwards". The ESA CAA 5th-anniversary talk
  (Sept 2005) already shows CAA deliveries as `"CEF-2.0"` and "CAA - QSAS support for CEF-2"; the CAA
  translated CDF to CEF-2. CEF-1 was the CSDS/pre-archive exchange syntax, not an archive format.
- TAP: 1965 datasets have `is_cef = true`, all with `file_extension = 'cef'`; the 382 non-CEF datasets
  are png/gif/ps/cdf/txt/pdf/zip only. No dataset is flagged legacy and none delivers a pre-2.0 dialect.

## Common to every file (omitted from the table)

- `DATA_UNTIL = END_DATA` (unquoted), last line literally `END_DATA` - not `EOF`/`"End_of_file"`.
- `!RECORDS= N` comment line after the data, before the terminator. Verified exact on 3 files
  (13222/13222, 286/286, 383/383): a free record count for pre-allocation, no counting pass needed.
- `ISO_TIME_RANGE` used in global metadata (FILE_TIME_SPAN, DATASET_VERSION, ...), although the spec's
  VALUE_TYPE lists (2.3, 2.7) omit it. Merged-file header fields (LOGICAL_FILE_ID, "Merged file...").
- `START_CEFMERGE_INCLUDE` / `END_CEFMERGE_INCLUDE` markers inside `!` comments where `INCLUDE` was.

## Per-dataset (F = fractional-second digits; U = unquoted marker; N = no marker, newline-delimited;
S1 = `SIZES = 1` scalar marker; C = CHAR vars; SP = spaces after commas in SIZES; E = empty data
section; B = backslash continuation; M = multi-dim SIZES)

| dataset | version | deviations | kept sample |
|---|---|---|---|
| C1_CP_CIS-CODIF_HS_H1_CS | CEF-2.0 | M 8,16,31; F3; ";" marker | corpus/C1_CP_CIS-CODIF_HS_H1_CS/ |
| C1_CP_CIS-CODIF_HS_He2_PEF | CEF-2.0 | M 8,16,31; F3; ";" | corpus/C1_CP_CIS-CODIF_HS_He2_PEF/ |
| C1_CP_CIS-CODIF_LS_RATES | CEF-2.0 | M 8,16,16...; SP; E | corpus/C1_CP_CIS-CODIF_LS_RATES/ |
| C1_CP_CIS-CODIF_RPA_H1_PF | CEF-2.0 | M 8,16,16; E | corpus/C1_CP_CIS-CODIF_RPA_H1_PF/ |
| C1_CP_EDI_MP | CEF-2.0 | DOUBLE; F3; S1 | corpus/C1_CP_EDI_MP/ |
| C1_CP_EFW_L3_E | CEF-2.0 | F6 (microseconds); S1 | corpus/C1_CP_EFW_L3_E/ |
| C1_CP_FGM_SPIN | CEF-2.0 | F3; S1 | corpus/C1_CP_FGM_SPIN/ |
| C1_CP_PEA_3DXPH_DEFlux | CEF-2.0 | M "64, 6, 32" (SP); E | corpus/C1_CP_PEA_3DXPH_DEFlux/ |
| C1_CP_WHI_ELECTRON_DENSITY | CEF-2.0 | U (`= $`); C; S1 | corpus/C1_CP_WHI_ELECTRON_DENSITY/ |
| C1_CT_AUX_ESOC_GSU_LOG | CEF-2.0 | F2; S1; C (event data) | corpus/C1_CT_AUX_ESOC_GSU_LOG/ |
| C1_JP_AUX_PMP | CEF-2.0 | F3; S1; 8641 rec | corpus/C1_JP_AUX_PMP/ |
| C1_PP_DWP | CEF-2.0 | F3; S1 | corpus/C1_PP_DWP/ |
| C1_PP_EDI | CEF-2.0 | F3; S1 | corpus/C1_PP_EDI/ |
| C1_PP_FGM | CEF-2.0 | F3; S1; 2005-ingested file | corpus/C1_PP_FGM/ |
| C1_PP_STA | CEF-2.0 | F3; S1 (spectra) | corpus/C1_PP_STA/ |
| C2_CP_PEA_PITCH_FIX_DEFlux | CEF-2.0 | M 12,100; F3; S1 | corpus/C2_CP_PEA_PITCH_FIX_DEFlux/ |
| C2_CQ_EFW_INST | CEF-2.0 | C; no S1 | corpus/C2_CQ_EFW_INST/ |
| C2_PP_ASP | CEF-2.0 | F3; S1 | corpus/C2_PP_ASP/ |
| C2_PP_FGM | CEF-2.0 | F3; S1 | corpus/C2_PP_FGM/ |
| C3_CP_EDI_EGD | CEF-2.0 | N; F6; 13222 rec | corpus/C3_CP_EDI_EGD/ |
| C3_CP_PEA_LERL_DEFlux | CEF-2.0 | M "8, 3, 16" (SP); F3; S1 | corpus/C3_CP_PEA_LERL_DEFlux/ |
| C3_CP_RAP_PED_BM | CEF-2.0 | M 4,16,9; B; one 1-record + one empty file | corpus/C3_CP_RAP_PED_BM/ |
| C3_CT_AUX_GRMB | CEF-2.0 | N; C ("OUT/SWF"); no frac | corpus/C3_CT_AUX_GRMB/ |
| C3_CT_WBD_COVERAGE2 | CEF-2.0 | ";" marker; F3; C; S1 | corpus/C3_CT_WBD_COVERAGE2/ |
| C3_PP_CIS | CEF-2.0 | F3; S1 | corpus/C3_PP_CIS/ |
| C4_CP_ASP_IONC | CEF-2.0 | F3 | corpus/C4_CP_ASP_IONC/ |
| C4_CP_AUX_ESOC_MEP | CEF-2.0 | F2; S1 | corpus/C4_CP_AUX_ESOC_MEP/ |
| C4_CP_WHI_ELECTRON_DENSITY | CEF-2.0 | U; F3; C; S1 | corpus/C4_CP_WHI_ELECTRON_DENSITY/ |
| C4_CQ_CIS-CODIF_CAVEATS | CEF-2.0 | ";" marker; C | corpus/C4_CQ_CIS-CODIF_CAVEATS/ |
| CC_CP_AUX_MAARBLE_KIR_ULF_PC35 | CEF-2.0 | M 21,3; B; C; S1 (ground-based) | corpus/CC_CP_AUX_MAARBLE_KIR_ULF_PC35/ |
| CL_SP_ASP | CEF-2.0 | F3; S1 (summary param) | corpus/CL_SP_ASP/ |
| CL_SP_AUX | CEF-2.0 | F3; S1 (auxiliary) | corpus/CL_SP_AUX/ |
| CM_CP_FGM_UT_PIOR | CEF-2.0 | N; E; C; S1 | corpus/CM_CP_FGM_UT_PIOR/ |
| D1_CP_PEA_3DXHAR_DEFlux | CEF-2.0 | M "64, 12, 16"; E; C; S1 | corpus/D1_CP_PEA_3DXHAR_DEFlux/ |
| D1_JP_AUX_PMP | CEF-2.0 | F3; S1 | corpus/D1_JP_AUX_PMP/ |
| D1_PP_HIA | CEF-2.0 | F3; S1 | corpus/D1_PP_HIA/ |
| D1_SP_AUX | CEF-2.0 | F3; S1 | corpus/D1_SP_AUX/ |
| D1_SP_FGM | CEF-2.0 | F3; S1; DOUBLE | corpus/D1_SP_FGM/ |
| D2_CP_FGM_SPIN | CEF-2.0 | F3; C; S1 | corpus/D2_CP_FGM_SPIN/ |

Not sampled: `C2_CP_WBD_WAVEFORM` (waveform windows return no products at 20 s - 1 day resolution) and
`C2_CP_EDI_MP_ISR2` (no products in window). Both are ordinary `is_cef` datasets like their C1/C3 twins.

## Residual CEF-1 exposure (outside CSA)

- CEF-1 files live only outside the archive: old CSDS exchanges, QSAS `-cef` dumps, the CAA IDL
  reader's test files, paper supplements. They usually have **no `FILE_FORMAT_VERSION` at all**
  (required only from 2.0), so "missing version" must be an explicit, named error - not a fallback.
- `INCLUDE`-based `.ceh` files are separately distributed by the old CAA file service; CSA deliveries
  inline them. Only a reader that accepts raw archive files needs the `CEFPATH` story.
- Types: CSA has **zero BYTE parameters**, and DOUBLE only in 14 parameters (EDI MP, D1/D2 SP/PP).
  FLOAT (22912), INT (9243), ISO_TIME (2303), CHAR (699), ISO_TIME_RANGE (234).
