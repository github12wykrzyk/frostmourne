-- FROSTMOURNE / WoW 3.3.5a. This addon observes only official Lua API results.
-- It neither communicates with the DLL nor casts spells or changes targets.
local running = false
local frame = CreateFrame("Frame")
local elapsed = 0
local lastKey = ""
local lastPrinted = 0
local function say(message)
    DEFAULT_CHAT_FRAME:AddMessage("|cff77ddffFM Cast Probe|r " .. message)
end
local function sample(unit)
    if not UnitExists(unit) then return nil end
    local name, rank, display, icon, starts, ends, trade, castID, noKick = UnitCastingInfo(unit)
    local mode = "CAST"
    if not name then
        name, rank, display, icon, starts, ends, trade, noKick = UnitChannelInfo(unit)
        mode = "CHANNEL"
    end
    if not name or not starts or not ends then return nil end
    local guid = UnitGUID(unit) or "UNKNOWN"
    local ms = math.max(0, math.floor(ends - GetTime() * 1000))
    local kickable = noKick and "NO" or "YES"
    return unit .. " mode=" .. mode .. " guid=" .. guid ..
        " spell=" .. tostring(name) .. " id=" .. tostring(castID or "?") ..
        " remaining_ms=" .. tostring(ms) .. " interruptible=" .. kickable,
        guid .. ":" .. tostring(starts) .. ":" .. tostring(ends) .. ":" .. mode
end
frame:SetScript("OnUpdate", function(self, dt)
    if not running then return end
    elapsed = elapsed + dt
    if elapsed < 0.10 then return end
    elapsed = 0
    local text, key = sample("target")
    if not text then text, key = sample("focus") end
    if not text then
        lastKey = ""
        return
    end
    local now = GetTime()
    if key ~= lastKey or now - lastPrinted > 0.5 then
        lastKey = key
        lastPrinted = now
        say(text)
    end
end)
SLASH_FROSTMOURNECASTPROBE1 = "/fmcast"
SLASH_FROSTMOURNECASTPROBE2 = "/fmprobe"
SlashCmdList["FROSTMOURNECASTPROBE"] = function(msg)
    msg = string.lower((msg or ""):match("^%s*(.-)%s*$"))
    if msg == "on" then
        running = true; elapsed = 0; lastKey = ""; lastPrinted = 0
        say("ON: read-only target/focus cast observations; Auto Kick is OFF.")
    elseif msg == "off" then
        running = false
        say("OFF")
    else
        say("Status=" .. (running and "ON" or "OFF") .. " | /fmcast on | /fmcast off")
        if running then
            local text = sample("target")
            if text then say(text) else say("No current target cast.") end
        end
    end
end

-- This message confirms addon initialization; a DLL binding PASS alone does not.
say("Addon LOADED. Uzyj /fmcast on (lub /fmprobe on). Auto Kick pozostaje OFF.")
