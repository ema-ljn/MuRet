# Only headers that never change, never a project header (docs/CODING_RULES.md § 7).
option(MU_PRECOMPILED_HEADERS "Precompile the client's standard-library headers" ON)

set(MU_STD_PRECOMPILED_HEADERS
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

function(mu_precompile_headers target)
    if(MU_PRECOMPILED_HEADERS)
        target_precompile_headers(${target} PRIVATE ${MU_STD_PRECOMPILED_HEADERS} ${ARGN})
    endif()
endfunction()

# REUSE_FROM needs the same options and definitions in both targets (GCC -Winvalid-pch, MSVC C4651).
function(mu_reuse_precompiled_headers target from)
    if(MU_PRECOMPILED_HEADERS)
        target_precompile_headers(${target} REUSE_FROM ${from})
    endif()
endfunction()
