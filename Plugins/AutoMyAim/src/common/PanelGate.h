// PanelGate.h - timing of the "an item panel is open" check, shared by Auto My
// Aim and Pick My Loot (the two copies are identical). No host calls here, so
// the logic is testable on its own; the plugin's worker thread does the calls.
//
// The host publishes its inventory list (and with it whether each item grid is
// drawn) only when someone requests a scan, and runs the scan on its own worker
// a little later. So the gate requests a scan, reads the listing one host cycle
// later, and repeats. A listing that was never read or is older than kStaleMs
// counts as "a panel is open": clicking or moving the cursor over an open
// stash or vendor window does real damage, pausing a moment longer does not.
#pragma once

#include <cstdint>

namespace PfGates {

class PanelGate {
public:
    // Scan cadence while the plugin acts (picking / aiming) and while it only
    // waits for its key (so the first press already has a fresh answer).
    static constexpr uint64_t kActiveIntervalMs = 200;
    static constexpr uint64_t kIdleIntervalMs = 350;
    // Read this long after requesting: reading at once returns the PREVIOUS scan.
    static constexpr uint64_t kReadDelayMs = 60;
    // A listing older than this is not trusted.
    static constexpr uint64_t kStaleMs = 600;

    enum class Step { Wait, RequestScan, ReadListing };

    Step Next(uint64_t nowMs, bool active) const {
        if (m_pending) return nowMs - m_requestAt >= kReadDelayMs ? Step::ReadListing : Step::Wait;
        const uint64_t interval = active ? kActiveIntervalMs : kIdleIntervalMs;
        if (m_requestAt == 0 || nowMs - m_requestAt >= interval) return Step::RequestScan;
        return Step::Wait;
    }

    void OnRequested(uint64_t nowMs) {
        m_pending = true;
        m_requestAt = nowMs;
    }

    void OnRead(uint64_t nowMs, bool open) {
        m_pending = false;
        m_open = open;
        m_readAt = nowMs;
    }

    void Reset() { *this = PanelGate{}; }

    bool Pending() const { return m_pending; }
    bool LastOpen() const { return m_open; }
    uint64_t LastReadAt() const { return m_readAt; }

    // The fail-safe verdict for a listing read at readAtMs (0 = never).
    static bool Verdict(bool open, uint64_t readAtMs, uint64_t nowMs) {
        if (open || readAtMs == 0) return true;
        return nowMs > readAtMs && nowMs - readAtMs > kStaleMs;
    }

private:
    bool m_pending = false;
    bool m_open = false;
    uint64_t m_requestAt = 0;
    uint64_t m_readAt = 0;
};

}  // namespace PfGates
