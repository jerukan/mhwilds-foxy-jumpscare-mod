// jumpscare_audio: tiny REFramework plugin that lets Lua scripts play .wav files.
//
// Lua API (global table `jumpscare_audio`):
//   jumpscare_audio.preload(name)        -> true | false, err   load + cache, no playback
//   jumpscare_audio.play(name [, vol])   -> true | false, err   vol: 0..4 (1 = original), 16-bit PCM only
//   jumpscare_audio.stop()
//
// `name` is relative to <game>/reframework/sounds/ (no "..", no absolute paths).
// Playback uses PlaySoundW (one sound at a time, default output device).
//
// None of the Lua-facing functions raise Lua errors: we return (false, message) instead,
// so no longjmp ever crosses a frame with live C++ objects.

#include <windows.h>
#include <mmsystem.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

extern "C" {
#include <lua.h>
}

#include <reframework/API.h>

namespace {

const REFrameworkPluginFunctions* g_fns = nullptr;

struct Sound {
    std::vector<uint8_t> bytes; // entire .wav file
    size_t data_offset = 0;
    size_t data_size = 0;
    bool pcm16 = false;
};

std::mutex g_mutex;
std::unordered_map<std::string, Sound> g_cache;
// Buffer currently handed to PlaySoundW (SND_MEMORY). Must stay alive while playing,
// so it is only replaced after PlaySoundW(nullptr, ...) has stopped playback.
std::vector<uint8_t> g_playing;

std::wstring utf8_to_wide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

std::filesystem::path sounds_dir() {
    std::wstring buf(32768, L'\0');
    DWORD n = GetModuleFileNameW(nullptr, buf.data(), (DWORD)buf.size());
    buf.resize(n);
    return std::filesystem::path(buf).parent_path() / L"reframework" / L"sounds";
}

bool is_safe_name(const std::string& n) {
    if (n.empty()) return false;
    if (n.find("..") != std::string::npos) return false;
    if (n.find(':') != std::string::npos) return false;
    if (n[0] == '/' || n[0] == '\\') return false;
    return true;
}

// Caller holds g_mutex. Returns nullptr on success.
const char* load_sound(const std::string& name, Sound*& out) {
    if (auto it = g_cache.find(name); it != g_cache.end()) {
        out = &it->second;
        return nullptr;
    }

    std::ifstream f(sounds_dir() / utf8_to_wide(name), std::ios::binary);
    if (!f) return "could not open file in reframework/sounds";

    Sound s;
    s.bytes.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    const size_t size = s.bytes.size();
    const uint8_t* b = s.bytes.data();

    if (size < 12 || std::memcmp(b, "RIFF", 4) != 0 || std::memcmp(b + 8, "WAVE", 4) != 0) {
        return "not a RIFF/WAVE file";
    }

    uint16_t format = 0, bits = 0;
    bool have_fmt = false;
    size_t pos = 12;
    while (pos + 8 <= size) {
        uint32_t len = 0;
        std::memcpy(&len, b + pos + 4, 4);
        const size_t body = pos + 8;
        if (len > size - body) len = (uint32_t)(size - body); // tolerate truncated files

        if (std::memcmp(b + pos, "fmt ", 4) == 0 && len >= 16) {
            std::memcpy(&format, b + body, 2);
            std::memcpy(&bits, b + body + 14, 2);
            have_fmt = true;
        } else if (std::memcmp(b + pos, "data", 4) == 0) {
            s.data_offset = body;
            s.data_size = len;
            break;
        }
        pos = body + len + (len & 1); // chunks are word aligned
    }

    if (!have_fmt || s.data_size == 0) return "wav is missing fmt or data chunk";

    // 1 = WAVE_FORMAT_PCM, 0xFFFE = WAVE_FORMAT_EXTENSIBLE (practically always PCM here)
    s.pcm16 = (format == 1 || format == 0xFFFE) && bits == 16;

    out = &g_cache.emplace(name, std::move(s)).first->second;
    return nullptr;
}

const char* preload(const std::string& name) {
    std::lock_guard lock(g_mutex);
    if (!is_safe_name(name)) return "invalid name (must be relative to reframework/sounds, no '..')";
    Sound* snd = nullptr;
    return load_sound(name, snd);
}

const char* play(const std::string& name, double volume) {
    std::lock_guard lock(g_mutex);
    if (!is_safe_name(name)) return "invalid name (must be relative to reframework/sounds, no '..')";

    Sound* snd = nullptr;
    if (auto err = load_sound(name, snd)) return err;

    PlaySoundW(nullptr, nullptr, 0); // stop whatever is playing before we free its buffer
    g_playing = snd->bytes;

    if (!std::isfinite(volume)) volume = 1.0;
    volume = std::clamp(volume, 0.0, 4.0);
    if (snd->pcm16 && volume != 1.0) {
        uint8_t* p = g_playing.data() + snd->data_offset;
        const size_t samples = snd->data_size / 2;
        for (size_t i = 0; i < samples; ++i) {
            int16_t v;
            std::memcpy(&v, p + i * 2, 2);
            const long scaled = std::lround(v * volume);
            v = (int16_t)std::clamp(scaled, -32768L, 32767L);
            std::memcpy(p + i * 2, &v, 2);
        }
    }

    if (!PlaySoundW(reinterpret_cast<LPCWSTR>(g_playing.data()), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT)) {
        return "PlaySound failed";
    }
    return nullptr;
}

void stop() {
    std::lock_guard lock(g_mutex);
    PlaySoundW(nullptr, nullptr, 0);
}

// ---- Lua bindings ----

int push_result(lua_State* L, const char* err) {
    if (err == nullptr) {
        lua_pushboolean(L, 1);
        return 1;
    }
    lua_pushboolean(L, 0);
    lua_pushstring(L, err);
    return 2;
}

// Returned error strings are static literals, so they outlive the std::string scope.
int l_preload(lua_State* L) {
    if (lua_type(L, 1) != LUA_TSTRING) return push_result(L, "expected file name string");
    size_t len = 0;
    const char* s = lua_tolstring(L, 1, &len);
    const char* err = preload(std::string(s, len));
    return push_result(L, err);
}

int l_play(lua_State* L) {
    if (lua_type(L, 1) != LUA_TSTRING) return push_result(L, "expected file name string");
    size_t len = 0;
    const char* s = lua_tolstring(L, 1, &len);
    const double volume = lua_type(L, 2) == LUA_TNUMBER ? lua_tonumberx(L, 2, nullptr) : 1.0;
    const char* err = play(std::string(s, len), volume);
    return push_result(L, err);
}

int l_stop(lua_State*) {
    stop();
    return 0;
}

void on_lua_state_created(lua_State* L) {
    lua_createtable(L, 0, 3);
    lua_pushcclosure(L, l_preload, 0);
    lua_setfield(L, -2, "preload");
    lua_pushcclosure(L, l_play, 0);
    lua_setfield(L, -2, "play");
    lua_pushcclosure(L, l_stop, 0);
    lua_setfield(L, -2, "stop");
    lua_setglobal(L, "jumpscare_audio");
}

void on_lua_state_destroyed(lua_State*) {
    stop(); // "Reset scripts" shouldn't leave a scream playing
}

} // namespace

extern "C" __declspec(dllexport) void reframework_plugin_required_version(REFrameworkPluginVersion* version) {
    // We only use on_lua_state_created/destroyed and log_info, which exist in every 1.x
    // plugin API, so ask for the lowest minor to load on older REFramework builds too.
    version->major = REFRAMEWORK_PLUGIN_VERSION_MAJOR;
    version->minor = 0;
    version->patch = 0;
}

extern "C" __declspec(dllexport) bool reframework_plugin_initialize(const REFrameworkPluginInitializeParam* param) {
    g_fns = param->functions;
    g_fns->on_lua_state_created(on_lua_state_created);
    g_fns->on_lua_state_destroyed(on_lua_state_destroyed);
    g_fns->log_info("%s", "[jumpscare_audio] loaded");
    return true;
}
