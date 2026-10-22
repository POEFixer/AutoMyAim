// AutoMyAim.cpp - lifecycle and the per-frame aim tick.
//
// Auto My Aim, originally created by Omer Faruk ARPA
// (https://github.com/omrfarukarpa/poefixer-automyaim), maintained by POEFixer.
// Licensed under the GNU General Public License v3.0 (see LICENSE).
#include "AutoMyAimPlugin.h"

#include "common/GameGates.h"
#include "input/Win32Input.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {
unsigned long long NowMs() { return ::GetTickCount64(); }
}  // namespace

const char* AutoMyAimPlugin::GetName() const { return PluginSDK_GetInfo()->name; }

bool AutoMyAimPlugin::WantsOverlay() const {
    return m_settings.showOverlay || m_settings.showRangeCircle || m_settings.debugShowTarget;
}

const char* AutoMyAimPlugin::StateText(State s) {
    switch (s) {
        case State::Off:       return "Idle";
        case State::Binding:   return "Key set: release it";
        case State::NotInGame: return "Waiting for the game";
        case State::MenuOpen:  return "Paused: POEFixer menu open";
        case State::PanelOpen: return "Paused: item panel open";
        case State::Picking:   return "Paused: picking loot";
        case State::Dead:      return "Paused: dead";
        case State::NoTarget:  return "No target";
        case State::Aiming:    return "Aiming";
    }
    return "";
}

void AutoMyAimPlugin::EnsureLoaded() {
    if (m_loaded) return;
    const std::filesystem::path dir = DirectoryPath();
    if (dir.empty()) return;
    const PfIo::LoadStatus st = m_settings.Load(dir);
    m_saveGuard.OnLoad(st);
    if (st == PfIo::LoadStatus::Corrupt)
        ctx()->Log.Warn("Auto My Aim: config/settings.json is not valid JSON - kept as settings.json.corrupt; "
                        "running on defaults, nothing is saved until a setting is changed");
    else if (st == PfIo::LoadStatus::Unreadable)
        ctx()->Log.Warn("Auto My Aim: config/settings.json exists but could not be read - running on defaults "
                        "without overwriting it; it is read again later");
    m_loaded = true;
}

void AutoMyAimPlugin::MarkDirty() {
    m_dirty = true;
    m_dirtyAt = NowMs();
    m_saveGuard.OnUserChange();  // the user's own change may replace an unreadable file
    PushSettings();
}

void AutoMyAimPlugin::PushSettings() {
    m_worker.SetSettings(std::make_shared<const AutoMyAimConfig::Settings>(m_settings));
}

void AutoMyAimPlugin::RetrySettingsLoad(const std::filesystem::path& dir) {
    AutoMyAimConfig::Settings fresh;
    const PfIo::LoadStatus st = fresh.Load(dir);
    if (st == PfIo::LoadStatus::Unreadable) return;  // still locked: try again at the next save
    if (st == PfIo::LoadStatus::Loaded) {
        m_settings = std::move(fresh);
        PushSettings();
        ctx()->Log.Info("Auto My Aim: config/settings.json could be read now - its settings are in use");
    }
    m_saveGuard.OnLoad(st);
}

void AutoMyAimPlugin::OnKeyBound(int vk) {
    m_waitReleaseVk = vk;
    MarkDirty();
}

void AutoMyAimPlugin::OnEnable(bool) {
    if (!HostCompatible()) {
        ctx()->Log.Error("Auto My Aim: this POEFixer version is not compatible with the plugin - disabled");
        return;
    }
    if (ctx()->ImGuiContext) ImGui::SetCurrentContext(static_cast<ImGuiContext*>(ctx()->ImGuiContext));
    EnsureLoaded();
    m_rng = static_cast<uint32_t>(NowMs()) | 1u;
    m_state = State::Off;
    m_toggled = false;
    m_togglePrev = true;  // a key still held at enable time must not toggle
    m_hasTarget = false;
    m_areaCounter = ctx()->Game.GetAreaChangeCounter();
    m_lastAction = 0;
    m_jitterTarget = 0;
    m_frame.reset();
    m_debugSeq = 0;
    PushSettings();
    m_worker.Start();

    auto& events = const_cast<PluginSDK::EventsService&>(ctx()->Events);
    if (!m_frameTok.Valid()) m_frameTok = events.OnFrame([this] {
        try {
            FrameTick();
        } catch (...) {
            SetIdle(State::Off);
        }
    });
    m_enabled = true;
    ctx()->Log.Info("Auto My Aim enabled");
}

void AutoMyAimPlugin::OnDisable() {
    auto& events = const_cast<PluginSDK::EventsService&>(ctx()->Events);
    if (m_frameTok.Valid()) events.Unsubscribe(m_frameTok);
    m_frameTok = {};
    m_worker.Stop();  // joined: no host call from the worker after this
    m_enabled = false;
    m_hasTarget = false;
    m_toggled = false;
    m_state = State::Off;
    m_frame.reset();
    SaveSettings();
    ctx()->Log.Info("Auto My Aim disabled");
}

void AutoMyAimPlugin::SaveSettings() {
    if (!m_loaded) return;
    const std::filesystem::path dir = DirectoryPath();
    if (m_saveGuard.ShouldRetryLoad()) RetrySettingsLoad(dir);
    if (m_saveGuard.MayWrite() && m_settings.Save(dir)) m_dirty = false;
}

void AutoMyAimPlugin::SetIdle(State s) {
    m_state = s;
    m_hasTarget = false;
}

float AutoMyAimPlugin::Rnd01() {
    m_rng ^= m_rng << 13;
    m_rng ^= m_rng >> 17;
    m_rng ^= m_rng << 5;
    return static_cast<float>(m_rng) / static_cast<float>(0xFFFFFFFFu);
}

const AutoMyAim::AimFrame* AutoMyAimPlugin::FreshFrame() const {
    if (!m_frame) return nullptr;
    const unsigned long long now = AutoMyAim::SteadyMs();
    if (now < m_frame->timeMs || now - m_frame->timeMs > 1000) return nullptr;
    return m_frame.get();
}

// Render thread, every frame. Every game read (inventory listing, monster
// snapshot, terrain, buffs) happens on the worker (game/AimWorker.h); this only
// reads keys and cheap host scalars, re-projects the target the worker chose
// (the camera moved since its snapshot) and sends the cursor move.
void AutoMyAimPlugin::FrameTick() {
    if (!HostCompatible()) return;
    const unsigned long long now = AutoMyAim::SteadyMs();  // the worker's clock
    if (m_waitReleaseVk != 0 && !PfInput::IsKeyDown(m_waitReleaseVk)) m_waitReleaseVk = 0;
    const bool keyJustBound = m_waitReleaseVk != 0;

    const bool foreground = ctx()->Game.IsForeground();
    const bool menu = ctx()->Game.IsMenuVisible();

    // Toggle: edge-triggered, never while a just-bound key is still held, never
    // while the POEFixer menu (where keys are bound) has the keyboard.
    const bool toggleDown = foreground && !menu && !keyJustBound && PfInput::IsKeyDown(m_settings.toggleKey);
    if (toggleDown && !m_togglePrev) m_toggled = !m_toggled;
    m_togglePrev = toggleDown;

    const bool held = foreground && PfInput::IsKeyDown(m_settings.aimKey);
    const bool inGame = foreground && ctx()->Game.IsInGame();
    const bool armed = inGame && !menu;
    m_worker.Heartbeat(armed, !keyJustBound && (m_toggled || held));

    if (keyJustBound) { SetIdle(State::Binding); return; }
    if (!m_toggled && !held) { SetIdle(State::Off); return; }
    if (!inGame) { SetIdle(State::NotInGame); return; }
    if (menu) { SetIdle(State::MenuOpen); return; }

    const unsigned long long interval = static_cast<unsigned long long>(
        std::max(m_settings.actionIntervalMs, AutoMyAimConfig::kActionIntervalMinMs));
    if (now - m_lastAction < interval) return;
    m_lastAction = now;

    if (m_settings.pauseWhilePicking && m_pickBusy.IsBusy()) { SetIdle(State::Picking); return; }
    // Fail safe: until the worker has read a fresh inventory listing, a panel
    // counts as open.
    if (m_settings.gatePanelOpen && m_worker.PanelOpen(now)) { SetIdle(State::PanelOpen); return; }

    const std::shared_ptr<const AutoMyAim::AimFrame> frame = m_worker.Latest();
    const unsigned long long maxAge = std::max(kFrameMaxAgeMs, interval * 4);
    if (!frame || now < frame->timeMs || now - frame->timeMs > maxAge) {
        // No fresh pass yet (the key was just pressed or the worker is behind).
        SetIdle(State::NoTarget);
        return;
    }
    m_frame = frame;
    if (frame->seq != m_debugSeq) {
        m_debugSeq = frame->seq;
        if (!frame->debug.empty()) m_targetDebug = frame->debug;
    }
    // A frame from the area the player just left: wait for the next pass.
    if (ctx()->Game.GetAreaChangeCounter() != frame->areaCounter) { SetIdle(State::NoTarget); return; }
    if (frame->areaCounter != m_areaCounter) {
        m_areaCounter = frame->areaCounter;
        m_jitterTarget = 0;
    }
    switch (frame->result) {
        case AutoMyAim::AimFrame::Result::NotInGame: SetIdle(State::NotInGame); return;
        case AutoMyAim::AimFrame::Result::Dead:      SetIdle(State::Dead); return;
        case AutoMyAim::AimFrame::Result::NoTarget:  SetIdle(State::NoTarget); return;
        case AutoMyAim::AimFrame::Result::Aiming:    break;
    }

    const PluginSDK::ScreenSize disp = ctx()->Game.GetScreenSize();
    if (disp.Width < 16.f || disp.Height < 16.f) { SetIdle(State::NotInGame); return; }
    AutoMyAim::Target target = frame->target;
    float sx = 0.f, sy = 0.f;
    if (!ctx()->Render.WorldToScreen(target.worldX, target.worldY, target.worldZ, sx, sy) ||
        sx < 0.f || sy < 0.f || sx >= disp.Width || sy >= disp.Height) {
        SetIdle(State::NoTarget);  // left the screen since the worker's snapshot
        return;
    }
    target.screenX = sx;
    target.screenY = sy;
    float ax = sx, ay = sy;

    if (m_settings.confineToCircle && frame->havePlayer) {
        float psx = 0.f, psy = 0.f;
        if (ctx()->Render.WorldToScreen(frame->playerWX, frame->playerWY, frame->playerWZ, psx, psy)) {
            const float ddx = ax - psx, ddy = ay - psy;
            const float d = std::sqrt(ddx * ddx + ddy * ddy);
            const float r = static_cast<float>(m_settings.circleRadius);
            if (d > r && d > 0.f) {
                ax = psx + ddx / d * r;
                ay = psy + ddy / d * r;
            }
        }
    }

    // The random offset is chosen per target (and renewed every 300 ms), so the
    // cursor rests near the monster instead of shaking around it every frame.
    if (m_settings.randomize && m_settings.randomizeRadius > 0) {
        if (target.id != m_jitterTarget || now - m_jitterAt >= 300) {
            m_jitterTarget = target.id;
            m_jitterAt = now;
            const float ang = Rnd01() * 6.2831853f;
            const float rad = std::sqrt(Rnd01()) * static_cast<float>(m_settings.randomizeRadius);
            m_jitterX = std::cos(ang) * rad;
            m_jitterY = std::sin(ang) * rad;
        }
        ax += m_jitterX;
        ay += m_jitterY;
    }

    const float margin = 4.f;
    ax = std::clamp(ax, margin, disp.Width - margin);
    ay = std::clamp(ay, margin, disp.Height - margin);

    PfInput::MoveCursorClient(ctx()->Game.GetGameWindow(), static_cast<int>(std::lround(ax)),
                              static_cast<int>(std::lround(ay)));
    m_curTarget = target;
    m_hasTarget = true;
    m_state = State::Aiming;
}

extern "C" PLUGIN_API PluginSDK::Plugin* CreatePlugin() { return new AutoMyAimPlugin(); }
extern "C" PLUGIN_API void DestroyPlugin(PluginSDK::Plugin* p) { delete p; }

// Plugin info: the host checks it before CreatePlugin; the publish tool reads it.
POEFIXER_PLUGIN_INFO(
    "AutoMyAim",
    "Auto My Aim",
    "1.0.9",
    "Omer Faruk ARPA, POEFixer",
    "Moves the cursor onto the best monster near you while you hold a key or after a toggle, so skills cast at it; line of sight, rarity and life priority.")
