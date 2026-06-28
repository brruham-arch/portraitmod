-- portraitmod_toggle.lua
-- Toggle portrait mode via AML mod libportraitmod.so
-- Author: brruham

local ffi = require("ffi")

ffi.cdef[[
typedef struct {
    void (*enable)(void);
    void (*disable)(void);
    int  (*is_enabled)(void);
} PortraitAPI;
]]

local api = nil
local loaded = false

local function loadAPI()
    local ok, err = pcall(function()
        local lib = ffi.load("portraitmod")
        api = ffi.cast("PortraitAPI*", lib.portraitmod_api)
    end)
    if not ok then
        sampAddChatMessage("[PORTRAIT] ERROR load API: " .. tostring(err), 0xFF4444)
        return false
    end
    loaded = true
    sampAddChatMessage("[PORTRAIT] API loaded OK", 0x44FF44)
    return true
end

-- Command /portrait
function sampev.onSendCommand(cmd)
    if cmd == "/portrait" then
        if not loaded then
            if not loadAPI() then return true end
        end

        if api ~= nil then
            if api.is_enabled() == 1 then
                api.disable()
                sampAddChatMessage("[PORTRAIT] Portrait mode: NONAKTIF", 0xFF8800)
            else
                api.enable()
                sampAddChatMessage("[PORTRAIT] Portrait mode: AKTIF", 0x00FF88)
            end
        else
            sampAddChatMessage("[PORTRAIT] ERROR: api nil", 0xFF4444)
        end
        return true
    end
end

sampAddChatMessage("[PORTRAIT] portraitmod_toggle.lua loaded. Ketik /portrait untuk toggle.", 0xFFFFFF)
