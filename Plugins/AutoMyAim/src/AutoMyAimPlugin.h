// AutoMyAimPlugin.h - the Auto My Aim plugin: while the aim key is held (or
// the toggle is on) it moves the cursor onto the best nearby monster so skills
// cast at it. It only moves the cursor; the player still casts.
#pragma once

#include "../../../POEFixer/plugin_sdk/PluginSDK.h"

#include "config/Settings.h"
#include "game/AimTypes.h"
#include "game/AimWorker.h"
#include "ipc/Coordination.h"

#include <imgui.h>

#include <memory>
#include <string>

class AutoMyAimPlugin : public PluginSDK::Plugin {
public:
    const char* GetName() const override;
    bool WantsOverlay() const override;
    void OnEnable(bool gameAttached) override;
    void OnDisable() override;
    void SaveSettings() override;
    void DrawUI() override;
    void DrawSettings() override;

private:
    // Why the aim is or is not moving the cursor right now (drives the status
    // chip and the settings header; no strings are built per frame).
    enum class State {
        Off,            // neither held nor toggled
        Binding,        // a key was just bound and is still held
        NotInGame,      // loading, not foreground, or not in game
        MenuOpen,       // the POEFixer menu is shown
        PanelOpen,      // an item panel (inventory, stash, vendor) is open
        Picking,        // Pick My Loot owns the cursor
        Dead,
        NoTarget,
        Aiming,
    };
    static const char* StateText(State s);
    static bool StateIsActive(State s) { return s != State::Off && s != State::Binding; }

    void EnsureLoaded();
    void MarkDirty();
    // The settings file was locked at load: read it again, adopt it if it reads.
    void RetrySettingsLoad(const std::filesystem::path& dir);
    // Hands the worker a copy of the current settings.
    void PushSettings();
    // A key setting changed: save it, and ignore that key until it is released
    // (binding the toggle key must not also toggle).
    void OnKeyBound(int vk);
    void FrameTick();
    void SetIdle(State s);
    float Rnd01();
    // A fresh worker frame (the overlay and diagnostics use it), else nullptr.
    const AutoMyAim::AimFrame* FreshFrame() const;

    // A worker frame older than this (or than four update intervals) is not acted on.
    static constexpr unsigned long long kFrameMaxAgeMs = 150;

    // Overlay (ui/Overlay.cpp).
    void DrawRangeCircle(ImDrawList* dl, float scale);
    void DrawTargetMarker(ImDrawList* dl, float scale);

    // Settings sections (ui/SettingsUi.cpp).
    void DrawHeader();
    void DrawSectionKeys();
    void DrawSectionTargeting();
    void DrawSectionPriority();
    void DrawSectionCursor();
    void DrawSectionSafety();
    void DrawSectionDisplay();
    void DrawSectionDiagnostics();

    AutoMyAimConfig::Settings       m_settings;
    PfIo::SaveGuard                 m_saveGuard;
    PoeFixerIpc::PickBusyChannel    m_pickBusy;
    PluginSDK::EventsService::Token m_frameTok;

    bool  m_loaded = false;
    bool  m_enabled = false;
    bool  m_dirty = false;
    unsigned long long m_dirtyAt = 0;

    State m_state = State::Off;
    bool  m_toggled = false;
    bool  m_togglePrev = false;
    int   m_waitReleaseVk = 0;

    bool  m_hasTarget = false;
    AutoMyAim::Target m_curTarget;
    uint32_t m_jitterTarget = 0;
    unsigned long long m_jitterAt = 0;
    float m_jitterX = 0.f, m_jitterY = 0.f;

    unsigned long long m_lastAction = 0;
    uint64_t m_areaCounter = 0;

    // The last worker frame the tick used (overlay and diagnostics read it).
    std::shared_ptr<const AutoMyAim::AimFrame> m_frame;
    std::string m_targetDebug;
    uint64_t    m_debugSeq = 0;

    uint32_t m_rng = 0x1234567u;

    // Last member: destroyed (joined) first, before anything it could touch.
    AutoMyAim::AimWorker            m_worker{ctx()};
};
