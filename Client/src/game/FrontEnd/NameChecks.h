#pragma once

#include "Ui/Notices.h"

#include <optional>
#include <span>
#include <string>
#include <string_view>

// The checks of a new character's name before F3 01 (0x63d990; Client/docs/research/06-render-escenas-y-ui.md,
// "La ventana de crear"). *Static*, and *emulated* with the contract's names.
namespace Client::Ui
{
// What 0x622c70 checks: the words of FilterName.bmd and Filter.bmd (Tables::LoadFilterWords)
// and texts 457 and 458 of Text.bmd. They have to outlive the query.
struct NameFilters
{
    std::span<const std::string> filterName;
    std::span<const std::string> filter;
    std::string_view operation; // text 457
    std::string_view server;    // text 458
};

// 0x576960(text, word, 0): whether `word` is in `text`, case-sensitive. An empty word is in any
// text. The original advances one character of its code page at a time (_mbclen); MuRet, one byte at a
// time, which is the same with its field's names, in Latin-1: one byte per character, like the Western
// code page (CP1252).
[[nodiscard]] bool ContainsLikeTheOriginal(std::string_view text, std::string_view word);

// 0x622b40(text, commands): with `skipCommands` and the text starting with '/', nothing; otherwise, some
// word of Filter.bmd (`filter`) in the text without spaces. The original builds the copy without spaces in
// 0x800 bytes of the stack.
[[nodiscard]] bool ContainsAFilteredWord(std::string_view text, std::span<const std::string> filter, bool skipCommands);

// 0x5ebbd0(title), the personal shop's (CL9 milestone 7): no word of Filter.bmd in the title without spaces
// (0x576960, without skipping the commands), and neither the first two letters nor the last two or more in
// spaces. Consecutive spaces in the middle pass: the count is only checked at letter 1 and at the end. Empty,
// it passes.
[[nodiscard]] bool IsAShopTitle(std::string_view title, std::span<const std::string> filter);

// 0x622c70: some word of FilterName.bmd in the name without spaces; some of Filter.bmd in the name
// without spaces, unless it starts with '/' (0x622b40 with 1); or, in the name as is, the space, the
// period, the double-byte "\xa1\xa1", "\xa1\xa4" and "\xa1\xad", "Webzen", "WebZen", "webzen", "WEBZEN"
// or texts 457 and 458. Since the space already filters, removing the spaces first does not change the
// result.
[[nodiscard]] bool IsFilteredName(std::string_view name, const NameFilters& filters);

// 0x447ca0, with this binary's NOP patch: checks bytes 0, 2, 4… of `buffer` while the one two further
// ahead is not 0 (past the end, zeros), and one is bad if it is below 0x21, between 0x7f and 0xaf or above
// 0xf7.
[[nodiscard]] bool HasForbiddenSymbol(std::string_view name);

// In 0x63d990's order: fewer than 4 bytes, notice 0x79; filtered, 0x72; with a bad byte in `buffer`
// (0x7798bf8, where the name is and what stayed behind it: Q-C118), 0x7a; otherwise, nothing: it is
// sent.
[[nodiscard]] std::optional<Ui::NoticeId> NoticeOfNewName(std::string_view name, std::string_view buffer,
                                                          const NameFilters& filters);
// The same with the name alone, with nothing behind it.
[[nodiscard]] std::optional<Ui::NoticeId> NoticeOfNewName(std::string_view name, const NameFilters& filters);
} // namespace Client::Ui
