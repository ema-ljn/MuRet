# vcpkg's x64-linux with SDL3 alone shared: Platform and Audio use one SDL (docs/adr/0028-shared-library-modules.md).
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME Linux)

if(PORT STREQUAL "sdl3")
    set(VCPKG_LIBRARY_LINKAGE dynamic)
endif()
