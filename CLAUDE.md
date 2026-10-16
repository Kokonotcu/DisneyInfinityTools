C++ rules for di1compat

Every agent reads this whole file before it writes or changes C++ in this repository. If you give C++ work to another
agent, tell it to read this file first. Rules have numbers so reviews and commits can point to them.

Scope

These rules cover the plugin's C++ in native/di1compat: di1compat.cpp and every header and source it builds from.
They do not cover test code (octtest.cpp, the offline test program), the third-party code we copied in (minhook/,
third_party/miniz/, renderdoc_app.h), the generated file ports10.hpp (change portmap.py instead), or the Python and
PowerShell tools.

New and changed code follows every rule. Old code that breaks a rule is listed in docs/audit and gets fixed by the plan
there. Never make an old violation worse.

Never run the game executable. Never run harness.ps1 or lua.ps1.

Before you write code

P1. Read docs/audit/README.md and the audit file for every file you will change.
P2. Look for an existing helper, hook, table or state before you add one. Read the whole file you find, not only the
    search hit. If something almost fits, use it or extend it.
P3. Know how often your code runs: once at start, per load, per file open, per frame or per draw. It decides which
    speed rules apply.
P4. A refactor never changes behaviour. A behaviour change needs the user's approval and its own commit.

Never do these

N0. No comments exceeding 20 words, make them informative not bloated.
N1. No singletons: no static T& get(), no instance(), no lazily built function-local static objects used as services.
N2. No new changeable global variables outside a module's State (see S1).
N3. No globals used as hidden parameters (set a global, then call a function that reads it). Pass an options struct.
N4. No if / else if chain with more than 3 branches that test the same thing (a string, an extension, a flag, an enum).
    Use a table of {key, handler} or a switch.
N5. No condition with more than 3 parts, or one that mixes && and ||. Put the parts in named const bool variables.
N6. No nesting deeper than 3 levels (if, for, while, switch, lambda). Return or continue early instead.
N7. No function longer than 60 lines. Do not add lines to a function that is already longer: split it first.
N8. No new code in a file longer than 800 lines. Put it in a new or smaller module.
N9. No god structs: a struct longer than 200 lines, with more than one mutex, or whose members are used by separate
    groups of functions. Split it.
N10. No bool parameter that switches what a function does. Write two functions, or pass an enum class or options struct.
N11. No more than 5 parameters. Use a parameter struct.
N12. No copied blocks of 5 or more lines. Make a function. If the copies differ on purpose, name the difference.
N13. Do not merge two similar helpers until you have proved they give the same result for every input. The two h131
     hashes differ in char signedness; such variants keep separate names.
N14. No magic numbers. Engine addresses, object offsets, file offsets, sizes and enum values get a named constexpr with
     a comment that says where the value comes from (FUN_ address, FORMATS.md section, measurement).
N15. No #include below the top of a file.
N16. No header that only compiles because of what the including file defined or included first.
N17. No namespace-level static data or static functions in headers. Use inline functions, and keep data in a State.
N18. No catch-all helper files such as utils.hpp or misc.hpp. Name a header after what it holds.
N19. No owning raw pointers and no new/delete. Use std::unique_ptr and containers. Only memory the engine keeps a
     pointer to may be allocated once and never freed (S5).
N20. No reading file data through pointer casts like *(uint32_t*)p, and no bounds check written as off + n <= size.
     Use the checks in base/bytes.hpp (E3).
N21. No sprintf_s or strcpy_s on text whose length you do not control. With the static runtime an overflow kills the
     game. Use snprintf with a size, or std::string.
N22. No C++ exception may leave a hook, a thunk or an engine callback. Catch it at the edge (H6).
N23. Never read engine memory in a way that can fault and rely on __try to save you. Check first (H8).
N24. No goto, no commented-out code, no dead code, no TODO without an audit ID such as TODO(GR-15).
N25. No lambda longer than 15 lines, and no lambda defined inside a loop. Use a named function.
N26. No using namespace in headers. No macros except include guards; use constexpr, inline and templates. #undef any
     local macro.
N27. No inheritance, except to match an outside binary interface (COM IUnknown, an engine vtable).
N28. Never change a stable name: hook name strings, config keys and their defaults, existing log text. Changing one is a
     behaviour change.
N29. Never edit ports10.hpp by hand. Change portmap.py and run it again.
N30. Do not reformat or rename code you are not otherwise changing. Keep the diff to what the change needs.

Modules

The target layout. The refactor in docs/audit moves the current flat files into it. New code goes there now.
Everything outside games/ is shared: it must work for any source game, so a later 2.0 port can reuse it as it is.
Only code that exists because of how 1.0 data looks goes in games/di1.

  di1compat.cpp  the Pentane entry only: exports, Pentane_Main, which starts core and calls each module's install()
  base/          small helpers with no engine code: bytes.hpp (bounds checks, little-endian reads), hash.hpp,
                 text.hpp (case and path helpers), lua_lex.hpp
  formats/       one header per file format: read and write, byte for byte (oct, btd, chd, dct, arc30, luachunk, sm3)
  convert/       steps that only need to know 3.0's form: record builders and editors, the counts and layouts 3.0
                 expects. Data in, data out.
  core/          log, config table, hook table and hook(), engine address helpers (rt), memory checks (readable,
                 guarded copy), serving files to the engine, the zip archive reader
  engine/        3.0 engine features any source game uses, one module each: its engine addresses, State, hooks and
                 install(). Examples: 3.0 asset reads, GameDB, zone list, UI glue, shader compile and cache, FMOD host.
  games/di1/     everything specific to 1.0: its translation rules and tables (ports10), its install layout, its
                 transcode dispatch, its runtime features, its config keys
  trace/         log-only diagnostic hooks, built like engine modules

M1. Includes go one way only. formats uses base. convert uses formats and base. core uses base. engine uses core,
    convert, formats and base. games/di1 may use everything shared. trace uses core. di1compat.cpp uses all of them.
    Shared folders never include games/. Never include one engine module or one game from another. If two need the
    same thing, move it down a level.
M2. base, formats, convert and the translation code in games/di1 never touch the engine, hooks, the log or the
    config. They report failure through their return value and a std::string* why. They take options as a parameter.
    They read other files only through a function object they are given (the render IO struct is the example to
    follow). This keeps them testable without the game.
M3. A module has one job. Its file name and its namespace both name that job, for example formats/btd.hpp and
    namespace btd. Only the Pentane exports and DllMain live at global scope.
M4. A header starts with a comment of at most 6 lines: what the module does, what it depends on, which threads use it.
    Its public functions come first. Its internals go in namespace detail.
M5. Every header starts with #pragma once, includes everything it uses, and compiles on its own:
    cl /Zs /std:c++17 /EHsc <header>.
M6. An engine or game module has one entry point: bool install(const Config&). Nothing outside it reaches into it.
M7. A sequence of passes that both the plugin and a test need (for example the 1.0 .oct transcode order) lives once,
    and both call it. Never write a second copy.
M8. Decide where code goes with one question: is it correct for any source game, or only because of how 1.0 data
    looks? General mechanisms (codecs, record editing, hooking, serving files, the FMOD host, Lua reading, the zip
    reader) go in shared folders. 1.0 facts (enum values, port numbers, file layouts, install paths, which passes run
    and in which order) go in games/di1.
M9. When a function mixes both, split it. The mechanism goes in a shared folder. The 1.0 facts become a table, an
    options struct or a function object that games/di1 passes in.
M10. Shared code never asks which game is running and carries no 1.0 marker in its names (no "10", "di1" or "1.0").
     A game plugs in through data: its archive root, its transcode dispatch table, its tables and its install().
     Adding a game means adding a folder under games/, not adding if-chains to shared code.
M11. Stable names keep their 1.0 markers: config keys such as serve10, hook name strings and log text (B1). Rename an
     internal identifier only when you move its code into a shared folder.

State

Engine hooks have fixed engine signatures and get no context pointer, so an engine or game module needs some
module-level state. This is the only allowed form.

S1. An engine or game module has at most one struct State and one instance of it. Only that module's functions use
    it. Other modules call functions; they never touch another module's State.
S2. install() creates and fills the State. No lazy creation, no get(), no hidden start order.
S3. Every changeable State member has a comment that names its lock or its thread, for example "guarded by mx",
    "render thread only" or "set once in install()".
S4. The config is read once, before any hook is installed, and never changes after that. Engine and game modules get
    it as const Config&. Conversion code gets only the options struct it needs.
S5. Anything the engine may keep a pointer to (a vtable, a registered string, a class definition) is allocated once,
    never freed and never moved. Say so in a comment.
S6. No thread_local for state that must survive an engine fiber switch. Engine fibers share threads.
S7. The original-function pointers of hooks live in the module's hook table. Only hook() writes them.

Functions

C1. A function does one thing and its name says what. If the name needs "and", split it.
C2. At most 60 lines and 3 levels of nesting. Return early instead of using else after return.
C3. When the order of a dispatch table matters (first match wins), say so in a comment above the table.
C4. One statement per line. Nothing that changes state inside a condition, except a C++17 if (init; cond).
C5. Return results by value (a struct or std::optional). If you keep an out-parameter for an old contract, write down
    whether it is written on failure, and keep that contract (for example "false means out is untouched").
C6. Pass read-only data as const& or std::string_view. Hand over ownership with std::move. Never copy a buffer just to
    read it.
C7. Move work that does not change inside a loop out of the loop and out of its condition. One call to f.blockEnd(p)
    in a for condition made a 0.8 ms pass take 132 ms (GR-04).
C8. Do not use a record index again after your own insert or erase has moved the records. Collect the edits and apply
    them in one pass (octx::editPoolChildren is the example).

Hooks and the engine

H1. Every hook is a row in its module's hook table: name, static address, prologue bytes, detour, original pointer,
    config switch. It is installed through core's hook(). Never call MH_ functions directly. Keep the install order:
    a hook is live the moment it is enabled.
H2. A feature installs all of its hooks or none. Check every prologue of the feature first. If one does not match,
    log which one, install none of the feature's hooks, and log that the feature is off. A feature that needs another
    feature's hook (for example everything that calls the asset-open original) is off when that hook is off.
H3. A detour's signature matches the engine function exactly. thiscall is written as
    __fastcall(void* self, void* edx, ...). The return type keeps the engine's size. The comment above each detour
    names the engine function (FUN_ address), its calling convention, the threads it runs on and how often it runs.
H4. Every engine function you call, not only the ones you hook, gets a prologue check before its first use, or a
    comment that says why it cannot have one.
H5. Engine addresses and object offsets live in the module's namespace eng as named constexpr. Copy values exactly.
    A changed address or offset is a behaviour change.
H6. Nothing thrown may reach engine or FMOD code. Catch it at the detour edge. If the detour already called the
    original, do not call it again in the catch.
H7. A function that contains __try has no C++ objects with destructors and no C++ try (MSVC errors C2712 and C2713).
    Put the guarded read in a small function of its own.
H8. Read engine memory only through core's checks (readable, guarded copy). Never cause a fault on purpose. Each
    handled access violation still writes a slow entry to pentane.log and uses one of CrabeLoader's 3 crash reports.
H9. Code in a vectored exception handler (avHandler, watchHandler and everything they call) must never fault and must
    never allocate. A fault inside such a handler is what crashes the game.
H10. Pentane_Main and every install() run while Windows holds the loader lock. They must not wait for another thread
     or take a lock another thread could hold.
H11. Do not keep an engine pointer across a zone change unless a teardown hook clears it.

Threads and locks

L1. Every piece of shared changeable data has exactly one lock, named in a comment (S3).
L2. The lock order is written down in one place in core. Every lock is taken in that order. The log lock is last:
    never take another lock while holding it.
L3. Do not hold a plugin lock during disk access, decompression, D3DCompile, an FMOD load or an engine call, unless the
    lock exists to make exactly that call one-at-a-time. If so, say it in the comment.
L4. Code that runs every frame or every draw never waits on a lock that other threads hold during slow work. Use
    try_lock, a copy or an atomic.
L5. Use std::lock_guard, std::unique_lock, or an RAII wrapper for CRITICAL_SECTION. No manual Enter/Leave pairs. No
    function-local static std::mutex: locks belong in the module State.
L6. "Log this only the first N times" uses the shared LogOnce helper, not a new static set and mutex.
L7. Flags shared between threads are std::atomic. Never use volatile to synchronise threads.

Errors, bad input and logging

E1. A damaged or unexpected 1.0 file must never crash, hang or corrupt memory. It fails the conversion cleanly and
    takes the existing failure path: the reason goes to the log and the game gets the raw 1.0 file. Return failure
    where the function already reports failure that way. Otherwise throw std::runtime_error with the reason; hkOpen's
    catch logs it and serves the raw file. Throw only where such a catch is above you; check the call chain.
E2. Exceptions are for bad input (E1), programming errors and std::bad_alloc only. Never use them for normal control
    flow: every C++ throw also costs a slow stack walk in Pentane's handler.
E3. Check every size, count, offset and index that comes from a file before you use it, with arithmetic that cannot
    overflow. The plugin is 32-bit, so size_t wraps at 4 GiB: write bytes::fits(size, off, n), never off + n <= size.
E4. Log only through core's logf. A new log line starts with a fixed subsystem tag. Existing log text is an interface
    and stays exactly the same (N28).
E5. Check a diagnostic switch before you build its message. Diagnostic hooks live in trace/ behind their switch, and
    diagnostic switches default to off. Real behaviour never hides behind a diagnostic switch.
E6. Nothing fails silently. A refused hook, a missing table or a rejected file is logged once with the reason.

Types and style

T1. C++17, MSVC, 32-bit x86 only. core has static_assert(sizeof(void*) == 4).
T2. Use fixed-size integers (uint32_t and so on) for file and engine data. Use size_t only for sizes in memory. Never
    compare signed and unsigned values.
T3. const by default, constexpr for constants, enum class for new enums.
T4. Write (std::min)(a, b) and (std::max)(a, b). windows.h defines min and max macros.
T5. Names: types PascalCase, functions and variables camelCase, constants kPascalCase, detours hk<EngineThing>,
    original pointers g_<engineThing>, the module state State. Names say what a thing is. No abbreviations beyond the
    project's own (oct, cho, mer, vtx, shd).
T6. Lines are at most 120 characters. Indent with 4 spaces. Place braces like the code around you.
T7. A comment states a rule that must hold, a reason, or evidence (a FUN_ address, a FORMATS.md section, a measured
    number). Comments do not tell history ("used to", "now", dates of test runs); that goes in the commit message.
    Keep comments short and put them above the line they explain.

Speed

F1. Code that runs every frame or every draw does no heap allocation, builds no std::string, makes no uncached system
    call (VirtualQuery, file attributes), logs nothing without a limit, and never waits on a busy lock (L4).
F2. Code that runs per load stays O(n log n) or better in the number of records: no insert or erase in the middle of a
    record vector per item (C8), and no rescan of a pool inside a loop over that pool (C7).
F3. Call reserve when you know the final size. Move large buffers; do not copy them.
F4. Lookups in hot code use std::unordered_map or a sorted vector, never std::map<std::string, ...> or a linear search
    over a container that keeps growing.
F5. A cache belongs to a module State, has a stated size limit or a rule for when it is cleared, and never changes a
    result. A cache that would change a result is a behaviour change.
F6. Measure before and after a speed change (loadprof10, fpslog10, or a timed offline run). Without a number it is not
    a speed fix.

What a change must keep the same

Unless the user approved the change, keep all of these exactly as they are:

B1. Hook name strings, config keys, config defaults, the logged config lines, existing log text.
B2. Every translated file, byte for byte. This includes the order of the oct name table, the generated HLSL text, the
    shader cache key made from it (fnv64 with its shortened start value), and any output that follows the order of an
    ordered container.
B3. Hook install order, the stage order inside hkOpen (first match wins, and two stages fall through), and the order of
    transcode passes.
B4. Engine addresses, offsets, prologue bytes, calling conventions and stack cleanup.
B5. Lock order, which thread runs which code, which thread delivers callbacks.
B6. Failure contracts such as "false means untouched", and what is served when a conversion fails.

When a C++ change is done

D1. The Win32 Release build passes:
    cmake -S native/di1compat -B build -G "Visual Studio 17 2022" -A Win32
    cmake --build build --config Release
    It adds no warnings at /W4. The project builds at /W1 today (docs/audit OT-28), so check the files you touched at
    /W4 yourself.
D2. Every header you touched compiles on its own (M5).
D3. The offline checks for the code still pass, and recorded reference outputs are unchanged byte for byte.
D4. One logical change per commit. A refactor commit says "no behaviour change" and names the audit IDs it fixes, for
    example: refactor(GR-04): hoist blockEnd out of mtc loops.
