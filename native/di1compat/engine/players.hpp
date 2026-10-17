// The player manager: local players and their ids. core. Main thread.
#pragma once
#include <cstddef>
#include <cstdint>
#include "../core/hooks.hpp"

namespace engine::players {

namespace eng {
// H4 open: called without a prologue check (X-12)
constexpr uint32_t kManager = 0x0225F540;   // *: the player manager
constexpr size_t kLocalCount = 0x90;        // local players: FUN_00b68f60 tests == 1 (0x00B69025)
constexpr size_t kTable = 0x108;            // the player table: an entry per player
constexpr size_t kEntryBytes = 0x50;
constexpr size_t kEntryLocal = 0x10;        // 1: a local player (FUN_015bcc10 selects on it)
constexpr uint32_t kPlayerId = 0x004291B0;  // FUN_004291b0: thiscall (uint32_t* out, player) -> its id
constexpr int kMaxPlayers = 4;              // FUN_01204cf0
}  // namespace eng

// The player manager, null before the engine makes it.
inline uint8_t* manager() { return *(uint8_t**)core::rt(eng::kManager); }

inline int localCount(const uint8_t* pm) { return *(const int*)(pm + eng::kLocalCount); }

// The player table, null when the manager has none yet.
inline const uint8_t* table(const uint8_t* pm) { return *(const uint8_t* const*)(pm + eng::kTable); }

inline bool isLocal(const uint8_t* table, int player)
{
    return *(const int*)(table + eng::kEntryLocal + player * eng::kEntryBytes) == 1;
}

inline uint32_t playerId(uint8_t* pm, int player)
{
    typedef void*(__fastcall * PlayerIdFn)(void* pm, void* edx, uint32_t* out, int player);
    uint32_t id = 0;
    ((PlayerIdFn)core::rt(eng::kPlayerId))(pm, nullptr, &id, player);
    return id;
}

}  // namespace engine::players
