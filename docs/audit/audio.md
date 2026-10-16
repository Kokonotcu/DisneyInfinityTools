# Audio host (audio.hpp) (`native/di1compat/audio.hpp`, `native/di1compat/di1compat.cpp`, `native/di1compat/octtest.cpp`)
Scope: all 2,241 lines of `audio.hpp` (HEAD 684539b); every `audio::` reference in `di1compat.cpp` (1631, 3691, 4332-4338) plus `logf`/`rt`/`hook` (70-128), `g_cfg` audio keys (211-216, 329-334), the hook install order (4245-4246 vs 4332-4338) and the serve10 block around 3645-3696; every `audio::` use in `octtest.cpp` (1773-2450, mode dispatch 2533-2538); the cross-file duplicates named by the report (`vfx.hpp`, `npc.hpp`, `sx.hpp`, `gen30.hpp`, `btd.hpp`, `dct.hpp`, `gameplay.hpp`, `ui.hpp`) and the Python oracles `vocheck.py` / `particles.py`. Read-only; nothing built or run.
Verification: 43 findings checked: 26 confirmed, 15 corrected, 2 unverified, 0 rejected; 1 new.

**Config status used for every "Hot path?" line.**
- Code defaults: `install10 = ""` (di1compat.cpp:132), so `audio::install` never runs (gate at 4332) and **no audio hook exists**. The exception is `hkChoStart` (installed at 4246 because `audio10` and `cineSync10` both default on). It calls `audio::choreographyStarted` for every choreography start. On the first audio-synced one (flag 0x20), `ready()` waits on a null handle, gets WAIT_FAILED at once, and silently latches `hostState = -1` (`logfn` is still null). `annoSource` is unreachable because it sits inside the `serve10` block (3645).
- Recommended config: `install10` is set and `audio10`/`music10`/`vo10`/`mixer10`/`cineSync10` stay at their code default (on), with `audiotrace10` off. Every hook in the hooks table is installed **except** "queued sound post (audiotrace10)".

**Threads** (the report's names; one added):
- P: Pentane_Main, inside DllMain under the loader lock.
- H: the FMOD host thread (`Sleep(16)` loop).
- D: the bank dispatcher, where `hkBankReq` runs.
- G: game threads that call Wwise, the music manager and the DialogueManager.
- F: whatever thread FMOD calls `onEvent` on.
- T: octtest.
- L (added; the report's globals table used it without defining it): the thread that runs 3.0's sound-file read `FUN_0182f570`, i.e. `hkLoadFile`.

Which engine thread D, G and L really are is UNVERIFIED. A one-line `GetCurrentThreadId()` log in `hkBankReq`, `hkMusicState`, `hkPostExt`, `hkLoadFile` and `hkSrcPos` would settle it.

**Lock order: CONFIRMED by enumerating every nested acquisition.** The order is `dmLock` (engine CS at DialogueManager+0x58) → `vo.mx` → `g.mx` → {`posMx`, `doneMx`}. `traceMx` and the `logf` CS are leaves.
- Nested pairs found:
  - `hkDlgReq` 2046→2048 (dmLock → vo.mx)
  - `Vo::open` 1329→1342, `Vo::unload` 1411→1419, `seqTick` 1476→1485, `hkPostExt` 1937→1945/698 (vo.mx → g.mx)
  - `tick` 792→797/656/808, `stopObject` 765→768, `playLine` 698→707/723, `hkQueuedPost` 1883→1886 (g.mx → posMx / doneMx)
  - `hkPostExt` → `trace` (vo.mx → traceMx)
- No reverse pair exists:
  - `tickAndDeliver` takes vo.mx (2108) and g.mx (2119) only after `g.tick` has released g.mx, and never nested.
  - `onEvent` takes only `doneMx`.
  - `doneMx` is never held across an FMOD call.
  - No `ready()` wait happens under a plugin lock.
- One correction to the report's §7 ("every engine original is called outside plugin locks, with three exceptions"): it misses `vo.lang()` (3.0 `FUN_00680c00`), which `pickLanguage` calls under `vo.mx` (1318, reached from `Vo::open` 1330).

## Findings
| ID | Title | Sev | Class | Where |
|---|---|---|---|---|
| AU-08 | Unlocked read of `music.playset` in `hkBankReq` | Med | NEEDS-VERIFICATION | audio.hpp:1665 |
| AU-30 | FMOD instance pointer shared by two `Play`s (steal / stop windows) | Med | BEHAVIOUR-CHANGE | audio.hpp:680-687, 739-749, 767, 802-803, 820 |
| AU-07 | Blocking FMOD loads under `g.mx` (and `vo.mx`), also on the music thread | Med | DOC-ONLY | audio.hpp:577-610, 1329-1351, 885-896 |
| AU-06 | Host tick holds `g.mx` across FMOD update; game-thread hooks block on it | Med | NEEDS-VERIFICATION | audio.hpp:790-828 |
| AU-18 | Per-frame/per-stop hooks take blocking `g.mx` for ids that are not ours | Med | NEEDS-VERIFICATION | audio.hpp:1746-1784, 739-770 |
| AU-09 | `hkPostExt` holds `vo.mx` across `oPostExt` for every dialog post | Med | NEEDS-VERIFICATION | audio.hpp:1937, 1975 |
| AU-10 | `buildEvNames` (reads every FEV) under `vo.mx` stalls the host thread | Med | NEEDS-VERIFICATION | audio.hpp:2000-2001, 1516-1543 |
| AU-01 | `Host` god struct (453 lines, one mutex for everything) | Med | SAFE-REFACTOR | audio.hpp:377-829 |
| AU-02 | `dlgTranscode` god function (229 lines, 8 params, partial out-param mutation) | Med | SAFE-REFACTOR | audio.hpp:1065-1293 |
| AU-03 | `Vo` god struct; `tickAndDeliver` does five jobs | Med | SAFE-REFACTOR | audio.hpp:1295-1544, 2089-2133 |
| AU-04 | octtest reaches into every internal (refactor blast radius) | Med | DOC-ONLY | octtest.cpp:1773-2450 |
| AU-N1 | Ungated hooks wait on `g.mx` through host init with no 20 s bound | Low | NEEDS-VERIFICATION | audio.hpp:432-464, 615-625, 739-770, 1754-1815 |
| AU-15 | `ready()` semantics: 20 s block, sticky latch, null event, CreateThread-fail path | Low | SAFE-REFACTOR | audio.hpp:2182-2191, 2195-2203 |
| AU-16 | Hook-order dependencies (`hkBankReq` before `vo.*` init; cinesync hook before `install`) | Low | SAFE-REFACTOR | audio.hpp:2207 vs 2217-2232; di1compat.cpp:4246 vs 4338 |
| AU-25 | 32-bit wrap in `offset + length` bounds checks | Low | BEHAVIOUR-CHANGE | audio.hpp:1071, 1098, 1169-1170, 1189-1195, 1207, 1214, 1221, 1530-1535 |
| AU-33 | C++ exceptions can unwind through engine and FMOD frames | Low | NEEDS-VERIFICATION | audio.hpp:833-843, 1607-2062 |
| AU-12 | Formally racy unlocked reads (`music.prompt`, `progSound`) | Low | DOC-ONLY | audio.hpp:839, 1848 |
| AU-14 | Implicit threading contracts: AK callbacks on H; shared Wwise id counter | Low | DOC-ONLY | audio.hpp:650, 2098, 2129, 2198 |
| AU-26 | `progSound[id & 4095]` slot aliasing | Low | DOC-ONLY | audio.hpp:398, 683, 715, 839 |
| AU-11 | `hkQueuedPost` holds `posMx` across FMOD calls and logging | Low | NEEDS-VERIFICATION | audio.hpp:1886-1919 |
| AU-13 | `logf` (CS + `fflush`) called under `g.mx`/`vo.mx`/`posMx` | Low | NEEDS-VERIFICATION | audio.hpp (see body) |
| AU-17 | Per-post string concatenation + 2x FNV in `hkPost` | Low | SAFE-REFACTOR | audio.hpp:1701-1712 |
| AU-19 | `tick()` locks `posMx` once per live play per tick | Low | NEEDS-VERIFICATION | audio.hpp:802-803, 652-661 |
| AU-20 | Copy-pasted linear scans over `plays`; `vo.media` scans every historic header | Low | SAFE-REFACTOR | audio.hpp:637, 701, 766, 779, 1420, 1777, 1501-1513 |
| AU-21 | Filesystem syscalls per bank request / cue check, repeated within one call | Low | NEEDS-VERIFICATION | audio.hpp:469-476, 1583-1587, 888 |
| AU-22 | `hkFader` → `applyVolumes` rebuilds a map and does FMOD by-name lookups per call | Low | NEEDS-VERIFICATION | audio.hpp:1607-1615, 408-423 |
| AU-23 | Boot critical path: `readRoot10` reopens every zip twice | Low | SAFE-REFACTOR | audio.hpp:2145-2173, 277-294 |
| AU-24 | Misc. allocation churn | Low | SAFE-REFACTOR | audio.hpp:203-205, 809, 1020, 1461 |
| AU-27 | `vo.scripts` keeps stale engine pointers forever | Low | DOC-ONLY | audio.hpp:1309, 2015, 2050 |
| AU-28 | Recursive `hkBankReq` hands pointers to stack strings to the engine | Low | DOC-ONLY | audio.hpp:1678-1682 |
| AU-29 | `Host::load`/`unload` semantics to preserve | Low | DOC-ONLY | audio.hpp:580-584, 639 |
| AU-31 | Unchecked FMOD results | Low | BEHAVIOUR-CHANGE | audio.hpp:456, 459, 684, 716, 804, 878 |
| AU-32 | `dbnkFromName10`: u16 truncation; pointer-before-insert fragility | Low | SAFE-REFACTOR | audio.hpp:1011, 1016-1021 |
| AU-34 | `LuaParse` limitations | Low | DOC-ONLY | audio.hpp:210-272 |
| AU-35 | Never-freed / unbounded resources; no shutdown path | Low | DOC-ONLY | audio.hpp (see body) |
| AU-36 | Raw engine offsets and static VAs inline | Low | SAFE-REFACTOR | audio.hpp:1363-1391, 1859-1862, 2045-2053, 2198-2232 |
| AU-37 | FMOD magic constants; FMOD struct layouts / category walk redeclared | Low | SAFE-REFACTOR | audio.hpp:441-457, 490-529, 675, 835-838, 857-858, 1344, 1898-1912 |
| AU-38 | `rt()` call targets unverified; `install` ignores `hook()` results | Low | BEHAVIOUR-CHANGE | audio.hpp:2198-2238 |
| AU-39 | Debug probes, trace-only and octtest-only state in shipping types | Low | SAFE-REFACTOR | audio.hpp:370-373, 390, 394, 402-403, 772, 1869-1923, 2141 |
| AU-40 | Dead state (`Project::registered`, `Cfg::on`) | Low | SAFE-REFACTOR | audio.hpp:171, 601, 1572 |
| AU-41 | Intra-file duplicated idioms | Low | SAFE-REFACTOR | audio.hpp (see body) |
| AU-42 | Cross-file duplicates, including a non-identical `h131` | Low | DOC-ONLY | audio.hpp:921-926; vfx.hpp:36-41 |
| AU-43 | Cryptic idioms, global macro leak, small redundancies | Low | SAFE-REFACTOR | audio.hpp:42, 1555, 1565, 1637 |
| AU-05 | Config split across `audio::cfg` / `g_cfg`; dead `Cfg::on` | Low | SAFE-REFACTOR | audio.hpp:1571-1578; di1compat.cpp:216, 1631, 3691, 4245 |

---

### AU-08 Unlocked read of `music.playset` in `hkBankReq`
- **Where:** `native/di1compat/audio.hpp:1665`, writers at 893-894 and 905-906
- **Severity / category / class:** Med · Thread safety · NEEDS-VERIFICATION
- **Evidence:**
  - `else if (lower(music.playset) == lower(x)) music.drop();` runs in `hkBankReq` with no lock held.
  - `playset` is written by `Music::ensure` (894) and `Music::drop` (906), both under `g.mx`.
  - `ensure` is reached from `hkBankReq` (1664, thread D) **and** from `hkMusicState` (1796) and `hkMusicOnce` (1830), which run on the music manager's thread.
  - Every other reader locks `g.mx` "because music.playset changes on the bank dispatcher's thread" (1700, 1808).
- **Why it matters:** Copying a `std::string` while another thread reassigns or clears it is UB. If the old heap buffer is freed mid-copy, the copy can read freed memory. Writers on D itself cannot race (same thread). Only the music-thread writers can.
- **Fix (behaviour-preserving):** Put `std::lock_guard<std::recursive_mutex> l(g.mx);` around the 1665 comparison and the `drop()` call. `drop` re-locks the recursive mutex. `hkBankReq` holds no lock at 1665, so the order stays valid. Keep the `AUDLOG` at 1666 byte-identical. It can stay outside the lock.
- **Refactor risk:** None for lock order. Timing only.
- **Verification:** CONFIRMED. Whether `FUN_007397b0`/`FUN_0073ed10` run on a thread other than D is still UNVERIFIED (thread-id log, see the header).

### AU-30 FMOD instance pointer shared by two `Play`s (steal / stop windows)
- **Where:** `native/di1compat/audio.hpp:680-687` (play), 713-719 (playLine), 739-749 (act), 767 (stopObject), 779-784 (pause), 802-803 (place loop), 817-823 (erase, `born + 1 >= tickNo`)
- **Severity / category / class:** Med · Latent bug · BEHAVIOUR-CHANGE
- **Evidence:**
  - A `Play` keeps `ev` after its end is signalled. `onEvent` only pushes the id to `done` (836-837). `tick` erases the entry later (822), and one tick later still when `born + 1 >= tickNo` (820).
  - `stopObject` (767) and `act` (746) stop the event but keep `ev` too.
  - **Correction:** the report lists `unload` (638) as a source. It is not one, because `unload` sets `pl.second.ev = nullptr`. `playLine`'s retire loop (705) nulls `ev` as well.
  - **Wider than the report says:**
    - FMOD Ex events have a per-event instance pool sized by MAX_PLAYBACKS, and the default behaviour is to steal the oldest instance.
    - A steal therefore hands the **same** `Event*` to the new `EventSystem_GetEventBySystemID` call (680, 713). FMOD reports STOLEN for the old id during that same call.
    - Right after `play()` returns, two `plays` entries hold the same `ev` until the next tick (or two ticks) erases the old one.
  - Consequences inside that window:
    - `StopPlayingID(oldId)` → `act` → `Event_Stop(ev, 0)` stops the **new** sound. 3.0 posts the new sound first and stops the old one after (the comment at 693-695 shows this pattern for dialog).
    - `pause` affects the new sound as well.
    - `tick`'s `place` loop (802-803) positions the shared instance twice in unordered-map order, so for one or two ticks the new sound can sit at the old object's position.
- **Why it matters:** With FMOD Ex's default steal behaviour, normal play reaches this whenever a MAX_PLAYBACKS-limited event is re-posted on another object shortly before 3.0 stops the old id. It shows up as a sound cut off or misplaced for 16-32 ms.
- **Fix (behaviour-preserving):** None is behaviour-preserving. Document it. The real fix is a behaviour change for the user to decide: in `play`/`playLine`, after the new `ev` is obtained and still under `g.mx`, set `ev = nullptr` on any other `plays` entry holding the same pointer. The old id still ends through `done`. What changes: a late stop or pause of the old id no longer reaches the new sound, and `place` no longer moves the new sound.
- **Refactor risk:** `onEvent` must not take `g.mx` (AU-12), so the fix belongs in `play`/`playLine` and not in the callback.
- **Verification:** CORRECTED: removed `unload` (638) as a source and added the steal path, which raises the severity Low → Med. Still UNVERIFIED in practice. To settle it, extend `octtest --fmod`: post a MAX_PLAYBACKS-1 event twice on two objects, compare `g.plays[a].ev == g.plays[b].ev`, and check that STOLEN arrived for `a`.

### AU-07 Blocking FMOD loads under `g.mx` (and `vo.mx`), also on the music thread
- **Where:** `native/di1compat/audio.hpp:577-610` (`Host::load`), 1329-1351 (`Vo::open`), 885-896 (`Music::ensure`)
- **Severity / category / class:** Med · Threading/Perf (lock held across I/O) · DOC-ONLY
- **Evidence:**
  - `Host::load` holds `g.mx` (577) across `EventSystem_Load` (585) and `EventProject_LoadSampleData(..., 0)` (595). Mode 0 is FMOD_EVENT_DEFAULT, documented as blocking; the non-blocking mode is a separate flag. That reading of the FMOD docs is UNVERIFIED here because the repo has no FMOD headers.
  - `Vo::open` holds `vo.mx` (1329) **and** `g.mx` (1342) across `System_CreateSound` (1344). Modes `0x20002c1`/`0x2000241` have no FMOD_NONBLOCKING (0x10000) bit, and the non-`_str` mode loads the whole FSB as a compressed sample.
  - **Correction:** `Music::ensure` (887-892) is called not only from `hkBankReq` (D) but also from `hkMusicState` (1796) and `hkMusicOnce` (1830) on the music manager's thread. The comment at 1793-1795 records that the in-game Inc playset reached the load through this push-time path. So a whole `Music_<X>.fev` load can run synchronously on a game thread.
- **Hot path?:** code defaults: no (not installed). Recommended config: per FEV/bank/FSB load (zone loads, dialog bank requests) and the first cue push of a 1.0-hosted playset whose music is not loaded yet.
- **Why it matters:** While a load runs:
  - H cannot tick.
  - Every blocking `g.mx` user waits on disk I/O: `hkStop`, `hkSrcPos` (per frame during lipsync), `hkUnreg`, `hkAction`, unknown `hkPost`, `hkFader`, and music `hkSetState`.
  - During an FSB load every `hkPostExt` dialog post also waits, on `vo.mx`.
  - A push-time music load stalls the music thread itself.
- **Fix (behaviour-preserving):** Document it. Moving FMOD calls out of `g.mx` depends on FMOD Ex 4.44's unverified thread safety, so the refactor must not do it. Optional, behaviour-preserving: pre-stat or prefetch files outside the lock.
- **Refactor risk:** High if FMOD call serialization is changed.
- **Verification:** CORRECTED: added the music-manager-thread caller of `ensure` and the FMOD mode-bit detail. Blocking semantics are still UNVERIFIED against FMOD docs.

### AU-06 Host tick holds `g.mx` across the whole FMOD update; game-thread hooks block on it
- **Where:** `native/di1compat/audio.hpp:790-828` (lock 792, listener 801, place loop 802-803, `EventSystem_Update` 804); loop 2175-2178
- **Severity / category / class:** Med · Threading/Perf · NEEDS-VERIFICATION
- **Evidence:**
  - The lock is held for every 16 ms tick on H.
  - Blocking (non-try) `g.mx` users on game threads:
    - `hkPost`, only for Wwise-unknown posts (1700 → `play` 671) and for the Pause/Resume ids (`pause` 776, 1697)
    - `hkStop` → `act` (741)
    - `hkSrcPos` → `position` (754)
    - `hkUnreg` → `stopObject` (765)
    - `hkAction` (1776, or `act`)
    - `hkSetState`, only for `Music_*` groups (1808)
    - `hkFader` (1610)
    - `hkMusicState` / `hkMusicOnce` (1797, 1831)
    - `hkPostExt` (1945 → `playLine` 698), only for 1.0 lines
    - `choreographyStarted` (2081)
  - Only `hkStackTick` uses `std::try_to_lock`, with the comment "per frame: never wait out FMOD's update" (1851).
- **Hot path?:** code defaults: no. Recommended config: H every 16 ms. The waiters include per-frame `hkSrcPos` (while a lipsynced line plays) and every `StopPlayingID` / `UnregisterGameObj`.
- **Why it matters:** Game threads wait out FMOD's update for ids that are not ours. A normal update is short. The long holds come from D's loads (AU-07), which take the same lock.
- **Fix (behaviour-preserving):**
  - (a) AU-19: snapshot positions under one `posMx` lock.
  - (b) Let pure-bookkeeping lookups skip `g.mx`.
    - **Caveat (correction):** an atomic `plays.size()` "empty" fast path rarely fires in a 1.0 playset, where looping ambiences keep `plays` non-empty.
    - The useful variant is a separate `playsMx` for the map, ordered `g.mx → playsMx`. A lookup takes `playsMx` alone and returns false for ids that are not ours. When an FMOD call is needed, release, take `g.mx` then `playsMx`, and look the id up again.
    - Results are identical: an id is in `plays` before `play`/`playLine`/`standIn` returns it, so no lookup can race its own insertion.
  - Do not remove FMOD serialization.
- **Refactor risk:**
  - The new lock must never be taken before `g.mx`.
  - `onEvent` must stay `doneMx`-only.
  - Every `plays` mutation (`play`, `playLine`, `standIn`, `tick` erase, plus entry edits in `act`/`stopObject`/`unload`/`playLine` retire/`Vo::unload`) must take `playsMx`.
- **Verification:** CORRECTED: added the missing lock users and the caveat on fix (b). The core claim is confirmed.

### AU-18 Per-frame/per-stop hooks take blocking `g.mx` for ids that are not ours
- **Where:** `native/di1compat/audio.hpp:1746-1751` (`hkSrcPos` → `position` 752-761), 1754-1758 (`hkStop` → `act` 739-749), 1761-1765 (`hkUnreg` → `stopObject` 763-770), 1768-1784 (`hkAction`)
- **Severity / category / class:** Med · Contention on a game-thread path · NEEDS-VERIFICATION
- **Evidence:**
  - Each of these hooks always locks `g.mx` and does a hash lookup or a linear scan.
  - Most ids and objects belong to Wwise.
  - `hkSrcPos` runs per lipsync query, i.e. per frame while a lipsynced 3.0 or 1.0 line plays (comment 1741-1743).
- **Hot path?:** code defaults: no. Recommended config: yes. `hkSrcPos` runs per frame during dialog; `hkStop` and `hkUnreg` run on every Wwise stop and unregister.
- **Why it matters:** These are the game-thread victims of AU-06 and AU-07. A per-frame call can wait out a bank load's disk I/O.
- **Fix (behaviour-preserving):** Same as AU-06(b). `stopObject` must still erase `pos[obj]` under `posMx` for every object (768-769), even when nothing of ours plays on it.
- **Refactor risk:** Lock order (see AU-06).
- **Verification:** CORRECTED: severity "Low-Med" → Med, because the path is per frame (hkSrcPos) and the lock can be held across I/O (AU-07).

### AU-09 `hkPostExt` holds `vo.mx` across `oPostExt` for every dialog post
- **Where:** `native/di1compat/audio.hpp:1937` (function-scope lock), 1975 (`return oPostExt(...)`)
- **Severity / category / class:** Med · Threading (engine call under plugin lock) · NEEDS-VERIFICATION
- **Evidence:**
  - Every 3.0 dialog post (Wwise PostEvent with externals) runs under `vo.mx`, including posts that are not ours.
  - Other `vo.mx` users:
    - H: `seqTick` 1476, every tick, blocking
    - D: `Vo::open`/`load`/`unload`, across FSB loads (AU-07)
    - L: `hkLoadFile` 2000 (AU-10)
    - `hkDlgReq` 2037/2048
    - `annoSource` 1559
    - the trace block in `tickAndDeliver` 2108
- **Hot path?:** code defaults: no. Recommended config: per dialog line post. Not per frame.
- **Why it matters:**
  - The Wwise call runs under a plugin lock. One way this could deadlock: Wwise holds an internal lock while running a callback, and that callback posts a dialog line on its own thread. The callback's post then waits for `vo.mx` while still holding the Wwise lock, and this thread holds `vo.mx` while waiting for that Wwise lock. Whether 3.0's callback ever posts a line synchronously is UNVERIFIED.
  - **Correction:** narrowing the lock removes this deadlock risk but not the I/O wait. `vo.media` (1503) still takes `vo.mx`, so a dialog post still waits while D holds `vo.mx` across an FSB load.
- **Fix (behaviour-preserving):** `{ std::lock_guard l(vo.mx); if (n == 1 && ext && ext[3] && vo.media(...)) { ...unchanged...; return id; } } return oPostExt(...);`. `media` + `playLine` stay under one `vo.mx` hold, which keeps the FSB-lifetime guarantee (comment 1937). Log text is unchanged.
- **Refactor risk:** Timing only. The static `posts` counter (1958) must stay inside the locked branch.
- **Verification:** CORRECTED: added that the fix does not remove the I/O wait, and the `vo.lang()` omission in the report's §7.

### AU-10 `buildEvNames` (reads every FEV) under `vo.mx` stalls the host thread
- **Where:** `native/di1compat/audio.hpp:2000-2001`, 1516-1543
- **Severity / category / class:** Med · Lock held across I/O (one-time) · NEEDS-VERIFICATION
- **Evidence:**
  - `hkLoadFile` takes `vo.mx` and then calls `vo.buildEvNames(g.sound10)`.
  - That reads every `*.fev` (`slurp` 1525) and byte-scans each one for `"STRR"` (1526-1527).
  - H blocks in `seqTick`'s unconditional `vo.mx` (1476), so there is no `EventSystem_Update` and no AK callback delivery for the whole scan. `hkPostExt`, `hkDlgReq` and `annoSource` block too.
- **Hot path?:** code defaults: no. Recommended config: once, on the first 1.0 v11 `.dlg` load.
- **Why it matters:** It is a one-time stall of all 1.0 sound updates and of every dialog post.
- **Fix (behaviour-preserving):** Build into a local map outside `vo.mx` (guarded by `std::once_flag` or its own mutex), then move it into `vo.evNames` under `vo.mx`. Keep the "not retried" semantics of `evNamesBuilt = true` being set before the scan (1519). Keep the "vo10: %u FEV strings hashed..." text.
- **Refactor risk:** octtest calls `buildEvNames` directly (octtest.cpp:2258). Its log line may move relative to other threads' lines.
- **Verification:** CONFIRMED.

### AU-01 `Host` god struct
- **Where:** `native/di1compat/audio.hpp:377-829` (453 lines)
- **Severity / category / class:** Med · Design · SAFE-REFACTOR
- **Evidence:** One struct holds all of the following under the one `mx`:
  - the FMOD API table (378) and system lifetime (`init` 428-465)
  - projects and events (384-385; 481-646)
  - plays (386) and 3D state (387-391)
  - delivery (395-398) and volumes (400-401, 408-423)
  - trace state (`lres` 390, `pausedNow` 772)
  - octtest hooks (`ownCounter` 394, `dropped` 402, `testSpeakerMode` 403)
- **Why it matters:** One mutex for every responsibility is the structural root of AU-06, AU-07 and AU-18, and every part is coupled to every other.
- **Fix (behaviour-preserving):** Split into components (`FmodSystem`, `ProjectRegistry`, `PlayTable`, `Spatial`, `Mixer`) that keep sharing the same `g.mx` instance.
- **Refactor risk:**
  - octtest touches nearly every member (AU-04).
  - `Host::onEvent` (833) uses the global `g`.
  - Lock order must stay the same.
- **Verification:** CONFIRMED.

### AU-02 `dlgTranscode` god function
- **Where:** `native/di1compat/audio.hpp:1065-1293` (229 lines)
- **Severity / category / class:** Med · Design/Readability · SAFE-REFACTOR
- **Evidence:**
  - Eight parameters, and nine lambdas: `fail`, `rd32`, `evName`, `ev`, `t32`, `t16`, `tstr`, `ev3`, `align`.
  - Three phases: v11 parse (1096-1166), 3.0 merge (1167-1241), serialization (1243-1292).
  - `lineEvents` (1112) and `seqs` (1153) are written **before** later `return fail(...)` calls (1098-1160 for later entries; 1174-1242). `hkLoadFile` passes `&vo.seqs, &vo.lineEvents` (2003), so a rejected script leaves its sequences and line events registered.
  - They also stay registered when `engineAlloc` fails (2005, 2008-2011).
- **Why it matters:** The function is hard to review, and the partial mutation is hidden behaviour.
- **Fix (behaviour-preserving):** Extract `parseV11`, `mergeBase30` and `serializeV15` over a shared context struct. Keep the out-param writes at the same points; do **not** make the function transactional. Keep every `fail("...")` string byte-identical: they reach the log via 2007, and octtest prints them.
- **Refactor risk:**
  - The output must stay byte-exact: entry order (classes then named), `align()` placement, the `std::map` order of `ranges` and `evIdx`, and event index assignment order.
  - octtest `--vo` calls it with two argument sets (octtest.cpp:2269, 2278) and writes `.dlg30` / `.merged.dlg30`. Diff those against the pre-refactor outputs and re-run `vocheck.py`.
- **Verification:** CORRECTED: the octtest mode is `--vo` (dispatch octtest.cpp:2537), not "--dlg"; added the engineAlloc-failure case.

### AU-03 `Vo` god struct; `tickAndDeliver` does five jobs
- **Where:** `native/di1compat/audio.hpp:1295-1544` (250 lines), 2089-2133 (45 lines)
- **Severity / category / class:** Med · Design · SAFE-REFACTOR
- **Evidence:**
  - `Vo` covers the following:
    - language selection (1316)
    - FSB/.name I/O and FMOD sound lifetime (1327-1355)
    - raw engine registry writes (`publish` 1360-1396)
    - the sequence runtime (1427-1498)
    - external-source identification (1501)
    - the FEV string hash (1516)
    - transcoded-script tracking (1309)
  - `tickAndDeliver` runs, in this order:
    - the tick
    - AK_Duration callbacks
    - two trace reports (2100-2125)
    - EndOfEvent callbacks
    - `vo.seqTick()`
- **Why it matters:** The struct mixes unrelated jobs, which makes it hard to change one without touching the rest.
- **Fix (behaviour-preserving):** Split out `VoBanks`, `VoRegistry`, `VoSequencer` and `FevNames`. Move the trace block into `traceEnds(fin)`.
- **Refactor risk:** The call order inside `tickAndDeliver` is load-bearing: Duration, then the trace logs, then EndOfEvent, then `seqTick`, all outside every lock.
- **Verification:** CONFIRMED.

### AU-04 octtest reaches into every internal
- **Where:** `native/di1compat/octtest.cpp:1773-2450`
- **Severity / category / class:** Med · Coupling · DOC-ONLY
- **Evidence:**
  - octtest reads or writes `g.f/es/ms/sys/byId/plays/pos/posMx/mx/doneMx/durs/mixer/faders/projects/dropped/testSpeakerMode`, plus `g.depsOf` and `g.applyVolumes`.
  - It sets `hostState = 1` (1866) and uses `hostStart` (2429, 2436).
  - It uses `music.prompt/file`, `Music::cue`, `music.begin/ensure/endPrompt`, and `vo.banks/speakers/hdrs/evNames/lineEvents/dir10/root10`.
  - It calls `vo.open/load/unload/media/pickLanguage`.
  - It overwrites `audio::oListener`/`oSetPos` (1902-1903, 2041-2042).
  - It calls the hooks `hkListener`, `hkSetPos` and `hkSrcPos` directly.
  - It redeclares FMOD's cue iterator structs (1814-1815, 1830-1831).
- **Why it matters:** octtest is the only behaviour oracle (`--fmod`, `--mixer`, `--vo`, `--voplay`, `--fevdeps`, `--host`). Any encapsulation breaks it unless octtest changes in the same commit.
- **Fix (behaviour-preserving):** Keep members public and refactor octtest in lockstep, or add an `audio::test` access surface.
- **Refactor risk:** Losing the harness means losing the oracle.
- **Verification:** CONFIRMED.

### AU-N1 Ungated hooks wait on `g.mx` through host init with no 20 s bound
- **Where:** `native/di1compat/audio.hpp:432-464` (`init` holds `mx` for the whole FMOD init), 615-625 and 575-611 (`loadTree` → `load`, `mx` across blocking loads, run on H before `SetEvent` at 2173); waiters 739-770, 1754-1784, 1805-1815
- **Severity / category / class:** Low · Robustness/boot latency · NEEDS-VERIFICATION
- **Evidence:**
  - `ready()` bounds its wait at 20 s (2186). The hooks that skip `ready()` but take `g.mx` have no bound:
    - `hkStop` → `act`
    - `hkSrcPos` → `position`
    - `hkUnreg` → `stopObject`
    - `hkAction`
    - `hkSetState` for `Music_*`
  - These hooks are live from `install` onward, while H holds `g.mx` through `EventSystem_Create/Init` and through each `Initialize.fev:Permanent` project load with `LoadSampleData`. A game thread that calls them during boot waits for the whole init.
  - If FMOD init ever hangs (an audio-driver hang inside `EventSystem_Init`), these hooks hang the game forever. The gated hooks would give up after 20 s and turn 1.0 audio off instead.
- **Hot path?:** code defaults: no (not installed). Recommended config: boot window only.
- **Why it matters:** The report's AU-15 calls the ungated hooks "safe because the containers stay empty". That holds for results, but not for waiting.
- **Fix (behaviour-preserving):** Before touching `g.mx`, return the "not ours" result when `hostState <= 0` (one volatile read).
  - Results are identical: `plays` and `music.playset` can only become non-empty after some `ready()` returned true, and that `ready()` call sets `hostState = 1` first (2188).
  - `stopObject` must still erase `pos[obj]` under `posMx`.
  - Only timing changes.
- **Refactor risk:** Do not use `ready()` there; it would add a 20 s wait where there is none today.
- **Verification:** NEW, verified by reading `init`, `loadTree`, `hostThread` (2164-2173) and the hook bodies. Whether boot-time game threads call these Wwise APIs before H finishes is UNVERIFIED; a timestamped log in `hkUnreg` would settle it.

### AU-15 `ready()` semantics
- **Where:** `native/di1compat/audio.hpp:2182-2191`, 2195-2203; `di1compat.cpp:1631`, 4245-4246, 4332-4338
- **Severity / category / class:** Low · Robustness · SAFE-REFACTOR (enum + docs only)
- **Evidence:**
  - The first caller blocks a game thread for up to 20 s (2186). Every concurrent caller waits on the same manual-reset event.
  - A timeout latches `hostState = -1` forever (2188), while H, once its init finishes, keeps ticking every 16 ms (2175-2178).
  - **Correction:** after the latch H has only loaded the boot `Initialize.fev` tree. No further banks load, because `hkBankReq` gates on `ready()`. The cost is the tick loop, not unused bank loads.
  - **Correction (null-event path):** with `install10` empty, `audio::install` never runs and `hostStart.done == nullptr`. `choreographyStarted` (via `hkChoStart`, installed when `audio10 && cineSync10`) calls `ready()`, which gets WAIT_FAILED at once and latches -1.
    - No misleading text is logged, because `audio::logfn` is assigned only inside the same `install10` branch (di1compat.cpp:4333) and is still null.
    - The misleading "did not finish its init in 20 s" can only appear in the tiny window between 4333 and `CreateEventA` (2200).
  - If `CreateThread` fails (2201-2202), the event exists but is never signalled. The first `choreographyStarted` then waits the full 20 s and logs "did not finish its init in 20 s", although the thread never started.
  - `ready()` gating is inconsistent:
    - Gated: `hkBankReq`, `hkPost`, `hkFader`, `hkMusicState`, `hkMusicOnce`, `hkLoadFile`, `choreographyStarted`.
    - Ungated: `hkSetPos`, `hkListener`, `hkSrcPos`, `hkStop`, `hkUnreg`, `hkAction`, `hkSetState`, `hkStackTick`, `hkPostExt`, `hkDlgReq`.
    - The ungated hooks give correct results only because the containers stay empty. They can still wait (AU-N1).
- **Why it matters:** The states and the empty-container invariant are undocumented.
- **Fix (behaviour-preserving):** An enum for `hostState`, plus documentation of the invariant. Any change to the 20 s, the latch or the log text is a behaviour or log change; flag it.
- **Refactor risk:** Log-grep scripts.
- **Verification:** CORRECTED: no log line on the null-handle path; "loads banks nothing uses" was overstated; the AU-N1 caveat was added.

### AU-16 Hook-order dependencies
- **Where:** `native/di1compat/audio.hpp:2207` (first hook) vs 2217, 2224-2232; `di1compat.cpp:4246` vs 4338
- **Severity / category / class:** Low · Robustness (latent) · SAFE-REFACTOR
- **Evidence:**
  - `hkBankReq` is live from 2207, because `MH_EnableHook` runs immediately (di1compat.cpp:124-125).
  - Through `vo.load` → `open` → `pickLanguage` it uses `vo.root10` and `vo.lang`, which are assigned at 2224-2225. With them unset, `dir10` would latch to `"\ENU"` (1320-1322).
  - Mitigated today: `hkBankReq` calls `ready()` first, and the host thread cannot start while P holds the loader lock (comment 2138-2140). `octtest --host` asserts exactly that ("blocked (as expected)", octtest.cpp:2436-2450).
  - `hkChoStart` is hooked at 4246, before `install` at 4338. An audio-synced choreography in that window would latch -1 (AU-15).
  - No other hook precedes its data: `stackPop` 2217 comes before 2220, and every vo pointer comes before 2233-2235.
- **Why it matters:** The correctness of `hkBankReq` depends on the loader-lock timing rather than on assignment order.
- **Fix (behaviour-preserving):** Move the `rt()`/string assignments (2217, 2224-2232, keeping their `cfg.music`/`cfg.vo` conditions) above the first `hook(...)`. **Correction:** this does not reorder any log line, because `rt()` and assignments do not log. Only reordering `hook()` calls changes the hook log order. Moving `hkChoStart`'s hook after `audio::install` would do that, so treat it as a log-order change.
- **Refactor risk:** Hook name strings must stay byte-identical.
- **Verification:** CORRECTED: the report's risk claim that the fix reorders hook log lines was wrong.

### AU-25 32-bit wrap in `offset + length` bounds checks
- **Where:** `native/di1compat/audio.hpp:1098`, 1103, 1189-1195, 1207, 1212-1215, 1221-1223, 1530-1535; bounded readers 1071, 1169-1170
- **Severity / category / class:** Low · Correctness (malformed input only) · BEHAVIOUR-CHANGE
- **Evidence:** On x86, `size_t` is 32-bit and the offsets are `uint32_t` from the file.
  - 1098: `off + 0x10` wraps for `off >= 0xFFFFFFF0`, and `e = in.data() + off` then points up to 16 bytes before the buffer.
  - 1189/1195: `co + 8`, then `t[co]` and an 8-byte copy before the buffer.
  - 1207: `o + 8`.
  - 1214: `o + len`.
  - 1221-1223: `lp + c*2u`.
  - 1530: `i + 8 + (size_t)len` passes for a huge `len`. Then `((const uint32_t*)(s+4))[k]` and `memchr(s+o, 0, len-o)` read past the file and **can AV**, and CrabeLoader dumps every AV.
  - **Correction:**
    - 1136-1137 (`lists + u32`) also wraps, but the wrapped `lo` is below `lists <= in.size()`, so the copy stays in bounds. The data is wrong, but there is no out-of-bounds read.
    - The "bounded" readers `rd32`/`t32`/`t16` (`o + 4 <= size`) themselves wrap for `o >= 0xFFFFFFFD`. So the 1174/1182 cases that "land on bounded readers" (e.g. `t16(ro)`, `t32(te + i*8u)`) can still read up to 3 bytes before the buffer.
  - All inputs are shipped files: 1.0 `.dlg`/`.fev` and 3.0 `.dlg`.
- **Why it matters:** These are UB and possible AVs on corrupted installs only.
- **Fix (behaviour-preserving):** Use subtraction-form checks (`off > size || size - off < 0x10`) or 64-bit math, in the readers too. Valid inputs are unaffected; malformed ones move from UB to `fail(...)` or a skip. Keep every `fail()` string.
- **Refactor risk:** octtest `--vo` `.dlg30` outputs and `vocheck.py` must stay byte-identical.
- **Verification:** CORRECTED: 1136-1137 is in bounds; added the wrap in the bounded readers.

### AU-33 C++ exceptions can unwind through engine and FMOD frames
- **Where:** `native/di1compat/audio.hpp:833-843` (`onEvent`), every detour 1607-2062, `hostThread` 2164-2179
- **Severity / category / class:** Low · Robustness · NEEDS-VERIFICATION
- **Evidence:**
  - No detour and not `onEvent` has a `try`. They all allocate (`std::string`, maps, vectors).
  - `di1compat.cpp:3714` documents that unwinding into engine code ends in a silent fail-fast.
  - The build uses the default MSVC `/EHsc` (CMakeLists.txt sets no EH flag), so `catch (...)` would not swallow SEH.
- **Why it matters:** An out-of-memory exception inside a hook kills the process silently.
- **Fix (behaviour-preserving):** **Correction:** the report's `catch (...) { return original(...); }` is NOT behaviour-preserving.
  - For detours that call the original first, it would run the engine function twice. These are `hkPost` (1690), `hkBankReq` (1643), `hkMusicState` (1790), `hkStackTick` (1847), `hkQueuedPost` (1871), and `hkLoadFile` after 1995. In those, catch and return the result already obtained (`r`; for `hkLoadFile`, `d30`/`n30`).
  - In detours that call the original last, wrap only the plugin part and call the original once, after the try.
  - `onEvent`: `catch (...) { return 0; }`.
  - Behaviour is identical outside out-of-memory.
- **Refactor risk:**
  - No `__try` in these functions (C2712: they hold `lock_guard`s and strings).
  - In `hkPostExt`, an exception after `playLine` started the FMOD sound must not fall through to `oPostExt`, or the line plays twice.
- **Verification:** CORRECTED: the proposed fix would double-call the originals.

### AU-12 Formally racy unlocked reads
- **Where:** `native/di1compat/audio.hpp:1848` (`music.prompt`), 839 (`g.progSound`)
- **Severity / category / class:** Low · Thread safety · DOC-ONLY
- **Evidence:**
  - `hkStackTick` reads `music.prompt` without a lock (1848) and re-checks it under the try-lock (1852).
  - Writers of `music.prompt`: `begin`/`endPrompt` under `g.mx` from `hkSetState`, which runs inside the same stack tick, and also from `hkPost` (StopMusic) and `drop` on other threads.
  - `onEvent` reads `progSound` without a lock; it is written under `mx` (683, 715).
- **Why it matters:** Benign on x86 for aligned pointers, but formally UB.
- **Fix (behaviour-preserving):** `std::atomic<void*>` for `prompt` (same codegen on x86) and a comment for `progSound`.
- **Refactor risk:** `onEvent` must never take `g.mx`. FMOD may call it inside calls made under `mx` (recursive, fine) or from its own thread (that would deadlock).
- **Verification:** CONFIRMED.

### AU-14 Implicit threading contracts
- **Where:** `native/di1compat/audio.hpp:2098`, 2129 (AK callbacks), 2198 + 650 (id counter)
- **Severity / category / class:** Low · Threading constraint · DOC-ONLY
- **Evidence:**
  - AK Duration (type 8) and EndOfEvent (type 1) are delivered on H, outside every lock; real Wwise delivers from its own thread.
  - `newId` calls `InterlockedIncrement` on Wwise's own counter `rt(0x022AFBE8)`. It is called from G (`play`, `playLine` via `hkPostExt`, `standIn`) and from H (`seqTick` → `playLine`).
- **Why it matters:** Both are cross-thread assumptions that nothing documents.
- **Fix (behaviour-preserving):** Document both.
- **Refactor risk:** Changing the delivery thread or the id source changes behaviour.
- **Verification:** CONFIRMED. Two points remain UNVERIFIED:
  - Whether 3.0's `LAB_01830120` is safe on an arbitrary thread.
  - Whether `FUN_01948a10` increments that counter with `lock xadd` (disassemble around the 0x022AFBE8 reference).

### AU-26 `progSound[id & 4095]` slot aliasing
- **Where:** `native/di1compat/audio.hpp:398`, 683, 715, 839
- **Severity / category / class:** Low · Latent bug · DOC-ONLY
- **Evidence:** The slot is valid only while fewer than 4,096 ids are issued between `playLine` and FMOD's SOUNDDEF_CREATE for that event. Ids are shared with every Wwise post.
- **Why it matters:** A late or repeated SOUNDDEF_CREATE (UNVERIFIED for FMOD Ex) would get another line's subsound.
- **Fix (behaviour-preserving):** Document it. A map keyed by full id would need a lock in `onEvent`.
- **Refactor risk:** Never add `g.mx` to `onEvent`.
- **Verification:** CONFIRMED.

### AU-11 `hkQueuedPost` holds `posMx` across FMOD calls and `logf`
- **Where:** `native/di1compat/audio.hpp:1886-1919`
- **Severity / category / class:** Low · Threading (trace only) · NEEDS-VERIFICATION
- **Evidence:** The `lock_guard p(g.posMx)` at 1886 covers two `AUDLOG`s (1890, 1915) and FMOD calls (1898-1912). That contradicts the design note at 391-392. The hook is installed only with `audio::cfg.trace` (2237), set from `audiotrace10` (di1compat.cpp:214, 4336).
- **Hot path?:** code defaults: no. Recommended config: no (audiotrace10 off).
- **Why it matters:** With tracing on, `hkSetPos` waits on FMOD and on disk flushes.
- **Fix (behaviour-preserving):** Copy `v` and `lpos`/`lfwd`/`lup` to locals under `posMx`, release it, then run the FMOD calls and logs. `g.lres` is still read under `g.mx` (1883). Log text is identical.
- **Refactor risk:** None beyond log timing.
- **Verification:** CONFIRMED.

### AU-13 Logging under locks
- **Where:**
  - `g.mx`: `init` 439/452/458/463, `load` 586/608, `unload` 642, `hkMusicState` 1800, `hkSetState` 1811, `hkMusicOnce` 1834
  - `vo.mx`: 1321, 1337, 1346, 1352, 1384, 1392, 1395, 1424, 1455, 1466, 1469, 1542, 2007, 2016, 2055 (2055 also under `dmLock`)
  - `posMx`: AU-11
- **Severity / category / class:** Low · Threading/Perf · NEEDS-VERIFICATION
- **Evidence:** `logf` takes a CRITICAL_SECTION and calls `fflush` for every line (di1compat.cpp:70-84). None of these call sites is per frame.
- **Hot path?:** code defaults: no. Recommended config: load and cue paths only.
- **Why it matters:** Each disk flush extends the lock hold.
- **Fix (behaviour-preserving):** Format into a local buffer and log after unlocking, where that is trivial. The text must stay byte-identical.
- **Refactor risk:** Line order relative to other threads can shift.
- **Verification:** CONFIRMED.

### AU-17 Per-post string building and hashing in `hkPost`
- **Where:** `native/di1compat/audio.hpp:1701-1712`
- **Severity / category / class:** Low · Perf · SAFE-REFACTOR
- **Evidence:**
  - `fnv1("StartMusic_" + music.playset)` (1702) and `fnv1("StopMusic_" + ...)` (1706) run under `g.mx`.
  - "StartMusic_" is 11 chars, so a playset name of 5+ chars exceeds MSVC's 15-char SSO and allocates on the heap.
  - Each `fnv1` makes one CRT `tolower` call per char (48).
- **Hot path?:** code defaults: no. Recommended config: every Wwise-unknown post (`r == 0 && !ext`) while a 1.0 music playset is loaded, i.e. most 1.0 sound posts in a 1.0 playset. Sub-microsecond; not per frame.
- **Why it matters:** It is avoidable work on a frequent path, done while holding `g.mx`.
- **Fix (behaviour-preserving):** Cache `startHash`/`stopHash` in `Music` at the only two writers of `playset`, `ensure` (894) and `drop` (906). `fnv1` is pure.
- **Refactor risk:** Any new writer of `playset` must update the cache.
- **Verification:** CORRECTED: severity "Low-Med" → Low (not measurable per frame).

### AU-19 `tick()` locks `posMx` once per live play per tick
- **Where:** `native/di1compat/audio.hpp:802-803` → `place` 652-661
- **Severity / category / class:** Low · Perf · NEEDS-VERIFICATION
- **Evidence:** Each tick makes N `posMx` lock/unlock pairs plus N `Event_Set3DAttributes` calls.
- **Hot path?:** code defaults: no. Recommended config: H every 16 ms × live plays (not a game thread).
- **Why it matters:** Minor CPU on H, and more contention with `hkSetPos`.
- **Fix (behaviour-preserving):** Lock `posMx` once, gather `(ev, V3)` into a reused vector, unlock, then make the FMOD calls. Values differ only under a concurrent `hkSetPos` (timing).
- **Refactor risk:** Keep `place()` for `play`/`playLine`. Combine with the AU-30 decision, since today's double `place` of a shared instance follows map order.
- **Verification:** CONFIRMED.

### AU-20 Duplicated linear scans; `vo.media` scans every historic header
- **Where:** `native/di1compat/audio.hpp:637-638`, 701-709, 766-767, 779-784, 1420-1421, 1777-1778; 1501-1513
- **Severity / category / class:** Low · Duplication/Perf · SAFE-REFACTOR
- **Evidence:**
  - Six "scan `plays` by X" loops.
  - `media` walks every speaker × every header ever published. `hdrs` only grows (1304, 1372), on every dialog post with an external source.
- **Hot path?:** code defaults: no. Recommended config: per dialog post (`media`); scans per stop, unregister or unload.
- **Why it matters:** Six copies drift independently, and the `media` scan grows over a long session.
- **Fix (behaviour-preserving):** A `forEachPlay(pred, fn)` helper over the same container (iteration order unchanged). For `media`, an interval set over `[data, data+size)`; the ranges never overlap, so the result is the same.
- **Refactor risk:** Keep each loop's side effects in place: the `doneMx` push in `playLine`, and the `endBy` strings.
- **Verification:** CONFIRMED.

### AU-21 Filesystem syscalls per request
- **Where:** `native/di1compat/audio.hpp:469-476` (`name10`), 1583-1587 (`hostedMusic`), 888 (`ensure`), 1669
- **Severity / category / class:** Low · Perf (load/cue) · NEEDS-VERIFICATION
- **Evidence:**
  - One `FindFirstFileA` per `.bnk`/`.fev` request.
  - A cue check for a 1.0-hosted playset costs `GetFileAttributesA` + `FindFirstFileA` + `FindFirstFileA` (`hkMusicState` 1796 / `hkMusicOnce` 1830 → `hostedMusic`, then `ensure` → `name10` again).
  - For a 3.0 playset, `hostedMusic` stops after the first syscall.
- **Hot path?:** code defaults: no. Recommended config: per bank request and per cue push.
- **Why it matters:** Repeated syscalls for the same answer within one call.
- **Fix (behaviour-preserving):** Pass the resolved name from `hostedMusic` into `ensure`. Memoizing `name10` assumes the 1.0 install does not change at runtime; state that assumption.
- **Refactor risk:** Low.
- **Verification:** CONFIRMED.

### AU-22 `applyVolumes` per fader call
- **Where:** `native/di1compat/audio.hpp:1607-1615`, 408-423
- **Severity / category / class:** Low · Perf · NEEDS-VERIFICATION
- **Evidence:** Each `hkFader` call copies `mixer` (411) and does `EventSystem_GetCategory(es, name)` per category (421), all under `g.mx`. The call rate of `FUN_0182c460` is UNVERIFIED; the comment cites only the Options path.
- **Hot path?:** code defaults: no. Recommended config: per fader set (rate unknown).
- **Why it matters:** Possibly frequent, and it holds `g.mx` across FMOD lookups.
- **Fix (behaviour-preserving):** **Correction:** "skip when the value is unchanged" must also require that the entry already exists, because `faders[cat][src]` default-creates 0.0f.
  - Use `auto& m = g.faders[cat]; auto it = m.find(src); if (it != m.end() && it->second == v) /*skip*/; else { m[src] = v; g.applyVolumes(); }`.
  - It is valid because `applyVolumes` is the only writer of category volumes (421).
  - `load` re-applies after every project load (605).
  - `loadMixer` runs before `ready()` can return true (2169, 2173).
- **Refactor risk:** NaN never compares equal, so it still applies, as today.
- **Verification:** CORRECTED: added the existence condition.

### AU-23 Boot critical path
- **Where:** `native/di1compat/audio.hpp:2145-2173`, 277-294
- **Severity / category / class:** Low · Perf (boot) · SAFE-REFACTOR
- **Evidence:**
  - `ready()` returns only after `init`, then `loadMixer`, then `loadTree("Initialize.fev")`.
  - `loadMixer` calls `readRoot10` twice (2150-2151). Each call re-opens and re-parses every `assets\*.zip` central directory until it finds a hit.
- **Hot path?:** code defaults: no. Recommended config: once at boot on H. The first game-thread `ready()` caller waits on it.
- **Why it matters:** Boot latency that a game thread waits through.
- **Fix (behaviour-preserving):** Keep the archives open across both lookups, searching in the same `FindFirstFile` order.
- **Refactor risk:** Keep "first zip in enumeration order wins". octtest `--mixer` calls `readRoot10` directly.
- **Verification:** CONFIRMED.

### AU-24 Miscellaneous allocation churn
- **Where:** `native/di1compat/audio.hpp:1461`, 203-205 (+332-343), 809, 1020, 1257-1288
- **Severity / category / class:** Low · Perf (cold) · SAFE-REFACTOR
- **Evidence:**
  - At 1461 the ternary (prvalue vs lvalue) copies the bank vector.
  - `LuaV::get` builds a `std::string` key (non-transparent `std::map`), and `str` returns by value inside the mixer loops.
  - `ids.swap(done)` (809) drops `done`'s capacity every tick.
  - Outputs grow without `reserve`.
- **Hot path?:** code defaults: no. Recommended config: only `tick` (809) recurs, and only when something finished.
- **Why it matters:** Small, avoidable allocations.
- **Fix (behaviour-preserving):** Bind by const reference and add `reserve`. Purely mechanical.
- **Refactor risk:** None; output bytes are unchanged.
- **Verification:** CONFIRMED.

### AU-27 `vo.scripts` keeps stale engine pointers
- **Where:** `native/di1compat/audio.hpp:1309`, 2015, 2050
- **Severity / category / class:** Low · Latent bug · DOC-ONLY
- **Evidence:** Engine-heap pointers are inserted and never erased. If 3.0 frees a script and a later 3.0-native script lands at the same address, `hkDlgReq` adds lower-case aliases to it.
- **Why it matters:** A 3.0 script could be treated as a 1.0 one.
- **Fix (behaviour-preserving):** None without an unload hook; document it.
- **Refactor risk:** None.
- **Verification:** UNVERIFIED. Settle it by checking whether `FUN_00409ed0` frees `+0x7c` script blobs at zone unload.

### AU-28 Recursive `hkBankReq` passes stack strings to the engine
- **Where:** `native/di1compat/audio.hpp:1678-1682`
- **Severity / category / class:** Low · Latent bug · DOC-ONLY
- **Evidence:** `dreq[2] = d.c_str()` and `dreq[4] = desc.c_str()` reach `oBankReq` (3.0 `FUN_0182db30`) and die when the call returns.
- **Why it matters:** The pointers dangle if 3.0 keeps them for asynchronous use.
- **Fix (behaviour-preserving):** Document it.
- **Refactor risk:** None.
- **Verification:** UNVERIFIED. Settle it by checking whether `FUN_0182db30` copies or queues `req[2]`/`req[4]`.

### AU-29 `Host::load`/`unload` semantics to preserve
- **Where:** `native/di1compat/audio.hpp:580-584`, 639, 480
- **Severity / category / class:** Low · Behaviour question · DOC-ONLY
- **Evidence:**
  - For a same-name event the last load wins. Unloading the winner erases the name even if another project still defines it (639).
  - A second request for a loaded project naming another group only bumps `refs`: no `LoadSampleData`.
- **Why it matters:** These look like bugs but may match 1.0; a refactor must not "fix" them silently.
- **Fix (behaviour-preserving):** Document them; do not change them.
- **Refactor risk:** None if left alone.
- **Verification:** CONFIRMED. Whether 1.0 behaves the same is UNVERIFIED.

### AU-31 Unchecked FMOD results
- **Where:** `native/di1compat/audio.hpp:684`, 716 (`Event_SetCallback`), 456, 459, 804, 878
- **Severity / category / class:** Low · Correctness · BEHAVIOUR-CHANGE
- **Evidence:** If `Event_SetCallback` fails, EVENTFINISHED never arrives: the `Play` leaks and 3.0 never gets EndOfEvent.
- **Why it matters:** The failures are silent.
- **Fix (behaviour-preserving):** None; logging on failure adds new log text, so flag it for the user.
- **Refactor risk:** None.
- **Verification:** CONFIRMED.

### AU-32 `dbnkFromName10` fragility
- **Where:** `native/di1compat/audio.hpp:1011`, 1016-1021 (also 1000)
- **Severity / category / class:** Low · Correctness/Fragility · SAFE-REFACTOR
- **Evidence:**
  - `(uint16_t)(base + (v & 0xfff))` and `(uint16_t)lines` truncate silently.
  - `me` is computed before `h.insert` and `rec` after it. That is correct now but breaks if reordered.
- **Why it matters:** A future edit could write through a pointer invalidated by the insert.
- **Fix (behaviour-preserving):** Index-based writes (`wr32(h, off, v)`).
- **Refactor risk:** Output must stay byte-identical. octtest `--vo` checks the records (octtest.cpp:2303-2320).
- **Verification:** CONFIRMED.

### AU-34 `LuaParse` limitations
- **Where:** `native/di1compat/audio.hpp:210-217`, 252-271
- **Severity / category / class:** Low · Correctness (latent) · DOC-ONLY
- **Evidence:**
  - `--[[...]]` is skipped like a line comment.
  - No `[[long strings]]`.
  - A bare identifier value fails the whole parse.
  - Today's Mixer.lua parses: octtest `--host` checks sfx 0.707946.
- **Why it matters:** A future Mixer.lua using these forms would silently lose the mixer.
- **Fix (behaviour-preserving):** Document. Extending the parser is a behaviour change.
- **Refactor risk:** None.
- **Verification:** CONFIRMED.

### AU-35 Never-freed and unbounded resources; no shutdown
- **Where:** `native/di1compat/audio.hpp:1304`/1372, 1300/1331, 1375, 1632, 1882, 2102, 1309, 124-126, 2175
- **Severity / category / class:** Low · Resources · DOC-ONLY
- **Evidence:**
  - These only grow: `hdrs`, `banks`, the `calloc`'d slot, the trace sets and `scripts`.
  - If `fmod_event` fails to load, `fmodex` stays loaded.
  - There is no `EventSystem_Release`, and the host loop never exits.
- **Why it matters:** Growth over a long session, and no clean shutdown.
- **Fix (behaviour-preserving):** Document. Freeing anything is a behaviour change.
- **Refactor risk:** 3.0 and posted lines hold pointers into `hdrs`, `banks` and `slot`.
- **Verification:** CONFIRMED.

### AU-36 Raw engine offsets and static VAs inline
- **Where:** `native/di1compat/audio.hpp:1363-1391`, 1859-1862, 2045-2053, 1644-1680, 1938, 2096, 2128, 2198-2232
- **Severity / category / class:** Low · Readability · SAFE-REFACTOR
- **Evidence:**
  - Offsets: `mgr+0xd4`, `node+0x14+i*4` (3×), the slot layout (0x50/+0x20/+0x28/+0x2c), `self+0xc` (2×), and the DialogueManager offsets (+0x58/+0x48/+4/+0x7c/+8/+0xc).
  - Ten `rt()` VAs.
- **Why it matters:** Reverse-engineered facts are scattered across functions.
- **Fix (behaviour-preserving):** `namespace eng { constexpr ... }` plus typed accessors; diff the generated code.
- **Refactor risk:** The values must stay byte-identical.
- **Verification:** CORRECTED: severity Med → Low. Each offset is used once or twice, with no real drift risk.

### AU-37 FMOD magic constants; duplicated FMOD struct layouts and category walk
- **Where:** `native/di1compat/audio.hpp:441`, 453-457, 490, 495, 519, 529, 675, 759, 835-838, 857-858, 1344, 1898-1912; octtest.cpp:1814-1815, 1830-1831, 1953-1962, 2123-2130
- **Severity / category / class:** Low · Readability/Duplication · SAFE-REFACTOR
- **Evidence:**
  - Unnamed numbers: property indices 35/13/14, INFOONLY 4, `info+0x20`/`+4`, callback types 3/4/6, the sound modes, and `Init(64, 2, …, 10)`.
  - The cue iterator structs are declared three times.
  - The category walk appears four times: uncapped at 500-511, capped `d < 8` at 1901, and twice in octtest.
- **Why it matters:** The same facts are restated in several places.
- **Fix (behaviour-preserving):** Named constants, `FmodCueIterator`/`FmodEventInfo` structs, and one walker with a depth parameter.
- **Refactor risk:** Keep 500-511 uncapped and the trace walk capped at 8.
- **Verification:** CORRECTED: line 675 is `EventSystem_GetEventBySystemID(..., 4, ...)`, not `GetEventByIndex`.

### AU-38 Unverified `rt()` targets; `install` ignores hook results
- **Where:** `native/di1compat/audio.hpp:2198-2238`; di1compat.cpp:4338
- **Severity / category / class:** Low · Robustness · BEHAVIOUR-CHANGE
- **Evidence:**
  - The ten called or dereferenced VAs get no prologue check.
  - `install` returns true even if every `hook()` was refused. Each refusal is still logged by `hook()` itself (di1compat.cpp:121, 126).
- **Why it matters:** A partly hooked audio host runs with no summary of what is missing.
- **Fix (behaviour-preserving):** None; prologue checks and a summary log line are new log text, so flag them.
- **Refactor risk:** None.
- **Verification:** CONFIRMED.

### AU-39 Debug probes and test-only state in shipping types
- **Where:** `native/di1compat/audio.hpp:1869-1923`, 2100-2125, 1957-1971, 370-373, 390, 394, 402-403, 772, 2141
- **Severity / category / class:** Low · Hygiene · SAFE-REFACTOR
- **Evidence:**
  - `Play::t0` calls `GetTickCount()` on every play construction.
  - The trace-only fields are written whether or not trace is on.
- **Why it matters:** Trace and test state is mixed into the shipping types.
- **Fix (behaviour-preserving):** Move trace bodies into functions. Keep them compiled in, because `audiotrace10` is user-facing, and keep the text byte-identical.
- **Refactor risk:** Log text.
- **Verification:** CONFIRMED.

### AU-40 Dead state
- **Where:** `native/di1compat/audio.hpp:171`/601, 1572
- **Severity / category / class:** Low · Dead code · SAFE-REFACTOR
- **Evidence:**
  - `Project::registered` is written and never read (grep).
  - `Cfg::on` is never referenced (grep across audio.hpp, di1compat.cpp, octtest.cpp).
- **Why it matters:** Dead fields mislead readers.
- **Fix (behaviour-preserving):** Delete both.
- **Refactor risk:** None.
- **Verification:** CONFIRMED.

### AU-41 Intra-file duplicated idioms
- **Where:**
  - variant decode: `native/di1compat/audio.hpp:959`, 1010-1011, 1441, 1464
  - `"%c%c"` prefix formatting: 1352, 1377, 1384, 1392, 1395, 1455, 1466, 1469
  - `.fev`/`_str` suffix checks: 621, 1341, 1564
  - stem extraction: 579, 634, 1335, 1563
  - `fail` lambdas: 319, 1070
  - `"Music_"+x+".fev"`: 888, 1586
  - `h131(name, false)` twice: 2036, 2042
- **Severity / category / class:** Low · Duplication · SAFE-REFACTOR
- **Evidence:** Each idiom is restated inline at every listed site.
- **Why it matters:** Small copies drift.
- **Fix (behaviour-preserving):** `variants`/`firstSub`/`fmtPrefix`/`iendsWith`/`stemOf` helpers.
- **Refactor risk:** Byte-identical formatting.
- **Verification:** CONFIRMED.

### AU-42 Cross-file duplicates, including a non-identical `h131`
- **Where:** `native/di1compat/audio.hpp:921-926` vs `vfx.hpp:36-41`; audio.hpp:53 vs npc.hpp:361 / sx.hpp:799 / di1compat.cpp:478; audio.hpp:928 vs octtest.cpp:31
- **Severity / category / class:** Low · Duplication · DOC-ONLY
- **Evidence:**
  - audio's `h131` adds `(uint8_t)*s` (unsigned); vfx's adds `(int32_t)(signed char)s[i]`. Both cite 1.0 `FUN_0040bac0`. They agree only on 7-bit ASCII.
  - **Added:** the Python oracles mirror each side. `vocheck.py` (vo10 oracle) hashes unsigned bytes and `particles.py` hashes signed. So neither oracle would catch a wrong merge.
  - **Settled by disassembly (parent, 2026-10-16):** the engine sign-extends. 1.0 `DisneyInfinity1.exe` `FUN_0040bac0`, 3.0 `FUN_0040bb50` (case kept) and 3.0 `FUN_0040bb80` (lower-cased) all load each byte with `movsx` before `imul eax, eax, 0x83` / `add`. `FUN_0040bb80` passes the sign-extended byte to the CRT `tolower` call. So `vfx::h131` and `particles.py` match the engine. `audio::h131` and `vocheck.py` do **not**, for any byte ≥ 0x80. Today this is harmless only if every hashed event/entry name is 7-bit ASCII (no non-ASCII name is known).
  - `audio::lower` is identical to `npc::lower`. `sx::lower` also maps `\`, and `norm` also strips `assets/`.
- **Why it matters:** A "dedupe" merge of `h131` would silently change one side's hashes. And audio's variant is a latent mismatch with the engine for non-ASCII names.
- **Fix (behaviour-preserving):** Do not merge `h131` without a signedness parameter. `lower` may be shared with `npc` only. Making audio sign-extend like the engine (and `vocheck.py` with it) is a **BEHAVIOUR-CHANGE** for non-ASCII names only. Decide it explicitly; it is not part of the mechanical refactor.
- **Refactor risk:** Merging helpers with different semantics changes behaviour.
- **Verification:** CORRECTED: severity "Low-Med" → Low / DOC-ONLY; added the oracle and signedness detail; engine signedness confirmed by disassembly (above).

### AU-43 Cryptic idioms and leaks
- **Where:** `native/di1compat/audio.hpp:1555`, 1565, 42, 1637
- **Severity / category / class:** Low · Readability · SAFE-REFACTOR
- **Evidence:**
  - `p.rfind("sound/dialog/", 0)` is used as starts-with, with inverted truth.
  - `AUDLOG` is never `#undef`'d and leaks into every later header in the TU.
  - `trace(id ^ fnv1(how))`.
- **Why it matters:** These are easy to misread.
- **Fix (behaviour-preserving):** Named helpers. Keep `AUDLOG`'s null check of `logfn`.
- **Refactor risk:** None.
- **Verification:** CONFIRMED.

### AU-05 Config split and dead flag
- **Where:** `native/di1compat/audio.hpp:1571-1578`; di1compat.cpp:216, 329-334, 1631, 3691, 4245, 4334-4337
- **Severity / category / class:** Low · Design · SAFE-REFACTOR
- **Evidence:**
  - `Cfg::on` is unused.
  - `cineSync10` and `vo10` are gated at di1compat call sites (1631, 3691, 4245) instead of inside `audio::`.
- **Why it matters:** The audio config lives in two places.
- **Fix (behaviour-preserving):** Delete `on`. If `cineSync` moves into `audio::cfg`, assign it **unconditionally and before the hook at 4246**. Today `audio::cfg` is filled only inside the `install10` branch (4334-4337), after `hkChoStart` is already live.
- **Refactor risk:** The config key names (329-334) must not change.
- **Verification:** CONFIRMED (risk added).

## Inventory
Every row was checked mechanically against HEAD 684539b. Line ranges come from the opening line and the matching closing brace. All rows are kept; only wrong cells were changed.

**Corrections to the report's tables:**
- `lower` hot/cold should read "Load". `hkBankReq` is a load path, not a hot one.
- Thread "L" is now defined (header).
- The `gameplay.hpp:125` duplicate is `luaTokens` (with `LuaTok` at 124), not `luaTableEntries`.
- `sx.hpp`'s FNV-1a is `keyFallback` at 243-248.
- Section 7 of the report also misses `vo.lang()` under `vo.mx` (header).

**Functions (verified ranges; >40-line functions in bold):**

| Name | Lines | Hot/cold (recommended config; none are installed under code defaults) |
|---|---|---|
| `fnv1` / overload | 45-50 / 51 | per unknown post (`hkPost`), per action, trace |
| `lower` | 53-57 | Load (**corrected** from Hot) |
| **`struct Api`** | 60-156 | Api::load 122-155 (Once) |
| `Entry` / `Project` | 159-165 / 167-173 | — |
| `splitLoad` | 177-191 | Load |
| `LuaV` / **`LuaParse`** (ws 210-217, word 218-224, **value 225-272**) | 197-206 / 207-273 | Once |
| `readRoot10` | 277-294 | Once (H) |
| `mixerPaths` | 298-307 | Once |
| `MixerRest` / **`mixerRest`** | 316 / 317-357 | Once |
| `Play` / `V3` | 360-374 / 375 | — |
| **`struct Host`** | 377-829 | — |
| `applyVolumes` | 408-423 | Load / per fader |
| `init` | 428-465 | Once (H) |
| `name10` | 469-476 | per bank request / cue |
| **`registerGroup`** | 481-542 | Load |
| `collectDeps` / `depsOf` | 545-555 / 565-573 | Load |
| `load` / `loadTree` / `unload` | 575-611 / 615-625 / 627-646 | Load |
| `onEvent` | decl 648, def 833-843 | per FMOD callback (F) |
| `newId` / `place` | 650 / 652-661 | per play; tick × plays |
| `play` / `playLine` / `standIn` | 669-689 / 696-727 / 730-736 | per 1.0 post / per dialog line / rare |
| `act` / `position` / `stopObject` / `pause` | 739-749 / 752-761 / 763-770 / 774-786 | per stop / per lipsync query (per frame) / per unregister / rare |
| `tick` | 790-828 | H every 16 ms |
| `g` / **`Music`** (cue 855-862, endPrompt 864-870, begin 873-882, ensure 885-896, drop 898-908) / `music` | 831 / 846-909 / 910 | per cue |
| `h131` / `slurp` | 921-926 / 928-939 | Load / per dialog request |
| `Name10` (valid 948-962) / `DlgBank` | 944-969 / 971-977 | per dialog post / Load |
| **`dbnkFromName10`** | 983-1029 | Load |
| `DlgStats` / `DlgSeqItem` / `DlgSeq` / `dlgLineKey` | 1054 / 1061 / 1062 / 1064 | — |
| **`dlgTranscode`** | 1065-1293 | Load (per .dlg) |
| **`struct Vo`** | 1295-1544 | — |
| `pickLanguage` / `open` / `publish` / `load` / `unload` | 1316-1323 / 1327-1355 / 1360-1396 / 1398-1407 / 1409-1425 | Load (D) |
| `lineOf` / `onLinePosted` / `playSeqLine` / `seqTick` | 1436-1445 / 1446-1457 / 1458-1471 / 1474-1498 | per dialog post / H tick |
| `media` / `buildEvNames` / `vo` | 1501-1513 / 1516-1543 / 1545 | per dialog post / once / — |
| `annoSource` | 1551-1568 | per AssetOpen miss (serve10 only) |
| `Cfg` / `cfg` / `sound30` / `ready` decl | 1571-1577 / 1578 / 1579 / 1580 | — |
| `hostedMusic` | 1583-1587 | per music bank request / cue |
| `hkFader` | 1607-1615 | per fader set |
| `trace` | 1633-1638 | trace only |
| **`hkBankReq`** | 1641-1685 | Load (D) |
| `hkPost` | 1688-1714 | **every Wwise post** (lock only for unknown) |
| `hkSetPos` / `hkListener` | 1717-1725 / 1728-1739 | per frame (likely) |
| `hkSrcPos` / `hkStop` / `hkUnreg` / `hkAction` | 1746-1751 / 1754-1758 / 1761-1765 / 1768-1784 | per frame during lipsync / per stop / per unregister / per action |
| `hkMusicState` / `hkSetState` / `hkMusicOnce` | 1788-1802 / 1805-1815 / 1825-1839 | per cue push / per state set / per stinger |
| `hkStackTick` | 1845-1865 | per frame (try-lock) |
| **`hkQueuedPost`** | 1869-1923 | trace only (not installed) |
| **`hkPostExt`** | 1932-1976 | per dialog post |
| `hkLoadFile` / `hkDlgReq` | 1987-2019 / 2031-2062 | per sound-file read / per dialog request |
| `choreographyStarted` | 2074-2086 | per choreography start (also under code defaults) |
| **`tickAndDeliver`** | 2089-2133 | H every 16 ms |
| `HostStart` / `hostStart` / `hostState` | 2141 / 2142 / 2143 | — |
| `loadMixer` / `hostThread` / `ready` | 2145-2162 / 2164-2179 / 2182-2191 | Once / thread / every gated hook (one volatile read after the first) |
| **`install`** | 2195-2239 | Once (P) |

**Hooks installed by `audio::install`:** all 17 name strings, static VAs and prologue strings match `install` (2207-2237) exactly. The report's hooks table is kept as is, with two notes:
- Every hook requires `g_cfg.audio10 && g_cfg.install10[0]` (di1compat.cpp:4332), so none exists under code defaults. All except "queued sound post (audiotrace10)" exist under the recommended config.
- "choreography start (cinesync10 / breaktrace)" 0x01449390 `83EC18568BF1E8` (di1compat.cpp:4246) is installed **without** the `install10` condition, and before `audio::install`.

| Name string | Static VA | Prologue | Detour → original | Gating beyond install | Frequency / thread |
|---|---|---|---|---|---|
| sound bank request (audio10) | 0x0182DB30 | 568B711C5785F6 | hkBankReq → oBankReq (thiscall) | — | per bank load/unload; D |
| Wwise PostEvent (audio10) | 0x01948A10 | 558BEC8B45088B0D???????? | hkPost → oPost | — | every Wwise post; G |
| Wwise SetPosition (audio10) | 0x01947A50 | 558BEC8B450885C07507 | hkSetPos → oSetPos | — (no ready) | per positioned object; G |
| Wwise SetListenerPosition (audio10) | 0x01947150 | 558BEC535657E8 | hkListener → oListener | — | likely per frame; G |
| Wwise StopPlayingID (audio10) | 0x019478D0 | 558BEC568B750885F6 | hkStop → oStop | — | per stop; G |
| Wwise GetSourcePlayPosition (audio10) | 0x019475A0 | 558BEC8B450C85C07507 | hkSrcPos → oSrcPos | — | per frame during lipsync; G |
| Wwise UnregisterGameObj (audio10) | 0x01947640 | 558BEC568B750885F67506 | hkUnreg → oUnreg | — | per unregister; G |
| Wwise ExecuteActionOnEvent (audio10) | 0x01948C20 | 558BEC8B450850E8 | hkAction → oAction | — | per action; G |
| volume fader (audio10) | 0x0182C460 | 8B4424048B4C240883EC30 | hkFader → oFader (stdcall, ret 0x10) | — | per fader set |
| MusicStates cue check (music10) | 0x007397B0 | 578BF9837F1400 | hkMusicState → oMusicState (thiscall) | cfg.music | per cue push |
| Wwise SetState (music10) | 0x01947BD0 | 558BEC8B450856 | hkSetState → oSetState | cfg.music | per SetState (from the MusicStack tick) |
| MusicStack tick (music10) | 0x0073EDE0 | A1????????DB05????????81EC04010000 | hkStackTick → oStackTick (thiscall) | cfg.music | per frame |
| music cue once (music10) | 0x0073ED10 | 8B44240883EC08535657 | hkMusicOnce → oMusicOnce (thiscall, ret 0xc) | cfg.music | per stinger |
| dialog line post (vo10) | 0x01948D80 | 558BEC83EC105356578B7D1C | hkPostExt → oPostExt | cfg.vo | per dialog post |
| sound file read (vo10) | 0x0182F570 | 8B44240485C0741F803800741A | hkLoadFile → oLoadFile | cfg.vo | per sound-file read; L |
| dialog request (vo10) | 0x0073BF50 | 53568BF1E8????????8D4618 | hkDlgReq → oDlgReq (thiscall, ret 8) | cfg.vo | per dialog request |
| queued sound post (audiotrace10) | 0x01830270 | 83EC44538B5C244C | hkQueuedPost → oQueuedPost (thiscall) | cfg.trace = audiotrace10 (off in both configs) | per queued post |

**`rt()` addresses that are called or dereferenced but not prologue-checked (2198-2232), verified:**

| VA | Used as |
|---|---|
| 0x022AFBE8 | `g.idCounter` |
| 0x0073B2C0 | `stackPop` |
| 0x00680C00 | `vo.lang` (called under `vo.mx`) |
| 0x0228D668 | `vo.mgr` |
| 0x013CC1F0 | `vo.mapFind` |
| 0x018308E0 | `vo.mapAdd` |
| 0x006EAA00 | `mapPut` |
| 0x02204128 | `dialogMgr` |
| 0x00409E90 | `engineAlloc` |
| 0x00409ED0 | `engineFree` |

**Calling conventions (verified):**
- `__fastcall` thiscall emulation: hkBankReq, hkMusicState, hkStackTick, hkMusicOnce, hkQueuedPost, hkDlgReq, mapFind, mapAdd, mapPut, stackPop.
- `__stdcall`: hkFader, every FMOD export, onEvent.
- `__cdecl`: the Wwise exports, AkCallback, LoadFileFn, AllocFn, FreeFn.

**Helpers (verified; corrections inline):**

| Name | Line | Known duplicates |
|---|---|---|
| `AUDLOG` | 42 | macro leaks (AU-43) |
| `fnv1` | 45, 51 | `sx.hpp:243-248 keyFallback` is FNV-1a exact-case (different); `di1compat.cpp:3779 fnv64` (different) |
| `lower` | 53 | `npc.hpp:361` identical; `sx.hpp:799` also maps `\`; `di1compat.cpp:478 norm` also strips `assets/` |
| `splitLoad` | 177 | — |
| `LuaV` / `LuaParse` | 197 / 207 | `gameplay.hpp:124-125 LuaTok/luaTokens`, `ui.hpp:101 luaTable` (tokenizers; not equivalent) |
| `readRoot10` | 277 | `di1compat.cpp` `zipFor`/`read10` (cached, locked; not visible to the header or octtest) |
| `mixerPaths` | 298 | — |
| `h131` | 921 | `vfx.hpp:36` uses **signed** bytes and has no lower-casing (AU-42) |
| `slurp` | 928 | `octtest.cpp:31 slurp(path) -> string` |
| `Name10::u16` | 947 | returns 0xffff out of range; `gen30.hpp:43 u16` returns 0; `vfx.hpp:31 rd16` unbounded |
| `rd32` / `t32` / `t16` / `tstr` | 1071, 1169-1171 | `gen30.hpp:42 u32` (bounded, on std::string), `vfx.hpp:30`, `btd.hpp:15`, `dct.hpp:40` (unbounded); these bounded readers wrap at the extreme (AU-25) |
| `align` / `clampd` / `fail` | 1254 / 329 / 319, 1070 | `std::clamp` |
| `dlgLineKey` | 1064 | — |
| suffix check | 621, 1341, 1564 | `di1compat.cpp:472 endsWith` |
| stem | 579, 634, 1335, 1563 | — |
| category walk | 500-511 (uncapped), 1899-1912 (`d<8`) | octtest 1953-1962, 2123-2130 |

**Globals:** the report's table was checked row by row and is correct, with three additions:
- The writer/reader thread "L" is now defined (header).
- `vo.lang` is also *called* under `vo.mx` (from `pickLanguage`).
- `music.prompt` is also written from `hkPost`'s StopMusic branch (1707) and from `drop` (902), both under `g.mx`.

## Rejected on verification
- None.
