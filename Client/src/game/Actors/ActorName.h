#pragma once

#include <string>
#include <string_view>

namespace Client::Kernel
{
struct ActorState;
} // namespace Client::Kernel

namespace Client::Actors
{

// 0x576960(character + 0x1d9, word, 0): whether `word` is in the character's name, up to its 0. Without a 0 in the
// ten bytes, the original goes on into +0x1e3, which the F3 03 reply sets to 0. The GM check of 0x413fe0 and the
// spectator camera of map 6 (0x635ae0, 0x5fb570 and 0x6371a0, Q-C152) use it.
[[nodiscard]] bool ActorNameContains(const Kernel::ActorState& actor, std::string_view word);

// +0x1d9 as strcmp and sprintf read it: up to its 0. With the ten bytes full, it goes on into +0x1e3, which the F3 03
// reply sets to 0.
[[nodiscard]] std::string_view HeroNameOf(const Kernel::ActorState& hero);

// +0x1d9 as strcmp and strcpy read it: with the ten bytes full, +0x1e3 follows. MuRet does not have what follows
// +0x1e3 and takes it as 0.
[[nodiscard]] std::string ChatNameOf(const Kernel::ActorState& actor);
} // namespace Client::Actors
