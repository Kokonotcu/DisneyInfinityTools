// Native Meridian node classes inside 3.0's Meridian (plan SYS-1 registration helpers, the base of MER-6 and of the
// sys/npc 1.0-only classes: ScourgeWave, SpawnFromActorNode, SpawnManaNode, DeliveryNode, Activate/SpawnDecoration
// Sockets, IntroChoGroup). Included by di1compat.cpp after rt()/logf(). The 3.0 ABI, read from MessageSender
// (template FUN_0177f290), ToggleSpline (FUN_017b7c30), SpawnExpendable (FUN_017c0a90) and the Meridian core:
//   register    FUN_006bc220(factory, "MNode::<name>", node size, 4) [cdecl]; definition id = FUN_018261f0
//               ("Meridian::<name>NodeDefinition"); FUN_013cc1f0(id) finds, FUN_006eaa00(id, &creator) adds a
//               definition creator [thiscall on the creator map 0x02202C80] (FUN_0177a8b0)
//   factory     cdecl(p1, p2, p3): FUN_006c8be0(node creator, id, p2, p3) [cdecl]; *p1 = 0; return p1 (FUN_01810640)
//   creator     cdecl(): node = FUN_006abff0(4, size); ctor(node, most derived) (FUN_0180d030)
//   node        +0 vftable (20 slots: 0 size, 12 LoadParameters(arg), 14 CreateInstance(arg) = fresh instance,
//               FUN_01804bc0), +4 vbptr {-4, vbase - 4}, +8..+0x13 Node base (FUN_006aeb40(node, 0) after
//               ContainedBase FUN_006abc80 at vbase), state, then the virtual base ContainedBase (vftable +
//               namespace, 8 B) whose slot 0 is the deleting destructor: FUN_006aeba0 on node + 0x14, vftable
//               0x01B3F800, free (FUN_017a8d30). Base vftables 0x01B3FE6C (20) / 0x01B3FE5C (3)
//   definition  0x28 B base (FUN_006ae840) + pin members: impulse in 0xc (FUN_006a7c60, handler at +8), impulse out 8
//               (FUN_006a7d20), data 8 (FUN_006a7d60), data with a constant 0x1c (FUN_006a8d90); vftable
//               {FUN_017c46d0 deleting dtor, describe(record), FUN_006ab890 equality, FUN_015e89d0};
//               describe binds each pin from the record's field of that name (pin index): in FUN_006c7ef0 (+handler),
//               out/data FUN_006c7ff0, data with constant FUN_006c7f80 (also describes the '<pin>' parameter)
//   impulse     handler cdecl(ctx, payload), node = *(ctx + 4) (MessageSender stub 0x01813220)
//   fire        FUN_006b4720(&def pin, node, payload, -1) [thiscall, ret 0xc]; def = *(FUN_006abca0(node) + 0x10)
//   parameter   FUN_006a9740(def, hash) -> slot, FUN_006a9770(def, slot) -> description (+4 type: 0 bool, 1 int,
//               2 float, 3 string); FUN_006b2080(desc, node) reads bool/int/float, FUN_006b6990(desc, node, &ok)
//               strings (FUN_006b2700 / FUN_006f05d0 without their node+0x10 parameter registration)
//   data input  typed list fetches of the engine on the pin member, e.g. FUN_010f3780(&def pin, node, list) for
//               transforms (SpawnExpendable LocatorList) and FUN_0115d4e0 for splines (ToggleSpline Splines); list
//               ctor FUN_01573b00 / dtor FUN_006a9f50
// Not modelled yet (no class here needs them): surge pins (FUN_006c7f10, implicit begin/end pins at index+1/+2) and
// data outputs, which are the impulse-in member + FUN_006c7ef0 with a getter instead of a handler (the fetch template
// FUN_0115d4e0 calls the source pin's +8 function for the DataElement: SpawnExpendable ExpendableHandle / Collector,
// 1.0 DeliveryNode OutLocatorIndex 0x8cbe1da6).
#pragma once
#include <array>
#include <cstring>
#include <string>
#include <utility>
#include <vector>
#include "mer.hpp"

namespace mernode {

enum Kind : uint8_t { In, Out, Data, DataV };   // (windows.h defines IN / OUT)
struct Pin { const char* name; Kind kind; };
struct Param { const char* name; int type; };                 // mer::T_*: the transcoder describes it so
typedef void (*Impulse)(uint8_t* node, int pin, void* payload);
struct Spec { const char* name; std::vector<Pin> pins; std::vector<Param> params; uint32_t state; Impulse impulse; };

constexpr int kMax = 8, kMaxIn = 16;
struct Rt {
    Spec spec;
    std::string cls, def;                                      // kept: the registries may hold the char*
    uint32_t defId = 0, defSize = 0, nodeSize = 0, vbase = 0;
    std::vector<uint32_t> off;                                 // pin member offset in the definition
    std::vector<int> inPin;                                    // handler k -> pin index
    int32_t vbt[2] = {};
    void* defVt[4] = {};
    void* mainVt[20] = {};
    void* cbVt[3] = {};
};
inline std::array<Rt, kMax>& rts() { static std::array<Rt, kMax> a; return a; }
inline int& count() { static int n = 0; return n; }

struct Engine {
    void*(__cdecl* alloc)(uint32_t);                                        // 00406E80
    void(__cdecl* free)(void*);                                             // 00409DF0
    void*(__cdecl* nodeAlloc)(uint32_t, uint32_t);                          // 006ABFF0 (align, size)
    void(__cdecl* regFactory)(void*, const char*, uint32_t, uint32_t);      // 006BC220
    uint32_t(__cdecl* hash)(const char*);                                   // 018261F0
    void*(__fastcall* defFind)(void*, void*, uint32_t);                     // 013CC1F0 on 02202C80
    void*(__fastcall* defAdd)(void*, void*, uint32_t, void**);              // 006EAA00 on 02202C80
    void(__cdecl* nodeLoad)(void*, uint32_t, void*, void*);                 // 006C8BE0
    void(__fastcall* defCtor)(void*, void*);                                // 006AE840
    void(__fastcall* pinCtor[4])(void*, void*);                             // 006A7C60 / 006A7D20 / 006A7D60 / 006A8D90
    void(__fastcall* bindIn)(void*, void*, void*, uint32_t, void*);        // 006C7EF0
    void(__fastcall* bind)(void*, void*, void*, uint32_t);                 // 006C7FF0
    void(__fastcall* bindV)(void*, void*, void*, uint32_t);                // 006C7F80
    void(__fastcall* nodeCtor)(void*, void*, int);                          // 006AEB40
    void(__fastcall* cbCtor)(void*, void*);                                 // 006ABC80
    void(__fastcall* nodeDtor)(void*, void*);                               // 006AEBA0 (node + 0x14)
    void(__fastcall* fire)(void*, void*, void*, void*, int);                // 006B4720
    uint8_t*(__fastcall* slot)(void*, void*);                               // 006ABCA0
    uint32_t(__fastcall* descIdx)(void*, void*, uint32_t);                  // 006A9740
    uint8_t*(__fastcall* desc)(void*, void*, uint32_t);                     // 006A9770
    uint32_t(__fastcall* raw)(void*, void*, void*);                         // 006B2080
    const char*(__fastcall* str)(void*, void*, void*, bool*);               // 006B6990
    void* defReg;                                                           // 02202C80
    void* cbVft;                                                            // 01B3F800
};
inline Engine E;

inline uint8_t* defOf(uint8_t* node) { return *(uint8_t**)(E.slot(node, nullptr) + 0x10); }

// Output pin 'pin' of a native node; payload = the one the impulse handler got (or nullptr outside a handler).
inline void fire(uint8_t* node, int c, int pin, void* payload) { E.fire(defOf(node) + rts()[c].off[pin], nullptr, node, payload, -1); }

// A described parameter of the node's record (the transcoder writes it per Spec::params); false = absent or another
// type, the caller keeps its default.
inline bool param(uint8_t* node, const char* name, int type, uint32_t* raw, std::string* s = nullptr)
{
    uint8_t* def = defOf(node);
    uint8_t* d = E.desc(def, nullptr, E.descIdx(def, nullptr, E.hash(name)) & 0xff);
    if (!d || d[4] != type) return false;
    if (type != mer::T_STRING) { *raw = E.raw(d, nullptr, node); return true; }
    bool ok = true;
    const char* v = E.str(d, nullptr, node, &ok);
    if (!ok || !v || !s) return false;
    *s = v;
    return true;
}
inline uint8_t* state(uint8_t* node) { return node + 0x14; }

template <int C> void construct(uint8_t* n)
{
    Rt& c = rts()[C];
    *(int32_t**)(n + 4) = c.vbt;
    E.cbCtor(n + c.vbase, nullptr);
    E.nodeCtor(n, nullptr, 0);
    *(void***)n = c.mainVt;
    *(void***)(n + c.vbase) = c.cbVt;
    memset(n + 0x14, 0, c.vbase - 0x14);
}
template <int C> uint32_t __fastcall getSize(void*, void*) { return rts()[C].nodeSize; }
template <int C> void* __fastcall createInstance(void*, void*, void*)
{
    uint8_t* n = (uint8_t*)E.alloc(rts()[C].nodeSize);
    if (n) construct<C>(n);
    return n;
}
template <int C> void* __fastcall cbDelete(uint8_t* vb, void*, uint32_t flags)
{
    uint8_t* n = vb - rts()[C].vbase;
    E.nodeDtor(n + 0x14, nullptr);
    *(void**)vb = E.cbVft;
    if (flags & 1) E.free(n);
    return n;
}
template <int C> void* __cdecl nodeCreate()
{
    uint8_t* n = (uint8_t*)E.nodeAlloc(4, rts()[C].nodeSize);
    if (n) construct<C>(n);
    return n;
}
template <int C> void* __cdecl factory(void* p1, void* p2, void* p3)
{
    E.nodeLoad((void*)&nodeCreate<C>, rts()[C].defId, p2, p3);
    *(uint32_t*)p1 = 0;
    return p1;
}
template <int C, int K> void __cdecl handler(uint8_t* ctx, void* payload)
{
    Rt& c = rts()[C];
    if (K < (int)c.inPin.size() && c.spec.impulse) c.spec.impulse(*(uint8_t**)(ctx + 4), c.inPin[K], payload);
}
template <int C, int... K> void* handlerAt(int k, std::integer_sequence<int, K...>)
{
    static void* const t[] = {(void*)&handler<C, K>...};
    return t[k];
}
template <int C> void __fastcall describe(uint8_t* def, void*, void* rec)
{
    Rt& c = rts()[C];
    for (size_t i = 0, k = 0; i < c.spec.pins.size(); ++i) {
        const Pin& p = c.spec.pins[i];
        uint32_t h = E.hash(p.name);
        if (p.kind == In) E.bindIn(def + c.off[i], nullptr, rec, h, handlerAt<C>((int)k++, std::make_integer_sequence<int, kMaxIn>()));
        else if (p.kind == DataV) E.bindV(def + c.off[i], nullptr, rec, h);
        else E.bind(def + c.off[i], nullptr, rec, h);
    }
}
template <int C> void* __cdecl defCreate()
{
    Rt& c = rts()[C];
    uint8_t* d = (uint8_t*)E.alloc(c.defSize);
    if (!d) return nullptr;
    E.defCtor(d, nullptr);
    *(void***)d = c.defVt;
    for (size_t i = 0; i < c.spec.pins.size(); ++i) E.pinCtor[c.spec.pins[i].kind](d + c.off[i], nullptr);
    return d;
}

struct Thunks { void *defCreate, *describe, *factory, *getSize, *create, *cbDelete; };
template <int C> Thunks thunksOf()
{
    return {(void*)&defCreate<C>, (void*)&describe<C>, (void*)&factory<C>, (void*)&getSize<C>, (void*)&createInstance<C>, (void*)&cbDelete<C>};
}
template <int... C> Thunks thunksAt(int c, std::integer_sequence<int, C...>)
{
    static const Thunks t[] = {thunksOf<C>()...};
    return t[c];
}

// Engine bytes these calls were read from (relocated immediates as ??); all must match or nothing registers.
struct Check { uint32_t va; const char* bytes; };
static const Check kChecks[] = {
    {0x00406E80, "8B4424046A026A006A00506A"}, {0x00409DF0, "8B4424046A0250E8"}, {0x006ABFF0, "8B4424088B4C24045051E8"},
    {0x006BC220, "8B442404568B74240C575650"}, {0x018261F0, "518B442408535556578BC88D"}, {0x013CC1F0, "83EC088D44240C508D542404"},
    {0x006EAA00, "83EC0856578B7C24148BF18D"}, {0x006C8BE0, "6AFF68????????64A1000000"}, {0x006AE840, "568BF1806608FE8D4E0CC706"},
    {0x006A7C60, "6A008BD1E8"}, {0x006A7D20, "6A068BD1E8"}, {0x006A7D60, "6A098BD1E8"}, {0x006A8D90, "6A088BD1E8"},
    {0x006C7EF0, "8B442408568BF18B4C240850"}, {0x006C7FF0, "E9DBFDFFFF"}, {0x006C7F80, "538B5C240856578B7C241457"},
    {0x006AEB40, "51837C240800568BF1C74424"}, {0x006ABC80, "568BF1C706????????E8"}, {0x006AEBA0, "8B41F0C741EC????????8B50"},
    {0x006B4720, "64A1000000006AFF68????????"}, {0x006ABCA0, "6683790E0174098B410C0FB7"}, {0x006A9740, "568B7124578B7C240C33C0EB"},
    {0x006A9770, "8B412485C074160FB740028B"}, {0x006B2080, "83EC08538B5C241056576A00"}, {0x006B6990, "51558B6C240C56576A00558B"},
    {0x017C46D0, "568BF1E8"}, {0x006AB890, "5153558B6C2410568BF18B5E"}, {0x015E89D0, "32C0C3"},
};

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

// Lays out and registers every added class (after FUN_0178e330 built 3.0's registry; rt = static VA -> runtime) and
// adds each registered one to the transcoder table. Returns the classes registered.
template <class RtFn, class LogFn> int registerAll(RtFn rt, LogFn log, mer::Table& T)
{
    for (const Check& k : kChecks)
        if (!bytesMatch(rt(k.va), k.bytes)) { log("meridian native: %08X differs from the analysed bytes; no native class registered", k.va); return 0; }
    E = {(void*(__cdecl*)(uint32_t))rt(0x00406E80), (void(__cdecl*)(void*))rt(0x00409DF0),
         (void*(__cdecl*)(uint32_t, uint32_t))rt(0x006ABFF0), (void(__cdecl*)(void*, const char*, uint32_t, uint32_t))rt(0x006BC220),
         (uint32_t(__cdecl*)(const char*))rt(0x018261F0), (void*(__fastcall*)(void*, void*, uint32_t))rt(0x013CC1F0),
         (void*(__fastcall*)(void*, void*, uint32_t, void**))rt(0x006EAA00), (void(__cdecl*)(void*, uint32_t, void*, void*))rt(0x006C8BE0),
         (void(__fastcall*)(void*, void*))rt(0x006AE840),
         {(void(__fastcall*)(void*, void*))rt(0x006A7C60), (void(__fastcall*)(void*, void*))rt(0x006A7D20),
          (void(__fastcall*)(void*, void*))rt(0x006A7D60), (void(__fastcall*)(void*, void*))rt(0x006A8D90)},
         (void(__fastcall*)(void*, void*, void*, uint32_t, void*))rt(0x006C7EF0), (void(__fastcall*)(void*, void*, void*, uint32_t))rt(0x006C7FF0),
         (void(__fastcall*)(void*, void*, void*, uint32_t))rt(0x006C7F80), (void(__fastcall*)(void*, void*, int))rt(0x006AEB40),
         (void(__fastcall*)(void*, void*))rt(0x006ABC80), (void(__fastcall*)(void*, void*))rt(0x006AEBA0),
         (void(__fastcall*)(void*, void*, void*, void*, int))rt(0x006B4720), (uint8_t*(__fastcall*)(void*, void*))rt(0x006ABCA0),
         (uint32_t(__fastcall*)(void*, void*, uint32_t))rt(0x006A9740), (uint8_t*(__fastcall*)(void*, void*, uint32_t))rt(0x006A9770),
         (uint32_t(__fastcall*)(void*, void*, void*))rt(0x006B2080), (const char*(__fastcall*)(void*, void*, void*, bool*))rt(0x006B6990),
         (void*)rt(0x02202C80), (void*)rt(0x01B3F800)};
    static const uint32_t pinSize[4] = {0xc, 8, 8, 0x1c};
    int done = 0;
    for (int ci = 0; ci < count(); ++ci) {
        Rt& c = rts()[ci];
        Thunks t = thunksAt(ci, std::make_integer_sequence<int, kMax>());
        c.defSize = 0x28;
        c.off.clear();
        c.inPin.clear();
        for (size_t i = 0; i < c.spec.pins.size(); ++i) {
            c.off.push_back(c.defSize);
            c.defSize += pinSize[c.spec.pins[i].kind];
            if (c.spec.pins[i].kind == In) c.inPin.push_back((int)i);
        }
        if ((int)c.inPin.size() > kMaxIn) { log("meridian native: %s has more than %d impulse inputs; skipped", c.spec.name, kMaxIn); continue; }
        c.vbase = (0x14 + c.spec.state + 3) & ~3u;
        c.nodeSize = c.vbase + 8;
        c.vbt[0] = -4;
        c.vbt[1] = (int32_t)c.vbase - 4;
        memcpy(c.mainVt, (void*)rt(0x01B3FE6C), sizeof(c.mainVt));
        memcpy(c.cbVt, (void*)rt(0x01B3FE5C), sizeof(c.cbVt));
        c.mainVt[0] = t.getSize;
        c.mainVt[14] = t.create;
        c.cbVt[0] = t.cbDelete;
        c.defVt[0] = (void*)rt(0x017C46D0);
        c.defVt[1] = t.describe;
        c.defVt[2] = (void*)rt(0x006AB890);
        c.defVt[3] = (void*)rt(0x015E89D0);
        c.cls = std::string("MNode::") + c.spec.name;
        c.def = std::string("Meridian::") + c.spec.name + "NodeDefinition";
        E.regFactory(t.factory, c.cls.c_str(), c.nodeSize, 4);
        c.defId = E.hash(c.def.c_str());
        if (!E.defFind(E.defReg, nullptr, c.defId)) {
            void* cr = t.defCreate;
            E.defAdd(E.defReg, nullptr, c.defId, &cr);
        }
        bool found = E.defFind(E.defReg, nullptr, c.defId) != nullptr;
        log("meridian native: %s registered (node %u B, definition %u B, %u pins; definition creator %s)", c.cls.c_str(),
            c.nodeSize, c.defSize, (unsigned)c.spec.pins.size(), found ? "found" : "MISSING");
        if (found) { addToTable(T, c.spec); ++done; }
    }
    return done;
}

inline int add(const Spec& s)
{
    if (count() >= kMax) return -1;
    rts()[count()].spec = s;
    return count()++;
}

}  // namespace mernode
