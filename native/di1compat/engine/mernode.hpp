// Native Meridian node classes inside 3.0 (FORMATS.md 5.9). formats/mer; registered at boot, thunks on engine threads.
#pragma once
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>
#include <string>
#include <utility>
#include <vector>
#include "../formats/mer.hpp"

namespace mernode {

static_assert(sizeof(void*) == 4, "the engine ABI below is 32-bit x86");

// The 3.0 exe (static VAs): the functions native classes call, their vtables and data (FORMATS.md 5.9)
namespace eng {
constexpr uint32_t kAlloc = 0x00406E80;         // operator new
constexpr uint32_t kFree = 0x00409DF0;          // operator delete
constexpr uint32_t kNodeAlloc = 0x006ABFF0;     // node memory (align, size)
constexpr uint32_t kRegFactory = 0x006BC220;    // registers a node factory under "MNode::<name>"
constexpr uint32_t kHash = 0x018261F0;          // definition id of a name
constexpr uint32_t kDefFind = 0x013CC1F0;       // definition creator map lookup, thiscall on kDefReg
constexpr uint32_t kDefAdd = 0x006EAA00;        // adds a definition creator, thiscall on kDefReg
constexpr uint32_t kNodeLoad = 0x006C8BE0;      // the factories' node load
constexpr uint32_t kDefCtor = 0x006AE840;       // definition base constructor
constexpr uint32_t kPinInCtor = 0x006A7C60;     // pin member constructors, by Kind
constexpr uint32_t kPinOutCtor = 0x006A7D20;
constexpr uint32_t kPinDataCtor = 0x006A7D60;
constexpr uint32_t kPinDataVCtor = 0x006A8D90;
constexpr uint32_t kBindIn = 0x006C7EF0;        // binds an impulse input pin and its handler
constexpr uint32_t kBind = 0x006C7FF0;          // binds an output or data pin
constexpr uint32_t kBindV = 0x006C7F80;         // binds a data pin with a constant
constexpr uint32_t kNodeCtor = 0x006AEB40;      // Node base constructor
constexpr uint32_t kCbCtor = 0x006ABC80;        // ContainedBase constructor
constexpr uint32_t kNodeDtor = 0x006AEBA0;      // Node base destructor, on node + kDtorThis
constexpr uint32_t kFire = 0x006B4720;          // fires an output pin
constexpr uint32_t kSlot = 0x006ABCA0;          // a node's slot, which holds its definition
constexpr uint32_t kDescIdx = 0x006A9740;       // parameter slot of a name hash
constexpr uint32_t kDesc = 0x006A9770;          // parameter description of a slot
constexpr uint32_t kRaw = 0x006B2080;           // reads a bool / int / float parameter
constexpr uint32_t kStr = 0x006B6990;           // reads a string parameter
constexpr uint32_t kDefDtor = 0x017C46D0;       // definition vtable: deleting destructor
constexpr uint32_t kDefEquals = 0x006AB890;     // definition vtable: equality
constexpr uint32_t kDefFalse = 0x015E89D0;      // definition vtable: returns false
constexpr uint32_t kDefReg = 0x02202C80;        // data: the definition creator map
constexpr uint32_t kCbVft = 0x01B3F800;         // data: ContainedBase vftable
constexpr uint32_t kNodeVt = 0x01B3FE6C;        // data: Node base vftable, 20 slots
constexpr uint32_t kCbVt = 0x01B3FE5C;          // data: ContainedBase vftable, 3 slots
// object layout (FORMATS.md 5.9)
constexpr uint32_t kStateAt = 0x14;             // Node base size: a native class's state starts here
constexpr uint32_t kDtorThis = 0x14;            // the Node base destructor's this, from the node
constexpr uint32_t kDefOfSlot = 0x10;           // the definition pointer inside a node's slot
constexpr uint32_t kDefBase = 0x28;             // definition base size (FUN_006ae840)
constexpr int kCreateInstanceSlot = 14;         // Node vftable slot CreateInstance (FUN_01804bc0)
}  // namespace eng

enum Kind : uint8_t { In, Out, Data, DataV };   // (windows.h defines IN / OUT)
struct Pin {
    const char* name;
    Kind kind;
};
struct Param {
    const char* name;
    int type;   // mer::T_*: the transcoder describes it so
};
using Impulse = void (*)(uint8_t* node, int pin, void* payload);
// A native class: its pins, the parameters it reads, its state size, its impulse handler
struct Spec {
    const char* name;
    std::vector<Pin> pins;
    std::vector<Param> params;
    uint32_t state;
    Impulse impulse;
};

constexpr int kMax = 8, kMaxIn = 16;
// A class's runtime record: the engine keeps pointers to its vtables, vbt and names
struct Rt {
    Spec spec;
    std::string cls, def;
    uint32_t defId = 0, defSize = 0, nodeSize = 0, vbase = 0;
    std::vector<uint32_t> off;   // pin member offset in the definition
    std::vector<int> inPin;      // handler k -> pin index
    int32_t vbt[2] = {};
    void* defVt[4] = {};
    void* mainVt[20] = {};
    void* cbVt[3] = {};
};

using CtorFn = void(__fastcall*)(void*, void*);
using BindFn = void(__fastcall*)(void*, void*, void*, uint32_t);
// The engine functions, at their runtime addresses
struct Engine {
    void*(__cdecl* alloc)(uint32_t);
    void(__cdecl* free)(void*);
    void*(__cdecl* nodeAlloc)(uint32_t, uint32_t);
    void(__cdecl* regFactory)(void*, const char*, uint32_t, uint32_t);
    uint32_t(__cdecl* hash)(const char*);
    void*(__fastcall* defFind)(void*, void*, uint32_t);
    void*(__fastcall* defAdd)(void*, void*, uint32_t, void**);
    void(__cdecl* nodeLoad)(void*, uint32_t, void*, void*);
    CtorFn defCtor;
    CtorFn pinCtor[4];   // by Kind
    void(__fastcall* bindIn)(void*, void*, void*, uint32_t, void*);
    BindFn bind;
    BindFn bindV;
    void(__fastcall* nodeCtor)(void*, void*, int);
    CtorFn cbCtor;
    CtorFn nodeDtor;
    void(__fastcall* fire)(void*, void*, void*, void*, int);
    uint8_t*(__fastcall* slot)(void*, void*);
    uint32_t(__fastcall* descIdx)(void*, void*, uint32_t);
    uint8_t*(__fastcall* desc)(void*, void*, uint32_t);
    uint32_t(__fastcall* raw)(void*, void*, void*);
    const char*(__fastcall* str)(void*, void*, void*, bool*);
    void* defReg;
    void* cbVft;
};

// The module state (S1)
struct State {
    std::array<Rt, kMax> rts;   // filled by add() and registerAll() on the boot thread; then read by thunks
    int count = 0;              // classes added; boot thread only
    Engine engine{};            // set once by registerAll(), before the first class is registered
};
namespace detail {
// S5: the engine points into rts, so the state is built once at load and never freed or moved
alignas(State) inline unsigned char g_storage[sizeof(State)];
}  // namespace detail
inline State& g_state = *::new (detail::g_storage) State();

// The classes added so far
inline int count() { return g_state.count; }

inline uint8_t* defOf(uint8_t* node)
{
    return *(uint8_t**)(g_state.engine.slot(node, nullptr) + eng::kDefOfSlot);
}

// Output pin 'pin' of a native node; payload = the one the impulse handler got (or nullptr outside a handler).
inline void fire(uint8_t* node, int c, int pin, void* payload)
{
    g_state.engine.fire(defOf(node) + g_state.rts[c].off[pin], nullptr, node, payload, -1);
}

template <int C> void construct(uint8_t* n)
{
    Rt& c = g_state.rts[C];
    *(int32_t**)(n + 4) = c.vbt;
    g_state.engine.cbCtor(n + c.vbase, nullptr);
    g_state.engine.nodeCtor(n, nullptr, 0);
    *(void***)n = c.mainVt;
    *(void***)(n + c.vbase) = c.cbVt;
    memset(n + eng::kStateAt, 0, c.vbase - eng::kStateAt);
}
template <int C> uint32_t __fastcall getSize(void*, void*) { return g_state.rts[C].nodeSize; }
template <int C> void* __fastcall createInstance(void*, void*, void*)
{
    uint8_t* n = (uint8_t*)g_state.engine.alloc(g_state.rts[C].nodeSize);
    if (n) construct<C>(n);
    return n;
}
template <int C> void* __fastcall cbDelete(uint8_t* vb, void*, uint32_t flags)
{
    uint8_t* n = vb - g_state.rts[C].vbase;
    g_state.engine.nodeDtor(n + eng::kDtorThis, nullptr);
    *(void**)vb = g_state.engine.cbVft;
    if (flags & 1) g_state.engine.free(n);
    return n;
}
template <int C> void* __cdecl nodeCreate()
{
    uint8_t* n = (uint8_t*)g_state.engine.nodeAlloc(4, g_state.rts[C].nodeSize);
    if (n) construct<C>(n);
    return n;
}
template <int C> void* __cdecl factory(void* p1, void* p2, void* p3)
{
    g_state.engine.nodeLoad((void*)&nodeCreate<C>, g_state.rts[C].defId, p2, p3);
    *(uint32_t*)p1 = 0;
    return p1;
}
template <int C, int K> void __cdecl handler(uint8_t* ctx, void* payload)
{
    Rt& c = g_state.rts[C];
    if (K < (int)c.inPin.size() && c.spec.impulse) c.spec.impulse(*(uint8_t**)(ctx + 4), c.inPin[K], payload);
}
template <int C, int... K> void* handlerAt(int k, std::integer_sequence<int, K...>)
{
    static void* const t[] = {(void*)&handler<C, K>...};
    return t[k];
}
template <int C> void __fastcall describe(uint8_t* def, void*, void* rec)
{
    Rt& c = g_state.rts[C];
    const Engine& e = g_state.engine;
    for (size_t i = 0, k = 0; i < c.spec.pins.size(); ++i) {
        const Pin& p = c.spec.pins[i];
        uint32_t h = e.hash(p.name);
        if (p.kind == In) {
            void* handlerK = handlerAt<C>((int)k++, std::make_integer_sequence<int, kMaxIn>());
            e.bindIn(def + c.off[i], nullptr, rec, h, handlerK);
        } else if (p.kind == DataV) e.bindV(def + c.off[i], nullptr, rec, h);
        else e.bind(def + c.off[i], nullptr, rec, h);
    }
}
template <int C> void* __cdecl defCreate()
{
    Rt& c = g_state.rts[C];
    uint8_t* d = (uint8_t*)g_state.engine.alloc(c.defSize);
    if (!d) return nullptr;
    g_state.engine.defCtor(d, nullptr);
    *(void***)d = c.defVt;
    for (size_t i = 0; i < c.spec.pins.size(); ++i) g_state.engine.pinCtor[c.spec.pins[i].kind](d + c.off[i], nullptr);
    return d;
}

struct Thunks {
    void* defCreate;
    void* describe;
    void* factory;
    void* getSize;
    void* create;
    void* cbDelete;
};
template <int C> Thunks thunksOf()
{
    return {(void*)&defCreate<C>, (void*)&describe<C>,      (void*)&factory<C>,
            (void*)&getSize<C>,   (void*)&createInstance<C>, (void*)&cbDelete<C>};
}
template <int... C> Thunks thunksAt(int c, std::integer_sequence<int, C...>)
{
    static const Thunks t[] = {thunksOf<C>()...};
    return t[c];
}

// Prologues the calls were read from ('??' relocated); all match or nothing registers; the first mismatch is logged
struct Check {
    uint32_t va;
    const char* bytes;
};
inline constexpr Check kChecks[] = {
    {eng::kAlloc, "8B4424046A026A006A00506A"},       {eng::kFree, "8B4424046A0250E8"},
    {eng::kNodeAlloc, "8B4424088B4C24045051E8"},     {eng::kRegFactory, "8B442404568B74240C575650"},
    {eng::kHash, "518B442408535556578BC88D"},        {eng::kDefFind, "83EC088D44240C508D542404"},
    {eng::kDefAdd, "83EC0856578B7C24148BF18D"},      {eng::kNodeLoad, "6AFF68????????64A1000000"},
    {eng::kDefCtor, "568BF1806608FE8D4E0CC706"},     {eng::kPinInCtor, "6A008BD1E8"},
    {eng::kPinOutCtor, "6A068BD1E8"},                {eng::kPinDataCtor, "6A098BD1E8"},
    {eng::kPinDataVCtor, "6A088BD1E8"},              {eng::kBindIn, "8B442408568BF18B4C240850"},
    {eng::kBind, "E9DBFDFFFF"},                      {eng::kBindV, "538B5C240856578B7C241457"},
    {eng::kNodeCtor, "51837C240800568BF1C74424"},    {eng::kCbCtor, "568BF1C706????????E8"},
    {eng::kNodeDtor, "8B41F0C741EC????????8B50"},    {eng::kFire, "64A1000000006AFF68????????"},
    {eng::kSlot, "6683790E0174098B410C0FB7"},        {eng::kDescIdx, "568B7124578B7C240C33C0EB"},
    {eng::kDesc, "8B412485C074160FB740028B"},        {eng::kRaw, "83EC08538B5C241056576A00"},
    {eng::kStr, "51558B6C240C56576A00558B"},         {eng::kDefDtor, "568BF1E8"},
    {eng::kDefEquals, "5153558B6C2410568BF18B5E"},   {eng::kDefFalse, "32C0C3"},
};

// True when the bytes at 'at' are hex's ('??' skips a byte); same rule as core's prologue check (X-06)
inline bool bytesMatch(uintptr_t at, const char* hex)
{
    for (size_t i = 0; hex && hex[2 * i]; ++i) {
        if (hex[2 * i] == '?') continue;
        unsigned v;
        sscanf_s(hex + 2 * i, "%2x", &v);
        if (((const uint8_t*)at)[i] != (uint8_t)v) return false;
    }
    return true;
}

// The class as the transcoder sees a registered one: its pins and described parameters.
inline void addToTable(mer::Table& T, const Spec& s)
{
    T.registered.insert(s.name);
    mer::Class& k = T.classes[s.name];
    for (const Pin& p : s.pins) k.pins.insert(p.name);
    for (const Param& p : s.params) k.params[p.name] = p.type;
}

// Every engine function at its address (rt = static VA -> runtime), one assignment each (GR-08)
template <class RtFn> Engine bindEngine(RtFn rt)
{
    Engine e{};
    e.alloc = (decltype(e.alloc))rt(eng::kAlloc);
    e.free = (decltype(e.free))rt(eng::kFree);
    e.nodeAlloc = (decltype(e.nodeAlloc))rt(eng::kNodeAlloc);
    e.regFactory = (decltype(e.regFactory))rt(eng::kRegFactory);
    e.hash = (decltype(e.hash))rt(eng::kHash);
    e.defFind = (decltype(e.defFind))rt(eng::kDefFind);
    e.defAdd = (decltype(e.defAdd))rt(eng::kDefAdd);
    e.nodeLoad = (decltype(e.nodeLoad))rt(eng::kNodeLoad);
    e.defCtor = (CtorFn)rt(eng::kDefCtor);
    e.pinCtor[In] = (CtorFn)rt(eng::kPinInCtor);
    e.pinCtor[Out] = (CtorFn)rt(eng::kPinOutCtor);
    e.pinCtor[Data] = (CtorFn)rt(eng::kPinDataCtor);
    e.pinCtor[DataV] = (CtorFn)rt(eng::kPinDataVCtor);
    e.bindIn = (decltype(e.bindIn))rt(eng::kBindIn);
    e.bind = (BindFn)rt(eng::kBind);
    e.bindV = (BindFn)rt(eng::kBindV);
    e.nodeCtor = (decltype(e.nodeCtor))rt(eng::kNodeCtor);
    e.cbCtor = (CtorFn)rt(eng::kCbCtor);
    e.nodeDtor = (CtorFn)rt(eng::kNodeDtor);
    e.fire = (decltype(e.fire))rt(eng::kFire);
    e.slot = (decltype(e.slot))rt(eng::kSlot);
    e.descIdx = (decltype(e.descIdx))rt(eng::kDescIdx);
    e.desc = (decltype(e.desc))rt(eng::kDesc);
    e.raw = (decltype(e.raw))rt(eng::kRaw);
    e.str = (decltype(e.str))rt(eng::kStr);
    e.defReg = (void*)rt(eng::kDefReg);
    e.cbVft = (void*)rt(eng::kCbVft);
    return e;
}

// The definition's pin members: offsets after the base, and which pins are impulse inputs
inline void layoutPins(Rt& c)
{
    static const uint32_t pinSize[4] = {0xc, 8, 8, 0x1c};   // by Kind (FORMATS.md 5.9)
    c.defSize = eng::kDefBase;
    c.off.clear();
    c.inPin.clear();
    for (size_t i = 0; i < c.spec.pins.size(); ++i) {
        c.off.push_back(c.defSize);
        c.defSize += pinSize[c.spec.pins[i].kind];
        if (c.spec.pins[i].kind == In) c.inPin.push_back((int)i);
    }
}

// The node's layout and vtables: the base's, with this class's size, create, delete and describe thunks
template <class RtFn> void layoutNode(Rt& c, const Thunks& t, RtFn rt)
{
    c.vbase = (eng::kStateAt + c.spec.state + 3) & ~3u;
    c.nodeSize = c.vbase + 8;
    c.vbt[0] = -4;
    c.vbt[1] = (int32_t)c.vbase - 4;
    memcpy(c.mainVt, (void*)rt(eng::kNodeVt), sizeof(c.mainVt));
    memcpy(c.cbVt, (void*)rt(eng::kCbVt), sizeof(c.cbVt));
    c.mainVt[0] = t.getSize;
    c.mainVt[eng::kCreateInstanceSlot] = t.create;
    c.cbVt[0] = t.cbDelete;
    c.defVt[0] = (void*)rt(eng::kDefDtor);
    c.defVt[1] = t.describe;
    c.defVt[2] = (void*)rt(eng::kDefEquals);
    c.defVt[3] = (void*)rt(eng::kDefFalse);
}

// Registers the node factory and, unless one exists, the definition creator; true when the creator is found
template <class LogFn> bool registerClass(Rt& c, const Thunks& t, LogFn log)
{
    const Engine& e = g_state.engine;
    c.cls = std::string("MNode::") + c.spec.name;
    c.def = std::string("Meridian::") + c.spec.name + "NodeDefinition";
    e.regFactory(t.factory, c.cls.c_str(), c.nodeSize, 4);
    c.defId = e.hash(c.def.c_str());
    if (!e.defFind(e.defReg, nullptr, c.defId)) {
        void* cr = t.defCreate;
        e.defAdd(e.defReg, nullptr, c.defId, &cr);
    }
    bool found = e.defFind(e.defReg, nullptr, c.defId) != nullptr;
    log("meridian native: %s registered (node %u B, definition %u B, %u pins; definition creator %s)", c.cls.c_str(),
        c.nodeSize, c.defSize, (unsigned)c.spec.pins.size(), found ? "found" : "MISSING");
    return found;
}

// Registers every added class once FUN_0178e330 built 3.0's registry, each into T too; returns the count
template <class RtFn, class LogFn> int registerAll(RtFn rt, LogFn log, mer::Table& T)
{
    for (const Check& k : kChecks) {
        if (bytesMatch(rt(k.va), k.bytes)) continue;
        log("meridian native: %08X differs from the analysed bytes; no native class registered", k.va);
        return 0;
    }
    g_state.engine = bindEngine(rt);
    int done = 0;
    for (int ci = 0; ci < g_state.count; ++ci) {
        Rt& c = g_state.rts[ci];
        Thunks t = thunksAt(ci, std::make_integer_sequence<int, kMax>());
        layoutPins(c);
        if ((int)c.inPin.size() > kMaxIn) {
            log("meridian native: %s has more than %d impulse inputs; skipped", c.spec.name, kMaxIn);
            continue;
        }
        layoutNode(c, t, rt);
        if (!registerClass(c, t, log)) continue;
        addToTable(T, c.spec);
        ++done;
    }
    return done;
}

// Adds a class before registerAll; its index, -1 when kMax classes are added already
inline int add(const Spec& s)
{
    if (g_state.count >= kMax) return -1;
    g_state.rts[g_state.count].spec = s;
    return g_state.count++;
}

}  // namespace mernode
