#pragma once
#include <memory>
#include "core/screen.h"

namespace gd::screens {
// The game's main menu, modelled explicitly (see AGENTS.md "Menus one by one"). Activation = a synthesized
// click at the control's label position, resolved at activation time from the text capture (the label is
// known to exist; only its pixel position is read from the game, which depends on resolution).
std::unique_ptr<gd::core::Screen> make_main_menu();
// The honest fallback for screens we have not written yet: always active at the bottom layer, speaks its name.
std::unique_ptr<gd::core::Screen> make_unsupported();
}  // namespace gd::screens
