# vcpkg's x64-windows with the outside libraries static and SDL3 alone shared, as on Linux (docs/adr/0028-shared-library-modules.md).
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)

if(PORT STREQUAL "sdl3")
    set(VCPKG_LIBRARY_LINKAGE dynamic)
endif()
