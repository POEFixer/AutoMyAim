// FileUtil.h - small, exception-free file helpers shared by Auto My Aim and
// Pick My Loot (the two copies are identical).
//
// - ReadTextFile / WriteTextFileAtomic take std::filesystem::path, so a
//   Unicode user folder works.
// - WriteTextFileAtomic writes "<name>.tmp" next to the target and swaps it in
//   with MoveFileExW(MOVEFILE_REPLACE_EXISTING): a crash mid-write leaves the
//   previous file intact instead of a half-written one.
// - JsonGet reads one typed field and falls back to the default on a missing
//   key or a wrong type, so one bad field never discards the rest of a config.
// - ReadTextFileChecked tells a missing file from one that exists but cannot
//   be read (locked by a backup or sync tool, too large); KeepCorruptCopy and
//   SaveGuard keep a settings file that could not be read or parsed from being
//   replaced by the defaults (see SaveGuard).
#pragma once

#include <Windows.h>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>
#include <type_traits>

#include "../../lib/nlohmann/json.hpp"

namespace PfIo {

enum class ReadStatus { Ok, Missing, Unreadable };

// Ok: `out` holds the whole file. Missing: there is no file (or folder) at the
// path. Unreadable: something is there but could not be read in full - opened
// exclusively by another program, a folder, a read error or larger than any
// config can be. A caller must not treat Unreadable as "no settings yet".
inline ReadStatus ReadTextFileChecked(const std::filesystem::path& path, std::string& out) {
    out.clear();
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || !f) {
        const DWORD attrs = ::GetFileAttributesW(path.c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES) {
            const DWORD err = ::GetLastError();
            if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND) return ReadStatus::Missing;
        }
        return ReadStatus::Unreadable;
    }
    char buf[16384];
    size_t n = 0;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) {
        out.append(buf, n);
        if (out.size() > (64u << 20)) {  // a config is never this large: refuse garbage
            std::fclose(f);
            out.clear();
            return ReadStatus::Unreadable;
        }
    }
    const bool failed = std::ferror(f) != 0;
    std::fclose(f);
    if (failed) {
        out.clear();
        return ReadStatus::Unreadable;
    }
    return ReadStatus::Ok;
}

inline bool ReadTextFile(const std::filesystem::path& path, std::string& out) {
    return ReadTextFileChecked(path, out) == ReadStatus::Ok;
}

// Copies a file that could not be parsed to "<name>.corrupt" next to it (an
// older copy is replaced), so the user can recover it by hand.
inline bool KeepCorruptCopy(const std::filesystem::path& path) {
    std::filesystem::path copy = path;
    copy += L".corrupt";
    return ::CopyFileW(path.c_str(), copy.c_str(), FALSE) != FALSE;
}

// How loading a settings file went.
enum class LoadStatus {
    Missing,     // no file yet: the defaults are used and may be written
    Loaded,      // read and parsed (a single bad field falls back on its own)
    Unreadable,  // the file exists but could not be read: kept untouched
    Corrupt,     // read but not valid JSON: copied to .corrupt, kept untouched
};

// Write gate for a settings file. After an Unreadable or Corrupt load the
// plugin runs on the defaults, but writing them would replace the user's file
// (the host saves every plugin every minute, on disable and on exit). So no
// write happens until the user changes a setting in this session; an
// Unreadable file is meanwhile retried (ShouldRetryLoad) and adopted once it
// can be read.
class SaveGuard {
public:
    void OnLoad(LoadStatus s) {
        m_reason = s;
        m_hold = s == LoadStatus::Unreadable || s == LoadStatus::Corrupt;
    }
    void OnUserChange() { m_hold = false; }
    bool MayWrite() const { return !m_hold; }
    bool Holding() const { return m_hold; }
    LoadStatus Reason() const { return m_reason; }
    bool ShouldRetryLoad() const { return m_hold && m_reason == LoadStatus::Unreadable; }

private:
    LoadStatus m_reason = LoadStatus::Missing;
    bool m_hold = false;
};

inline bool WriteTextFileAtomic(const std::filesystem::path& path, const std::string& text) {
    std::error_code ec;
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), ec);
    std::filesystem::path tmp = path;
    tmp += L".tmp";
    FILE* f = nullptr;
    if (_wfopen_s(&f, tmp.c_str(), L"wb") != 0 || !f) return false;
    const bool written = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    const bool flushed = std::fflush(f) == 0;
    std::fclose(f);
    if (!written || !flushed) {
        ::DeleteFileW(tmp.c_str());
        return false;
    }
    if (!::MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        ::DeleteFileW(tmp.c_str());
        return false;
    }
    return true;
}

// Parses without throwing; a discarded value (parse error) is returned as null.
inline nlohmann::json ParseJson(const std::string& text) {
    nlohmann::json j = nlohmann::json::parse(text, nullptr, false);
    if (j.is_discarded()) return nlohmann::json();
    return j;
}

inline std::string DumpJson(const nlohmann::json& j, int indent = 2) {
    return j.dump(indent, ' ', false, nlohmann::json::error_handler_t::replace);
}

template <typename T>
T JsonGet(const nlohmann::json& obj, const char* key, T fallback) {
    if (!obj.is_object()) return fallback;
    const auto it = obj.find(key);
    if (it == obj.end()) return fallback;
    const nlohmann::json& v = *it;
    if constexpr (std::is_same_v<T, bool>) {
        if (v.is_boolean()) return v.get<bool>();
        if (v.is_number_integer()) return v.get<long long>() != 0;
        return fallback;
    } else if constexpr (std::is_integral_v<T>) {
        if (v.is_number_integer()) return static_cast<T>(v.get<long long>());
        if (v.is_number_float()) {
            const double d = v.get<double>();
            return std::isfinite(d) ? static_cast<T>(d) : fallback;
        }
        return fallback;
    } else if constexpr (std::is_floating_point_v<T>) {
        if (!v.is_number()) return fallback;
        const double d = v.get<double>();
        return std::isfinite(d) ? static_cast<T>(d) : fallback;
    } else if constexpr (std::is_same_v<T, std::string>) {
        return v.is_string() ? v.get<std::string>() : fallback;
    } else {
        static_assert(sizeof(T) == 0, "JsonGet: unsupported type");
    }
}

inline void ColorFromJson(const nlohmann::json& j, float dst[4]) {
    if (!j.is_array()) return;
    for (size_t i = 0; i < 4 && i < j.size(); ++i) {
        if (!j[i].is_number()) continue;
        const double d = j[i].get<double>();
        if (!std::isfinite(d)) continue;
        dst[i] = static_cast<float>(d < 0.0 ? 0.0 : (d > 1.0 ? 1.0 : d));
    }
}

inline nlohmann::json ColorToJson(const float c[4]) {
    return nlohmann::json::array({c[0], c[1], c[2], c[3]});
}

}  // namespace PfIo
