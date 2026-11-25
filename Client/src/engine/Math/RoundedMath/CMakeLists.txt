# RoundedMath: the one file that sees LLVM-libc (third_party/llvm-libc), behind Math/RoundedMath.h. An object
# library, so Engine carries it and it keeps its own compile options.
#
# Always optimized, also in Debug: LLVM-libc is header-only and runs 10 to 40 times slower unoptimized. MSVC
# refuses /RTC1 next to /O2, so this folder drops it. Never /fp:contract, /fp:fast or -ffast-math here: they change
# the result (RoundedMathTest).
if(MSVC)
    string(REGEX REPLACE "[/-](RTC[1csu]+|Od|Ob[0-9])" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
endif()
add_library(RoundedMath OBJECT RoundedMath.cpp)
# Engine's include directories and the definition that makes ENGINE_API an export.
target_include_directories(RoundedMath PRIVATE $<TARGET_PROPERTY:Engine,INTERFACE_INCLUDE_DIRECTORIES>)
target_compile_definitions(RoundedMath PRIVATE Engine_EXPORTS)
set_target_properties(RoundedMath PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
if(NOT MU_MONOLITHIC)
    set_target_properties(RoundedMath PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()
target_include_directories(RoundedMath SYSTEM PRIVATE "${PROJECT_SOURCE_DIR}/third_party/llvm-libc/libc")
target_compile_definitions(RoundedMath PRIVATE LIBC_NAMESPACE=mu_llvm_libc)
if(MSVC)
    target_compile_options(RoundedMath PRIVATE /O2 /fp:precise)
else()
    target_compile_options(RoundedMath PRIVATE -O2 -ffp-contract=off)
endif()
