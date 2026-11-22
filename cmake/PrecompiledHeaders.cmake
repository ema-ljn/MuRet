# The client's precompiled headers (decided 2026-11-03): the modern replacement of MSVC's `stdafx.h`.
# CMake (target_precompile_headers) builds the header and forces it into every source of the target
# (`-include` on GCC and Clang, `/FI` on MSVC), so no file includes it by hand.
#
# Only headers that do not change go in: the standard library, the glm types (header-only, and no
# target defines GLM_FORCE_*) and GoogleTest in the tests. A project header would rebuild everything
# each time it is touched. It saves no include: every file still includes what it uses
# (docs/CODING_RULES.md § 7, Core Guidelines SF.10 and SF.11), and the Linux CI job builds with
# MURET_PRECOMPILED_HEADERS=OFF so that a missing include still fails.
#
# A target may use another's header (REUSE_FROM) only if it compiles with the same options and
# definitions, as CMake requires: otherwise the precompiled header may not be valid for it (GCC warns
# with -Winvalid-pch, MSVC with C4651). That is why the tests have two: ClientTestSupport's and
# ClientGlTestSupport's, which also sees ClientPlatform's SDL_OPENGL_1_FUNCTION_TYPEDEFS.

option(MURET_PRECOMPILED_HEADERS "Precompile the client's standard library and glm headers" ON)

# The standard-library headers that Client/src and Client/tests include most.
set(MURET_STD_PRECOMPILED_HEADERS
    <algorithm>
    <array>
    <bit>
    <chrono>
    <cstddef>
    <cstdint>
    <filesystem>
    <format>
    <functional>
    <memory>
    <optional>
    <span>
    <string>
    <string_view>
    <system_error>
    <utility>
    <variant>
    <vector>
)

# The glm types of the poses, the cloth and the scenes. Measured with clang -ftime-trace on
# 2026-11-22, they and <bitset>, <deque>, <list>, <map> and <ranges> cut ClientLib's compile time by
# 18% over the standard-library list of 2026-11-03.
set(MURET_GLM_PRECOMPILED_HEADERS)

# muret_precompile_headers(<target> [header ...])
#
# Gives <target> its own precompiled header: the lists above and the headers passed (for example
# <gtest/gtest.h>). <target> has to see glm, as every client target does through ClientLib. Without
# MURET_PRECOMPILED_HEADERS it does nothing.
function(muret_precompile_headers target)
    if(MURET_PRECOMPILED_HEADERS)
        target_precompile_headers(${target} PRIVATE
            ${MURET_STD_PRECOMPILED_HEADERS} ${MURET_GLM_PRECOMPILED_HEADERS} ${ARGN})
    endif()
endfunction()

# muret_reuse_precompiled_headers(<target> <from>)
#
# <target> uses the precompiled header of <from> instead of building one: they have to compile with
# the same definitions (see above). Without MURET_PRECOMPILED_HEADERS it does nothing.
function(muret_reuse_precompiled_headers target from)
    if(MURET_PRECOMPILED_HEADERS)
        target_precompile_headers(${target} REUSE_FROM ${from})
    endif()
endfunction()
