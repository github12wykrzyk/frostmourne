-- FROSTMOURNE / WoW 3.3.5a. This addon observes only official Lua API results.
-- It neither communicates with the DLL nor casts spells or changes targets.
local running = true -- Auto-start.
local kickRequests = false -- Opt-in via pinned GUI only.
local lastKickRequest = nil
local lastTrial = nil
local lastConfirmed = nil
local frame = CreateFrame("Frame")
local elapsed = 0
local lastKey = ""
local lastPrinted = 0
local announced = false
local indicator = nil

-- The banner is independent of chat tabs/filters. It means LUA ADDON loaded,
-- not that the diagnostic DLL can read live casts or execute Kick.
local function showIndicator()
    if not UIParent then return end
    if not indicator then
        indicator = CreateFrame("Frame", nil, UIParent)
        indicator:SetWidth(520)
        indicator:SetHeight(30)
        indicator:SetPoint("TOP", UIParent, "TOP", 0, -95)
        indicator:SetFrameStrata("HIGH")
        indicator:SetBackdrop({bgFile="Interface\\Tooltips\\UI-Tooltip-Background",
            edgeFile="Interface\\Tooltips\\UI-Tooltip-Border", tile=true, tileSize=16,
            edgeSize=10, insets={left=3,right=3,top=3,bottom=3}})
        indicator:SetBackdropColor(0,0,0,0.85)
        local message = indicator:CreateFontString(nil, "OVERLAY", "GameFontNormal")
        message:SetPoint("CENTER", indicator, "CENTER", 0, 0)
        message:SetText("|cff55ff99FM CAST PROBE: LUA AKTYWNE|r  " ..
            (kickRequests and "|cffffbb00AUTO KICK TRIAL - NIEPOTWIERDZONE|r" or "|cffffff00AUTO KICK OFF|r"))
    end
    indicator:Show()
end

local function say(message)
    local chat = DEFAULT_CHAT_FRAME or ChatFrame1
    if chat and chat.AddMessage then
        chat:AddMessage("|cff77ddffFM Cast Probe|r " .. message)
        return true
    end
    return false
end
-- UI-thread, opt-in diagnostic marker. DLL independently verifies the GUID
-- and cast in the exact registered game client before attempting a Kick.
local function requestNativeKick()
    if not kickRequests then return end
    if select(2,UnitClass("player")) ~= "ROGUE" or UnitIsDeadOrGhost("player") then return end
    if not UnitExists("target") or not UnitCanAttack("player","target") or
        UnitIsDeadOrGhost("target") then return end
    local spellName = GetSpellInfo(1766)
    if not spellName or IsSpellInRange(spellName,"target") ~= 1 then return end
    if UnitPower("player",3) < 25 or not IsUsableSpell(1766) then return end
    local cdStart,cdDuration = GetSpellCooldown(1766)
    if not cdStart or (cdStart > 0 and cdDuration and cdStart + cdDuration > GetTime()) then return end
    local name,_,_,_,starts,ends,_,_,noKick = UnitCastingInfo("target")
    if not name then name,_,_,_,starts,ends,_,noKick = UnitChannelInfo("target") end
    if not name or not starts or not ends or noKick then return end
    local remaining = ends - GetTime()*1000
    if remaining > 3000 or remaining <= 150 or starts >= ends then return end
    local guid=UnitGUID("target")
    if not guid or not string.match(guid,"^0x%x+$") then return end
    local token=guid..":"..tostring(starts)..":"..tostring(ends)
    if lastKickRequest==token then return end
    lastKickRequest=token
    lastTrial = { guid=guid, when=GetTime(), token=token }
    say("KICK TRIAL: przekazano znacznik do DLL; wynik akcji NIEPOTWIERDZONY; GUID="..guid)
    -- Marked UnitCastingInfo is intercepted only by the matching opt-in DLL.
    -- The marker requests an action; only actual combat logs can confirm Kick.
    UnitCastingInfo("FMKICK12340|"..guid.."|"..
        string.format("%.0f",starts).."|"..string.format("%.0f",ends).."|1766")
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
-- PLAYER_LOGIN/PLAYER_ENTERING_WORLD are observed on the game UI thread.
-- Print after chat exists. The visible banner remains if chat tabs hide messages.
frame:RegisterEvent("PLAYER_LOGIN")
frame:RegisterEvent("PLAYER_ENTERING_WORLD")
frame:RegisterEvent("COMBAT_LOG_EVENT_UNFILTERED")
frame:SetScript("OnEvent", function(self, event)
    if event == "COMBAT_LOG_EVENT_UNFILTERED" then
        -- WoW 3.3.5a event payload is delivered as varargs (not CombatLogGetCurrentEventInfo).
        local timestamp, subevent, hideCaster, sourceGUID, sourceName, sourceFlags,
              destGUID, destName, destFlags, spellId, spellName, spellSchool,
              extraSpellId, extraSpellName = ...
        if kickRequests and subevent == "SPELL_INTERRUPT" and
           sourceGUID == UnitGUID("player") and spellId == 1766 then
            local correlated = lastTrial and destGUID == lastTrial.guid and
                               GetTime() - lastTrial.when >= 0 and
                               GetTime() - lastTrial.when <= 3
            say("KICK: przerwanie POTWIERDZONE w COMBAT LOG; spell="..
                tostring(extraSpellName or extraSpellId or "?")..
                " target="..tostring(destName or destGUID or "?")..
                (correlated and " [czas/GUID zgodne z testem DLL]" or
                                " [nie przypisano do zadanego testu DLL]"))
            lastConfirmed = GetTime()
        end
        return
    end
    if event == "PLAYER_LOGIN" or event == "PLAYER_ENTERING_WORLD" then
        running = true
        showIndicator()
        if not announced then
            announced = say("LUA ADDON ZALADOWANY: casty ON; "..(kickRequests and "Kick TRIAL (wynik niepotwierdzony)" or "Auto Kick OFF")..". DLL sprawdz w loaderze.")
        end
    end
end)
frame:SetScript("OnUpdate", function(self, dt)
    if not running then return end
    if not announced then
        showIndicator()
        announced = say("LUA ADDON ZALADOWANY: casty ON; "..(kickRequests and "Kick TRIAL (wynik niepotwierdzony)" or "Auto Kick OFF")..". DLL sprawdz w loaderze.")
    end
    elapsed = elapsed + dt
    if elapsed < 0.10 then return end
    elapsed = 0
    requestNativeKick()
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
        say("ON: monitoring castow; Auto Kick trial: "..(kickRequests and "wlaczony w loaderze" or "OFF")..".")
    elseif msg == "off" then
        running = false
        say("OFF")
    else
        say("Status=" .. (running and "ON" or "OFF") .. " | Kick trial=" .. (kickRequests and "ON (niepotwierdzony)" or "OFF") .. " | /fmcast on | /fmcast off")
        if running then
            local text = sample("target")
            if text then say(text) else say("No current target cast.") end
        end
    end
end

-- If neither the banner nor chat message appears, WoW did not execute this addon.
