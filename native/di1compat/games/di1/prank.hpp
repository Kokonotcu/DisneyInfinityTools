// prank10: MU prank sockets, RegisterPranks, PrankTargetQuery on our socket registry. mernode, spawner.
// Threads: Meridian (impulses, getters); graph unload (destroy).
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <utility>
#include <vector>
#include "../../core/hooks.hpp"
#include "../../core/log.hpp"
#include "../../engine/mernode.hpp"
#include "../../formats/mer.hpp"
#include "config.hpp"
#include "spawner.hpp"

namespace di1::prank {

// Adds SpawnPrankSocket, RegisterPranks and PrankTargetQuery when c.prank10 is on (needs native10).
inline bool install(const Config& c);

namespace detail {

namespace eng {
constexpr uint32_t kExpendables = 0x0228AA40;   // *: the expendables manager (deliveries.hpp, rcbox10)
constexpr uint32_t kDispense = 0x0160F370;      // FUN_0160f370: thiscall (name, float xform[16], count, 1) -> handle
inline constexpr char kDispenseBytes[] = "6AFF68????????64A100000000506489250000000081EC84000000";
}  // namespace eng

using Vec = std::array<float, 3>;

constexpr int kOrients = 4;              // Orientation_One..Four (1.0 parameters 0xd77b70)
constexpr float kBindRadius = 1.5f;      // metres; 1.0 FUN_00b791c0 matched a socket at the locator itself
constexpr float kQueryRadius = 4.0f;     // metres from a toy's 'facing' locator to a socket's prank locator
constexpr float kDefaultHeight = 1.0f;   // arc height with no socket: ActionPrank's own default (ArcHeight_value)
constexpr float kDegToRad = 0.017453292f;
constexpr float kNear = 1e-3f;
constexpr int kXformFloats = 16;         // FUN_0160f370's transform: quaternion, position, then 1.0s
constexpr int kXformPos = 4;
inline constexpr char kNone[] = "NONE";
inline constexpr char kDecoPrefix[] = "INV_PRANK_DECO_";   // MU inventory__lua.lua: RRO filenames MU_Decor_<rest>
inline constexpr char kDecoExpendable[] = "MU_Decor_";     // mu_expendables.lua names
// 1.0 monstersu_actors.lua: DecorationSocket.dnax, model objects/DecorationMarker (the socket's marker)
inline constexpr char kMarkerKey[] = "decorationsocket";
inline constexpr const char* kAngleParams[kOrients] = {"Orientation_One", "Orientation_Two", "Orientation_Three",
                                                       "Orientation_Four"};
inline constexpr const char* kHeightParams[kOrients] = {"Height_One", "Height_Two", "Height_Three", "Height_Four"};

// Pins in the 1.0 graphs' numbering (describes FUN_00d7ec90, FUN_00d8d180, FUN_00d8d200); Facing is the converter's
enum SocketPin : int {
    kSocketIn, kTargetsOne, kTargetsTwo, kTargetsThree, kTargetsFour, kSocketLocator, kPrankLocator
};
enum RegisterPin : int { kRegisterIn, kLocatorList };
enum QueryPin : int { kTransform, kHeight, kFacing };

struct Target {
    uint8_t* handle;   // a held reference (mernode::fetchHandles), released with its socket
    Vec pos;
    int orient;
};

// A prank socket, as 1.0's SlotManager FUN_00ba0660 files one
struct Socket {
    uint8_t* node = nullptr;   // the SpawnPrankSocket node that made it
    void* ns = nullptr;        // its namespace: a RegisterPranks with no locator binds these (1.0 FUN_00ba0aa0)
    int id = 0;
    Vec socketPos{}, prankPos{};
    std::array<float, kOrients> angle{}, height{};
    std::vector<Target> targets;
    std::string item;          // the prank item RegisterPranks bound, "" none
};

// PrankTargetQuery's outputs (1.0 node +0x30 transform, +0x24 height) and what it last logged
struct QueryState {
    mernode::XformElement xform;
    mernode::FloatElement height;
    int loggedSocket, loggedTarget;   // id + 1 / index + 1 last logged, 0 none
};

struct State {
    std::mutex mx;                  // guards sockets and nextId; never held across an engine call
    std::vector<Socket> sockets;
    int nextId = 0;
    int socketClass = -1, registerClass = -1, queryClass = -1;   // set once in install()
    bool dispense = false;          // FUN_0160f370's prologue matched: set once in install()
    bool marker = false;            // prankmarker10: a socket gets its DecorationSocket actor; set once in install()
    core::LogOnce<std::string> told;   // its own leaf lock
};
inline State g_state;

inline void tellOnce(const std::string& key, const std::string& line)
{
    if (g_state.told.first(key)) core::logf("%s", line.c_str());
}

inline float sq(float v) { return v * v; }
inline float dist2(const Vec& a, const Vec& b) { return sq(a[0] - b[0]) + sq(a[1] - b[1]) + sq(a[2] - b[2]); }

inline float floatParam(uint8_t* node, const char* name, float fallback)
{
    uint32_t bits = 0;
    memcpy(&bits, &fallback, sizeof bits);
    bits = mernode::rawParam(node, name, bits);
    float v = 0.0f;
    memcpy(&v, &bits, sizeof v);
    return v;
}

// --- sockets (SpawnPrankSocket)

// What filing a socket did: its id, whether it is new, the target references the replaced one held
struct Filed {
    int id;
    bool fresh;
    std::vector<uint8_t*> drop;
};

// Files k as its node's socket, replacing the one an earlier Link_In made (its item is kept).
inline Filed fileSocket(Socket&& k)
{
    State& s = g_state;
    Filed f{0, false, {}};
    std::lock_guard<std::mutex> lock(s.mx);
    auto it = std::find_if(s.sockets.begin(), s.sockets.end(), [&](const Socket& o) { return o.node == k.node; });
    if (it == s.sockets.end()) {
        f.id = k.id = s.nextId++;
        f.fresh = true;
        s.sockets.push_back(std::move(k));
        return f;
    }
    for (const Target& t : it->targets) f.drop.push_back(t.handle);
    f.id = k.id = it->id;
    k.item = it->item;
    *it = std::move(k);
    return f;
}

// Socket and prank positions, angles, heights and the four target lists of a SpawnPrankSocket node.
inline bool readSocket(uint8_t* node, Socket& k)
{
    const int c = g_state.socketClass;
    const auto sock = mernode::fetchPositions(node, c, kSocketLocator);
    const auto prank = mernode::fetchPositions(node, c, kPrankLocator);
    if (sock.empty() && prank.empty()) return false;
    k.socketPos = sock.empty() ? prank[0] : sock[0];
    k.prankPos = prank.empty() ? k.socketPos : prank[0];
    for (int o = 0; o < kOrients; ++o) {
        k.angle[o] = floatParam(node, kAngleParams[o], 0.0f);
        k.height[o] = floatParam(node, kHeightParams[o], kDefaultHeight);
        for (const mernode::HeldTransform& h : mernode::fetchHandles(node, c, kTargetsOne + o))
            k.targets.push_back({h.handle, h.pos, o});
    }
    return true;
}

inline int targetsIn(const Socket& k, int o)
{
    return (int)std::count_if(k.targets.begin(), k.targets.end(), [o](const Target& t) { return t.orient == o; });
}

// 1.0 0xd77d30 -> SlotManager FUN_00ba0660: a socket at the locators, its actor posed there (FUN_00d10410).
inline void socketImpulse(uint8_t* node, int, void*)
{
    try {
        Socket k;
        k.node = node;
        k.ns = mernode::namespaceOf(node, g_state.socketClass);
        if (!readSocket(node, k)) {
            tellOnce("nolocator", "prank10: SpawnPrankSocket has no socket or prank locator; no socket made");
            return;
        }
        const Vec at = k.socketPos, prank = k.prankPos;
        const int counts[kOrients] = {targetsIn(k, 0), targetsIn(k, 1), targetsIn(k, 2), targetsIn(k, 3)};
        const Filed f = fileSocket(std::move(k));
        for (uint8_t* h : f.drop) mernode::releaseHandle(h);
        const bool markIt = f.fresh && g_state.marker && spawner::actorCallsOn();
        const int32_t marker = markIt ? spawner::createActor(kMarkerKey, at) : 0;
        core::logf("prank10: socket %d %s at (%.1f, %.1f, %.1f), prank spot (%.1f, %.1f, %.1f), targets "
                   "%d/%d/%d/%d, marker actor %d", f.id, f.fresh ? "created" : "renewed", at[0], at[1], at[2],
                   prank[0], prank[1], prank[2], counts[0], counts[1], counts[2], counts[3], marker);
    } catch (...) {   // H6: bad_alloc only; this socket is not filed
    }
}

// Before a SpawnPrankSocket node goes: its socket and the target references it held (H11).
inline void socketDestroy(uint8_t* node)
{
    State& s = g_state;
    std::vector<uint8_t*> drop;
    {
        std::lock_guard<std::mutex> lock(s.mx);
        const auto mine = [node](const Socket& k) { return k.node == node; };
        for (const Socket& k : s.sockets)
            if (mine(k))
                for (const Target& t : k.targets) drop.push_back(t.handle);
        s.sockets.erase(std::remove_if(s.sockets.begin(), s.sockets.end(), mine), s.sockets.end());
    }
    for (uint8_t* h : drop) mernode::releaseHandle(h);
}

// 1.0 describe FUN_00d7ec90; parameters 0xd77b70 (degrees, metres)
inline mernode::Spec socketSpec()
{
    using mernode::Data;
    return {"SpawnPrankSocket",
            {{"Link_In", mernode::In}, {"OrientationOneTargets", Data}, {"OrientationTwoTargets", Data},
             {"OrientationThreeTargets", Data}, {"OrientationFourTargets", Data}, {"SocketLocator", Data},
             {"PrankLocator", Data}},
            {{"Orientation_One", ::mer::T_FLOAT}, {"Orientation_Two", ::mer::T_FLOAT},
             {"Orientation_Three", ::mer::T_FLOAT}, {"Orientation_Four", ::mer::T_FLOAT},
             {"Height_One", ::mer::T_FLOAT}, {"Height_Two", ::mer::T_FLOAT}, {"Height_Three", ::mer::T_FLOAT},
             {"Height_Four", ::mer::T_FLOAT}},
            0,
            &socketImpulse,
            &socketDestroy};
}

// --- RegisterPranks

// A socket a RegisterPranks bound: its id, prank spot and the yaw its item stands at
struct Bound {
    int id;
    Vec at;
    float degrees;
};

// The socket nearest p within kBindRadius, or null. Caller holds mx.
inline Socket* socketAt(const Vec& p)
{
    Socket* best = nullptr;
    float bestD = sq(kBindRadius);
    for (Socket& k : g_state.sockets) {
        const float d = (std::min)(dist2(p, k.socketPos), dist2(p, k.prankPos));
        if (d > bestD) continue;
        bestD = d;
        best = &k;
    }
    return best;
}

// Binds item to the sockets at the locators, or with none to every socket of namespace ns (1.0 FUN_00ba0aa0).
inline std::vector<Bound> bind(const std::vector<Vec>& at, void* ns, const std::string& item, int orient)
{
    State& s = g_state;
    std::vector<Socket*> hit;
    std::lock_guard<std::mutex> lock(s.mx);
    for (const Vec& p : at)
        if (Socket* k = socketAt(p)) hit.push_back(k);
    for (Socket& k : s.sockets)
        if (at.empty() && k.ns == ns) hit.push_back(&k);
    std::vector<Bound> out;
    const int o = (std::max)(0, (std::min)(orient, kOrients - 1));
    const std::string held = item == kNone ? std::string() : item;
    for (Socket* k : hit) {
        if (k->item == held) continue;   // already shown: 1.0 kept one object per socket
        k->item = held;
        out.push_back({k->id, k->prankPos, k->angle[o]});
    }
    return out;
}

// The expendable a prank decoration item stands as ("" for a toy): INV_PRANK_DECO_<x> -> MU_Decor_<x>.
inline std::string decoExpendable(const std::string& item)
{
    const size_t n = sizeof(kDecoPrefix) - 1;
    return item.compare(0, n, kDecoPrefix) ? std::string() : kDecoExpendable + item.substr(n);
}

// The expendable at p turned degrees about up, through 3.0's dispense (rcbox10's call); its handle, 0 none.
inline uint32_t dispense(const std::string& expendable, const Vec& p, float degrees)
{
    typedef uint32_t(__fastcall * DispenseFn)(void* mgr, void* edx, const char* name, const float* xform, int count,
                                              uint32_t a4);
    void* mgr = *(void**)core::rt(eng::kExpendables);
    if (!mgr || !g_state.dispense) return 0;
    float xform[kXformFloats];
    std::fill(xform, xform + kXformFloats, 1.0f);
    const float half = degrees * kDegToRad * 0.5f;
    xform[0] = 0.0f;
    xform[1] = std::sin(half);
    xform[2] = 0.0f;
    xform[3] = std::cos(half);
    std::copy(p.begin(), p.end(), xform + kXformPos);
    return ((DispenseFn)core::rt(eng::kDispense))(mgr, nullptr, expendable.c_str(), xform, 1, 1);
}

// One bound socket's item made visible as 1.0's FUN_00d877e0 did (decorations only), logged
inline void showItem(const Bound& b, const std::string& item)
{
    if (item == kNone) {
        core::logf("prank10: RegisterPranks: socket %d cleared", b.id);
        return;
    }
    const std::string exp = decoExpendable(item);
    if (exp.empty()) {
        core::logf("prank10: RegisterPranks: socket %d holds toy %s; 3.0 has no rumpus object spawn, so it is not "
                   "drawn", b.id, item.c_str());
        return;
    }
    const uint32_t h = dispense(exp, b.at, b.degrees);
    core::logf("prank10: RegisterPranks: socket %d holds %s; %s spawned at (%.1f, %.1f, %.1f) -> %u", b.id,
               item.c_str(), exp.c_str(), b.at[0], b.at[1], b.at[2], h);
}

// 1.0 0xd87ac0: each socket at a locator (or tagged actor) gets the Prank item (FUN_00d877e0).
inline void registerImpulse(uint8_t* node, int, void*)
{
    try {
        const int c = g_state.registerClass;
        const std::string item = mernode::strParam(node, "Prank");
        const int orient = (int)mernode::rawParam(node, "Orientation", 0);
        if (mernode::rawParam(node, "UseTag", 0))
            tellOnce("usetag", "prank10: RegisterPranks UseTag (tagged actors) is not ported; its locators are used");
        const std::vector<Vec> at = mernode::fetchPositions(node, c, kLocatorList);
        const std::vector<Bound> bound = bind(at, mernode::namespaceOf(node, c), item, orient);
        if (bound.empty())
            core::logf("prank10: RegisterPranks %s: no socket changed (%u locators)", item.c_str(),
                       (unsigned)at.size());
        for (const Bound& b : bound) showItem(b, item);
    } catch (...) {   // H6: bad_alloc only; nothing bound
    }
}

// 1.0 describe FUN_00d8d180, parameters FUN_00d92970 (Orientation an int)
inline mernode::Spec registerSpec()
{
    return {"RegisterPranks",
            {{"Link_In", mernode::In}, {"LocatorList", mernode::Data}},
            {{"UseTag", ::mer::T_BOOL}, {"TargetName", ::mer::T_STRING}, {"Prank", ::mer::T_STRING},
             {"Orientation", ::mer::T_INT}},
            0,
            &registerImpulse};
}

// --- PrankTargetQuery

// The target a toy facing from f launches at: socket, target index, its handle and arc height
struct Choice {
    int socket = -1, target = -1;
    uint8_t* handle = nullptr;
    float height = kDefaultHeight;
    Vec at{};
};

// Of socket k's targets, the one whose direction from the prank spot best matches facing d (level). Caller holds mx.
inline int bestTarget(const Socket& k, float dx, float dz)
{
    const float dl = std::sqrt(dx * dx + dz * dz);
    int best = k.targets.empty() ? -1 : 0;
    float bestDot = -2.0f;
    for (size_t i = 0; dl > kNear && i < k.targets.size(); ++i) {
        const float tx = k.targets[i].pos[0] - k.prankPos[0], tz = k.targets[i].pos[2] - k.prankPos[2];
        const float tl = std::sqrt(tx * tx + tz * tz);
        const float dot = tl > kNear ? (dx * tx + dz * tz) / (dl * tl) : -1.0f;
        if (dot <= bestDot) continue;
        bestDot = dot;
        best = (int)i;
    }
    return best;
}

// 1.0 FUN_00b81550 / FUN_00b79fc0: the socket under the toy and the target its placement chose; here by facing.
inline Choice choose(const Vec& f)
{
    State& s = g_state;
    std::lock_guard<std::mutex> lock(s.mx);
    const Socket* closest = nullptr;
    float closestD = sq(kQueryRadius);
    for (const Socket& k : s.sockets) {
        const float d = dist2(f, k.prankPos);
        if (d > closestD) continue;
        closestD = d;
        closest = &k;
    }
    Choice ch;
    const int t = closest ? bestTarget(*closest, f[0] - closest->prankPos[0], f[2] - closest->prankPos[2]) : -1;
    if (t < 0) return ch;
    const Target& tg = closest->targets[t];
    ch.socket = closest->id;
    ch.target = t;
    ch.handle = tg.handle;
    ch.height = closest->height[tg.orient];
    ch.at = tg.pos;
    return ch;
}

// A changed choice, logged once per change of socket or target
inline void logChoice(QueryState& q, const Vec& f, const Choice& ch)
{
    if (q.loggedSocket == ch.socket + 1 && q.loggedTarget == ch.target + 1) return;
    q.loggedSocket = ch.socket + 1;
    q.loggedTarget = ch.target + 1;
    if (ch.socket < 0) {
        core::logf("prank10: PrankTargetQuery facing (%.1f, %.1f, %.1f): no socket within %.0f m; launch in place",
                   f[0], f[1], f[2], kQueryRadius);
        return;
    }
    core::logf("prank10: PrankTargetQuery facing (%.1f, %.1f, %.1f): socket %d target %d at (%.1f, %.1f, %.1f), arc "
               "height %.1f", f[0], f[1], f[2], ch.socket, ch.target, ch.at[0], ch.at[1], ch.at[2], ch.height);
}

// Transform (1.0 getter 0xd88220) and Height (0xd884e0); a null Transform makes ActionPrank launch in place.
inline void* queryOut(uint8_t* node, int pin)
{
    const mernode::Engine& e = mernode::g_state.engine;
    QueryState& q = *(QueryState*)mernode::stateOf(node);
    q.xform = {e.xformElemVt, e.xformType, nullptr};
    q.height = {e.floatElemVt, e.floatType, kDefaultHeight};
    try {
        const std::vector<Vec> f = mernode::fetchPositions(node, g_state.queryClass, kFacing);
        if (f.empty()) {
            tellOnce("nofacing", "prank10: PrankTargetQuery has no Facing link (prankfacing10 off?); launch in place");
            return pin == kHeight ? (void*)&q.height : nullptr;
        }
        const Choice ch = choose(f[0]);
        logChoice(q, f[0], ch);
        q.xform.handle = ch.handle;
        q.height.value = ch.height;
    } catch (...) {   // H6: bad_alloc only; the defaults stand
    }
    if (pin == kHeight) return &q.height;
    return q.xform.handle ? &q.xform : nullptr;
}

// 1.0 describe FUN_00d8d200 (Transform, Height) + Facing: the toy's 'Anchor|facing' locator (mer.hpp prankFacing10)
inline mernode::Spec querySpec()
{
    return {"PrankTargetQuery",
            {{"Transform", mernode::DataOut}, {"Height", mernode::DataOut}, {"Facing", mernode::Data}},
            {},
            sizeof(QueryState),
            nullptr,
            nullptr,
            &queryOut};
}

inline int add(const mernode::Spec& spec)
{
    const int c = mernode::add(spec);
    if (c < 0) core::logf("prank10: %s not added (native class table full)", spec.name);
    return c;
}

}  // namespace detail

inline bool install(const Config& c)
{
    detail::State& s = detail::g_state;
    if (!c.prank10) return true;
    s.dispense = core::prologueMatches(detail::eng::kDispense, detail::eng::kDispenseBytes);
    if (!s.dispense) core::logf("prank10: FUN_0160f370 differs from the analysed bytes; decorations are not drawn");
    s.marker = c.prankMarker10;
    s.socketClass = detail::add(detail::socketSpec());
    s.registerClass = detail::add(detail::registerSpec());
    s.queryClass = detail::add(detail::querySpec());
    return true;
}

}  // namespace di1::prank
