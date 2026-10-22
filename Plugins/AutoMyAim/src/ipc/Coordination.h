// Coordination.h - the loot-pickup busy channel between Pick My Loot and
// Auto My Aim. The two plugin copies of this file are identical.
//
// Protocol (unchanged since the original 1.0.x releases, so old and new
// builds of either plugin keep talking to each other in both directions):
//   - a pagefile-backed mapping named  Local\PoeFixer_PickMyLoot_Busy
//   - layout  { uint32 magic = 0x504D4C31 ('PML1'); 4 bytes padding;
//               uint64 pickBusyUntilTick }   (16 bytes, natural x64 alignment)
//   - pickBusyUntilTick is a GetTickCount64() value: Pick My Loot writes
//     "now + N ms" while it moves the cursor or clicks; Auto My Aim pauses while
//     GetTickCount64() < pickBusyUntilTick. Writing 0 releases the cursor at once.
//
// Hardening over the original: 64-bit reads and writes are atomic
// (Interlocked*), a mapping that carries a foreign non-zero magic is left alone
// (the channel reports itself unavailable instead of overwriting it), and a
// deadline further than kMaxBusyMs in the future is ignored so a stale or
// corrupt value can never pause aiming for long.
#pragma once

#include <Windows.h>

#include <cstddef>
#include <cstdint>

namespace PoeFixerIpc {

inline constexpr uint32_t kMagic = 0x504D4C31;
inline constexpr const wchar_t* kMappingName = L"Local\\PoeFixer_PickMyLoot_Busy";
inline constexpr uint64_t kMaxBusyMs = 2000;

struct Shared {
    uint32_t          magic;
    volatile uint64_t pickBusyUntilTick;
};
static_assert(sizeof(Shared) == 16, "busy channel layout is a cross-plugin contract");
static_assert(offsetof(Shared, pickBusyUntilTick) == 8, "busy channel layout is a cross-plugin contract");

class PickBusyChannel {
public:
    PickBusyChannel() {
        m_map = ::CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                     static_cast<DWORD>(sizeof(Shared)), kMappingName);
        if (!m_map) return;
        m_p = static_cast<Shared*>(::MapViewOfFile(m_map, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Shared)));
        if (!m_p) return;
        // A new mapping is zero-filled: claim it. A mapping that already carries
        // our magic is shared as is. Anything else belongs to someone else.
        const LONG prev = ::InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(&m_p->magic),
                                                       static_cast<LONG>(kMagic), 0);
        m_ok = (prev == 0 || static_cast<uint32_t>(prev) == kMagic);
    }

    ~PickBusyChannel() {
        if (m_p) ::UnmapViewOfFile(m_p);
        if (m_map) ::CloseHandle(m_map);
    }

    PickBusyChannel(const PickBusyChannel&) = delete;
    PickBusyChannel& operator=(const PickBusyChannel&) = delete;

    bool Valid() const { return m_ok; }

    // Pick My Loot: "I own the cursor for the next ms milliseconds".
    void MarkBusy(uint32_t ms) {
        if (!m_ok) return;
        const uint64_t until = ::GetTickCount64() + (ms > kMaxBusyMs ? kMaxBusyMs : ms);
        ::InterlockedExchange64(reinterpret_cast<volatile LONG64*>(&m_p->pickBusyUntilTick),
                                static_cast<LONG64>(until));
    }

    // Pick My Loot: release the cursor now (picking finished or stopped).
    void MarkIdle() {
        if (!m_ok) return;
        ::InterlockedExchange64(reinterpret_cast<volatile LONG64*>(&m_p->pickBusyUntilTick), 0);
    }

    // Auto My Aim: is the cursor owned by the loot picker right now?
    bool IsBusy() const {
        if (!m_ok) return false;
        const uint64_t until = static_cast<uint64_t>(::InterlockedCompareExchange64(
            reinterpret_cast<volatile LONG64*>(&m_p->pickBusyUntilTick), 0, 0));
        const uint64_t now = ::GetTickCount64();
        return now < until && until - now <= kMaxBusyMs;
    }

private:
    HANDLE  m_map = nullptr;
    Shared* m_p = nullptr;
    bool    m_ok = false;
};

}  // namespace PoeFixerIpc
