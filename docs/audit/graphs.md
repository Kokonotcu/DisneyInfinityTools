# Graphs, choreography and missions (`cho.hpp`, `ports10.hpp`, `mer.hpp`, `mernode.hpp`, `mission.hpp`)
Scope: every line of cho.hpp (1036), mer.hpp (511), mernode.hpp (296), mission.hpp (292), ports10.hpp (lines 1-14 and 160-204; the numeric rows were not checked) and its generator portmap.py:150-215; plus oct.hpp (all), octx.hpp (blobRec, clip30, setNames, keyedNode, SnEdit, editPoolChildren, eachAssociation, redirectAssociations, addAllCounts), gameplay.hpp:120-216 and 504-570, dct.hpp:1-40, the di1compat.cpp call sites (transcodeCho/transcodeOct 669-757, transcode() 853-1000, hkOpen serve branch 3645-3729, beacon10 3596-3615, hkMerRegistry 2696-2722, table load 4141-4166, hook 4268-4269), octtest.cpp (--cho-file/--cho-corpus 1574-1650, --cho-const 966-1180, --mer 2735-2757, --oct 2944-2976, --activity 2999-3020) and CMakeLists.txt. The verifier also scanned the 1.0 install's archives with a throw-away read-only Python parser (all 4,464 `.mer` members; 242 world/realm and 5,675 choreography/exploder `.oct` members), and timed `mer::mtc` with a throw-away 32-bit /O2 harness that includes the repo's `mer.hpp` unchanged (scratchpad only; nothing in the repository was built, run or edited).
Verification: 39 findings checked: 20 confirmed, 17 corrected, 2 unverified, 0 rejected; 1 new.

## 0. Rules every refactor must follow (break these and output bytes or behaviour change) -- verified

1. **Name-table order.** Every `f.addName` appends a new string in call order (oct.hpp:133-148) and `File::write` writes the
   names in table order (oct.hpp:90), so the order in which records are *built* is part of the output bytes. Name-adding
   helpers: `File::intScalar/intList/node/nameScalar` (oct.hpp:216-238), `mer::blob`, `octx::blobRec`, `octx::setNames`,
   `octx::keyedNode`, `cho::keyed`, `cho::floatRec`. A record built and then discarded still adds its name
   (mer.hpp:458 builds `EnableNetworking`, 464 may erase it). Orders to keep:
   - `mer::transcode` builds namespace blobs back to front (mer.hpp:457); the per-namespace `MergedLayoutPath` value
     names (463) therefore enter the table last namespace first.
   - cho passes walk their units last to first: SubNetworks (cho.hpp:362 time30, 427 anim30, 575 const30), Associations
     (836 scene30), Layouts (924 layout30). Order-sensitive names: const30's new header strings via `setNames` (768),
     layout30's `InstanceNode` keys `"0","1",...` (984). `const30` adds `AltNodeName` only after all SubNetworks (807).
   - Braced-init lists evaluate left to right (mer.hpp:458, cho.hpp:411, 511); function arguments do NOT. Never put two
     name-adding expressions into one call's argument list. Today's mixed cases, `nameScalar(lvl, f.addName("X"), value)`
     at mer.hpp:463 and cho.hpp:807, are deterministic only because the value's name is added inside `nameScalar`, after
     all arguments are evaluated.
2. **The name table may hold the same string twice** (`addName` index: "first wins", oct.hpp:139). Comparing name
   indices is not comparing strings; don't turn `f.name(r) == "X"` into an index compare. Rebuilding a record through
   `addName` (e.g. `octx::setNames`) can change the index it stores to the first duplicate (see GR-34).
3. **"false means not changed" contracts.** `mer::transcode` (comment mer.hpp:159): every `return false` (164, 165, 300)
   happens before any `addName` or `f.recs` change. This is observable: octtest `--activity` writes `f` even when
   transcode returns false (octtest.cpp:3017-3019). `cho::banm30` (returns false at 857 before any edit) and
   `cho::clipBlock30` ("out untouched", comment 182; `out` assigned only at 236) have the same contract.
4. **Side effects that survive a `continue`.** In `const30`, Static nodes are demoted with `f.setInt(...)` (683, 698)
   before the SubNetwork may be skipped (`if (!keepAny) continue;` 702, `if (moving.empty()) continue;` 707).
5. **Runtime calling convention (`mernode.hpp`).** `__fastcall` thunks stand in for engine `thiscall` methods (ecx =
   this, edx dummy); the stack-parameter count sets callee cleanup. `Rt` objects must never move: the engine keeps
   pointers to `vbt`, `mainVt`, `cbVt`, `defVt` inside them (mernode.hpp:115, 118-119, 176) and possibly to the `cls`/`def`
   strings (51). `registerAll` checks all 28 `kChecks` signatures before it writes `E` (230-232). Order inside
   `hkMerRegistry`: original registry, then aliases, then native classes (di1compat.cpp:2705-2722).
6. **Tests do not cover byte equality here, and the octtest pipelines are not the plugin's.** `octtest --cho-corpus`
   checks re-parse, 3.0 parse of clip blocks, counts and two clip10ids invariants, not golden bytes; it disables the
   `CHO_OUT` dump in corpus mode (octtest.cpp:1629 `if (!corpus && getenv("CHO_OUT"))`); `--cho-file <files>` does
   dump. `--cho-file/--cho-corpus` pass `motion = true` for every template (1603), the plugin `!has30(owner)`
   (di1compat.cpp:685), and never run const30. The `--oct <in> <out> cho` lap pipeline (octtest.cpp:2953-2971) differs
   from `transcodeOct` (di1compat.cpp:734-757) in more than links30/motion: it has no render, navmesh30 or surfaces30;
   it runs `addAssetPool` after skin30/locator30/camera30 (plugin: before skin30) and `curves30` before the cho passes
   (plugin: after transcodeCho) -- both change name-table order (rule 1); it calls `clips30(f, nullptr)` (motion null,
   `ids = false`, so no ports10 renumbering) and skips links30 (plugin: clip10Motions/clip10Ids default 1); it runs
   camera30 and const30 unconditionally (plugin: camWorld10; const30 only for `choreographies/` and `exploders/`); its
   realm rule is `find("/realms/")` vs the plugin's `rfind("realms/", 0) == 0`. Its comment "the plugin's choreography
   steps too" (2963) is therefore inaccurate. `--mer` writes bytes per file (2735-2753) and can serve as a golden
   source. Before refactoring, add a golden dump-and-diff over the corpus for the cho passes (via `--cho-file` +
   `CHO_OUT`, or a new mode that calls one shared pipeline, GR-32) and for `--mer`.
7. **(added) Record indices go stale after an insert/erase.** Every pass captures record indices first and edits later
   ones first. anim30 additionally relies on `HeaderStrings` preceding `DataNodePool` (GR-N1). If GR-05's
   `octx::editPoolChildren` is adopted: while edits run, `f`'s name table is swapped into the per-child File `g`
   (octx.hpp:586-594), so callbacks must use `g` (inside them `f.names` is empty), and the child records are moved out
   of `f.recs` (596).

## Findings
| ID | Title | Sev | Class | Where |
|---|---|---|---|---|
| GR-04 | `mer::mtc` re-scans the node pool in its loop condition (measured ~130 ms on in_city) | High | SAFE-REFACTOR | mer.hpp:488, 490 (278, 282) |
| GR-01 | `mer::transcode` is a 318-line function that does everything | Med | NEEDS-VERIFICATION | mer.hpp:160-477 |
| GR-02 | `cho::const30` is a 258-line function with ~15 jobs | Med | NEEDS-VERIFICATION | cho.hpp:553-810 |
| GR-03 | `mer::Table` mixes flags, class catalogue and boot-mutated registry state | Med | NEEDS-VERIFICATION | mer.hpp:56-85 |
| GR-06 | `anim30` EventList offset -1..-3 wraps `cho::rd`'s bounds check on Win32 | Med | BEHAVIOUR-CHANGE | cho.hpp:494-496, 124-127 |
| GR-07 | `mer::transcode` writes out of bounds when a graph has no NameSpaceBegin | Med | BEHAVIOUR-CHANGE | mer.hpp:342-453 |
| GR-08 | `mernode` engine binding: 25 function pointers set by position | Med | SAFE-REFACTOR | mernode.hpp:63-88, 194-204, 232-245, 269-272 |
| GR-12 | Pool/record walking hand-written ~15 times with different rules | Med | NEEDS-VERIFICATION | cho.hpp, mer.hpp (many) |
| GR-13 | `beaconMap30` has its own Lua scanner, weaker than `gameplay::luaTokens` | Med | NEEDS-VERIFICATION | mission.hpp:210-243 |
| GR-14 | Long functions with several jobs (anim30, layout30, ...) | Med | NEEDS-VERIFICATION | cho.hpp:419-529, 915-1034 |
| GR-N1 | anim30 patches `HeaderStrings` through an index that its own edits may have shifted | Med | BEHAVIOUR-CHANGE | cho.hpp:434, 515-523 |
| GR-05 | cho passes insert into `f.recs` in place (measured: only a handful of inserts per file) | Low | NEEDS-VERIFICATION | cho.hpp:412, 518-519, 775, 807, 840, 1026-1027 |
| GR-09 | Native thunks/registration could let a C++ exception unwind through engine frames | Low | BEHAVIOUR-CHANGE | mernode.hpp:149-179, 228-287 |
| GR-10 | Exact copies of `octx` record builders | Low | SAFE-REFACTOR | mer.hpp:105-112, cho.hpp:907-912, 347-352 |
| GR-11 | `lookup2` implemented twice; engine hash assumed equal to it | Low | SAFE-REFACTOR | mer.hpp:36-52, dct.hpp:19-36, mernode.hpp:101 |
| GR-15 | Meridian graph parsing duplicated with `activityGraph30`; uuid link branch dead in the corpus | Low | NEEDS-VERIFICATION | mer.hpp:169-177, 292-331 |
| GR-16 | `[0]` on `nameValues`/`ints` results without checking the record kind | Low | BEHAVIOUR-CHANGE | mer.hpp:175, 184, 284, 285, 380, 381, 463, 491 |
| GR-17 | LinksBuffer size check can wrap in 32-bit arithmetic | Low | BEHAVIOUR-CHANGE | mer.hpp:300 |
| GR-18 | Allocation-heavy access and whole-payload copies in per-record loops | Low | NEEDS-VERIFICATION | cho.hpp:130-134, 377-379, 439, 486 |
| GR-19 | `const30` clash fix-point and string lookups grow quadratically | Low | NEEDS-VERIFICATION | cho.hpp:632-636, 687-700, 760-765 |
| GR-20 | `mer::transcode` per-node set allocations and a deep copy of every record | Low | SAFE-REFACTOR | mer.hpp:210, 341-372, 469 |
| GR-21 | `table10` allocates one vector per clip component | Low | NEEDS-VERIFICATION | cho.hpp:137, 150-156 |
| GR-22 | Stale or inaccurate comments | Low | DOC-ONLY | cho.hpp:2, 549, 741, 73-74; mernode.hpp:3 |
| GR-23 | `layout30` relies on C++17 evaluation order | Low | SAFE-REFACTOR | cho.hpp:960 |
| GR-24 | Unnamed magic numbers, hard-coded record levels, clashing names | Low | SAFE-REFACTOR | cho.hpp, mer.hpp, mission.hpp |
| GR-25 | `mernode`: offset 0x14 means two things; unused API | Low | SAFE-REFACTOR | mernode.hpp:98-110, 120, 132, 246, 251, 260 |
| GR-26 | Lifetime of what `mernode` hands to the engine | Low | DOC-ONLY | mernode.hpp:49-61, 273-276 |
| GR-27 | `behaviour30` searches to end-of-file for every matching record | Low | NEEDS-VERIFICATION | mission.hpp:184 |
| GR-28 | `taskLines30` scans to end-of-file; Parms search duplicated | Low | SAFE-REFACTOR | mission.hpp:79, 100, 114 |
| GR-29 | `mission.hpp` holds an engine address called with no signature check | Low | BEHAVIOUR-CHANGE | mission.hpp:31-34; di1compat.cpp:2711 |
| GR-30 | `Table::load` does not strip `\r` | Low | BEHAVIOUR-CHANGE | mer.hpp:71-81 |
| GR-31 | `clips30`: hidden state machine and inconsistent template choice | Low | NEEDS-VERIFICATION | cho.hpp:242-305 |
| GR-32 | Pass order written in several places; octtest pipelines drift from the plugin's | Low | NEEDS-VERIFICATION | di1compat.cpp:679-756; octtest.cpp:1062-1097, 1602-1627, 2953-2971 |
| GR-33 | Unchecked 16-bit truncation in clip re-packing | Low | BEHAVIOUR-CHANGE | cho.hpp:173, 224 |
| GR-34 | Two ways of rewriting `HeaderStrings` | Low | SAFE-REFACTOR | cho.hpp:522-523, 768 |
| GR-35 | Lookups that mutate | Low | SAFE-REFACTOR | cho.hpp:682, 959-1015; mer.hpp:92 |
| GR-36 | `g_mer` changed by the boot hook without synchronisation | Low | NEEDS-VERIFICATION | di1compat.cpp:2714, 2719; mernode.hpp:284 |
| GR-37 | `ports10.hpp` generated-code shape (fix in portmap.py) | Low | SAFE-REFACTOR | ports10.hpp:10, 168-202; portmap.py:203-213 |
| GR-38 | Filesystem probes per motion-clip SubNetwork | Low | NEEDS-VERIFICATION | di1compat.cpp:672-677, 684-686 |
| GR-39 | `mer::transcode` rewrites links only in records after the node pool | Low | BEHAVIOUR-CHANGE | mer.hpp:341, 468-473 |

Common frequency facts (used below):
- Served-file transforms (all cho passes, `mer::transcode`/`mtc`, `mission::luaEvents30/progress30/taskLines30/behaviour30`,
  `cho::banm30`) run inside `transcode()` (di1compat.cpp:853), reached only from hkOpen's serve branch
  (di1compat.cpp:3645, requires `serve10` and `install10`). **Code defaults (`serve10 = 0`): never run. Recommended
  config: once per open of a 1.0-served file**, on the thread that opens it; `.oct`/`.mer` outputs are not cached (only
  session products and the `.mtc` built beside a `.mer`, 3710-3713). const30 only for `choreographies/` and
  `exploders/` paths (754).
- `mission::beaconMap30` runs on the 3.0 `zonelist.lua` open (di1compat.cpp:3600-3614), not gated by `serve10`
  (`beacon10` default 1): **both configs**, once per open.
- `hkMerRegistry` (aliases + `mernode::registerAll`) is hooked when `(aliases10 || native10) && g_merOk`
  (di1compat.cpp:4268-4269), not gated by `serve10`: **both configs**, once at boot, if `di1compat_mer.txt` is present.
- CMakeLists.txt sets no `/EHa`, so MSVC's default `/EHsc` applies: `catch (...)` at di1compat.cpp:3717 does NOT catch
  access violations; an AV in a transform is a CrabeLoader dump plus a game crash.

### GR-04 `mer::mtc` re-scans the node pool in its loop condition
- **Where:** `native/di1compat/mer.hpp:488`, `mer.hpp:490`; same pattern `mer.hpp:278`, `mer.hpp:282`
- **Severity / category / class:** High · CPU (quadratic loop condition) · SAFE-REFACTOR
- **Evidence:** `for (size_t i = p + 1; p >= 0 && i < f.blockEnd(p); i = f.blockEnd(i)) { ... for (size_t j = i + 1; j <
  f.blockEnd(i); ++j)`. `blockEnd` is a linear scan (oct.hpp:109-114); the body calls `std::map::operator[]` and the
  allocating `nameValues`, so the compiler re-evaluates `blockEnd(p)` once per node: nodes x pool-records compares.
  1.0 corpus (verifier scan): in_city.mer 4,304 nodes / 35,144 node-pool records, intro.mer 5,331 / 47,308, mu_campus.mer
  3,995 / 33,531. **Measured** (32-bit /O2 /MT harness including the repo's mer.hpp unchanged; in_city.mer transcoded with
  a stand-in table, every node type registered, no class data, pool grows to 57,122 records): `mer::mtc` 132 ms per call
  (3 runs) vs 0.8 ms with both `blockEnd` calls hoisted, byte-identical output; `mer::transcode` itself 17 ms. The real
  `di1compat_mer.txt` was not available, so the post-transcode pool size differs; expect roughly 80-130 ms for in_city
  and more for intro.mer. In transcode (278/282) the loop runs over the link pool (one NameSpaceBegin + LinksBuffer per
  namespace, in_city 5): negligible.
- **Hot path?:** code defaults: never (`serve10 = 0`). Recommended config: once per served `.mer` (di1compat.cpp:948 builds
  the `.mtc` alongside; the engine's `.mtc` request is then served from the session), plus once per uncached `.mtc`
  request (931-933, 947). About 48 `.mer` per world load (comment at 851-852), cost dominated by the large world graphs.
- **Why it matters:** about 0.1 s of every world load spent re-scanning the same pool, 8x the cost of the transcode itself.
- **Fix (behaviour-preserving):** `if (p >= 0) for (size_t i = p + 1, pe = f.blockEnd(p); i < pe; i = f.blockEnd(i))` and
  `for (size_t j = i + 1, je = f.blockEnd(i); j < je; ++j)`. Same at 278/282 (`f.recs` is not modified inside that loop).
- **Refactor risk:** keep the `p >= 0` guard in front of the hoisted call: `blockEnd((size_t)-1)` would read
  `recs[SIZE_MAX]` (the report's "none" missed this). Otherwise pure; output verified identical on in_city.
  Cross-match outside this slice: gameplay.hpp:541 evaluates `blockEnd(lpool)` per link-pool record (small pool).
- **Verification:** CORRECTED: severity Med -> High (measured ~132 ms vs 0.8 ms, per-open in the recommended config);
  added the `p >= 0` hoisting risk; node/record counts measured instead of assumed.

### GR-01 `mer::transcode` is a 318-line function that does everything
- **Where:** `native/di1compat/mer.hpp:160-477`
- **Severity / category / class:** Med · god function, flag-driven branching · NEEDS-VERIFICATION
- **Evidence:** ~11 jobs: detect not-a-graph / already-converted (164-165); collect nodes (169-177); bracket namespaces
  (178-188); mark stand-ins (189-190); infer port names (198-225); per-class pin tables under two rules (227-251); pin
  lookup + MasterNetSync gate (253-268); decode/filter/re-encode links in uuid and global-index forms (270-338); rebuild
  every node: stand-in rewrite, NumIn/OutLinks fix-up, parameter description, events10, toggleType10, pin-table copy,
  LeafNameHash (340-455); namespace blobs (456-466); copy the rest with rewritten links (467-474). Nesting depth 6
  (316). Six `Table` flags branch inside.
- **Why it matters:** phases share ad-hoc locals (`port`, `portRefs`, `pinTables`, `nsOut`/`nsPriv`, `dIn`/`dOut`,
  `newLinks`, `beginAt`, `values`/`strs`); untestable alone; rules 1 and 3 are invisible.
- **Fix (behaviour-preserving):** phase functions over a `Graph` struct, called in today's order: `collectNodes`,
  `portNames`, `pinTables`, `rewriteLinks`, `describeNode`, `namespaceBlobs`, `copyTail`; keep `Stats` updates where
  they are.
- **Refactor risk:** rules 1 and 3 (every early return before any `addName`); `pinTables` taken from the first instance
  listing pins (235); the second `Type` record counts as a parameter only after `firstType` (370, 385); `field()` reads
  node records after the rebuild loop (461, 463). Check with `octtest --mer` (golden bytes) and `--activity`.
- **Verification:** CORRECTED: severity High -> Med (the rubric puts a large god function at Med; it does not block
  refactoring outside itself). Line ranges and job list confirmed.

### GR-02 `cho::const30` is a 258-line function with ~15 jobs
- **Where:** `native/di1compat/cho.hpp:553-810`
- **Severity / category / class:** Med · god function · NEEDS-VERIFICATION
- **Evidence:** list SubNetworks (555-559); map Association NodeName to SubNetwork (561-574); scan DataNodes, processors,
  BasisConversion (575-612); decode/validate header tree (613-630); path helpers (631-636); 7 demotion rules incl. a
  33-entry static table (637-686, counted); clash fix-point (687-700); moving/used/dropped sets (701-715); constant trie
  (716-740); numbering (741-759); rewrite string table (760-768); renumber Header fields (769-774); insert
  `ConstantHeaderCount` (775); record constant paths (776-780); second AssociationPool pass for `AltNodeName` (783-808).
- **Why it matters:** engine-crash research behind each rule (637-653) mixed with mechanical index rewriting.
- **Fix (behaviour-preserving):** `scanSubNetwork` (struct of dns, pn, procs, convRefs); `HeaderTree` (the `octx::SnEdit`
  model, octx.hpp:392-489, plus const30's validation at 623-630, which SnEdit lacks); `demoteStatics(rules)`,
  `dropClashes`, `buildConstantTrie`, `renumber`, `addAltNodeNames`.
- **Refactor risk:** rule 4; `ordinal` (577) counts "SubNetwork"-named level-2 records only; constant headers are numbered
  in trie-creation order, not breadth first (GR-22b) - keep it; `std::find` over `strs` keeps the first match (762).
  Check with `octtest --cho-const` (set/path rules, not numbering) plus a golden byte diff.
- **Verification:** CORRECTED: severity High -> Med (rubric). Everything else confirmed.

### GR-03 `mer::Table` mixes 6 behaviour flags, a class catalogue and boot-mutated registry state
- **Where:** `native/di1compat/mer.hpp:56-85`; flag branches in transcode: ports10 216, 392; portRefs 224; pins10 237,
  241-244, 439-442; gate10 267; events10 400; toggleType10 420. Writers: di1compat.cpp:4158-4164, 2714 (`addAliases`),
  mernode.hpp:218-224 + 284. Duplicate flag wiring: octtest.cpp:2738-2745.
- **Severity / category / class:** Med · god struct, flag arguments, duplication · NEEDS-VERIFICATION
- **Evidence:** `bool events10 = true; ... toggleType10 = true; std::set<std::string> registered; std::map<std::string,
  Class> classes; bool load(...)`. The `pins10 == false` "previous rule" is split across 237-244 and 439-442.
  Config-to-flag mapping written twice (di1compat.cpp:4159-4164, octtest.cpp:2739-2744).
- **Why it matters:** config, data (exe registry + catalogue from di1compat_mer.txt) and boot-time mutation (aliases,
  native classes) share one object; output for the same `.mer` depends on whether `hkMerRegistry` has run
  (di1compat.cpp:2696-2698).
- **Fix (behaviour-preserving):** `mer::Options` (6 bools, default true) + `mer::Catalog` (registered, classes, load); pass
  both; one `optionsFromCfg()` used by plugin and octtest; keep each legacy branch as a named rule function.
- **Refactor risk:** defaults stay true. `addAliases` uses `T.classes[a.twin]` (92), which creates an empty class for an
  absent twin; the verifier checked the four `classes.find` uses (236-248, 358-395, 441) and an empty class gives the
  same output as an absent one under both pins10 settings, but keep operator[] semantics anyway (log line 4165 counts
  `classes.size()`; it runs before addAliases today). Callers: di1compat.cpp:941, octtest.cpp:2750, 3017.
- **Verification:** CONFIRMED (added the empty-class equivalence check).

### GR-06 `anim30` EventList offset -1..-3 wraps `cho::rd`'s bounds check on Win32
- **Where:** `native/di1compat/cho.hpp:494-496`, `cho.hpp:124-127`
- **Severity / category / class:** Med · correctness (UB on malformed data) · BEHAVIOUR-CHANGE
- **Evidence:** `uint32_t off = (uint32_t)(o < 0 ? -1 : first(f, f.recs[o], -1)), cnt = rd(nb, off, 4);` and
  `return o + w <= s.size() ? oct::u((const uint8_t*)s.data() + o, w) : 0xffffffffu;`. With 32-bit `size_t`, off
  0xFFFFFFFF + 4 wraps to 3, the check passes when `nb.size() >= 3`, and `u()` reads `nb.data() - 1` (1-3 bytes before
  the buffer for offsets -1..-3; other negative offsets do not wrap and return 0xffffffff). The event loop then reads
  in-bounds but misaligned bytes (`off + 4 + 8*k2` wraps to `3 + 8*k2`): garbage RangeMin/RangeMax, possibly NaN.
  Reached when the EventList data node has no `NetworkBufferOffset`, an empty one, or -1..-3. time30 guards it:
  `if (ev && off >= 0)` (395).
- **Hot path?:** code defaults: never; recommended: per served `.oct` (choAnim10), only on malformed data.
- **Why it matters:** UB / nondeterministic output for a malformed EventList; no shipped case known (UNVERIFIED).
- **Fix (behaviour-preserving):** overflow-safe `rd` (`w <= s.size() && o <= s.size() - w`, identical whenever `o + w`
  does not wrap) and/or mirror time30's guard. Changes only inputs that are UB today.
- **Refactor risk:** `rd` is also used by octtest (1400-1564); identical there for non-wrapping offsets.
- **Verification:** CONFIRMED (narrowed: only offsets -1..-3 wrap; class set to BEHAVIOUR-CHANGE for UB inputs only).

### GR-07 `mer::transcode` writes out of bounds when a graph has no NameSpaceBegin
- **Where:** `native/di1compat/mer.hpp:342`, `357`, `399`, `422`, `426-427`, `450`, `453`
- **Severity / category / class:** Med · robustness (heap corruption on unusual input) · BEHAVIOUR-CHANGE
- **Evidence:** `std::vector<std::string> values(nss.size()), strs(nss.size());` then `int ns = n.ns < 0 ? 0 : n.ns;` and
  `strs[ns]`, `values[ns].size()`, `values[ns] += val`: with `nss.empty()` every non-namespace node writes past an empty
  vector (450/453 run for every such node). Nodes before the first Begin go to namespace 0 (357). FORMATS.md:385 (not
  388) covers 3.0 graphs only. **1.0 corpus (verifier scan): 0 of 4,464 `.mer` members lack a NameSpaceBegin, 0 have
  nodes before the first Begin** - unreachable with shipped data.
- **Why it matters:** heap corruption in the game process on a modded or truncated graph.
- **Fix (behaviour-preserving):** `if (nss.empty()) { if (why) *why = "no namespace"; return false; }` before line 341
  (before any `addName`, keeping rule 3). Document that nodes before the first Begin go to namespace 0.
- **Refactor risk:** keep the check ahead of the first `addName` (376).
- **Verification:** CORRECTED: FORMATS.md line 385; added 422/426-427; 1.0 corpus checked (0/4,464).

### GR-08 `mernode` engine binding: 25 function pointers set by position
- **Where:** `native/di1compat/mernode.hpp:63-88` (fields, address in comment), `194-204` (`kChecks`), `232-245`
  (positional `E = {...}`), `269-272` (`defVt` addresses)
- **Severity / category / class:** Med · fragile engine coupling, duplication · SAFE-REFACTOR
- **Evidence:** `Engine` has 22 function-pointer members, one of them `pinCtor[4]`, so 25 function pointers (not 24), plus
  2 data pointers (`defReg`, `cbVft`). `kChecks` has 28 entries = those 25 + the 3 `defVt` code addresses (269-272). Seven
  pointers share `void(__fastcall*)(void*, void*)` (defCtor 72, pinCtor[4] 73, cbCtor 78, nodeDtor 79); `bind`/`bindV`
  (75/76) share a type. Swapping same-type initializers compiles and silently calls the wrong engine function. The
  verifier checked all 28 addresses: the field comments, `kChecks` and the initializer (plus 269-272) agree, and every
  cast matches its field type.
- **Why it matters:** each engine address is written two or three times by hand.
- **Fix (behaviour-preserving):** one table `{va, checkBytes, &E.field}` or explicit member assignments
  (`E.defCtor = (decltype(E.defCtor))rt(0x006AE840);`, same machine code); keep "check all, then bind".
- **Refactor risk:** keep the return at 231 before `E` is set; keep `__fastcall` signatures exactly; data addresses
  0x01B3FE6C (80 B memcpy, 264), 0x01B3FE5C (12 B, 265), 0x02202C80, 0x01B3F800 are not signature-checked today.
- **Verification:** CORRECTED: 25 function pointers (22 members), not 24; "all 28 agree" re-checked and confirmed.

### GR-12 Pool/record walking hand-written ~15 times with slightly different meanings
- **Where:** SubNetwork list cho.hpp:358-360, 423-425, 557-559 (also octx.hpp:1171-1174); AssociationPool walk
  cho.hpp:564-574, 788-805, 820-831 (`octx::eachAssociation`, octx.hpp:621-637); HeaderLayout decode cho.hpp:452-456,
  615-630 (also `SnEdit` ctor, octx.hpp:443-445); child by name cho.hpp:459-463 (`kid`), 936-941 (`kids`), mer.hpp:192-195
  (`field`); loops at cho.hpp:246, 286, 319, 369, 390, 431, 587, 793, 820, 927 and mer.hpp:174, 183, 282, 490; type
  compare through an allocated vector `f.nameValues(r) == std::vector<std::string>{"Clip"}` at cho.hpp:248, 288, 374,
  392, 857, 1015 (also octx.hpp:264, 287).
- **Severity / category / class:** Med · duplication, readability · NEEDS-VERIFICATION
- **Evidence:** each copy has its own rules: depth (const30's first pass takes NodeName/SubNetworkRef at any depth,
  567-571; scene30 level 3 only, 823); which match wins (`kids` map keeps the last, 939; `kid()` the first, 461; time30's
  `isClip` the last, 374); index space (cho counts "SubNetwork"-named children only; `octx::redirectAssociations`
  counts every level-2 child, octx.hpp:646-648).
- **Why it matters:** the rules differ silently; a fix in one copy does not reach the others.
- **Fix (behaviour-preserving):** small query helpers on `oct::File` or in octx: `child(i, name, Policy)`, `str1`,
  `int1`, `nameIs(rec, "Clip")`, `subNetworks(f)`, `forEachAssociation(f, fn)`, `HeaderTree`.
- **Refactor risk:** each call site must keep its own depth, first/last and index-space rule; one unified helper changes
  edge cases.
- **Verification:** CONFIRMED.

### GR-13 `beaconMap30` has its own Lua scanner, weaker than `gameplay::luaTokens`
- **Where:** `native/di1compat/mission.hpp:210-243` (`skipQuoted`, `closeOf`, identifier/`=`/`==` test) vs
  gameplay.hpp:125-216 (`luaTokens` 125-155, `assignedAt` 157-161, `luaGlobalTable` 189-198, `luaTableEntries` 200-)
- **Severity / category / class:** Med · duplication, robustness · NEEDS-VERIFICATION
- **Evidence:** `closeOf` treats `--` as a comment to end of line only (220, 250); no `[[ ]]` long strings or `--[[ ]]`
  block comments; `luaTokens` handles both (133-140). The input is the 3.0 text `zonelist.lua` (the 1.0 one is bytecode).
- **Hot path?:** both configs (beacon10 default 1, not gated by serve10), once per `zonelist.lua` open: not hot.
- **Why it matters:** two Lua scanners with different coverage; the weaker one decides where tables end.
- **Fix (behaviour-preserving):** move the Lua lexer into its own small header used by both (avoids mission.hpp, included
  by mer.hpp, depending on gameplay.hpp).
- **Refactor risk:** identical only if the zone list has no long brackets/comments containing braces or quotes
  (UNVERIFIED; no text zonelist found in the extracts on this machine); diff with `octtest --beaconmap`.
- **Verification:** CORRECTED: gameplay.hpp range (125-216, not 124-197); frequency added.

### GR-14 Long functions with several jobs (below god-function size)
- **Where:** cho.hpp:419-529 (anim30, 111 lines), 915-1034 (layout30, 120), 354-416 (time30, 63); mission.hpp:208-290
  (beaconMap30, 83), 98-161 (taskLines30, 64)
- **Severity / category / class:** Med (anim30, layout30) / Low (others) · long function · NEEDS-VERIFICATION
- **Evidence:** anim30 defines lambdas `kid`, `str1`, `range` inside the per-SubNetwork loop (nesting 6 at 497-498);
  layout30 has 5 lambdas (`kids`, `entries`, `ival`, `copyAt`, `ref` at 972), rebuilds 3 pools and splices them.
- **Why it matters:** readability; each function mixes scanning, rule logic and record surgery.
- **Fix (behaviour-preserving):** lambdas -> named functions; layout30 -> buildAttachments, buildInstances, buildPlacements.
- **Refactor risk:** layout30 builds `att`, `inst`, `pl` in that order (name order, rule 1) before replacing pools
  later-first (1021-1028); anim30 builds all replacements before any edit (505) and edits last-first (515).
- **Verification:** CONFIRMED (lambda count corrected to 5; does not change the finding).

### GR-N1 anim30 patches `HeaderStrings` through an index that its own edits may have shifted
- **Where:** `native/di1compat/cho.hpp:434` (index captured), `515-520` (erase/insert per DataNode), `522-523` (patch)
- **Severity / category / class:** Med · correctness (latent heap corruption, hidden record-order assumption) ·
  BEHAVIOUR-CHANGE (identical on all shipped data)
- **Evidence:** `hs` is recorded during the scan (434); then each converted DataNode's children are erased and 5
  records inserted (518-519: 1.0 nodes have 3 children, so +2 records each); then
  `memcpy(&f.recs[hs].pay[ss.cw + 2 * (size_t)s], &a, 2);` (523) uses the old index. That is correct only when
  HeaderStrings comes before the DataNodePool in the SubNetwork. Verifier scan of the 1.0 install: 3,017 of 3,017
  SubNetworks in `.oct` files containing `Global.Time` have HeaderStrings before DataNodePool. Nothing checks it.
- **Hot path?:** code defaults: never; recommended: per served `.oct` (choAnim10); only matters for an unusual order.
- **Why it matters:** with HeaderStrings after the pool, the 2-byte write lands in another record's payload, possibly past
  its end (`std::string::operator[]` is unchecked): heap corruption, plus an unrenamed `Global.Time` header.
- **Fix (behaviour-preserving):** move `if (!here) continue;` and the patch (521-523) before the edit loop (515). `"AnimState"`
  is already in the name table by then (511's `nameScalar` added it), so `addName` returns the same index and the bytes
  are identical whenever HeaderStrings precedes the pool. Alternative: skip the SubNetwork when `hs > dp`.
- **Refactor risk:** keep `++touched`/`n += here` semantics; keep the `addName("AnimState")` call (it is a no-op today
  but documents the dependency).
- **Verification:** NEW (verified by reading and corpus scan).

### GR-05 cho passes insert into `f.recs` in place once per edit (measured: only a handful per file)
- **Where:** `native/di1compat/cho.hpp:412` (time30), `518-519` (anim30), `775`, `807` (const30), `840` (scene30),
  `1026-1027` (layout30)
- **Severity / category / class:** Low · CPU, reuse · NEEDS-VERIFICATION
- **Evidence:** `f.recs.insert(f.recs.begin() + at, ...)` moves every later record. octx.hpp:573-577 measured this pattern
  at 283 inserts per pass on in_city (~0.25 s) and added `octx::editPoolChildren` (578-618). **But the cho passes do few
  inserts** (verifier scan of the 1.0 install): in_city.oct (206,759 records, 283 SubNetworks) needs 0 time30 inserts,
  0 anim30 SubNetworks, 1 scene30 insert, plus layout30's 3 pool replacements; the worst world, ts_maintown.oct (94,979
  records), needs 2 + 2. Across 5,675 choreography/exploder `.oct` files the worst is ~34 edits x 21,479 records
  (intro_castleend). const30 never runs on worlds (di1compat.cpp:754).
- **Hot path?:** code defaults: never; recommended: per served `.oct` (worlds included, di1compat.cpp:747): a few record
  shifts per file, i.e. low single-digit ms at most.
- **Why it matters:** mostly consistency with the octx fix; no measurable load cost today.
- **Fix (behaviour-preserving):** optional: route time30/anim30/const30 bodies through `editPoolChildren` (order last to
  first) and collect scene30/AltNodeName/layout30 edits into one merge pass. Not worth the refactor risk for CPU alone.
- **Refactor risk:** rule 1 (same SubNetwork order) and rule 7: inside `editPoolChildren` callbacks `f.names` is empty and
  child records are moved out of `f` (use `g`); const30's `ordinal` and Association pass use whole-file indices (the
  second Association pass stays outside the per-SubNetwork callback). `editPoolChildren` takes whatever `kids` the caller
  lists (addAllCounts and redirectAssociations list every level-2 child); the cho ordinal counts only "SubNetwork"-named
  ones, so pass a consistent list (GR-12).
- **Verification:** CORRECTED: severity Med -> Low (insert counts measured on the corpus; the report's "per SubNetwork"
  cost does not occur in worlds); clarified what `editPoolChildren` puts in `kids`; added the name-table swap risk.

### GR-09 Native thunks and registration could let C++ exceptions unwind through engine frames
- **Where:** `native/di1compat/mernode.hpp:149-153` (`handler`), `159-179` (`describe`, `defCreate`), `98-109` (`param`,
  `*s = v;`), `228-287` (`registerAll` allocates: 255, 257, 273-274); called from `hkMerRegistry`
  (di1compat.cpp:2705-2722) with no try/catch
- **Severity / category / class:** Low · runtime safety · BEHAVIOUR-CHANGE (exception path only)
- **Evidence:** `c.spec.impulse(*(uint8_t**)(ctx + 4), c.inPin[K], payload);` calls user code from engine code. The only
  impulse today is `probeImpulse` -> `mernode::fire` -> engine (di1compat.cpp:2704), which does not throw; `describe` and
  `defCreate` call only engine functions and unchecked `vector::operator[]`. Only `std::bad_alloc` (registration-time
  `std::string`/`vector` growth, `mernode::add`'s Spec copy) can throw today. The project rule (di1compat.cpp:3714):
  "never unwind into engine code".
- **Why it matters:** a future Spec impulse that throws would unwind through engine frames (silent fail-fast).
- **Fix (behaviour-preserving):** `try { ... } catch (...) { log; }` in `handler<C,K>`, `describe`, `defCreate` (return
  nullptr) and around the `registerAll` call. Only the exception path changes.
- **Refactor risk:** don't mark thunks `noexcept` without a catch (std::terminate); a catch in `describe` leaves a
  partially bound definition - decide what the engine should see.
- **Verification:** CORRECTED: severity Med -> Low (nothing in the thunks can throw today except bad_alloc at boot).

### GR-10 Exact copies of `octx` record builders
- **Where:** `native/di1compat/mer.hpp:105-112` (`mer::blob`) == octx.hpp:34-41 (`octx::blobRec`); `cho.hpp:907-912`
  (`cho::keyed`) == octx.hpp:373-378 (`octx::keyedNode`), same `addName(n)` then `addName(to_string(key))` order;
  `cho.hpp:347-352` (`floatRec`) has the same record encoding as the inline `SCALAR|FLOAT<<3` builders at octx.hpp:273
  (which copies raw payload bytes) and 510
- **Severity / category / class:** Low · duplication · SAFE-REFACTOR
- **Evidence:** byte-for-byte identical bodies (checked).
- **Why it matters:** record-type bit encodings live in several places; small drift risk (the encoding is fixed by the
  format).
- **Fix (behaviour-preserving):** use the octx versions (mer.hpp would include octx.hpp: no cycle, octx.hpp includes only
  <cmath>, <map>, <set>, oct.hpp) or move them into `oct::File`. Bytes identical.
- **Refactor risk:** mer.hpp is included by mernode.hpp and octtest; including octx.hpp there only adds compile time.
- **Verification:** CORRECTED: severity Med -> Low; octx line range and the "==" for floatRec qualified.

### GR-11 `lookup2` implemented twice; runtime engine hash assumed equal to it
- **Where:** `native/di1compat/mer.hpp:36-52` vs dct.hpp:19-36; `mernode.hpp:101` (`E.hash(name)`, engine FUN_018261F0) vs
  `mer.hpp:407` (`lookup2(pn)`)
- **Severity / category / class:** Low · duplication, hidden coupling · SAFE-REFACTOR
- **Evidence:** the two implementations give the same result (zero-padded tail vs partial words, length added to c,
  same mix). `mernode::param` would look up descriptions written with `mer::lookup2` using the engine's hash; nothing
  checks they agree. dct.hpp documents its copy as the engine's FUN_0040b900; FUN_018261F0 is not documented as lookup2.
- **Why it matters:** a frozen, well-known algorithm (low drift risk); the coupling is in code with no callers (GR-25).
- **Fix (behaviour-preserving):** one `lookup2(const void*, size_t, uint32_t seed)`; optionally a boot check
  `E.hash("Type") == mer::lookup2("Type")` (adds a log line and an engine call: not behaviour-neutral, decide separately).
- **Refactor risk:** keep seed 0 for Meridian and SEED30 for dct. Whether FUN_018261F0 == lookup2 is UNVERIFIED.
- **Verification:** CORRECTED: severity Med -> Low.

### GR-15 Meridian graph parsing duplicated with `gameplay::activityGraph30`; the uuid link branch is dead in the corpus
- **Where:** node collection `native/di1compat/mer.hpp:169-177` vs gameplay.hpp:511-517; `isInt` mer.hpp:196 vs
  gameplay.hpp:524; LinksBuffer decode mer.hpp:292-331 vs gameplay.hpp:541-560; uuid branch mer.hpp:301-306, 313-317
- **Severity / category / class:** Low · duplication, dead code, CPU · NEEDS-VERIFICATION
- **Evidence:** gameplay.hpp:550 `// the global-index form (all 1.0 graphs, linkcodec.py)`; **verifier scan: 0 of 4,464 1.0
  `.mer` members have a uuid-form LinksBuffer** (byte 4 != 0). `byUuid` is rebuilt for every uuid-form buffer by
  scanning every node's block: O(buffers x nodes), never reached with shipped data.
- **Hot path?:** code defaults: never; recommended: once per served `.mer`; the uuid branch never runs on shipped data.
- **Why it matters:** two parsers of the same graph; dead branch to maintain.
- **Fix (behaviour-preserving):** shared `mer::GraphView` (nodes, namespaces, decoded link buffers); build `byUuid` lazily
  once. Deleting the uuid branch changes behaviour only for uuid-form (non-shipped, e.g. modded) graphs: user decision.
- **Refactor risk:** `activityGraph30` runs before `transcode` on the same file and changes it (di1compat.cpp:937-941),
  so the view must be rebuilt after it.
- **Verification:** CORRECTED: "probably dead" -> dead for the shipped 1.0 corpus (scanned); gameplay line range.

### GR-16 `[0]` on `nameValues`/`ints` results without checking the record kind
- **Where:** `native/di1compat/mer.hpp:175`, `184`, `284`, `285`, `380`, `381`, `463`, `491`
- **Severity / category / class:** Low · robustness · BEHAVIOUR-CHANGE (malformed input only)
- **Evidence:** `n.type = f.nameValues(f.recs[j])[0];` - `nameValues` returns an empty vector for a non-NAME, non-keyed
  record (oct.hpp:169-177); `ints` returns empty for a NODE record. In a release build `[0]` on an empty vector reads
  through a null pointer: an AV that `catch (...)` (/EHsc) does not catch -> crash.
- **Why it matters:** a malformed graph crashes the game instead of being served unchanged.
- **Fix (behaviour-preserving):** `str1`/`int1` helpers returning a default. Identical on well-formed data.
- **Refactor risk:** for keyed NAME records `[0]` is the key, not the value; keep that if a helper is introduced.
- **Verification:** CONFIRMED (crash mode added).

### GR-17 LinksBuffer size check can wrap in 32-bit arithmetic
- **Where:** `native/di1compat/mer.hpp:300`
- **Severity / category / class:** Low · robustness · BEHAVIOUR-CHANGE (crafted input only)
- **Evidence:** `if (len != 5 + n * (uu ? 34u : 6u))`: on Win32 both sides are 32-bit, so e.g. n = m + 2^31 matches
  len = 5 + 6m; the loop at 310-331 then reads `d + 5 + 2*k` far past the payload: an AV that the `catch` at
  di1compat.cpp:3714-3720 does not catch (/EHsc). gameplay.hpp:551 already uses `5 + 6ull * n`.
- **Fix (behaviour-preserving):** compare `5 + (uint64_t)n * (uu ? 34 : 6)`. Identical for valid sizes.
- **Verification:** CONFIRMED.

### GR-18 Allocation-heavy access and whole-payload copies in per-record loops
- **Where:** `native/di1compat/cho.hpp:130-134` (`cho::first` allocates via `ints()`), used by const30's `hdr` lambda
  (690-694); the `std::vector<std::string>{"Clip"}` temporaries (GR-12); `cho.hpp:379`, `486` (whole ClipDataBlock
  copied to read 8 bytes); `377`, `439` (NetworkBuffer copied for every SubNetwork, before the 398 / 441-447 skips)
- **Severity / category / class:** Low · CPU · NEEDS-VERIFICATION
- **Evidence:** `hdr` is called twice per DataNode/ProcessorNode per fix-point iteration (not ~3); time30 and anim30
  each copy every SubNetwork's NetworkBuffer: two copies per SubNetwork per served `.oct` (283 SubNetworks in in_city).
- **Hot path?:** code defaults: never; recommended: per served `.oct`; small next to GR-04.
- **Why it matters:** avoidable allocations and copies on the asset-open path.
- **Fix (behaviour-preserving):** non-allocating accessors (`int1`, `nameIs`), `std::string_view` over payloads.
- **Refactor risk:** `ints()` accounts for the key (oct.hpp:157); the ad-hoc `pay.substr(cw)` does not - keep each site's
  exact offset. time30 does not check that ClipDataBlock is BYTE kind (378); clips30 and anim30 do (299, 485).
- **Verification:** CORRECTED: call count per node (2, not ~3).

### GR-19 `const30` clash fix-point and string lookups grow quadratically
- **Where:** `native/di1compat/cho.hpp:687-700`, `632-636` (`pathOf` builds by repeated prefixing), `760-765` (linear
  `std::find` over header strings per new header)
- **Severity / category / class:** Low · CPU · NEEDS-VERIFICATION
- **Evidence:** each iteration rebuilds `var` (all variable paths) and runs `any_of` for every Static node:
  O(iterations x statics x variables x path length).
- **Hot path?:** code defaults: never; recommended: per processor SubNetwork of each served choreography/exploder.
- **Fix (behaviour-preserving):** compute `pathOf` once per header; sort `var` or use a prefix set; a hash index for
  `strs` that keeps the FIRST match and is updated on push_back, as `SnEdit::hsIndex` does (octx.hpp:474-488).
- **Refactor risk:** the logic is string-based (two headers with the same path count as equal); don't switch to header-id
  tree logic unless duplicate paths are proven absent.
- **Verification:** CONFIRMED.

### GR-20 `mer::transcode` per-node set allocations and a deep copy of every record
- **Where:** `native/di1compat/mer.hpp:210` (one std::set per node), `360`, `369` (3 sets per node), `373` (string copy
  per field), `341`, `346`, `349`, `372`, `469` (every record copied; `f.recs = std::move(outRecs)` at 474)
- **Severity / category / class:** Low · CPU · SAFE-REFACTOR
- **Evidence:** MSVC `std::set`/`std::map` allocate a head node even when empty, so `portRefs(nodes.size())` costs one
  allocation per node. Measured whole transcode on in_city.mer (stand-in table): 17 ms.
- **Hot path?:** code defaults: never; recommended: once per served `.mer`.
- **Fix (behaviour-preserving):** `outRecs.reserve(...)`; move the tail records at 469 (nothing reads them afterwards);
  keep string comparisons (rule 2).
- **Refactor risk:** `field()` (461, 463) still reads node records after the main loop - don't move node records out.
- **Verification:** CONFIRMED (timing added).

### GR-21 `table10` allocates one vector per clip component
- **Where:** `native/di1compat/cho.hpp:137`, `150-156`
- **Severity / category / class:** Low · CPU · NEEDS-VERIFICATION
- **Evidence:** `std::vector<std::vector<uint16_t>> comps;` with one `std::vector<uint16_t> w;` per component, no reserve;
  runs per ClipDataBlock, per output, per component; the motion corpus has ~285k ids (cho.hpp:34).
- **Hot path?:** code defaults: never; recommended: per served `.oct` with clip blocks (clip10).
- **Fix (behaviour-preserving):** flat `std::vector<uint16_t>` + per-output component count; `table30` needs only the
  counts (7-way split) and the words in order.
- **Refactor risk:** split must count components, not words; an output with 0 components still writes one head
  (do/while at 171). Check with `octtest --cho-clips` / `--clip-ids` plus a byte diff.
- **Verification:** CONFIRMED.

### GR-22 Stale or inaccurate comments
- **Where:** `native/di1compat/cho.hpp:2`, `549`, `741`, `73-74`; `mernode.hpp:3`
- **Severity / category / class:** Low · readability · DOC-ONLY
- **Evidence:**
  - (a) cho.hpp:2 says "Three load-time transforms"; the header documents five (clips30 at 4, time30 48, anim30 58,
    scene30 78, layout30 93) and the file has eight passes (+ links30, const30, banm30).
  - (b) cho.hpp:549 and 741 say constant headers are numbered "breadth first"; the loop at 745-750 numbers them by trie
    id (creation) order. The report's example only works when the 1.0 header numbers put A/m/x before B/y. A robust
    example, with BFS-numbered input A1 B2 m3 n4 x5 y6 and Static nodes at x and y (moving = {5, 6}): trie ids
    CA1 m2 x3 CB4 n5 y6, the code numbers CA1 CB2 m3 x4 n5 y6, true breadth first is CA1 CB2 m3 n4 x5 y6. Orders differ
    only when some Static path is 3+ headers deep; depth-2 paths always come out breadth first. The layout stays valid
    (parents before children). Which order 3.0 uses is UNVERIFIED; `octtest --cho-const` checks sets and paths, not
    the numbering.
  - (c) cho.hpp:73-74 says "Type and Data replaced (Header kept)"; line 518 erases every child and 511 re-inserts a copy
    of Header first. 1.0 Global.Time nodes are {Header, Type Float, Data 0.0} (68), so the result is the same today.
  - (d) mernode.hpp:3 says "Included by di1compat.cpp after rt()/logf()"; it is included at di1compat.cpp:29, before
    `logf` (70) and `rt` (106); registerAll receives both as template parameters (228).
- **Fix (behaviour-preserving):** correct the comments. Do NOT switch numbering to real breadth first (changes output).
- **Verification:** CORRECTED: (b) example replaced (the report's example does not arise from BFS-numbered input) and
  scope of the difference stated.

### GR-23 `layout30` relies on C++17 evaluation order
- **Where:** `native/di1compat/cho.hpp:960`
- **Severity / category / class:** Low · fragility · SAFE-REFACTOR
- **Evidence:** `remap[(int)i] = (int)remap.size();` gives 0-based ranks only because C++17 sequences the right operand of
  built-in `=` first (P0145); unspecified under C++14. The project builds as C++17 (CMakeLists.txt).
- **Fix (behaviour-preserving):** `int next = (int)remap.size(); remap[(int)i] = next;`
- **Verification:** CONFIRMED.

### GR-24 Unnamed magic numbers, hard-coded record levels, clashing names
- **Where:** cho clip format: 0x14, 0x10, offsets 8/10/12, 0xf, 0xf000/0x1000/0x2000, 0x1fff, <<13, 7, 0xee
  (`cho.hpp:150-152`, `169-174`, `194-196`, `219`, `226`); layout flags `cho.hpp:992-995`, `1005`, `1010`, `1015`; mer: 6
  (= len "_value", `mer.hpp:246`, `365`, `391`), 23 (= len "MissionPresentationList", 400), 64 (221), link code >> 10 /
  0x3ff (324-325), "\x00\x03\x00" (333); mission: `key.size() - 3` (`mission.hpp:190`)
- **Severity / category / class:** Low · readability · SAFE-REFACTOR
- **Evidence:** absolute record levels 2-6 throughout cho while anim30's `kid` uses relative levels. Name clashes:
  `oct::Spec` vs `mernode::Spec`, `oct::Kind` vs `mernode::Kind`, `cho::rd/wr` (bounds-checked, width param; `wr`
  appends) vs `sx::rd/wr` (unchecked 4-byte; `sx::wr` overwrites in place at an offset) - same names, different
  semantics.
- **Fix (behaviour-preserving):** named constexpr values; `endsWith`/`startsWith` helpers (di1compat.cpp:472 has one, but
  defined after these headers are included).
- **Verification:** CORRECTED: added that `cho::wr` appends while `sx::wr` overwrites.

### GR-25 `mernode`: offset 0x14 means two different things; unused API
- **Where:** `native/di1compat/mernode.hpp:110`, `120`, `260` (0x14 = start of state) and `132` (0x14 = destructor `this`
  adjustment, per comment line 13); `251` (0x28 definition base size), `91` (+0x10), `101` (& 0xff), `102` (d[4] = type),
  `267` (vtable slot 14), `246` (`pinSize`, indexed by `Kind` in the same order as `E.pinCtor`, 177)
- **Severity / category / class:** Low · readability, dead code · SAFE-REFACTOR
- **Evidence:** `param()` (98-109) and `state()` (110) have no callers (grep over native/di1compat).
- **Fix (behaviour-preserving):** named constants (kNodeBaseSize, kDtorThisAdjust); `static_assert(sizeof(void*) == 4)`;
  size `pinSize` by the Kind count; mark param/state reserved for MER-6.
- **Verification:** CONFIRMED.

### GR-26 Lifetime of what `mernode` hands to the engine
- **Where:** `native/di1compat/mernode.hpp:49-61` (`Rt`, `rts()`), `273-276`
- **Severity / category / class:** Low · runtime lifetime · DOC-ONLY
- **Evidence:** engine objects point into `Rt::vbt`, `mainVt`, `cbVt`, `defVt` (115, 118-119, 176); `cls`/`def` kept because
  "the registries may hold the char*" (51). `std::array<Rt,8>` (function-local static) has a non-trivial destructor, so
  those strings and vectors are freed during the DLL's static destruction. A second `registerAll` would reassign
  `cls`/`def` and re-register every class (hkMerRegistry runs once at boot, di1compat.cpp:2696).
- **Why it matters:** address stability is a hard requirement that nothing states.
- **Fix (behaviour-preserving):** document it; optionally make the storage immortal (leaked). Identical while the game runs.
- **Refactor risk:** turning `rts()` into a growable container would leave the engine with dangling vtable pointers.
- **Verification:** UNVERIFIED: whether the engine touches these at process exit (after the exe's own atexit
  destructors, before DLL_PROCESS_DETACH) would be settled by a breakpoint/log on exit in-game.

### GR-27 `behaviour30` searches to end-of-file for every matching record
- **Where:** `native/di1compat/mission.hpp:184`
- **Severity / category / class:** Low · CPU · NEEDS-VERIFICATION
- **Evidence:** `auto has = [&](const char* s) { size_t h = t.find(s, rs ...); return h < end; };` - two scans per
  `MissionBehavior = 2,` record, each to EOF when the string is absent: O(records x file length).
- **Hot path?:** code defaults: never; recommended: once per served `__missiongraph.lua`.
- **Fix (behaviour-preserving):** search only the window `[rs, min(size, end - 1 + strlen(s)))`: finds exactly the matches
  that start before `end`, as `h < end` does today.
- **Refactor risk:** a naive "fully inside [rs, end)" search drops matches that start before `end` and end after it.
  Check with octtest's behaviour mode.
- **Verification:** CONFIRMED.

### GR-28 `taskLines30` scans to end-of-file; Parms search duplicated
- **Where:** `native/di1compat/mission.hpp:114`; `79` vs `100`
- **Severity / category / class:** Low · robustness, duplication · SAFE-REFACTOR (helper); bounding the scan would be a
  BEHAVIOUR-CHANGE
- **Evidence:** `for (size_t a = t.find("\tTasks = "), next; a != npos && a < t.size(); a = next)` walks every later line;
  any later `Data = {` block (e.g. Presentations[].Data) is parsed as an eval, and `field()` strips all leading tabs, so
  nested lines count. Output changes only for `Eval_LogicProgressOverride` + ProgressStr or
  `UseGeneratedProgressStringNew = true` inside such a block. Runs only for missions with
  `UseMissionGeneratedProgressStringNew = false` (3 Toy Story missions).
- **Fix (behaviour-preserving):** shared `parmsBlock(t)` helper for 79/100. Bounding the scan to the Tasks table is a
  behaviour change if any eval sits outside it; check with `octtest --mission`.
- **Verification:** CONFIRMED (mechanism); "harmless today" depends on the 3 missions' text (settle with `--mission` diffs).

### GR-29 `mission.hpp` holds an engine address called with no signature check
- **Where:** `native/di1compat/mission.hpp:31-34` (`{"...", 0x0177A8B0, "..."}`); called at di1compat.cpp:2711
  (`((RegFn)rt(a.templateVa))(a.name)`)
- **Severity / category / class:** Low · coupling · BEHAVIOUR-CHANGE (adding a refusing check); moving the address is
  SAFE-REFACTOR
- **Evidence:** runtime ABI data lives in a translation header also compiled into octtest (via mer.hpp); every other
  engine call here is checked first (`hook()`, `kChecks`). Runs at boot in both configs when aliases10 (default 1).
- **Fix (behaviour-preserving):** move the address next to the runtime registration; adding a byte check changes failure
  handling on a different exe (user decision).
- **Verification:** CONFIRMED.

### GR-30 `Table::load` does not strip `\r`
- **Where:** `native/di1compat/mer.hpp:71-81`
- **Severity / category / class:** Low · robustness · BEHAVIOUR-CHANGE (CRLF input only)
- **Evidence:** `std::getline` keeps `\r`; the file is read in binary mode (di1compat.cpp:4144 "rb"); mertable.py:90 writes
  `newline='\n'`, so fine today; a CRLF table would silently match no names and drop every `A` type
  (`registered.empty()` catches only an empty file). taskLines30 does strip `\r` (105).
- **Fix (behaviour-preserving):** strip a trailing `\r`. Identical on LF files.
- **Verification:** CONFIRMED.

### GR-31 `clips30`: hidden state machine and inconsistent template choice
- **Where:** `native/di1compat/cho.hpp:276-305` (`skip`/`pm` carried across records), `283` + `286-290` (two extra scans
  per SubNetwork), `242-258`
- **Severity / category / class:** Low · readability, subtle inconsistency · NEEDS-VERIFICATION
- **Evidence:** any record named ClipDataBlock anywhere in the file uses the state of the last level-2 SubNetwork seen.
  `clipPorts` takes a ConnectionTemplate only under a LayerBind (250); the `motion()` argument is the first level-5
  ConnectionTemplate anywhere in the SubNetwork (289), which could be a BaseDataNodeLinks one (UNVERIFIED whether any Clip
  SubNetwork has one). When `pm` is set, the 286 loop does not run at all, so `skip` stays false.
- **Fix (behaviour-preserving):** compute a per-SubNetwork descriptor once. Keep the "first level-5" rule for `motion()`.
- **Verification:** CONFIRMED.

### GR-32 Pass order written in several places; octtest pipelines drift from the plugin's
- **Where:** di1compat.cpp:679-707, 734-757 vs octtest.cpp:1062-1066 + 1097 (`--cho-const`), 1602-1627 (`--cho-file`),
  2953-2971 (`--oct ... cho`)
- **Severity / category / class:** Low · coupling, test fidelity · NEEDS-VERIFICATION
- **Evidence:** const30 must run before `octx::addAllCounts` (comment di1compat.cpp:751-752); links30 feeds
  `octx::convertLink` (cho.hpp:309-311). The `--oct` lap pipeline differs from `transcodeOct` in order and content (see
  rule 6: addAssetPool and curves30 positions, no render/navmesh/surfaces, `clips30(f, nullptr)` with ids off, no links30,
  unconditional camera30/const30); `--cho-file` passes `motion = true` for all templates and never runs const30.
- **Why it matters:** timings and outputs from octtest are not the plugin's; a refactor checked only with octtest can
  still change plugin bytes.
- **Fix (behaviour-preserving):** one `cho::Options` + `cho::transcodeAll(f, opts, notes)` used by both; octtest builds
  options that reproduce its current choices (switching octtest to the plugin's choices changes test results: decide
  separately). Keep exact order and per-pass gating (const30 only for `choreographies/` and `exploders/` paths).
- **Verification:** CORRECTED: the pipeline differences are larger than "skips links30, motion = nullptr".

### GR-33 Unchecked 16-bit truncation in clip re-packing
- **Where:** `native/di1compat/cho.hpp:173` (`wr(t, (uint32_t)heads.size(), 2)`), `224`
  (`wr(t, (uint32_t)(t.size() + 2 + st.size()), 2)`)
- **Severity / category / class:** Low · robustness · BEHAVIOUR-CHANGE
- **Fix (behaviour-preserving):** return false when > 0xFFFF (only affects outputs that would be corrupt anyway).
- **Verification:** CONFIRMED.

### GR-34 Two ways of rewriting `HeaderStrings`
- **Where:** `native/di1compat/cho.hpp:522-523` (raw memcpy into `pay[cw + 2*s]`) vs `cho.hpp:768` (`octx::setNames`)
- **Severity / category / class:** Low · consistency · SAFE-REFACTOR (wrap the patch in a named helper)
- **Evidence:** the byte patch assumes a non-keyed NAME record (checked at 443), 2-byte elements, little-endian.
  `setNames` (octx.hpp:349-359) rebuilds the record type (always LIST) and re-adds every string through `addName`
  (first-wins index), so it is NOT byte-identical in every case.
- **Fix (behaviour-preserving):** keep the patch, wrap it in a named helper (see also GR-N1 for where it must run).
- **Verification:** CONFIRMED.

### GR-35 Lookups that mutate
- **Where:** `native/di1compat/cho.hpp:682` (`named[ordinal].count(...)` inserts an empty set); `mer.hpp:92`
  (`T.classes[a.twin]` creates a class, GR-03); `cho.hpp:959-1015` (`ctx[i]["..."]`, `a["..."]`, `c["..."]`, `p["..."]`
  after `count`; safe today)
- **Severity / category / class:** Low · readability · SAFE-REFACTOR
- **Fix (behaviour-preserving):** use `find` where the insert has no effect; keep mer.hpp:92 as is or make its effect
  explicit.
- **Verification:** CONFIRMED.

### GR-36 `g_mer` changed by the boot hook without synchronisation
- **Where:** writers di1compat.cpp:2714, 2719 (-> mernode.hpp:284); reader `mer::transcode` on asset-open threads
  (di1compat.cpp:941)
- **Severity / category / class:** Low · thread safety · NEEDS-VERIFICATION
- **Evidence:** `std::set`/`std::map` inserts at boot vs concurrent lookups; safe only if the registry hook finishes before
  any `.mer` is served.
- **Fix (behaviour-preserving):** build the final table then publish it (atomic pointer swap). No change in non-racy runs;
  a transcode that today would see a half-updated table would see the old one instead.
- **Verification:** UNVERIFIED: settle by logging thread id/time of hkMerRegistry and of the first served `.mer` in-game.

### GR-37 `ports10.hpp` generated-code shape (fix in the generator portmap.py)
- **Where:** `native/di1compat/ports10.hpp:10`, `168-202`; portmap.py:203, 207, 210-215; consumers cho.hpp:252, 323
- **Severity / category / class:** Low · generated-code shape · SAFE-REFACTOR
- **Evidence:** `n10` is written separately from the array length, but both come from the same `len(to30)` in one
  generator loop (203, 207), so they cannot drift unless the file is hand-edited (its header forbids that). `_stricmp`
  ignores case but does not treat `\` and `/` as equal; callers pass raw record strings (backslashes UNVERIFIED). Linear
  search over 25 entries per ConnectionTemplate is cheap.
- **Fix (behaviour-preserving):** optionally generate `n10` as `std::size(kPortsN)`. Do not hand-edit ports10.hpp.
- **Verification:** CORRECTED: the drift risk exists only for hand edits.

### GR-38 Filesystem probes per motion-clip SubNetwork (outside this slice; triggered by the clips30 callback)
- **Where:** di1compat.cpp:672-677 (`has30`: 2x `GetFileAttributesA`), 684-686
- **Severity / category / class:** Low · CPU, I/O on the asset-open path · NEEDS-VERIFICATION
- **Evidence:** `motion()` is called once per Type 'Clip' SubNetwork naming a ConnectionTemplate when no ports10 map applies
  (cho.hpp:291-293).
- **Hot path?:** code defaults: never; recommended: per served motion-clip `.oct` (clip10, clip10Motions default 1).
- **Fix (behaviour-preserving):** cache results per directory behind a mutex (asset opens run on several threads);
  identical as long as the install does not change at runtime.
- **Verification:** CONFIRMED.

### GR-39 `mer::transcode` rewrites links only in records after the node pool
- **Where:** `native/di1compat/mer.hpp:341` (records before the pool copied as they are), `468-473` (`newLinks` applied
  only for `i >= poolEnd`)
- **Severity / category / class:** Low · hidden assumption · BEHAVIOUR-CHANGE (non-shipped layouts only)
- **Evidence:** if MeridianLinkPool came first, its LinksBuffer records would be copied in the 1.0 global-index form (which
  3.0 does not read) while NumIn/OutLinks and Stats were still adjusted. **Verifier scan: 0 of 4,464 1.0 `.mer` members
  put the link pool before the node pool.**
- **Fix (behaviour-preserving):** apply `newLinks` by record index in both copy loops. Identical for node-pool-first files.
- **Verification:** CORRECTED: "silently dropped" -> "left in the 1.0 form"; unreachable in the shipped corpus.

## Inventory
All function and table line ranges below were checked mechanically (awk scan of top-level definitions); every range in
the original report matched.

### Functions and structs
| File | Item | Lines | Notes |
|---|---|---|---|
| cho.hpp | rd / wr / first | 124-127 / 128 / 130-134 | checked LE read (wraps on Win32, GR-06); append; first int (allocates) |
| cho.hpp | ClipOut / table10 / table30 | 137 / 140-163 / 165-179 | 1.0 output table parse; 3.0 encode, 7-way split |
| cho.hpp | clipBlock30 | 183-238 (56) | "out untouched" on false |
| cho.hpp | clipPorts / clips30 / links30 | 242-258 / 273-307 / 312-344 | |
| cho.hpp | floatRec / time30 | 347-352 / 354-416 (63) | |
| cho.hpp | anim30 | 419-529 (111) | GR-14, GR-N1 |
| cho.hpp | const30 | 553-810 (258) | GR-02 |
| cho.hpp | scene30 / banm30 | 814-844 / 852-868 | |
| cho.hpp | Aliases (add, resolve) | 876-904 | mutex-guarded Cho DataMap aliases |
| cho.hpp | keyed / layout30 | 907-912 / 915-1034 (120) | keyed == octx::keyedNode |
| ports10.hpp | PortMap / kDropPort / kPorts0..24 / kPortMaps / portMap | 10 / 11 / 14-166 / 168-194 / 197-202 | generated |
| mer.hpp | lookup2 / T_* / Class / Table (+load 65-84) | 36-52 / 54 / 55 / 56-85 | |
| mer.hpp | addAliases / isGeneric / blob / encode / typeOf / Stats | 88-96 / 98-103 / 105-112 / 115-144 / 146-155 / 157 | |
| mer.hpp | transcode | 160-477 (318) | GR-01 |
| mer.hpp | mtc | 482-509 (28) | GR-04 |
| mernode.hpp | Kind, Pin, Param, Impulse, Spec / kMax, kMaxIn / Rt / rts(), count() | 42-46 / 48 / 49-59 / 60-61 | |
| mernode.hpp | Engine / E | 63-88 / 89 | GR-08 |
| mernode.hpp | defOf / fire / param / state | 91 / 94 / 98-109 / 110 | param, state: no callers |
| mernode.hpp | construct, getSize, createInstance, cbDelete, nodeCreate, factory | 112-148 | engine-called thunks |
| mernode.hpp | handler / handlerAt / describe / defCreate | 149-153 / 154-158 / 159-169 / 170-179 | engine-called thunks |
| mernode.hpp | Thunks, thunksOf, thunksAt / Check, kChecks / bytesMatch / addToTable | 181-190 / 193-204 / 206-215 / 218-224 | |
| mernode.hpp | registerAll / add | 228-287 (60) / 289-294 | |
| mission.hpp | Alias, kAliases / presentEvent30 / luaEvents30 / progress30 | 31-34 / 36-40 / 43-59 / 77-85 | |
| mission.hpp | taskLines30 / behaviour30 / beaconMap30 | 98-161 (64) / 176-194 / 208-290 (83) | |

### Hooks and engine bindings in this area
| Name | Static VA | Where | Gate | Notes |
|---|---|---|---|---|
| hook "Meridian class registry" -> hkMerRegistry | 0x0178E330 (prologue `6A006A0068????????68????????`) | di1compat.cpp:4268-4269, 2705-2722 | `(aliases10 || native10) && g_merOk`; not serve10 | runs once at boot |
| alias registration template (direct call, unchecked) | 0x0177A8B0 | mission.hpp:33; di1compat.cpp:2711 | aliases10 | GR-29 |
| `mernode::E` function pointers (25) + `defVt` (3) | see mernode.hpp:64-85, 269-272 | mernode.hpp:232-245 | native10, all 28 kChecks match | GR-08 |
| unchecked data addresses | 0x02202C80, 0x01B3F800, 0x01B3FE6C (80 B), 0x01B3FE5C (12 B) | mernode.hpp:245, 264-265 | native10 | |

### Generic helpers (for cross-file duplicate matching)
- `cho::rd(const std::string&, size_t o, int w)` cho.hpp:124 - checked LE read (wraps on Win32, GR-06). Similar:
  gen30.hpp:42-43 u32/u16 (checked, return 0; same `o + 4 <= size` pattern); unchecked: btd.hpp:15, dct.hpp:40,
  vfx.hpp:30-33, sx.hpp:38-39 (`sx::rd/wr`: unchecked, and `sx::wr` overwrites in place).
- `cho::wr` cho.hpp:128 - alias of `oct::File::put` (append).
- `cho::first(const oct::File&, const oct::Rec&, int)` cho.hpp:130 - first int of a record; used heavily by octtest.
- `cho::floatRec` cho.hpp:347 - same encoding as octx.hpp:273, 510.
- `cho::keyed` cho.hpp:907 - identical to `octx::keyedNode` (octx.hpp:373-378).
- lambdas `kid`/`str1` cho.hpp:459, 464 - octtest has its own `anim::kid`/`anim::str1` (octtest.cpp:701, 707);
  `kids`/`entries`/`ival`/`copyAt`/`ref` cho.hpp:936, 942, 949, 962, 972; `hdr`/`pathOf` cho.hpp:631, 632.
- lowercase (+ `\`->`/`) loops inline at cho.hpp:885 (+sep), 990; mission.hpp:189 - same as di1compat.cpp `norm()`
  (478-481) and read10 (398), bent.hpp:39, `npc::lower` (npc.hpp:361), audio.hpp:55, sx.hpp:801, gen30.hpp:268, 273.
- `cho::portMap` ports10.hpp:197 - `_stricmp` linear lookup.
- `mer::lookup2(const std::string&, uint32_t seed=0)` mer.hpp:36 - same result as `dct::lookup2` (dct.hpp:19).
- `mer::blob` mer.hpp:105 - identical to `octx::blobRec` (octx.hpp:34).
- lambdas `field`/`isInt`/`pinOf` mer.hpp:192, 196, 254 - `isInt` duplicates gameplay.hpp:524.
- `_value` suffix test inline mer.hpp:246, 365, 391 - di1compat.cpp:472 has `endsWith(const char*, const char*)`.
- `mission::presentEvent30(int)` mission.hpp:36 - also used by mer.hpp:401.
- lambdas `lineAt`/`field` mission.hpp:104, 106; `skipQuoted`/`closeOf`/`trim`/`isStr`/`isNum` mission.hpp:210-232 -
  overlap gameplay.hpp:125-216 (`luaTokens`, `assignedAt`, `luaGlobalTable`, `luaTableEntries`).
- `mernode::bytesMatch(uintptr_t, const char* hex)` mernode.hpp:206 - duplicates the prologue compare in di1compat.cpp
  `hook()` (~113-119).
- `mernode::addToTable(mer::Table&, const Spec&)` mernode.hpp:218 - same shape as `mer::addAliases` (mer.hpp:88).

### Namespace-level globals and statics
| Name | Where | Written by | Notes |
|---|---|---|---|
| `cho::kDropPort`, `kPorts0..24`, `kPortMaps` | ports10.hpp:11, 14-166, 168 | generated, static const | |
| `staticInputs30` (function-local) | cho.hpp:654 | static const std::set (33 entries) | |
| `g_choAlias` (`cho::Aliases`) | di1compat.cpp:391 | `add` from the DataMap rewrite (1086) | mutex inside; read by read10 (401) |
| `types`, `g`, `subTypes` (function-local) | mer.hpp:70, 100, 207 | static const | |
| `g_mer` (`mer::Table`) | di1compat.cpp:587 | load + flags (4158-4164), addAliases (2714), registerAll -> addToTable (2719, mernode.hpp:284) | passed explicitly; contents depend on hook timing (GR-03, GR-36) |
| `g_merOk` | di1compat.cpp:588 | 4158 | gates .mer serving and the registry hook |
| `mission::kAliases` | mission.hpp:32 | static const (internal linkage per TU) | |
| `map`, `key`, `line` (function-local) | mission.hpp:38, 45, 178, 236 | static const | |
| `rts()` storage | mernode.hpp:60 | add (292), registerAll (249-280) | static `std::array<Rt,8>`; must not move (rule 5) |
| `count()` storage | mernode.hpp:61 | add (293) | |
| `mernode::E` | mernode.hpp:89 | registerAll (232) | hidden input of every thunk, fire, param, defOf |
| `mernode::kChecks` | mernode.hpp:194 | static const (28 entries) | |
| `handlerAt::t`, `thunksAt::t`, `pinSize` (function-local) | mernode.hpp:156, 188, 246 | static const | |
| `g_merRegistry`, `g_probeClass` | di1compat.cpp:2702-2703 | hook install / hkMerRegistry | |

### Missing includes / include-order notes (no standalone compile was done)
- cho.hpp: `tolower` (885, 990) without <cctype>; std::string/vector, memcpy, int64_t come via oct.hpp/ports10.hpp.
- mission.hpp: `isdigit` (52), `tolower` (189), `isalnum` (238, 240) without <cctype>.
- mernode.hpp: `sscanf_s` (211) without <cstdio> (di1compat.cpp:13 includes it first; mer.hpp's <sstream> also pulls it
  in); `uintptr_t` via mer.hpp -> oct.hpp; its "include after rt()/logf()" comment is false (GR-22d). `sys::readable`
  is in sys.hpp, included by di1compat.cpp at line 36, after mernode.hpp (29).
- mer.hpp: relies on oct.hpp for <vector>, <string>, <cstring>, <cstdint>; includes mission.hpp, so the Meridian
  translator depends on mission data (kAliases, presentEvent30).
- ports10.hpp: `_stricmp` is MSVC-only (declared via <cstring> on MSVC).
- cho.hpp: `(std::min)`/`(std::max)` in parentheses (171, 384-385, 404-405, 471, 596) protects against windows.h macros.

## Rejected on verification
- None. (GR-05 and several others were downgraded, not rejected; see their Verification lines.)
