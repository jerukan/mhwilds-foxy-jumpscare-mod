-- Foxy Jumpscare: every second there's a 1-in-N chance Foxy jumps you.
--
-- Requires:
--   reframework/plugins/reframework-d2d.dll  (+ its autorun lua)  -> draws the frames
--   reframework/plugins/jumpscare_audio.dll                       -> plays the sound
-- Assets:
--   reframework/images/foxy_jumpscare/000.png .. 025.png  (chroma-keyed RGBA frames)
--   reframework/sounds/foxy_jumpscare.wav

local re, d2d, imgui, json, log = re, d2d, imgui, json, log

local FRAME_DIR = "foxy_jumpscare/"
local FRAME_COUNT = 26
local FPS = 30000 / 1001
local DURATION = FRAME_COUNT / FPS
local SOUND = "foxy_jumpscare.wav"
local CONFIG_FILE = "foxy_jumpscare.json"

local cfg = {
    enabled = true,
    one_in = 10000, -- chance per second = 1 / one_in
    volume = 1.0,
}

local saved = json.load_file(CONFIG_FILE)
if type(saved) == "table" then
    for k, v in pairs(saved) do
        if cfg[k] ~= nil and type(v) == type(cfg[k]) then cfg[k] = v end
    end
end

local function save_config()
    json.dump_file(CONFIG_FILE, cfg)
end

local function audio() return rawget(_G, "jumpscare_audio") end

if audio() then
    local ok, err = audio().preload(SOUND)
    if not ok then log.warn("[foxy_jumpscare] preload failed: " .. tostring(err)) end
end

-- ---------------------------------------------------------------------------
-- Trigger logic
-- ---------------------------------------------------------------------------

local playing_since = nil -- os.clock() when the current jumpscare started
local last_roll = os.clock()

local function trigger()
    playing_since = os.clock()
    local a = audio()
    if a then
        local ok, err = a.play(SOUND, cfg.volume)
        if not ok then log.warn("[foxy_jumpscare] play failed: " .. tostring(err)) end
    end
end

re.on_frame(function()
    local now = os.clock() -- wall clock on Windows

    if playing_since then
        if now - playing_since >= DURATION then playing_since = nil end
        last_roll = now
        return
    end

    if not cfg.enabled then
        last_roll = now
        return
    end

    -- After a long hitch/load, don't fire a burst of catch-up rolls.
    if now - last_roll > 5.0 then last_roll = now - 1.0 end

    while now - last_roll >= 1.0 do
        last_roll = last_roll + 1.0
        if math.random(math.max(1, cfg.one_in)) == 1 then
            trigger()
            break
        end
    end
end)

-- ---------------------------------------------------------------------------
-- Drawing (d2d)
-- ---------------------------------------------------------------------------

local frames = {}
local frames_loaded = 0

if d2d then
    d2d.register(function()
        -- Called on (re)initialization of the d2d device: images must be created here.
        frames, frames_loaded = {}, 0
        for i = 0, FRAME_COUNT - 1 do
            local ok, img = pcall(d2d.Image.new, string.format("%s%03d.png", FRAME_DIR, i))
            if ok and img then
                frames[i] = img
                frames_loaded = frames_loaded + 1
            end
        end
        if frames_loaded < FRAME_COUNT then
            log.warn(string.format("[foxy_jumpscare] loaded %d/%d frames", frames_loaded, FRAME_COUNT))
        end
    end, function()
        if not playing_since then return end

        local idx = math.floor((os.clock() - playing_since) * FPS)
        local img = frames[math.min(idx, FRAME_COUNT - 1)]
        if not img then return end

        -- Scale to cover the whole screen, keeping aspect ratio (centered, cropped).
        local sw, sh = d2d.surface_size()
        local iw, ih = img:size()
        local scale = math.max(sw / iw, sh / ih)
        local w, h = iw * scale, ih * scale
        d2d.image(img, (sw - w) / 2, (sh - h) / 2, w, h)
    end)
else
    log.error("[foxy_jumpscare] reframework-d2d plugin not found; frames will not be drawn")
end

-- ---------------------------------------------------------------------------
-- Config UI (REFramework menu -> Script Generated UI)
-- ---------------------------------------------------------------------------

local function fmt_duration(sec)
    if sec < 60 then return string.format("%d s", sec) end
    if sec < 3600 then return string.format("%.1f min", sec / 60) end
    return string.format("%.1f h", sec / 3600)
end

re.on_draw_ui(function()
    if not imgui.tree_node("Foxy Jumpscare") then return end

    local changed, any = false, false
    changed, cfg.enabled = imgui.checkbox("Enabled", cfg.enabled); any = any or changed
    changed, cfg.one_in = imgui.drag_int("Chance: 1 in N per second", cfg.one_in, 10, 1, 1000000); any = any or changed
    imgui.text("Average wait: " .. fmt_duration(cfg.one_in))
    changed, cfg.volume = imgui.slider_float("Volume", cfg.volume, 0.0, 2.0, "%.2f"); any = any or changed
    if any then save_config() end

    if imgui.button("Test jumpscare") then trigger() end

    imgui.text(string.format("d2d: %s (%d/%d frames)", d2d and "ok" or "MISSING", frames_loaded, FRAME_COUNT))
    imgui.text("jumpscare_audio plugin: " .. (audio() and "ok" or "MISSING"))

    imgui.tree_pop()
end)
