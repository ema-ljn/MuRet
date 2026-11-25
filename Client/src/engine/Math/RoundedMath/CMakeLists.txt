# ClientRoundedMath: the one file that sees LLVM-libc (third_party/llvm-libc), behind Math/RoundedMath.h.
#
# Always optimized, also in Debug: LLVM-libc is header-only and runs 10 to 40 times slower unoptimized. MSVC
# refuses /RTC1 next to /O2, so this folder drops it. Never /fp:contract, /fp:fast or -ffast-math here: they change
# the result (RoundedMathTest).
if(MSVC)
    string(REGEX REPLACE "[/-](RTC[1csu]+|Od|Ob[0-9])" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
endif()
add_library(ClientRoundedMath STATIC RoundedMath.cpp)
target_include_directories(ClientRoundedMath PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../.."
    "${CMAKE_CURRENT_SOURCE_DIR}/../..")
target_include_directories(ClientRoundedMath SYSTEM PRIVATE "${PROJECT_SOURCE_DIR}/third_party/llvm-libc/libc")
target_compile_definitions(ClientRoundedMath PRIVATE LIBC_NAMESPACE=mu_llvm_libc)
if(MSVC)
    target_compile_options(ClientRoundedMath PRIVATE /O2 /fp:precise)
else()
    target_compile_options(ClientRoundedMath PRIVATE -O2 -ffp-contract=off)
endif()
