# Format codecs (oct, octx, bent, btd, chd, dct, datamap, arc30, luachunk) (`native/di1compat/oct.hpp`, `octx.hpp`, `bent.hpp`, `btd.hpp`, `chd.hpp`, `dct.hpp`, `datamap.hpp`, `arc30.hpp`, `luachunk.hpp`)

Scope: all 9 headers read end to end (2,403 lines). Callers and tests opened at the cited lines: `di1compat.cpp`
(read30x/lang30 519-549, transcodeOct 734-758, transcode 853-1112, avHandler 1268-1278, chdForLua 3197-3210, hkOpen
serve gate 3645 and try/catch 3703-3720, fnv64 3779-3830, init 4152-4183), `octtest.cpp` (countsCheck 405-478,
--cho-const 973-1212, --variants 1219-1290, --ui dct part 205-235, --chd 2720-2734, --dct 2792-2806, --oct 2944-2976,
--datamap 3128-3141, --arc 3365-3390, round trip / --counts 3391-3426), `gameplay.hpp` 26-72 and 570-590, `cho.hpp`
118-134 / 296-306 / 343-352 / 903-912, `mer.hpp` 36-52 / 105-112, `npc.hpp` 146-152, `sys.hpp` 700-731.
Compile check (scratchpad `vfy_codecs`, `/Zs` only, nothing written to the repo): every header compiles standalone
with MSVC 14.44 `/std:c++17 /EHsc /W4 /permissive- /w14365 /w14242 /w14254 /w14267 /w14389 /w14018`; C4456 at
dct.hpp:59 reproduced; C4365 per header: octx 105, oct 7, bent 10, dct 6 (+1 inside `<utility>`), arc30 3, chd 1;
`static_assert(sizeof(oct::Rec) == 32)` holds on x86; `<windows.h>` + btd.hpp + `std::max(a, b)` fails with C2589
(confirms CO-29). No type-punned or unaligned loads exist in the slice (every multi-byte read is `memcpy` or
byte-assembled), so there is no strict-aliasing finding to make.
Verification: 36 findings checked: 12 confirmed, 24 corrected, 0 unverified, 0 rejected; 0 new (missed points were
folded into CO-01, CO-06, CO-09, CO-11, CO-13, CO-22, CO-26, CO-34 and the coverage table below).

**Hot-path convention (applies to every perf finding below).** All of these codecs run only inside hkOpen's 1.0 serve
path (`!r && !blocked && g_cfg.serve10 && g_cfg.install10[0]`, di1compat.cpp:3645) or chdForLua (3626, CHD-mode opens
of `.lua` tables, in practice the 1.0 DataMap's). Code defaults (`serve10 = false`, di1compat.cpp:133): not reached.
Recommended config (`serve10 = 1`): load time, once per served 1.0 file open, on the engine's file-open thread(s),
never per frame. No perf item in this slice has been measured; the estimates below put each at sub-ms to tens of ms per
world load, so none reaches High under the "measurable cost per open" rule until someone measures it (CO-13's lang30
first call is the likeliest exception).

## Regression coverage today (verified)

| Code | What octtest actually checks | Gap (verified) |
|---|---|---|
| `oct::File::parse/write` | Round trip of every `.oct .mer .merdebug .bent .banm .bct` under the given roots, byte-exact and `tail` empty (octtest.cpp:2532, 3412-3413) | CONFIRMED. The write path never runs with a non-empty `tail`. |
| `octx::addCounts` | `--counts` (dispatch 3391, `countsCheck` 437-478): per shipped 3.0 SubNetwork, strip its count records, call `addCounts(g, i)` on a whole-file copy, byte-compare only "fits" SubNetworks (no ProcessorNodeCount, no ConstantDataNodeCount); the rest per field | CONFIRMED: nothing in octtest writes `g_slack` (only writer: di1compat.cpp:4156, `= 4096`), so the shipped configuration's arithmetic is never byte-checked. ADDED: (a) `--counts` calls `addCounts` directly, so `addAllCounts`'s `editPoolChildren` splice and `clip30` never run there; (b) `ConstantHeaderCount` is stripped as a count record (`isCountRecord`, 405-412), so `chc == -1` and the `constSection` branch (octx.hpp:194-204, 211, 237) is never byte-checked (`--cho-const` checks only its count rules); (c) shipped 3.0 links are already in 3.0 form, so `convertLink`'s conversion branch (octx.hpp:75-89) is never byte-checked. |
| `chd::lua` + `compile` | `--chd` (2720-2734): each shipped 3.0 `.chd` against `compile(lua(<.lua beside it>))`, byte-exact | CONFIRMED |
| `dct` | `--dct` (2792-2806): `load`/`dump` round trip of the one table named, byte-exact, then `reseed` and one key probe | CORRECTED: `reseed` and `supplement` *are* checked, functionally, by `--ui` (205-235): lang/IN reseed must keep 2,534 keys, supplement must add > 0 keys, shadow no 3.0 key, give `kept + add` keys, and answer all 28 `Load_Tips_Incredibles_NN` probes. Neither has a byte oracle. |
| `arc30::readMember` | `--arc` (3365-3390): every render-table donor / matindex member read from the install must equal the extract | ADDED: only render-table members are read, so the fully encrypted `.dct` branch (arc30.hpp:137-139) is never compared. |
| `clip30`, `addAssetPool`, `skin30`, `locator30`, `camera30`, `collision30`, `realm30w`, `SnEdit`, `bent::transcode`, `btd::convert/btm`, `luachunk`, `datamap` | No byte assertion. `--oct` (2944), `--btd` (2807) and `--bent` (2817) write files for external Python comparison; `--variants` / `--cho-const` assert rules and re-parse identity; `--datamap` (3128), `--cho-alias` (2686), `--dbtriggers` (2699) run `luachunk::rewrite` + `datamap::normalise` and print counts; `--zones10` / `--currency10` run `luachunk::eval` | Golden-master advice stands, with a caveat: the plugin pipeline cannot run offline as it is. `transcodeAssetRender` needs `g_render` / `read30x`, `surfaces30` reads the engine's surface table (`surfaceTable30`, di1compat.cpp:718), and `transcodeCho`'s motion predicate checks the 3.0 install (`has30`). A golden harness must stub these and record how. `octtest --oct`'s `lap()` timings (2947-2951) give the perf before/after. |

## Findings

| ID | Title | Sev | Class | Where |
|---|---|---|---|---|
| CO-01 | Hidden global parameters change the output; the shipped configuration is never byte-tested | High | NEEDS-VERIFICATION | octx.hpp:114, 213-236, 387, 526; di1compat.cpp:4156-4157 |
| CO-02 | Name-table order is part of the byte output | High | DOC-ONLY | oct.hpp:90, 133-148; octx.hpp:573-577, 651 |
| CO-05 | Unchecked `[0]` on decoded values crashes the game instead of falling back | Med | BEHAVIOUR-CHANGE | octx.hpp (33 lines), bent.hpp (6 lines) |
| CO-07 | `btd::convertBlock`: a 32-bit wrap in the validation leads to a multi-GB memcpy | Med | BEHAVIOUR-CHANGE | btd.hpp:45-58 |
| CO-15 | `oct::File::parse`: 32-bit size arithmetic can wrap into an OOB read | Med | BEHAVIOUR-CHANGE | oct.hpp:43, 64-69 |
| CO-16 | `worldChain` indexes `dns` with file-provided refs | Med | BEHAVIOUR-CHANGE | octx.hpp:728, 737 |
| CO-26 | luachunk: OOB register access on malformed bytecode; quadratic tables | Med | BEHAVIOUR-CHANGE | luachunk.hpp:28, 45, 68, 127-159, 185-207 |
| CO-03 | `realm30w` is a god function | Med | NEEDS-VERIFICATION | octx.hpp:914-1162 |
| CO-04 | `addCounts` is a god function, and its contract does not match the code | Med | NEEDS-VERIFICATION | octx.hpp:92-93, 116-249 |
| CO-06 | The .oct transcode pipeline exists 4 times, and the copies have drifted | Med | NEEDS-VERIFICATION | di1compat.cpp:734-758; octtest.cpp:1057-1098, 1271-1277, 2944-2976 |
| CO-09 | Four parsers of an Association entry; two pool scans per redirect pass | Med | NEEDS-VERIFICATION | octx.hpp:309-324, 621-673, 818-832, 1144-1152 |
| CO-10 | Record builders duplicated and split across two layers | Med | SAFE-REFACTOR | octx.hpp:34-41, 336-337, 373-378; mer.hpp:105-112; cho.hpp:347-352, 907-912 |
| CO-11 | `editPoolChildren` swaps the name table: hidden coupling, moved-from state | Med | NEEDS-VERIFICATION | octx.hpp:578-618 |
| CO-12 | `SnEdit`: view, builder and applier in one struct with public mutable state | Med | NEEDS-VERIFICATION | octx.hpp:392-571 |
| CO-13 | `arc30::readMember`: index re-read per call, one BCrypt call per 16 B, lock held across I/O | Med | NEEDS-VERIFICATION | arc30.hpp:50-62, 93-148; di1compat.cpp:531-549 |
| CO-08 | Per-call heap allocations in the accessors and builders | Low | SAFE-REFACTOR | oct.hpp:154-177, 195, 226 |
| CO-14 | Repeated linear scans: `pool()`, `blockEnd` in loop conditions | Low | NEEDS-VERIFICATION | oct.hpp:109-122; octx.hpp:281, 313, 810, 1091, 1094, 1146; bent.hpp:55-103 |
| CO-17 | `realm30w` CPU waste | Low | NEEDS-VERIFICATION | octx.hpp:921-928, 970-1099 |
| CO-18 | Low-level helpers duplicated across files | Low | SAFE-REFACTOR | btd.hpp:15-17; dct.hpp:19-40; mer.hpp:36-52; chd.hpp:164 |
| CO-19 | The "FNV-1a" basis literal is truncated and must not be "fixed" | Low | DOC-ONLY | octx.hpp:1079-1088; di1compat.cpp:3781, 3830; octtest.cpp:319, 3250 |
| CO-20 | Misplaced and stale comments | Low | DOC-ONLY | octx.hpp:92-93, 639-640, 702-721, 872 |
| CO-21 | `oct::File` encapsulation and API smells | Low | SAFE-REFACTOR | oct.hpp:51-239 |
| CO-22 | Magic numbers and positional assumptions | Low | SAFE-REFACTOR | octx.hpp, btd.hpp, chd.hpp, dct.hpp, luachunk.hpp |
| CO-23 | Step return values double as a "changed" flag and under-report | Low | BEHAVIOUR-CHANGE | di1compat.cpp:741-756; octx.hpp:803-830, 1176 |
| CO-24 | arc30 robustness (unchecked OS/BCrypt results) | Low | BEHAVIOUR-CHANGE | arc30.hpp:44-60, 95-100, 142-145 |
| CO-25 | chd: deep copies and per-table linear key dedup | Low | SAFE-REFACTOR | chd.hpp:99-129, 148, 180-183 |
| CO-27 | dct: duplicated re-key loop and repeated parsing | Low | NEEDS-VERIFICATION | dct.hpp:59, 78-86, 109-163; di1compat.cpp:963-964 |
| CO-28 | `bent::transcode` positional assumptions | Low | BEHAVIOUR-CHANGE | bent.hpp:63, 93-116 |
| CO-29 | Missing direct includes; MSVC-only constructs; live `min`/`max` macros | Low | SAFE-REFACTOR | all 9 headers; btd.hpp:49; di1compat.cpp:8 |
| CO-30 | Endianness handling is inconsistent | Low | SAFE-REFACTOR | oct.hpp:32, 93-101; btd.hpp:15-17; dct.hpp:40, 81-87; ... |
| CO-31 | Width and count edge cases (UB unreachable today) | Low | DOC-ONLY | oct.hpp:208, 233; octx.hpp:36, 353, 369; chd.hpp:155, 174 |
| CO-32 | parse/write buffer handling | Low | SAFE-REFACTOR | oct.hpp:58-103 |
| CO-33 | Function-local static in the hottest accessor | Low | SAFE-REFACTOR | oct.hpp:105; octx.hpp:453 |
| CO-34 | `long` index sentinels mixed with `size_t` | Low | NEEDS-VERIFICATION | oct.hpp:117-131; octx.hpp passim |
| CO-35 | Dangling-reference hazard around `addName` | Low | DOC-ONLY | octx.hpp:60-66, 731; bent.hpp:86-88 |
| CO-36 | collision30 copies and double lookups | Low | SAFE-REFACTOR | octx.hpp:812, 835, 854-855 |

### CO-01 Hidden global parameters change the output; the shipped configuration is never byte-tested
- **Where:** `native/di1compat/octx.hpp:114` (`inline int g_slack = 0;`), read at `:213-216`, `:232`, `:236`;
  `native/di1compat/octx.hpp:387` (`inline bool g_variantGroups = true;`), read at `:526`; writers
  `native/di1compat/di1compat.cpp:4156-4157`, `native/di1compat/octtest.cpp:2685` (`--variants` argv only).
- **Severity / category / class:** High · hidden global parameter + test gap · NEEDS-VERIFICATION
- **Evidence:** `if (statics || g_slack) { int c32 = 128 * statics + (statics ? dnBytes : 0) + g_slack; ...` (:215-216).
  With the plugin's 4096 every counted SubNetwork gets ConstantAccumulatedSize32/64 (`{4, 4096 + ..., statics}` /
  `{8, 2*c32, statics}`), VariableAccumulatedSize32/64 +4096/+8192 (:213-214), DefaultDataSize and
  BaseDataNodeLinksAdditionalSize +256 (`g_slack / 16`, :232, :236). octtest never writes `g_slack`.
  `g_variantGroups` gates the VariantGroup append in `SnEdit::processor` for 2-ref `*VariantProcessor` types only
  (:525-527). **Corrected scope:** only `skin30` (:696) and `worldTransform30` (:756, i.e. `locator30` and
  `collision30`'s redirect) emit such processors. `camera30` (`worldChain` writes 3-ref `Transform<t>With<c>`, :737)
  and `gameplay::curves30` (`worldDilation30` writes `TransformDilationWithDilation`, gameplay.hpp:43) never do, so
  `g_variantGroups` does not affect them. Both globals are written in init before the first hook (AssetOpen,
  di1compat.cpp:4183) and only read afterwards from file-open threads; that ordering is the only thing making the
  plain non-atomic globals safe.
- **Why it matters:** the bytes of every served SubNetwork depend on process state that is invisible at the call
  site, and the only byte check (`--counts`) covers slack 0, i.e. a configuration the plugin never ships. See the
  coverage table: `--counts` also bypasses `addAllCounts`/`editPoolChildren`/`clip30` and the `constSection` branch.
- **Fix (behaviour-preserving):** `struct octx::Options { int slack = 0; bool variantGroups = true; };` passed
  `addAllCounts -> addCounts`, and `redirectAssociations -> SnEdit` (a field read by `processor`). `transcodeOct`
  passes `{4096, g_cfg.variant10}`; octtest passes `{0, true}` (or argv for `--variants`). Before touching
  `addCounts`, capture today's plugin-config output (slack 4096, through `addAllCounts`) for a corpus as a golden
  file; there is no shipped oracle for slack 4096, so the golden is the only way to prove the refactor.
- **Refactor risk:** defaults must equal today's globals so octtest outputs do not move. Because `SnEdit` is built
  inside `redirectAssociations`, threading Options through it reaches every user (including gameplay::curves30 at
  gameplay.hpp:71) without touching `worldDilation30`. The original report's claim that the option "must reach curves
  or the variant groups for curves change" was wrong (curves never make a VariantGroup).
- **Verification:** CORRECTED: `g_variantGroups` affects skin30/locator30/collision30 only, not camera30/curves30;
  refactor-risk statement fixed; coverage gap extended (editPoolChildren, clip30 and constSection are also outside
  `--counts`).

### CO-02 Name-table order is part of the byte output
- **Where:** `native/di1compat/oct.hpp:133-148` (`addName` appends unseen names in call order, first wins at :139),
  `:90` (`write` emits names in table order); `native/di1compat/octx.hpp:573-577`, `:651`; key names from
  `std::to_string` at `octx.hpp:337, 376, 848, 1138, 1151`.
- **Severity / category / class:** High · hidden coupling (blocks byte-safe refactoring of all of octx) · DOC-ONLY
- **Evidence:** as reported. `f.nameScalar(3, f.addName("Type"), v)` interns the key name before the value
  (argument evaluated before the body). Braced lists (:84-86, :276, :1119) are evaluated left to right by the
  language rule for list-initialisation. No call in the slice has two name-interning arguments whose order would be
  unspecified (grep). Existing drift: `octtest --oct` runs skin30/locator30/camera30 before addAssetPool
  (octtest.cpp:2954-2957), the plugin runs addAssetPool first (di1compat.cpp:741-744); see CO-06 for more.
  Example of a load-bearing "unused" name: collision30 builds `keyed(keyedNode(f, 2, "Association", 0))` (:862),
  which interns "0" before re-keying.
- **Why it matters:** semantically equal refactors (pre-interning, merging passes, reordering builder calls, changing
  `order`, last-wins dedup, dropping unused names) change bytes.
- **Fix (behaviour-preserving):** document the invariant at `addName` and at every multi-step builder; any interning
  cache must intern at the same moment as today; golden-compare whole files.
- **Refactor risk:** this is the main risk for every other fix in this slice.
- **Verification:** CONFIRMED

### CO-05 Unchecked `[0]` on decoded values crashes the game instead of falling back
- **Where:** `native/di1compat/octx.hpp:126, 181, 197, 315-318, 424, 428-431, 433, 631, 633, 649, 666, 812 (x2), 823-827,
  936, 951, 956, 961, 977, 981, 1002, 1078, 1129, 1147` (33 lines, 34 sites); `native/di1compat/bent.hpp:56, 58, 72, 73,
  88, 105`.
- **Severity / category / class:** Med · robustness (malformed input) · BEHAVIOUR-CHANGE
- **Evidence:** `if (n == "Type") type = f.nameValues(f.recs[j])[0];` (:315). **Corrected:** `ints()` is empty for
  nodes and zero-count lists (oct.hpp:158), and `nameValues()` is empty for *every* unkeyed record whose kind is not
  NAME (int/float/byte scalars included) and for zero-count name lists (oct.hpp:173-175). The DLL builds with `/MT`
  (CMakeLists.txt: `MSVC_RUNTIME_LIBRARY "MultiThreaded"`, so no `_DEBUG`, `_ITERATOR_DEBUG_LEVEL 0`): `v[0]` on an
  empty vector dereferences null, an access violation. CMake's default `/EHsc` means the `catch (...)` at
  di1compat.cpp:3714-3720 does not see it; `avHandler` (di1compat.cpp:1268-1277) only logs and continues the search,
  and CrabeLoader writes a dump. The result is a crash (or whatever engine SEH frame sits above hkOpen), never the
  "serve the raw 1.0 bytes" fallback. Only malformed or modded files reach it (shipped 1.0 shapes are consistent).
- **Why it matters:** the transcode layer's stated failure mode (exception, raw bytes served, one log line) is
  bypassed for this class of bad input.
- **Fix:** a checked accessor in `oct::File` (`cho::first`, cho.hpp:130-134, cannot be reused from octx because
  cho.hpp:118 includes octx.hpp; move it down into `oct::File` and add a string twin). For malformed input prefer
  *throwing* `std::runtime_error` over a silent default: it lands in the existing catches (di1compat.cpp:921 for .oct,
  3714 otherwise) and serves the raw 1.0 file. This changes behaviour only for inputs that crash today (crash ->
  raw bytes + "oct parse failed: ..." / "TRANSCODE EXCEPTION" log text), so it needs the user's decision.
- **Refactor risk:** for keyed records `nameValues()[0]` is the key, for unkeyed ones the first value; the keyed use
  is load-bearing at bent.hpp:58, :105 and octx.hpp:424 (`std::stoi` of the Basis key). Keep that distinction.
- **Verification:** CORRECTED: emptiness condition widened (any unkeyed non-NAME record); SEH path detailed;
  class set to BEHAVIOUR-CHANGE and the fix steered to throw.

### CO-07 `btd::convertBlock`: a 32-bit wrap in the validation leads to a multi-GB memcpy
- **Where:** `native/di1compat/btd.hpp:45-47, 57-58`; also `:25`.
- **Severity / category / class:** Med · integer overflow on malformed input · BEHAVIOUR-CHANGE
- **Evidence:** `aoff + alen != boff || boff + blen != voff` are `uint32_t` sums. Worked through: `aoff = 0x40,
  alen = 0xFFFFFFF0, boff = 0x30, blen = 0x10, voff = ioff = 0x40, vcnt = tcnt = 0`, `size >= 252` passes :45-46;
  :47 `aNew = alen + 16` wraps to 0; `out` is 268 bytes; :57 copies `alen - 188 = 0xFFFFFF34` bytes into `&out[268]`.
  The same wrap through `boff + blen` makes `bNew` small and :58 copies `blen - 16` bytes. :25 truncates a 64-bit end
  to `size_t`; the following bounds check keeps that one in range (garbage blocks only). Reached for any served
  `.btd`/`.btm` (no cfg gate, di1compat.cpp:972-985).
- **Why it matters:** an AV (uncatchable under `/EHsc`, see CO-05) on a corrupt collision file.
- **Fix:** do the :45 sums in `uint64_t` and add `(uint64_t)aoff + alen <= src.size()`. Valid files never wrap, so
  they are unchanged; malformed ones become "btd: not the 1.0 block layout" (raw bytes served).
- **Refactor risk:** none for valid files; btdconv parity is checked outside octtest.
- **Verification:** CONFIRMED

### CO-15 `oct::File::parse`: 32-bit size arithmetic can wrap into an OOB read
- **Where:** `native/di1compat/oct.hpp:64-69`, `:43`.
- **Severity / category / class:** Med (was Low-Med; malformed-input memory bug per rubric) · robustness · BEHAVIOUR-CHANGE
- **Evidence:** `size_t ds = HDR + nb, de = ds + db; if (de > n) throw ...; for (...; q < ds; ++q) if (b[q] == 0)`.
  On x86 `db` near `0xFFFFFFFF` wraps `de` below `ds`, the check passes and the name loop reads `b[q]` up to `ds > n`
  (OOB read); `tail.assign(b + p, de - p)` then throws `length_error`, but only after the read. (A huge `nb` that wraps
  `ds` itself below HDR only yields garbage, no OOB.) :43 `(long)u(...) * s.ew` overflows a 32-bit `long` for counts
  >= 2^29 with width 4 (signed overflow, wraps in practice); a wrapped short length is accepted and `ints()` later
  reads `u(p, cw)` elements past the payload.
- **Fix:** `if (nb > n - HDR || db > n - HDR - nb) throw ...`; compute `paylen` in `int64_t`. Round trip of valid
  files stays byte-exact (octtest); malformed ones throw instead of reading out of bounds.
- **Refactor risk:** none for valid files.
- **Verification:** CORRECTED: severity raised to Med; the `nb`-wrap case is garbage-only, the `db`-wrap case is the
  OOB one; overflow threshold is 2^29 (signed) not 2^30.

### CO-16 `worldChain` indexes `dns` with file-provided refs
- **Where:** `native/di1compat/octx.hpp:728` (`const std::string& ct = e.dns[s.first].type;`), `:737`; refs come from
  BasisConversion `DataNodeRef` (:433-435). Also `:443-444` trusts HeaderLayout counts.
- **Severity / category / class:** Med (was Low-Med) · robustness (malformed input) · BEHAVIOUR-CHANGE
- **Evidence:** `s.first` is never range-checked against `dns.size()` (OOB read, then a string compare on garbage).
  :443-444 pushes `hl[i + 1]` parents per entry: a huge count means a huge allocation (`bad_alloc`, caught), not OOB.
  Other `dns` indexing (skin30 :690/695, worldTransform30 :748-749, gameplay.hpp:31-32) uses `dnOf()` results, which
  are valid by construction.
- **Fix:** in `worldChain`, treat `s.first < 0 || s.first >= (int)e.dns.size()` like an unknown converter type
  (`return -1`). Valid files unchanged.
- **Refactor risk:** none for valid files.
- **Verification:** CORRECTED: severity raised to Med; :443-444 is an allocation blow-up, not an OOB.

### CO-26 luachunk: OOB register access on malformed bytecode; quadratic tables
- **Where:** `native/di1compat/luachunk.hpp:127` (`std::vector<Value> R(256 + 50)`), `:140`, `:143`, `:151`; `:28`, `:45`,
  `:68`, `:109-111`; `:89-95`, `:129-136`, `:173`, `:185-207`.
- **Severity / category / class:** Med (was Low) · memory safety on malformed input + CPU · BEHAVIOUR-CHANGE (bounds) /
  the CPU parts are SAFE-REFACTOR
- **Evidence:** `b` and `c` are 9-bit fields. MOVE `R[a] = R[b]` (:140) reads up to R[511]; **LOADNIL (:143) writes**
  `R[k]` for `k <= b` up to 511 (heap corruption, not just a read); SETLIST (:151) reads `R[a + k]` up to 766.
  `need()` (:28) computes `at + k`, and `(size_t)r.u32() * 4` (:45, :68) can wrap on 32-bit: no OOB follows (`at` stays
  `<= n`, `substr` then throws), but backward skips can loop/recurse until the stack overflows. `eval` sizes `code`
  and `K` from untrusted `u32` counts before checking them (:109, :111): multi-GB requests (`bad_alloc`, caught).
  CPU: `set` is a linear `find_if` per store (:129-136), `get` is linear (:89-94) and `text()` probes keys 1..n with
  it twice (:195, :197), O(n^2) per table; the sort comparator rebuilds `make_pair(int, string)` (calling
  `numText`/`text`) per comparison (:201-203); `quote()` makes a `std::string` per character (:173).
  Realistic sizes: zonelist / DNA / expendables tables have tens to low hundreds of entries, so the quadratic parts
  cost well under a millisecond. `std::sort` ties need two table-valued keys with equal text (string, number and bool
  keys always have distinct sort keys), which data chunks do not contain.
- **Hot path?:** code defaults: not reached; recommended config: load time (isSpark10 per DNA, memoised; zones10 once;
  sys::groups30 per expendables package; datamap.lua rewrite per playset).
- **Fix:** bounds-check `a`, `b`, `a + k` against `R.size()` and throw `runtime_error` (callers already catch
  `std::exception`: di1compat.cpp:845, 2640, 3714); `need`: `k > n - at`; validate counts against the remaining bytes
  before allocating. CPU: index tables by key while keeping insertion order; precompute sort keys; append to `o`
  directly in `quote`.
- **Refactor risk:** `text()` output is served (sys.hpp:727 groups30, gameplay.hpp:368-372), so the key order
  (`std::sort` with today's comparator: rank, then *text*, so "10" < "9") must stay.
- **Verification:** CORRECTED: LOADNIL is an out-of-bounds *write*; severity raised to Med; sizes checked; the wrap is
  not an OOB by itself.

### CO-03 `realm30w` is a god function
- **Where:** `native/di1compat/octx.hpp:914-1162` (249 lines).
- **Severity / category / class:** Med (was High) · god function · NEEDS-VERIFICATION
- **Evidence:** ten lambdas (`entries` :921, `field` :929, `nodeField` :937, `head` :942, `tail` :948, `typeOf` :951,
  `flt` :1003, `kv` :1009, `kvTrue` :1017, `uuid` :1079); the bool parameter is reassigned at :973; seven concerns
  (RealmData 954-972, RealmVolume 975-997, lights 998-1059, splice 1062-1074, rig Uuids 1088-1101, realm-file
  conversion 1102-1158, pool drops 1159-1160). All verified.
- **Why it matters:** hard to review or change, and only checked externally (realmcmp.py, comment :913).
- **Fix (behaviour-preserving):** split into `realmDataBlocks`, `volumeBlocks`, `lightBlocks`, `spliceNodeBlocks`,
  `addRigUuids`, `realmFile30` sharing a context struct (`f`, `nodes`, `rds`, `rvs`, `blocks`).
- **Refactor risk:** keep the `addName` call order (CO-02); `blocks` must stay a key-ordered map (:934, splice :1066);
  the light loop skips nodes already in `blocks` (:999), so realm and volume blocks must be built first; `rds`/`rvs`
  must be captured before the splice (later code uses `rds[0]` copies after `nodes` indices are stale).
- **Verification:** CORRECTED: severity High -> Med (a large god function is Med in the rubric; the refactor-blocking
  part is CO-02).

### CO-04 `addCounts` is a god function, and its contract does not match the code
- **Where:** `native/di1compat/octx.hpp:92-93` (doc), `:116-249` (134 lines).
- **Severity / category / class:** Med (was "Med-High") · god function, wrong contract · NEEDS-VERIFICATION
- **Evidence:** five passes (:119, :124, :133, :148, :196); three modes via nested ternaries (:211, :237) plus
  `g_slack`; index juggling (:239-247). The doc says "Returns false (file unchanged) if it ... lacks
  Type/Name/HeaderLayout", but (a) the `return false` at :185 runs after ConstantHeaderCount was erased (:127) and
  links converted (:133-141), and (b) the code does not require Name at all (":185 ... clips have no Name"). Only the
  "already has counts" exit (:120) really leaves the file unchanged. Under `addAllCounts` the mutated SubNetwork is
  spliced back and served. Static scope: :196-197 zeroes every level-5 `Static` in the SubNetwork, :181 counts only
  DataNodePool ones (equal only if no other pool has a level-5 `Static`; not checked against the corpus). :242
  computes `after` after the insert at `layoutAt + 1`, which is right only if Type/Name precede HeaderLayout.
- **Fix:** split into `stripConstHeaderCount`, `convertLinks`, `scan() -> SnStats`, `chooseMode`,
  `buildCountRecords(stats, mode, slack)`, `place()`; fix the doc (both points).
- **Refactor risk:** insert/erase order at :240-247 must stay identical; `--counts` covers only slack 0, the direct
  (non-`editPoolChildren`) path and never `constSection` (coverage table). Moving the validity check before the
  mutations is a BEHAVIOUR-CHANGE (link conversion would stop for SubNetworks lacking Type/HeaderLayout); do not do it
  silently.
- **Verification:** CORRECTED: severity normalised to Med; the doc's "Name" requirement is also wrong.

### CO-06 The .oct transcode pipeline exists 4 times, and the copies have drifted
- **Where:** `native/di1compat/di1compat.cpp:734-758`; `native/di1compat/octtest.cpp:2944-2976` (`--oct`), `:1057-1098`
  (`--cho-const`), `:1271-1277` (`--variants`).
- **Severity / category / class:** Med · duplication with drift · NEEDS-VERIFICATION
- **Evidence (verified, with additions):** addAssetPool position (CO-02); `--oct` runs camera30/curves30
  unconditionally, the plugin gates them on `camWorld10`/`curves10` (744, 748; both default true); realm test
  `find("/realms/")` (2960-2961) vs `rfind("realms/", 0) == 0` on the normalised path (746) plus `realmFile10`; slack 0
  vs 4096. **Added:** `--oct` runs curves30 (2962) *before* the cho steps (2964-2969), the plugin runs `transcodeCho`
  (747) *before* curves30 (748), and both add names and headers, so bytes differ; octtest's `const30` runs on any file
  when argv[4] == "cho", the plugin only for `choreographies/` and `exploders/` (754); octtest ignores
  `clip10Motions`/`clip10Ids` and never runs `cho::links30` (694); no navmesh/render/surfaces steps.
- **Why it matters:** ordering rules live only in comments (octx.hpp:800, gameplay.hpp:55, di1compat.cpp:751), and the
  `--oct` bytes that feed the Python comparisons are not what the plugin serves.
- **Fix:** extract the octx/cho/gameplay step sequence of 741-756 into one function with an Options struct (cfg
  flags, slack, variantGroups, realmFile, motion predicate, surface table) and have `--oct`, `--variants` and
  `--cho-const` call it. Render, navmesh and the engine surface table (di1compat statics, engine memory) cannot live
  in octx: inject them as callbacks or keep them in `transcodeOct` around the shared core.
- **Refactor risk:** keep the plugin's order exactly (golden master); octtest `--oct` bytes change (intended, tell the
  Python comparison owners).
- **Verification:** CORRECTED: added the curves30-vs-cho order, const30 gating and links30 drift; the proposed
  `transcode30` cannot own render/navmesh/surfaces.

### CO-09 Four parsers of an Association entry; two pool scans per redirect pass
- **Where:** `native/di1compat/octx.hpp:309-324` (addAssetPool), `:621-637` (eachAssociation), `:818-832` (collision30),
  `:1144-1152` (realm30w); redirectAssociations calls eachAssociation at `:649` and `:665`.
- **Severity / category / class:** Med · duplication + CPU · NEEDS-VERIFICATION
- **Evidence:** verified. Per world: 5 redirect passes (skin, locator, camera, collision, curves) x 2 parses, plus
  addAssetPool and collision30's own, about 12 full AssociationPool parses, each allocating per Type/NodeName field
  (CO-08).
- **Hot path?:** see convention; per in_city load roughly 12 x (thousands of associations x ~5 fields) compares and
  small allocations: low ms, unmeasured.
- **Fix:** one `std::vector<Assoc> associations(const File&)` (type, node, nodeRec, sn, inst, stn, hasIndex, at, end).
- **Refactor risk:** repeated fields are last-wins today. collision30 must re-parse after its redirect (:803).
  **Added:** a parsed list must not be reused across an `editPoolChildren` call: when the SubNetworkPool precedes the
  AssociationPool, the splice shifts every association record index, so redirectAssociations' second pass (:665)
  must re-resolve (or offset) the indices it got in the first (:649).
- **Verification:** CORRECTED: added the index-shift risk.

### CO-10 Record builders duplicated and split across two layers
- **Where:** `native/di1compat/octx.hpp:34-41` (`blobRec`) = `native/di1compat/mer.hpp:105-112` (`mer::blob`, token for
  token); `octx.hpp:373-378` (`keyedNode`) = `cho.hpp:907-912` (`cho::keyed`), inlined at `octx.hpp:336-337`; inline
  float scalars `octx.hpp:273`, `:510` vs `cho.hpp:347-352`; hand-built type codes `oct::NODE | 4` (:336, :375,
  cho.hpp:909), `0x0b` (oct.hpp:218).
- **Severity / category / class:** Med · duplication, layering · SAFE-REFACTOR
- **Evidence:** verified. cho.hpp uses octx only for `blobRec` (cho.hpp:302) and `setNames` (:768). npc.hpp:146-152
  and gameplay.hpp:582-585 do the same narrowest-count-width blob encoding, but **in place** on an existing record
  (keep level and name), so they need a `setBlob(Rec&, data)` helper, not `blobRec`.
- **Fix:** move all builders into `oct` (`setBlob`, `blob = node name + setBlob`, `keyedNode`, `floatScalar`,
  `floatList`, `nameList`, `setNames`) and delete the copies. Each adds the record name first and then key/value
  names, as today, so bytes are identical.
- **Refactor risk:** keep `floatList`'s fixed u8 count (CO-31) and both width rules; `setBlob` must keep
  `r.pay.clear()` semantics (drops a key; these records are unkeyed).
- **Verification:** CONFIRMED (added the in-place distinction).

### CO-11 `editPoolChildren` swaps the name table: hidden coupling and moved-from state
- **Where:** `native/di1compat/octx.hpp:586-591` (`Names` swap), `:596` (records moved out), `:578-618`.
- **Severity / category / class:** Med · hidden coupling · NEEDS-VERIFICATION
- **Evidence:** verified. During `edit`, the outer `f` holds g's empty table with `nameIndexed == 0`; an
  `f.addName` there would return index 0 (colliding with the real name 0) and be lost on the swap back. Today's
  lambdas (:653-663 with skin30/worldTransform30/camera30/worldDilation30, :1175-1178) use only `g`. On an exception
  `f.recs[kids[k]..ends[k])` stays moved-from; callers discard `f` (di1compat.cpp:916-924; octtest modes either catch
  or terminate). `order` must not hold duplicates (a second visit would copy moved-from records).
- **Fix:** document "edit sees only g" and assert `order` is unique. For the name table, prefer a non-owning
  `NameTable*` that `g` borrows for the duration of the call.
- **Refactor risk (added):** the report's `shared_ptr<NameTable>` inside `File` changes `File`'s value semantics:
  `oct::File g = f;` (octtest.cpp:446, countsCheck) would alias the table, so names added while checking one
  SubNetwork would leak into the next copy and change `--counts` bytes. All edits must still append to the same table
  in the same order (CO-02).
- **Verification:** CORRECTED: the proposed shared ownership would break octtest's per-SubNetwork copy; the borrow
  pattern avoids it.

### CO-12 `SnEdit`: view, builder and applier in one struct with public mutable state
- **Where:** `native/di1compat/octx.hpp:392-571` (180 lines; constructor :406-447).
- **Severity / category / class:** Med · god class, readability, CPU · NEEDS-VERIFICATION
- **Evidence:** free functions read `dns` and `parent` directly (skin30 :690-695, worldChain :725-737,
  gameplay.hpp:31-42) but change them only through `header`/`node`/`processor` (corrected: no free function writes the
  members, and `hs`/`hsi`/`hl` are touched only by members). Members declared after the constructor that fills them
  (`kids`/`dnAt` :448-449, `hsIndex` :474); `std::map<int,...>` for dense ints (:402, :448-449);
  `std::map<std::string,int> c` costs 4-7 string-keyed lookups per BasisConversion field (:433-435); `child()`
  (:456-462) is linear in siblings. The "1.5M compares per locator pass" figure assumes the 1,251 locators are
  siblings of one header (the comment at :474-475 does not say so); unmeasured.
- **Hot path?:** see convention; one SnEdit per touched SubNetwork per redirect pass (up to 5 per world).
- **Fix:** split into a read-only `SnGraph` (vectors; a `(parent, name) -> first child` map) and an emitter; three
  locals instead of `c`.
- **Refactor risk:** `dnAt` is last-wins and has key -1 for header-less nodes (:446); `conv` last-wins; `child`
  returns the first match in creation order; `header()` appends a `{par, 1}` layout pair per header (:484); no byte
  check exists (only `--variants`/`--cho-const` rules).
- **Verification:** CORRECTED: "mutated by free functions" -> read directly, mutated via members; lookup count; the
  sibling-count premise is unverified.

### CO-13 `arc30::readMember`: index re-read per call, one BCrypt call per 16 B, lock held across I/O
- **Where:** `native/di1compat/arc30.hpp:93-148` (lock :103), `Aes::x` :50-62; caller lock
  `native/di1compat/di1compat.cpp:531-549` (`lang30`).
- **Severity / category / class:** Med · CPU + lock held across I/O · NEEDS-VERIFICATION
- **Evidence:** verified: every call reopens the archive and reads + decrypts the whole index (:95-119);
  `BCryptEncrypt` per 16-byte block (:59), so `count/2` calls per index and `size/16` per `.dct` (fully encrypted,
  :137-139); `cipherLock` is held across `readAt` (:118, :127, :136) and `tinfl` (:144), also when `which == 0`.
  **Added:** `lang30` holds its own `mx` across the whole `<game>\assets\lang\*` scan (2 `.dct` reads per package,
  each decrypted 16 B per call), so the first served 1.0 `.dct` per language takes two locks across many archive reads.
  Lock order is `lang30.mx -> cipherLock`; no reverse path exists in the slice.
- **Hot path?:** code defaults: not reached; recommended config: load time: render donor reads (about 92 per in_city
  world, comment :92), `lang30` once per language (cached), scene-settings once, npcrt per playset (gamedb30). The
  lang30 first call is the likeliest measurable cost in this slice (unmeasured).
- **Fix:** cache each archive's decrypted index; generate the keystream for a whole span in one ECB call over
  consecutive counter blocks (identical output); lock only around cipher use (or drop it if the key handle is safe
  for concurrent `BCryptEncrypt`, unverified); never hold it over I/O or inflate.
- **Refactor risk:** bytes unchanged, but `--arc` does not cover the `.dct` full-decrypt branch: add a `.dct` member to
  the check before changing `Aes::x`. An index cache adds state (archives do not change at runtime, but invalidation
  must be stated).
- **Verification:** CORRECTED: coverage gap for `.dct`; lang30's outer lock added.

### CO-08 Per-call heap allocations in the accessors and builders
- **Where:** `native/di1compat/oct.hpp:154-167` (`ints`, no `reserve`), `:169-177` (`nameValues`), `:195`
  (`width({(int32_t)v})`), `:226` (`width({v})`); comparisons against temporaries at `octx.hpp:264`, `:287`,
  `bent.hpp:61`.
- **Severity / category / class:** Low (was Med) · CPU · SAFE-REFACTOR
- **Evidence:** verified. Estimate for in_city: about 283 SubNetworks x ~15 `intScalar` + 947 aggregates x ~6 =
  ~10k one-element vectors, well under 1 ms; `ints`/`nameValues` run per field of *selected* records (associations,
  scene nodes, DataNode fields), not per record of the file.
- **Hot path?:** see convention (load time only).
- **Fix:** non-allocating `int32_t int0(const Rec&, int32_t def)`, `const std::string& name0(const Rec&)`,
  `width(int32_t)` overload, `out.reserve(n)`.
- **Refactor risk:** keep sign extension (oct.hpp:163) and key-first-for-keyed.
- **Verification:** CORRECTED: severity Med -> Low (cold path, sub-ms by estimate; settle with `--oct` laps).

### CO-14 Repeated linear scans: `pool()`, `blockEnd` in loop conditions
- **Where:** `native/di1compat/oct.hpp:109-114`, `:117-122`; loop conditions `octx.hpp:281, 313, 810, 1091, 1094, 1146`,
  `bent.hpp:55, 67, 70, 84, 103`.
- **Severity / category / class:** Low (was Low-Med) · CPU · NEEDS-VERIFICATION
- **Evidence:** 28 in-slice `pool()` calls per world counted (addAssetPool 2, 5 redirects x 3, collision30 +2,
  realm30w 7 for a world, curves30 1, addAllCounts 1). Each scans until its pool (all ~200k 32-byte `Rec`s if
  missing). The quadratic loop conditions are O(k^2) in the *block* size: :281 runs only for Clip SubNetworks (early
  return :270), :313/:810/:1094/:1146 walk one association/asset/rig (k ~ 5-10), bent files are small; only :1091
  scans the whole LightRigPool per rig (rig count unverified). :281 and :1091 insert inside the loop and need the
  re-evaluation.
- **Hot path?:** see convention; estimated a few to tens of ms per world load, unmeasured.
- **Fix:** hoist `end` where the loop does not mutate; `end += inserted` where it does; pass already-found pool
  indices between steps. Avoid a `File`-level pool cache: `recs` is public and mutated directly in dozens of places
  (invalidation would be easy to miss).
- **Refactor risk:** the two inserting loops; first-match semantics of `pool()`.
- **Verification:** CORRECTED: severity Low; :281 is clip-only; fix steered away from a cache.

### CO-17 `realm30w` CPU waste
- **Where:** `native/di1compat/octx.hpp:1099` (insert per rig), `:998-999`, `:921-928`, `:970`, `:995`, `:1052`, `:1056`.
- **Severity / category / class:** Low (was Low-Med) · CPU · NEEDS-VERIFICATION
- **Evidence:** verified: :1099 inserts into the whole file per LightRig lacking a Uuid (O(N) shift each), while the
  file's own pattern is a one-pass rebuild (:838-840, :1061); `typeOf(n)` scans each scene node's fields and allocates
  (`nameValues`) for every node; `entries` copies all realm entries; `blocks[n] = b` copies. Node and rig counts
  unmeasured.
- **Hot path?:** see convention; once per world/realm `.oct`.
- **Fix:** collect the Uuid inserts and splice once; move `b`; compare Type via the name index.
- **Refactor risk:** `uuid(seed, k)` uses the ordinal `k` of *every* rig (skipped ones included); keep `"Uuid"`
  interning order.
- **Verification:** CORRECTED: severity Low (load time, counts unmeasured; settle with the `--oct` "lighting nodes
  converted" lap).

### CO-18 Low-level helpers duplicated across files
- **Where:** `native/di1compat/btd.hpp:15-17`, `native/di1compat/dct.hpp:19-36, 40`, `native/di1compat/mer.hpp:36-52`,
  `native/di1compat/chd.hpp:164`, plus the helpers table below.
- **Severity / category / class:** Low (was Low-Med) · duplication · SAFE-REFACTOR
- **Evidence:** verified samples: `btd::u32` == `dct::u32` == `vfx::rd32` (vfx.hpp:30); `btd::put32` == chd's `put32`
  lambda; `dct::lookup2` == `mer::lookup2` (equivalent tail via a zero-padded buffer); four FNV-64 loops
  (octx.hpp:1081-1083, di1compat.cpp:3779-3784, octtest.cpp:319-320, 3250); lowercase helpers in npc/audio/sx/di1compat;
  the Lua header twice in luachunk.hpp (:34, :102). Only the lookup2 pair carries real drift risk (both must equal the
  engine hash), and each is exercised by tests (`--ui` tips probes, mer tests).
- **Fix:** one `bytes.hpp` (LE get/put, checked and unchecked) and one `hash.hpp`.
- **Refactor risk:** checked readers return 0 / 0xffffffff on short input (gen30::u32, cho::rd), unchecked ones do not:
  never swap one for the other.
- **Verification:** CORRECTED: severity normalised to Low.

### CO-19 The "FNV-1a" basis literal is truncated and must not be "fixed"
- **Where:** `native/di1compat/octx.hpp:1079-1088`; `native/di1compat/di1compat.cpp:3781`;
  `native/di1compat/octtest.cpp:319`, `:3250`.
- **Severity / category / class:** Low · readability / refactor trap · DOC-ONLY
- **Evidence:** the FNV-1a 64-bit offset basis is 14695981039346656037 (0xcbf29ce484222325); `1469598103934665603ull`
  is it with the last digit dropped. `1099511628211ull` and `0x100000001b3ull` are the same prime.
- **Why it matters:** "correcting" it changes every generated LightRig/RealmData Uuid (served bytes) **and**, added,
  every on-disk shader cache file name: di1compat.cpp:3830 names cache entries
  `fnv64(target + "\n" + hlsl)`, and octtest.cpp:319 must produce the same names, so the whole
  `di1compat_shaders` cache would be invalidated.
- **Fix:** correct the comments only; a shared `fnv64` must keep this literal.
- **Verification:** CORRECTED: added the shader-cache dependency; severity set to a single Low.

### CO-20 Misplaced and stale comments
- **Where:** `native/di1compat/octx.hpp:702-721`, `:639-640`, `:872`, `:92-93`.
- **Severity / category / class:** Low · readability · DOC-ONLY
- **Evidence:** :702-711 documents `locator30` (:760), :712-714 `worldTransform30` (:743), only :715-721
  `worldChain`; :639-640's "earlier record indices stay valid" predates `editPoolChildren` (the real reason is name
  order, :651); :872's reasoning is moot because `setInt` runs before the rebuild (:874-880) and nothing touches
  `f.recs` between :808 and :873; :92-93 see CO-04.
- **Fix:** edit comments only.
- **Verification:** CONFIRMED

### CO-21 `oct::File` encapsulation and API smells
- **Where:** `native/di1compat/oct.hpp:51-239`.
- **Severity / category / class:** Low · API design · SAFE-REFACTOR (the 0xFFFF throw is a separate BEHAVIOUR-CHANGE)
- **Evidence:** verified: public name-index cache validated only by size (:137), declared mid-class (:149-150),
  swapped from outside (octx.hpp:590); the only other outside reader of `names` is npc.hpp:213. `(uint16_t)`
  truncation at :139/:145/:147; `f14` (:55) is the record count (:99); `name()` returns "?" out of range (:105); 189
  lines. **Corrected:** not every `nameScalar` caller passes `addName(...)`: bent.hpp:88 (`r.name`), bent.hpp:94
  (`f.recs[version].name`) and cho.hpp:511 (`f.recs[ty].name`) reuse an existing record's name index.
- **Fix:** private `NameTable`; rename `f14`; add `nameScalar(level, const std::string&, value)` (add name, then
  value) *beside* the index overload. Throwing past 0xFFFF names changes output that is already corrupt: user decision.
- **Verification:** CORRECTED: the index overload must stay.

### CO-22 Magic numbers and positional assumptions
- **Where:** octx.hpp:119-120, 151-171, 206-207, 212-237, 414-426, 496-527, 534, 983-986, 1028, 1031-1043, 1108-1113,
  1119-1134; btd.hpp:47-68; chd.hpp:27, 166-194; dct.hpp:59, 81, 86; luachunk.hpp:127; oct.hpp:19, 24.
- **Severity / category / class:** Low · readability · SAFE-REFACTOR
- **Evidence:** verified as reported. **Corrected:** the realm-file reference shift (:1108-1113) has three distinct
  rules, not four: mode 1 (`x >= 1 ? x + 1 : x == 0 ? 1 : x`) equals mode 3 (`x >= 0 ? x + 1 : x`). Rules:
  ParentNodeReferences and LinkedNodeRefs `x >= 0 -> x + 1`; LightRefs and LinkedSceneTreeNodeRefs always `+1` (-1
  included); SceneTreeNodeRef `x >= 1 -> x + 1`. The comments (:906, :1105) state only ">= 1 shift by one".
- **Fix:** named constants; a three-row rule table for the shift.
- **Refactor risk:** keep the values exactly; why LightRefs shifts -1 is undocumented (intent unverified).
- **Verification:** CORRECTED: four rules -> three.

### CO-23 Step return values double as a "changed" flag and under-report
- **Where:** `native/di1compat/di1compat.cpp:741-756`, `:918`; `native/di1compat/octx.hpp:803-830`, `:1176`.
- **Severity / category / class:** Low · contract · BEHAVIOUR-CHANGE
- **Evidence:** verified: collision30 edits NodeNames via its redirect (:803) and can still return 0 (:805, :830, or
  when no asset matches); addAllCounts drops clip30's result (:1176); `transcodeOct` returning false serves raw bytes
  (:918). camera30 itself counts its chains (`n`), only its redirect returns 0. Masked today because addAllCounts
  returns > 0 for 1.0 files with SubNetworks (unverified for every kind).
- **Fix:** an explicit `changed` flag. This changes behaviour only where edits are silently dropped today; user
  decision.
- **Verification:** CONFIRMED

### CO-24 arc30 robustness (unchecked OS/BCrypt results)
- **Where:** `native/di1compat/arc30.hpp:44-46`, `:52-60`, `:95`, `:98-100`, `:142-145`.
- **Severity / category / class:** Low · robustness · BEHAVIOUR-CHANGE
- **Evidence:** verified: `GetFileSizeEx` unchecked (`sz` uninitialised on failure); BCrypt statuses unchecked, so a
  failed `BCryptEncrypt` XORs uninitialised `ks`; `CreateFileA` (ANSI path); `out.assign(ucomp, '\0')` trusts the
  header (huge -> `bad_alloc`, caught); any method != 0 is inflated (only 8 is valid; others fail cleanly); static
  `Aes` destructors call BCrypt at DLL unload (effect unverified).
- **Fix:** check statuses and sizes (valid archives unaffected; failures become "not found").
- **Verification:** CONFIRMED

### CO-25 chd: deep copies and per-table linear key dedup
- **Where:** `native/di1compat/chd.hpp:120-123`, `:148`, `:180`, `:183`; caller `native/di1compat/di1compat.cpp:3203`.
- **Severity / category / class:** Low · CPU · SAFE-REFACTOR (`--chd` checks bytes)
- **Evidence:** verified: `emplace_back(key, v)` / `items.push_back(v)` copy subtrees (a leaf at depth d is copied d
  times); `find_if` per named key is O(k^2) per table (GameDB rows: tens of keys, negligible); `ko[...]`/`so[...]`
  are `std::map` lookups. :148 interns "true"/"false" into the key pool for bool values; removing it shifts the
  offsets of keys sorting after them and the header's section offsets (corrected wording: not "every" key offset).
  `parse` recursion is unbounded on malformed nesting (stack overflow), malformed input only.
- **Hot path?:** code defaults: practically not reached; recommended config: once per GameDB `.lua` opened in CHD mode.
- **Fix:** `std::move(v)`; a side `unordered_map` for dedup keeping "first position, last value".
- **Verification:** CORRECTED: offset-shift wording; recursion note added.

### CO-27 dct: duplicated re-key loop and repeated parsing
- **Where:** `native/di1compat/dct.hpp:113-122`, `:149-157`, `:59`, `:78-86`; caller
  `native/di1compat/di1compat.cpp:963-964`.
- **Severity / category / class:** Low · duplication + CPU · NEEDS-VERIFICATION
- **Evidence:** verified: the "names from ids -> latin1 -> lookup2" loop twice; every served `.dct` (with `text10`,
  default on) re-reads and re-parses 1.0 lang/main and its stringids (9,467 named keys, :139); `dump` does three
  hash lookups per value; :59's inner `e` shadows :52's (the `for` condition at :55 still uses the outer one, so it
  is correct, only confusing); `mer::lookup2` is equivalent.
- **Hot path?:** see convention; per served `.dct`.
- **Fix:** `Dict` overloads; one shared re-key helper; optionally cache the 1.0 main `Dict` per language.
- **Refactor risk:** a cache must be locked (transcodes run on several threads; mirror `lang30`); output bytes are
  checked only functionally (`--ui`) and by the `--dct` round trip.
- **Verification:** CONFIRMED

### CO-28 `bent::transcode` positional assumptions
- **Where:** `native/di1compat/bent.hpp:63`, `:93`, `:97-108`, `:116`.
- **Severity / category / class:** Low · robustness (malformed input) · BEHAVIOUR-CHANGE
- **Evidence:** :99-100 assumes the Model File produced at least one child and that the first is Filename. With zero
  children, `out.begin() + modelAt + 1` is past the end; in MSVC's STL the range constructor sees distance -1 and
  throws `length_error("vector too long")`, which the catch at di1compat.cpp:3714 handles (raw bytes served), so the
  practical effect is a logged exception, not memory corruption. If Filename is not first, Channels land after the
  wrong record (semantic only). :93 assumes record 0 is the root; level shifts are implicit; :63 calls
  `find_last_of` twice.
- **Fix:** guard `modelAt`, locate Filename explicitly, name the level constants.
- **Verification:** CORRECTED: on MSVC the zero-children case throws (caught) rather than inserting a default `Rec`.

### CO-29 Missing direct includes; MSVC-only constructs; live `min`/`max` macros
- **Where:** all 9 headers (see the includes table); `native/di1compat/btd.hpp:49`; `native/di1compat/di1compat.cpp:8`.
- **Severity / category / class:** Low · include hygiene · SAFE-REFACTOR
- **Evidence:** standalone compile verified. MSVC-only: `_stricmp` (datamap.hpp:29, arc30.hpp:137), `_byteswap_ulong`
  (luachunk.hpp:25), `#pragma comment(lib)` (arc30.hpp:14). **Corrected cause of the `max` hazard:** di1compat.cpp
  includes `<windows.h>` at line 8, before btd.hpp (:26) and before sys.hpp/audio.hpp define `NOMINMAX` (too late to
  matter), so the `min`/`max` macros are live in the plugin TU (hence `(std::min)` at di1compat.cpp:1258).
  `std::max<size_t>(` survives only because `max<` is not a function-like macro call; `std::max(a, b)` fails (C2589,
  reproduced). In octtest.cpp btd.hpp (:12) precedes the first `<windows.h>` (arc30.hpp at :16), so the same rewrite
  would compile there and fail only in the plugin.
- **Fix:** add the direct includes; write `(std::max)`; optionally `NOMINMAX` before di1compat.cpp:8.
- **Verification:** CORRECTED: the macro comes from di1compat.cpp:8, not arc30.hpp.

### CO-30 Endianness handling is inconsistent
- **Where:** reads: oct.hpp:32 (LE) vs btd.hpp:15, dct.hpp:40 (host `memcpy`); host-order writes: oct.hpp:93-94, 101;
  octx.hpp:82, 370, 510, 1084-1085; btd.hpp:16-17, 67-68; chd.hpp:156, 164, 174, 193-195; dct.hpp:81-87;
  luachunk.hpp:58.
- **Severity / category / class:** Low · portability, readability · SAFE-REFACTOR
- **Evidence:** verified; correct only on x86 (CMakeLists.txt: "x86 only").
- **Fix:** one LE get/put set (identical on x86).
- **Verification:** CONFIRMED

### CO-31 Width and count edge cases (UB unreachable today)
- **Where:** `native/di1compat/oct.hpp:208`, `:233`; `native/di1compat/octx.hpp:36`, `:353`, `:369`;
  `native/di1compat/chd.hpp:155`, `:174`.
- **Severity / category / class:** Low · integer edge cases · DOC-ONLY
- **Evidence:** verified: `(size_t)1 << (8 * cw)` reaches a shift by 32 on x86 at >= 2^24 elements; `floatList` always
  writes a u8 count (callers pass <= 16 floats); `intList`/`blobRec` cap count width at 3 unchecked; chd truncates to
  u16. `width()` itself is safe (the `w < 4` guard prevents `1 << 31`).
- **Fix:** document the limits (asserts compile out in release, so they change nothing).
- **Refactor risk:** keep both width rules: never-narrow (`setInt`/`setInts`/`setNames`) vs narrowest (builders).
- **Verification:** CONFIRMED

### CO-32 parse/write buffer handling
- **Where:** `native/di1compat/oct.hpp:58-85`, `:87-103`.
- **Severity / category / class:** Low · CPU · SAFE-REFACTOR
- **Evidence:** no `reserve` for names or records; `write` grows `data` then copies again in `h + nt + data`.
  **Corrected:** MSVC's `vector` grows by 1.5x, so ~200k records cost about 30 reallocations (not 18).
- **Hot path?:** see convention; once per served tuple file.
- **Fix:** reserve records from `f14` (the header's record count) when it is non-zero, not from `db / 4` (that bound
  would reserve tens of MB in a 32-bit process); write into one pre-sized string. Byte-identical (round trip).
- **Verification:** CORRECTED: reallocation count.

### CO-33 Function-local static in the hottest accessor
- **Where:** `native/di1compat/oct.hpp:105`; `native/di1compat/octx.hpp:453`.
- **Severity / category / class:** Low · CPU · SAFE-REFACTOR
- **Evidence:** a dynamically initialised local static: the thread-safe-init guard is checked on every call (the
  declaration is at function entry, before the ternary). Magnitude unmeasured, probably small.
- **Fix:** namespace-scope `inline const std::string` (SSO, no allocation at DLL load).
- **Verification:** CONFIRMED

### CO-34 `long` index sentinels mixed with `size_t`
- **Where:** `native/di1compat/oct.hpp:117-131`; octx.hpp passim (`pool`, `typeAt`, `cmudnAt`, `hsAt`, `pnPool`, ...).
- **Severity / category / class:** Low · type hygiene · NEEDS-VERIFICATION (was implied mechanical)
- **Evidence:** about 105 C4365 warnings in octx (reproduced). **Added risk:** several comparisons rely on -1 being
  negative: `if (pnPool > dnPool)` (:554), `pnPool < dnPool` (:561), `cmudnAt > layoutAt` (:241),
  `cmudnAt < (long)after` (:245). A mechanical switch to `size_t` with an `npos` sentinel flips them (`npos > x` is
  true) and would insert at `blockEnd(npos)`.
- **Fix:** `std::optional<size_t>` (forces every comparison to be rewritten explicitly), or keep `long` and add casts.
- **Verification:** CORRECTED: the fix is not mechanical.

### CO-35 Dangling-reference hazard around `addName`
- **Where:** `native/di1compat/octx.hpp:60-66`, `:731`; `native/di1compat/bent.hpp:86-88`.
- **Severity / category / class:** Low · robustness under refactor · DOC-ONLY
- **Evidence:** verified: `n` (a reference into `names`) is not used after the `addName` that may reallocate; :731
  copies `t` before `e.node()` grows `dns`.
- **Fix:** comment or copy.
- **Verification:** CONFIRMED

### CO-36 collision30 copies and double lookups
- **Where:** `native/di1compat/octx.hpp:812`, `:835`, `:854-855`.
- **Severity / category / class:** Low · CPU · SAFE-REFACTOR
- **Evidence:** verified: every association record is copied into the rebuilt pool although only levels of
  `f.recs[ap..poolEnd)` are read afterwards (`blockEnd` at :874; moved-from `Rec`s keep their level); `ints()` twice
  at :812; `count` + `operator[]` at :835.
- **Fix:** `make_move_iterator`, `emplace`.
- **Verification:** CONFIRMED

## Inventory

All line ranges below were re-checked against the source; every row of the original tables was correct (only the
`g_variantGroups` row changed).

**oct.hpp (241 lines)**

| Name | Lines | Purpose |
|---|---|---|
| `MAGIC`, `HDR`, `Shape`, `Kind`, `Spec` | 14-19 | File magic, header size, type-field enums, decoded fields |
| `spec` | 22-30 | Decode a type bit field and check it |
| `u` | 32 | LE unsigned read of 1-4 bytes (unchecked) |
| `paylen` | 35-44 | Payload length of a record, or -1 |
| `Rec` | 46-49 | Level, type, name index, payload (32 bytes on x86) |
| `File` | 51-239 | Parse, write, interning, navigation, decode/encode (189 lines) |
| `File::parse` / `write` | 58-85 / 87-103 | Bytes <-> header, names, records, tail |
| `File::name` x2, `blockEnd`, `pool`, `dropPool` | 105-131 | Navigation |
| `File::addName` + `nameIndex`/`nameIndexed` | 133-150 | Intern (first wins), lazy public index |
| `File::ints` / `nameValues` | 154-177 | Decode values |
| `File::width`, `put`, `setInt`, `setInts` | 180-214 | Encode, never-narrow rewrites |
| `File::nameScalar`, `node`, `intScalar`, `intList` | 216-238 | Record builders (narrowest widths) |

**octx.hpp (1,182 lines)**

| Name | Lines | Purpose |
|---|---|---|
| `COUNTS_A`, `COUNTS_B` | 21-24 | SubNetwork count record names (namespace `static`) |
| `typeSize`, `blobRec` | 26-41 | Arena size per type; blob record builder |
| `convertLink` | 54-90 | Base link into 3.0's three u16 blobs (37 lines) |
| `g_slack` | 114 | Global arena slack (hidden parameter) |
| `addCounts` | 116-249 | Count records (134 lines) |
| `clip30` | 255-294 | Clip MinTime/MaxTime, ClipData External (40) |
| `addAssetPool` | 302-346 | AssetPool from associations (45) |
| `setNames`, `nameList`, `floatList`, `keyedNode` | 349-378 | List / keyed builders |
| `g_variantGroups` | 387 | Global: VariantGroup for 2-ref variant processors |
| `SnEdit` | 392-571 | SubNetwork view + appender (180); ctor 406-447 (42); queries 451-473; `header` 476-489; `node` 491-517; `processor` 518-528; `toWorld` 531-546; `apply` 547-570 |
| `editPoolChildren` | 578-618 | Edit pool children in isolation, splice once (41) |
| `eachAssociation`, `redirectAssociations` | 621-673 | Association visitors / NodeName redirect |
| `parentPath`, `skin30` | 675-700 | SkinInfluence twin |
| `worldChain`, `worldTransform30`, `locator30` | 722-760 | World-basis chains |
| `camera30` | 778-789 | Camera world twin |
| `collision30` | 801-882 | Redirect + AssetIndex + aggregates (82) |
| `realm30w` | 914-1162 | Realms, volumes, lights, rigs, realm files (249) |
| `addAllCounts` | 1165-1180 | clip30 + addCounts per SubNetwork |

**bent.hpp (123):** `resolve` 21-41; `transcode` 44-121 (78).
**btd.hpp (98):** `u32/put32/put64` 15-17; `blocks` 20-31; `A_CONST` 34-37; `convertBlock` 40-70; `convert` 73-85; `btm` 88-96.
**chd.hpp (198):** `V` 18-25; `Tok` 27; `tokens` 29-97 (69); `parse` 99-129; `lua` 131-136; `compile` 138-196 (59).
**dct.hpp (165):** `SEED30` 16; `lookup2` 19-36; `Dict` 38; `u32` 40; `load` 42-64; `dump` 67-90; `latin1` 94-105; `reseed` 109-126; `hashes` 129-135; `supplement` 142-163.
**datamap.hpp (37):** `normalise` 13-35.
**arc30.hpp (150):** `mm3` 18-36; `Aes` 39-63; `keyBytes` 65-70; `cipher` 74-78; `cipherLock` 79; `readAt` 82-89; `readMember` 93-148 (56).
**luachunk.hpp (209):** `Reader` 21-29; `rewrite` 32-78 (47); `Value` 82-97; `eval` 100-159 (60); `numText` 163-169; `quote` 170-175; `ident` 176-184; `text` 185-207.

**Helpers (cross-file matching; verified samples)**

| Helper | Location | Duplicates / near-duplicates |
|---|---|---|
| `oct::u` (LE read, unchecked) | oct.hpp:32 | cho::rd (cho.hpp:124, checked), gen30::u32/u16 (gen30.hpp:42-43, checked), vfx::rd32/rd16 (vfx.hpp:30-31), dct `word` (dct.hpp:27), arc30 inline shifts (:26, :115, :129) |
| `oct::File::put` (LE append) | oct.hpp:187 | cho::wr (cho.hpp:128), octx `u16s` (octx.hpp:82, host order) |
| `btd::u32` | btd.hpp:15 | identical `dct::u32` (dct.hpp:40), `vfx::rd32` |
| `btd::put32/put64` | btd.hpp:16-17 | identical chd `put32` lambda (chd.hpp:164) |
| `octx::blobRec` | octx.hpp:34-41 | identical `mer::blob` (mer.hpp:105-112); in-place re-encodes npc.hpp:146-152, gameplay.hpp:582-585 |
| `octx::keyedNode` | octx.hpp:373-378 | identical `cho::keyed` (cho.hpp:907-912); inline octx.hpp:336-337 |
| float scalar | octx.hpp:273, 510 (inline) | `cho::floatRec` (cho.hpp:347-352) |
| `uuid` lambda (2x FNV-style) | octx.hpp:1079-1087 | `fnv64` di1compat.cpp:3779-3784 (also the shader-cache key, :3830), octtest.cpp:319-320, 3250 |
| `dct::lookup2` | dct.hpp:19-36 | `mer::lookup2` (mer.hpp:36-52) |
| `arc30::mm3` | arc30.hpp:18-36 | other hashes: FNV-1a 32 (sx.hpp:245-246, audio.hpp:47), CRC32 (gen30.hpp:466) |
| `bent::resolve` | bent.hpp:21-41 | di1compat `norm` (di1compat.cpp:478-484) |
| lowercase loops | bent.hpp:39; uppercase octx.hpp:1118 | npc::lower (npc.hpp:361), audio::lower (:53), sx::lower (:799), di1compat norm, octtest.cpp:2960 |
| case-insensitive suffix | arc30.hpp:137, datamap.hpp:28-29 | di1compat `endsWith` (di1compat.cpp:472-476) |
| Lua 5.1 header | luachunk.hpp:34, 102 | same file twice |
| "first int, else default" | inline `[0]` (CO-05) | `cho::first` (cho.hpp:130-134), octtest `anim::kid`/`str1` (octtest.cpp:701-712) |
| world conversion chain | octx `worldChain` (722-741) | `gameplay::worldDilation30` loop (gameplay.hpp:36-45; fixed Dilation processor, no converter-type check) |
| ConstantLayoutCount loop | octx.hpp:203 | octtest.cpp:1149 |
| dct probe | - | octtest.cpp:231-233 and 2801-2803 |
| "VariantProcessor" suffix | octx.hpp:525-526 | octtest.cpp:1248 |

**Namespace-level globals and statics**

| Name | File:line | Kind | Written by | Hidden parameter? |
|---|---|---|---|---|
| `oct::MAGIC`, `HDR`, `dct::SEED30` | oct.hpp:14-15, dct.hpp:16 | `constexpr` | - | no |
| `File::name()::none`, `SnEdit::name()::none` | oct.hpp:105, octx.hpp:453 | function-local `static const std::string` | - | no (guard per call, CO-33) |
| `octx::COUNTS_A/B`, `btd::A_CONST` | octx.hpp:21-24, btd.hpp:34-37 | namespace `static const` arrays (one copy per TU) | - | no; COUNTS read by octtest.cpp:407-408 |
| **`octx::g_slack`** | octx.hpp:114 | `inline int` | di1compat.cpp:4156 (`4096`, before hooks at :4183); never by octtest | **yes**: addCounts output |
| **`octx::g_variantGroups`** | octx.hpp:387 | `inline bool` | di1compat.cpp:4157 (`variant10`, default true); octtest.cpp:2685 (argv) | **yes**: `SnEdit::processor`, hence skin30, locator30, collision30 (corrected: not camera30 / curves30) |
| `convertBlock::tail`, `normalise::K`, `keyBytes::def/psx`, luachunk `HDR` x2, `ident::kw` | btd.hpp:50, datamap.hpp:25, arc30.hpp:67-68, luachunk.hpp:34/102/178 | function-local static const arrays | - | no |
| `cipher()::def/psx` | arc30.hpp:76 | function-local `static Aes` (BCrypt handles, destroyed at unload) | first use, under `cipherLock` | no (shared OS state) |
| `cipherLock()::m` | arc30.hpp:79 | function-local `static std::mutex` | - | no; serialises all 3.0 archive reads (CO-13) |

No free function in the slice is `static`; all are `inline`. No mutable namespace state other than the two `octx`
globals; everything else is per-call or const, so the codecs are re-entrant across file-open threads (arc30 locks its
shared cipher objects).

**Includes (verified by standalone `/Zs` compile; gaps compile only through transitive STL includes)**

| Header | Used but not included directly |
|---|---|
| oct.hpp | `<utility>` (`std::move` :80) |
| octx.hpp | `<cctype>` (`toupper` :1118), `<iterator>` (`make_move_iterator` :596, 606, 611, 877-879, 1068-1072), `<utility>`; `<cstring>`/`<string>`/`<vector>`/`<unordered_map>`/`<cstdint>` only via oct.hpp |
| bent.hpp | `<cctype>` (`tolower` :39); `<string>`/`<vector>` via oct.hpp |
| btd.hpp | complete |
| chd.hpp | `<cctype>` (:33, :37, :56), `<cstdint>` |
| dct.hpp | `<utility>` (trivial) |
| datamap.hpp | complete for MSVC (`_stricmp`) |
| arc30.hpp | complete; pulls `<windows.h>` without `NOMINMAX`/`WIN32_LEAN_AND_MEAN`; `#pragma comment(lib, "bcrypt.lib")` |
| luachunk.hpp | `<cstdio>` (`snprintf` :166), `<cctype>` (:180-181); `_byteswap_ulong` MSVC-only |

Outside-slice notes (verified): octtest `countsCheck` copies the whole file per SubNetwork (octtest.cpp:446,
test-only); the `.btm` request re-reads and re-converts the sibling `.btd` (di1compat.cpp:975-979) and runs
`btd::blocks` again just for the log line (:981); `lang30` holds its mutex across archive I/O (di1compat.cpp:533-548,
see CO-13).

## Rejected on verification
- None. All 36 findings survive; 24 needed corrections (scope, severity, line-level facts or fix safety), listed in
  each finding's Verification line.
