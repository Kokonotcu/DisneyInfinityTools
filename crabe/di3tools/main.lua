-- DI3Tools: test bench for Disney Infinity 1.0 worlds ported into 3.0 (native30.py).
-- UNTESTED IN-GAME. Every call below is cited to where CrabeLoader (CL) or CrabeMenu (CM) defines
-- or uses it; CL = CrabeLoader/src/api unless a path says otherwise.
--
-- Edit CONFIG, save, press F4 in-game (hot reload). The menu shows in CrabeMenu: F5 > Mods > DI3Tools.

local CONFIG = {
    -- Travel: the zone record name to force-load.
    TRAVEL_ZONE = "OUT_Intro2",
    -- Sky: realm names for Game.LoadSkyDome, one menu entry each.
    SKY_REALMS = { "rlm_in_day", "rlm_intro_day" },
    -- Spawn: DNA paths the game can open (relative to assets/, like CL 14_world.lua:72
    -- "DNAFile=characters/X.dnax"). An entry containing "=" is passed as a full parameter string.
    SPAWN_DNA = { "exploders/bb/bb05_socket04/bb05_socket04.dnax" },
    SPAWN_FORWARD = 2.0, -- metres in front of the player
    SPAWN_UP = 2.0,      -- metres above the player
}

local TAG = "[DI3Tools] "
local FORCE_WINDOW = 5    -- s: a zone the game refuses is forced only on a second press (CM modules/world.lua:92-117)
local LEAVE_COOLDOWN = 5  -- s: one transition at a time (CM modules/world.lua:20-32)

-- Zone record keys, typed after the 3.0 assets/gamedb/core/zonelist.lua (strings are quoted there,
-- the four flags are true/false), so each is read through the native of its own type.
local ZONE_KEYS = {
    { "ZoneType", "string" }, { "MasterZone", "string" }, { "PlaysetProperties", "string" },
    { "WorldIsIntro", "bool" }, { "OverrideInitialCreation", "string" }, { "OverrideCreation", "string" },
    { "AlternateStartPointZone", "string" }, { "StartWorld", "string" }, { "DefaultProxyAvatar", "string" },
    { "_ShowInMenu_", "bool" }, { "availableInConsumerBuild", "bool" }, { "availableInShippingBuild", "bool" },
}

local clock = 0
local lastLeave = -LEAVE_COOLDOWN
local forceArmedAt = -FORCE_WINDOW

-- Crabe.write (CL 00_core.lua:25) reaches loader.log / the Insert console only at logLevel "debug":
-- the loader drains it at DEBUG (CrabeLoader/src/application/loader.cpp:463) and defaults to "info".
-- Crabe._fileLog (CrabeLoader/src/application/lua_natives.cpp:680-701, registered :765) always
-- appends to <game>\logs\mods\di3tools.log.
local function log(message)
    Crabe.write(TAG .. message)
    if type(Crabe._fileLog) == "function" then
        pcall(Crabe._fileLog, "di3tools", "INFO", message)
    end
end

-- Results of fn, or nil when it raised (CM core/native.lua:12-15, 32-34: Native.poll).
local function valueOnSuccess(ok, ...)
    if ok then return ... end
    return nil
end
local function poll(fn, ...)
    return valueOnSuccess(pcall(fn, ...))
end

local function xyz(x, y, z)
    return string.format("%.2f,%.2f,%.2f", x, y, z)
end

local function inFrontEnd() -- CM modules/world.lua:41-44
    local world = poll(Game.CurrentWorld) -- CL 14_world.lua:154
    return type(world) == "string" and world:lower() == "frontend"
end

-- x, y, z (y up) of the host's avatar, or nil and a reason. Handle check as CL 26_camera.lua:246-247.
local function playerPosition()
    local handle = poll(Game.GetAvatarHandle) -- CL 11_avatar.lua:61
    if type(handle) ~= "number" or handle == 0 then return nil, "no avatar: load a world first" end
    local x, y, z = Game.ActorPosition(handle) -- CL 14_world.lua:56
    if not x then return nil, "the avatar's position cannot be read" end
    return handle, x, y, z
end

-- 1. Player position -------------------------------------------------------------------------

local function logPosition()
    local handle, x, y, z = playerPosition()
    if not handle then return x end
    local zone = tostring(poll(Game.CurrentZone)) -- CL 14_world.lua:172
    log(string.format("player position in %s (world coords of the loaded zone, y up): %s", zone, xyz(x, y, z)))
    log("  native30.py ... --start=" .. xyz(x, y, z))
    local cx, cy, cz = poll(Crabe.Camera.GetPosition) -- CL 26_camera.lua:230 (the free camera while it runs)
    if cx then log("  camera eye: " .. xyz(cx, cy, cz)) end
    return "Player " .. xyz(x, y, z) .. " (logged)"
end

-- 2. Zone record dump ------------------------------------------------------------------------

local function show(fn, key, zone)
    local ok, value = pcall(fn, key, zone)
    if not ok then return "error: " .. tostring(value) end
    if type(value) == "string" then return string.format("%q", value) end
    return tostring(value)
end

-- zone nil = "<current>", the API's default (CL 14_world.lua:176-183).
local function dumpZone(zone)
    local label = zone or "<current>"
    log(string.format("zone record %s (world %s, player zone %s):", label,
        tostring(poll(Game.CurrentWorld)), tostring(poll(Game.CurrentZone))))
    for _, entry in ipairs(ZONE_KEYS) do
        local key, kind = entry[1], entry[2]
        local fn = (kind == "bool") and Game.ZoneBool or Game.ZoneString -- CL 14_world.lua:185 / :178
        log(string.format("  %-26s = %s", key, show(fn, key, zone)))
    end
    return "Zone record of " .. label .. " logged"
end

-- 3. Travel ----------------------------------------------------------------------------------

-- CM modules/world.lua:101-117: a refused zone needs a second press within FORCE_WINDOW.
local function travel()
    local zone = CONFIG.TRAVEL_ZONE
    if clock - lastLeave < LEAVE_COOLDOWN then return "Already leaving, wait for the transition" end
    local allowed = poll(Game.CanLoadLevel, zone) == true -- CL 14_world.lua:233
    if not allowed and clock - forceArmedAt >= FORCE_WINDOW then
        forceArmedAt = clock
        return "The game refuses " .. zone .. ": select again within 5 s to force it (may get stuck loading)"
    end
    forceArmedAt = -FORCE_WINDOW
    lastLeave = clock
    log(string.format("travel: force-loading %s (CanLoadLevel = %s)", zone, tostring(allowed)))
    Game.LoadLevel(zone, true) -- CL 14_world.lua:257: force skips CanLoadLevel, releases the free camera
    return (allowed and "Loading " or "Forcing ") .. zone .. "..."
end

-- 4. Sky -------------------------------------------------------------------------------------

local function loadSky(realm) -- CM modules/world.lua:76-90
    if inFrontEnd() then return "No sky in the main menu: load a world first" end
    local loaded = Game.LoadSkyDome(realm) -- CL 14_world.lua:162
    log(string.format("sky: %s -> %s", realm, loaded and "loaded" or "refused by the engine"))
    if loaded then return "Sky: " .. realm .. " (stays until the world reloads)" end
    return "The engine refused the sky " .. realm
end

-- 5. Spawn -----------------------------------------------------------------------------------

-- Game.SpawnActorNearPlayer (CL 14_world.lua:120-148) has no height offset and returns a count,
-- not the handle, so its forward direction (camera -> player, horizontal) is reproduced here.
local function spawn(dna)
    local handle, px, py, pz = playerPosition()
    if not handle then return px end

    local dx, dz = 0, 1
    local cx, _, cz = poll(Crabe.Camera.GetPosition) -- CL 26_camera.lua:230
    if cx then dx, dz = px - cx, pz - cz end
    local length = math.sqrt(dx * dx + dz * dz)
    if length < 0.001 then dx, dz, length = 0, 1, 1 end
    dx, dz = dx / length, dz / length

    local x, y, z = px + dx * CONFIG.SPAWN_FORWARD, py + CONFIG.SPAWN_UP, pz + dz * CONFIG.SPAWN_FORWARD
    local parameters = dna:find("=", 1, true) and dna or ("DNAFile=" .. dna) -- CL 14_world.lua:70-72
    local actor = Game.CreateActor(parameters, x, y, z, math.atan2(-dx, -dz)) -- CL 14_world.lua:78
    log(string.format("spawn: %s at %s -> %s", parameters, xyz(x, y, z),
        actor and ("handle " .. tostring(actor)) or "nothing built (see loader.log EngineActors)"))
    if actor then return "Spawned " .. dna .. " at " .. xyz(x, y, z) end
    return "The engine built nothing for " .. dna
end

-- Menu ---------------------------------------------------------------------------------------

local function actions(list, prefix, fn)
    local items = {}
    for _, value in ipairs(list) do
        items[#items + 1] = { label = prefix .. value, action = function() return fn(value) end }
    end
    return items
end

-- One top-level entry holding the category, the shape registerInCategory builds
-- (CL 15_menu.lua:90-91). Crabe.Menu.register replaces an entry by label (CL 15_menu.lua:55-68),
-- so a hot reload after editing CONFIG leaves no stale rows. Handlers run under pcall and their
-- return value is the status line (CL 15_menu.lua:20-21, 109-121; CM ui/pages/mods.lua:81-86).
-- CrabeMenu draws this registry under F5 > Mods (CM ui/pages/mods.lua:112-115, ui/pages/main.lua:20).
Crabe.Menu.register({
    label = "DI3Tools",
    submenu = {
        title = "DI3TOOLS",
        items = {
            { label = "Log player position", action = logPosition },
            { label = "Dump zone record (current)", action = function() return dumpZone(nil) end },
            { label = "Dump zone record: " .. CONFIG.TRAVEL_ZONE,
              action = function() return dumpZone(CONFIG.TRAVEL_ZONE) end },
            { label = "Travel (force): " .. CONFIG.TRAVEL_ZONE, action = travel },
            { label = "Sky", submenu = { title = "SKY", items = actions(CONFIG.SKY_REALMS, "Sky: ", loadSky) } },
            { label = "Spawn DNA", submenu = { title = "SPAWN DNA", items = actions(CONFIG.SPAWN_DNA, "", spawn) } },
        },
    },
})

-- Console fallback without CrabeMenu (Insert): Crabe.Exports.DI3Tools.position() etc.
-- (CL 01_sandbox.lua:137-143; Crabe.Sandbox.import is documented but not implemented).
Crabe.Sandbox.export("DI3Tools", {
    position = logPosition, dumpZone = dumpZone, travel = travel, sky = loadSky, spawn = spawn,
})

-- Lifecycle: onUpdate keeps the clock the travel guards use (CM main.lua:68-70; CL 03_lifecycle.lua:91).
Crabe.Mod.register({
    name = "DI3Tools",
    onInit = function()
        log("ready: F5 > Mods > DI3Tools (travel " .. CONFIG.TRAVEL_ZONE .. ")")
    end,
    onUpdate = function(dt)
        clock = clock + (dt or 0.016)
    end,
})
