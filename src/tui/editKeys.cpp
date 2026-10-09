#include "editKeys.hpp"

#include "muckaii/base/wire.hpp"

#include <string>
#include <string_view>

namespace muckaii::tui
{
namespace
{
// kEscape is the terminal escape byte that prefixes control sequences.
constexpr char kEscape { static_cast<char>(wire::kEsc) };

// The characters after ESC that introduce a control sequence (CSI) and a single-shift-3 sequence.
constexpr char kCsiIntroducer { '[' };
constexpr char kSs3Introducer { 'O' };

// The bytes after the introducer that name Home and End in each form.
constexpr std::string_view kCsiHome { "1~" };
constexpr std::string_view kCsiEnd { "4~" };
constexpr std::string_view kSs3Home { "H" };
constexpr std::string_view kSs3End { "F" };

// The length of "ESC <character>", an Alt chord.
constexpr std::size_t kEscapePrefixLength { 2 };

// isSequence reports whether `input` is exactly ESC, `introducer`, then `tail`.
bool isSequence(const std::string& input, char introducer, std::string_view tail)
{
    if (input.size() != kEscapePrefixLength + tail.size() || input[0] != kEscape || input[1] != introducer)
        return false;
    return std::string_view { input }.substr(kEscapePrefixLength) == tail;
}

// insertCharacter inserts `character` into `editor` as typed text.
void insertCharacter(client::LineEditor& editor, char character)
{
    editor.insert(std::string_view { &character, 1 });
}
} // namespace

types::OptChar findAltCharacter(const ftxui::Event& event)
{
    const std::string& input { event.input() };
    if (input.size() != kEscapePrefixLength || input[0] != kEscape)
        return std::nullopt;
    if (! wire::isPrintable(static_cast<std::uint8_t>(input[1])))
        return std::nullopt;
    return input[1];
}

bool isHome(const ftxui::Event& event)
{
    return event == ftxui::Event::Home || isSequence(event.input(), kCsiIntroducer, kCsiHome)
           || isSequence(event.input(), kSs3Introducer, kSs3Home);
}

bool isEnd(const ftxui::Event& event)
{
    return event == ftxui::Event::End || isSequence(event.input(), kCsiIntroducer, kCsiEnd)
           || isSequence(event.input(), kSs3Introducer, kSs3End);
}

bool applyEditKey(client::LineEditor& editor, const ftxui::Event& event)
{
    if (event == ftxui::Event::Backspace)
        editor.backspace();
    else if (event == ftxui::Event::Delete)
        editor.deleteForward();
    else if (event == ftxui::Event::ArrowLeft)
        editor.moveLeft();
    else if (event == ftxui::Event::ArrowRight)
        editor.moveRight();
    else if (isHome(event))
        editor.moveHome();
    else if (isEnd(event))
        editor.moveEnd();
    else if (event.is_character())
        editor.insert(event.character());
    else if (const types::OptChar alt { findAltCharacter(event) }; alt.has_value())
        insertCharacter(editor, *alt);
    else
        return false;
    return true;
}
} // namespace muckaii::tui
