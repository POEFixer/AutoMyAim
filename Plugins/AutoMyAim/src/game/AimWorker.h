// AimWorker.h - Auto My Aim's target worker: every game read of the aim loop
// runs here, off the render thread. While the aim key is held (or the toggle
// is on) it takes the monster snapshot, keeps the line-of-sight terrain,
// weighs the monsters, checks line of sight and buffs, builds the optional
// target readout and publishes an immutable AimFrame. While the plugin only
// waits for its key it keeps the item-panel gate fresh, so the first press
// already has an answer. The frame tick on the render thread only re-projects
// the chosen monster and sends the cursor move.
//
// Inputs from the render thread: a heartbeat every frame tick and a copy of
// the settings on every change. Without a recent heartbeat the worker idles
// and reads nothing.
#pragma once

#include "../../../../POEFixer/plugin_sdk/PluginSDK.h"

#include "../common/GameGates.h"
#include "../common/PanelGate.h"
#include "../config/Settings.h"
#include "Aim.h"
#include "AimTypes.h"
#include "RayCaster.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace AutoMyAim {

// The aim loop's clock, in ms. GetTickCount64 advances in 15.6 ms steps,
// which would turn a 16 ms update interval into 31 ms.
inline unsigned long long SteadyMs() {
    return static_cast<unsigned long long>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                               std::chrono::steady_clock::now().time_since_epoch())
                                               .count());
}

// One published pass. Never modified after publication.
struct AimFrame {
    enum class Result { NotInGame, Dead, NoTarget, Aiming };

    uint64_t seq = 0;
    unsigned long long timeMs = 0;  // SteadyMs() when the snapshot was taken
    uint64_t areaCounter = 0;
    Result result = Result::NotInGame;
    Target target;                  // valid when result == Aiming
    bool havePlayer = false;
    float playerWX = 0.f, playerWY = 0.f, playerWZ = 0.f;
    int candidates = 0;             // hostile monsters in range and on screen
    bool terrainLoaded = false;     // line-of-sight terrain available
    std::string debug;              // target readout (diagnostics option), else empty
};

class AimWorker {
public:
    explicit AimWorker(const PluginSDK::Context* ctx) : m_ctx(ctx) {}
    ~AimWorker() { Stop(); }
    AimWorker(const AimWorker&) = delete;
    AimWorker& operator=(const AimWorker&) = delete;

    void Start() {
        if (m_thread.joinable()) return;
        {
            std::lock_guard<std::mutex> lk(m_wakeMutex);
            m_stop = false;
        }
        ResetState();
        m_thread = std::thread(&AimWorker::Loop, this);
    }

    // Joins the thread: after Stop() returns the worker makes no host call.
    void Stop() {
        {
            std::lock_guard<std::mutex> lk(m_wakeMutex);
            m_stop = true;
        }
        m_wakeCv.notify_all();
        if (m_thread.joinable()) m_thread.join();
        ResetState();
    }

    // Render thread: after loading and on every settings change.
    void SetSettings(std::shared_ptr<const AutoMyAimConfig::Settings> s) {
        std::lock_guard<std::mutex> lk(m_inMutex);
        m_settings = std::move(s);
    }

    // Render thread, every frame tick. armed: in game, game in front, POEFixer
    // menu closed (the panel gate stays fresh). active: armed and the aim key
    // held or the toggle on (targets are computed).
    void Heartbeat(bool armed, bool active) {
        m_beatMs.store(SteadyMs(), std::memory_order_relaxed);
        m_armed.store(armed, std::memory_order_relaxed);
        const bool was = m_active.exchange(armed && active, std::memory_order_acq_rel);
        if (armed && active && !was) m_wakeCv.notify_all();  // first target right after the press
    }

    std::shared_ptr<const AimFrame> Latest() const {
        std::lock_guard<std::mutex> lk(m_outMutex);
        return m_frame;
    }

    // Fail-safe item-panel verdict: a listing never read or older than
    // PanelGate::kStaleMs counts as open.
    bool PanelOpen(unsigned long long nowMs) const {
        return PfGates::PanelGate::Verdict(m_panelOpen.load(std::memory_order_acquire),
                                           m_panelReadAt.load(std::memory_order_acquire), nowMs);
    }
    uint64_t Faults() const { return m_faults.load(std::memory_order_relaxed); }

private:
    static constexpr unsigned long long kHeartbeatTimeoutMs = 400;
    static constexpr unsigned long long kGridPollMs = 250;
    static constexpr unsigned long long kDebugRefreshMs = 250;

    void ResetState() {
        m_aim.ClearCaches();
        m_rc.Reset();
        m_gate.Reset();
        m_panelOpen.store(false, std::memory_order_relaxed);
        m_panelReadAt.store(0, std::memory_order_release);
        m_lastPass = 0;
        m_lastGridPoll = 0;
        m_area = ~0ull;
        m_wasArmed = false;
        m_debugTarget = 0;
        m_debugAt = 0;
        m_debug.clear();
        std::lock_guard<std::mutex> lk(m_outMutex);
        m_frame.reset();
    }

    void Loop() {
        for (;;) {
            {
                std::lock_guard<std::mutex> lk(m_wakeMutex);
                if (m_stop) return;
            }
            // This is the plugin's own thread: the host's SEH bridge guards only
            // host -> plugin calls, so a structured fault or a C++ exception here
            // would end the whole program. Every tick is guarded twice and a
            // faulting tick costs only that pass (and a one-second pause).
            if (TickSEH(this) == 0) {
                m_faults.fetch_add(1, std::memory_order_relaxed);
                m_aim.ClearCaches();
                m_rc.Reset();
                m_lastGridPoll = 0;
                m_gate.Reset();
                m_panelReadAt.store(0, std::memory_order_release);
                m_backoffUntil = SteadyMs() + 1000;
            }
            const unsigned long long now = SteadyMs();
            int waitMs = 50;
            if (IsActive(now)) waitMs = 5;
            else if (IsArmed(now)) waitMs = 20;
            std::unique_lock<std::mutex> lk(m_wakeMutex);
            if (m_stop) return;
            m_wakeCv.wait_for(lk, std::chrono::milliseconds(waitMs));
        }
    }

    // SEH layer: no locals that need unwinding (C2712), it only forwards.
    static int TickSEH(AimWorker* self) {
        __try {
            return self->TickGuarded();
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return 0;
        }
    }

    int TickGuarded() {
        try {
            Tick();
            return 1;
        } catch (...) {
            return 0;
        }
    }

    bool Fresh(unsigned long long now) const {
        return now - m_beatMs.load(std::memory_order_relaxed) < kHeartbeatTimeoutMs;
    }
    bool IsArmed(unsigned long long now) const { return m_armed.load(std::memory_order_relaxed) && Fresh(now); }
    bool IsActive(unsigned long long now) const { return m_active.load(std::memory_order_acquire) && Fresh(now); }

    void Tick() {
        const unsigned long long now = SteadyMs();
        if (now < m_backoffUntil || !m_ctx) return;
        if (!IsArmed(now)) {
            if (m_wasArmed) {
                // A pending scan request would be read as fresh on return:
                // forget it (the gate starts again with a new request).
                m_gate.Reset();
                m_panelReadAt.store(0, std::memory_order_release);
                m_wasArmed = false;
            }
            return;
        }
        m_wasArmed = true;

        std::shared_ptr<const AutoMyAimConfig::Settings> cfg;
        {
            std::lock_guard<std::mutex> lk(m_inMutex);
            cfg = m_settings;
        }
        if (!cfg) return;

        const bool active = IsActive(now);
        if (cfg->gatePanelOpen) {
            StepPanelGate(now, active);
        } else if (m_gate.LastReadAt() != 0 || m_gate.Pending()) {
            // Gate switched off: drop its state, so switching it on again
            // starts from "not read yet" (counts as open) with a new request.
            m_gate.Reset();
            m_panelReadAt.store(0, std::memory_order_release);
        }
        if (!active) return;
        // The render tick pauses on an open panel anyway: no snapshot for it.
        if (cfg->gatePanelOpen && PanelOpen(now)) return;

        const unsigned long long interval = static_cast<unsigned long long>(
            std::max(cfg->actionIntervalMs, AutoMyAimConfig::kActionIntervalMinMs));
        if (m_lastPass != 0 && now - m_lastPass < interval) return;
        m_lastPass = now;
        Pass(*cfg, now);
    }

    void StepPanelGate(unsigned long long now, bool active) {
        switch (m_gate.Next(now, active)) {
            case PfGates::PanelGate::Step::RequestScan:
                m_ctx->Inventory.Scan(-1);  // only flags the host worker; returns at once
                m_gate.OnRequested(now);
                break;
            case PfGates::PanelGate::Step::ReadListing: {
                const bool open = PfGates::AnyItemPanelOpen(m_ctx);
                m_gate.OnRead(now, open);
                m_panelOpen.store(open, std::memory_order_relaxed);
                m_panelReadAt.store(now, std::memory_order_release);
                break;
            }
            case PfGates::PanelGate::Step::Wait:
                break;
        }
    }

    void Pass(const AutoMyAimConfig::Settings& cfg, unsigned long long now) {
        auto frame = std::make_shared<AimFrame>();
        frame->timeMs = now;
        // Player + monsters only: the host drops every other entity before
        // copying its strings, which is most of the cost of a full snapshot.
        const PluginSDK::Snapshot snap = m_ctx->Game.GetSnapshot(PluginSDK::EntityType::Monster);
        frame->areaCounter = snap.AreaChangeCounter;
        if (snap.State != PluginSDK::GameState::InGame || !snap.Player.IsValid) {
            frame->result = AimFrame::Result::NotInGame;
            Publish(std::move(frame));
            return;
        }
        if (PfGates::PlayerDead(snap)) {
            frame->result = AimFrame::Result::Dead;
            Publish(std::move(frame));
            return;
        }
        if (snap.AreaChangeCounter != m_area) {
            m_area = snap.AreaChangeCounter;
            m_aim.ClearCaches();
            m_rc.Reset();
            m_lastGridPoll = 0;
        }
        if (cfg.useLineOfSight && (m_lastGridPoll == 0 || now - m_lastGridPoll >= kGridPollMs)) {
            m_lastGridPoll = now;
            m_rc.Poll(m_ctx);
        }
        frame->terrainLoaded = m_rc.Valid();

        const PluginSDK::ScreenSize disp = m_ctx->Game.GetScreenSize();
        if (disp.Width < 16.f || disp.Height < 16.f) {
            frame->result = AimFrame::Result::NotInGame;
            Publish(std::move(frame));
            return;
        }
        const Target* best = m_aim.Compute(m_ctx, snap, cfg, m_rc, disp.Width, disp.Height, now);
        frame->havePlayer = m_aim.HavePlayer();
        frame->playerWX = m_aim.PlayerWorldX();
        frame->playerWY = m_aim.PlayerWorldY();
        frame->playerWZ = m_aim.PlayerWorldZ();
        frame->candidates = m_aim.Candidates();
        if (!best) {
            frame->result = AimFrame::Result::NoTarget;
        } else {
            frame->result = AimFrame::Result::Aiming;
            frame->target = *best;
            if (cfg.debugShowTarget) frame->debug = TargetDebug(*best, now);
        }
        Publish(std::move(frame));
    }

    // Rebuilt when the target changes or every kDebugRefreshMs, not every pass.
    const std::string& TargetDebug(const Target& t, unsigned long long now) {
        if (t.id == m_debugTarget && !m_debug.empty() && now - m_debugAt < kDebugRefreshMs) return m_debug;
        m_debugTarget = t.id;
        m_debugAt = now;
        const char* rar = t.rarity == 3 ? "Unique" : t.rarity == 2 ? "Rare" : t.rarity == 1 ? "Magic" : "Normal";
        char head[200];
        if (t.targetableAddr) {
            const PluginSDK::Targetable tg = m_ctx->Components.ReadTargetable(t.targetableAddr);
            std::snprintf(head, sizeof(head), "%s, %.0f cells | targetable %d, highlightable %d, hidden %d", rar,
                          t.distance, tg.IsTargetable ? 1 : 0, tg.IsHighlightable ? 1 : 0,
                          tg.HiddenFromPlayer ? 1 : 0);
        } else {
            std::snprintf(head, sizeof(head), "%s, %.0f cells | targetable: n/a", rar, t.distance);
        }
        m_debug = head;
        std::string names;
        if (t.buffsAddr) {
            for (const auto& b : m_ctx->Components.EnumerateBuffs(t.buffsAddr)) {
                if (b.Name.empty()) continue;
                if (!names.empty()) names += ", ";
                names += b.Name;
                if (names.size() > 280) {
                    names += ", ...";
                    break;
                }
            }
        }
        m_debug += "\nBuffs: ";
        m_debug += names.empty() ? std::string("(none)") : names;
        return m_debug;
    }

    void Publish(std::shared_ptr<AimFrame> frame) {
        frame->seq = ++m_seq;
        std::lock_guard<std::mutex> lk(m_outMutex);
        m_frame = std::move(frame);
    }

    const PluginSDK::Context* m_ctx = nullptr;
    std::thread m_thread;
    std::mutex m_wakeMutex;
    std::condition_variable m_wakeCv;
    bool m_stop = false;

    // Inputs.
    mutable std::mutex m_inMutex;
    std::shared_ptr<const AutoMyAimConfig::Settings> m_settings;
    std::atomic<bool> m_armed{false};
    std::atomic<bool> m_active{false};
    std::atomic<unsigned long long> m_beatMs{0};

    // Outputs.
    mutable std::mutex m_outMutex;
    std::shared_ptr<const AimFrame> m_frame;
    std::atomic<bool> m_panelOpen{false};
    std::atomic<unsigned long long> m_panelReadAt{0};
    std::atomic<uint64_t> m_faults{0};

    // Worker-thread state.
    AimEngine m_aim;
    RayCaster m_rc;
    PfGates::PanelGate m_gate;
    uint64_t m_seq = 0;
    uint64_t m_area = ~0ull;
    unsigned long long m_lastPass = 0;
    unsigned long long m_lastGridPoll = 0;
    unsigned long long m_backoffUntil = 0;
    bool m_wasArmed = false;
    uint32_t m_debugTarget = 0;
    unsigned long long m_debugAt = 0;
    std::string m_debug;
};

}  // namespace AutoMyAim
