// ImGuiText.h - one Dear ImGui fix-up shared by Auto My Aim and Pick My Loot
// (the two copies are identical).
//
// A plugin compiles its own copy of Dear ImGui and draws into POEFixer's
// context. ImGui 1.93 fills the character table its word wrapping uses (which
// characters are blanks and punctuation) only when a font atlas is built, and
// the atlas is built by POEFixer's copy, never by this one. Without the call
// below every wrapped text of the plugin (hints, tooltips, the target readout)
// breaks lines in the middle of words. The call is idempotent and cheap (one
// table lookup once the table is filled), so it runs before each draw.
#pragma once

#include <imgui.h>
#include <imgui_internal.h>

namespace PfImGui {

inline void EnsureTextTables() { ImTextInitClassifiers(); }

}  // namespace PfImGui
