#pragma once

#include "muckaii/base/types.hpp"
#include "muckaii/client/lineEditor.hpp"

#include <ftxui/component/event.hpp>

// The command box and profile form share these terminal key translations so editing behaves
// consistently across their one-line text fields.
namespace muckaii::tui
{
// findAltCharacter returns the printable character of an Alt+<character> chord, which a terminal
// sends as ESC followed by the character, or an empty result for any other event. The character is
// typed text, so the caller inserts it and never drops it.
[[nodiscard]] types::OptChar findAltCharacter(const ftxui::Event& event);

// isHome reports whether `event` is the Home key in any of the forms terminals send it.
[[nodiscard]] bool isHome(const ftxui::Event& event);

// isEnd reports whether `event` is the End key in any of the forms terminals send it.
[[nodiscard]] bool isEnd(const ftxui::Event& event);

// applyEditKey applies Backspace, Delete, Left, Right, Home, End, a typed character, or an
// Alt+<character> chord to `editor`. It returns false, leaving the editor untouched, for any other
// event.
[[nodiscard]] bool applyEditKey(client::LineEditor& editor, const ftxui::Event& event);
} // namespace muckaii::tui
