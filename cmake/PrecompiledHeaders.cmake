# Only headers that never change, never a project header (docs/CODING_RULES.md § 7).
option(MURET_PRECOMPILED_HEADERS "Precompile the client's standard library and glm headers" ON)

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

set(MURET_GLM_PRECOMPILED_HEADERS)

function(muret_precompile_headers target)
    if(MURET_PRECOMPILED_HEADERS)
        target_precompile_headers(${target} PRIVATE
            ${MURET_STD_PRECOMPILED_HEADERS} ${MURET_GLM_PRECOMPILED_HEADERS} ${ARGN})
    endif()
endfunction()

# REUSE_FROM needs the same options and definitions in both targets (GCC -Winvalid-pch, MSVC C4651).
function(muret_reuse_precompiled_headers target from)
    if(MURET_PRECOMPILED_HEADERS)
        target_precompile_headers(${target} REUSE_FROM ${from})
    endif()
endfunction()
