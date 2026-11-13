# SPDX-FileCopyrightText: 2026 jerbanpls
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Cross build for aarch64 Linux (musl) with zig cc (docs/ANDROID.md):
#   cmake -B build-arm64 -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-aarch64-zig.cmake \
#         -DCT_ZIG=/path/to/zig -DCT_ENGINE_ONLY=OFF
# Tests run under CMAKE_CROSSCOMPILING_EMULATOR (qemu-aarch64-static by
# default, CT_QEMU to override), with the musl loader zig links against
# resolved through CT_QEMU_SYSROOT if set.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

if(NOT CT_ZIG)
    set(CT_ZIG $ENV{CT_ZIG})
endif()
if(NOT CT_ZIG)
    find_program(CT_ZIG zig REQUIRED)
endif()
set(CT_ZIG ${CT_ZIG} CACHE FILEPATH "zig used as the cross compiler")
set(CT_ZIG_TARGET aarch64-linux-musl CACHE STRING "zig -target")
# Try-compile projects see the same settings.
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES CT_ZIG CT_ZIG_TARGET)

# zig cc as the compiler: one-word wrappers (here, and for
# tools/build_module.py --cc, which runs CC as a single program), through
# cmake/zig-wrap.sh, which turns CMake's "-Xlinker X" into "-Wl,X" (zig cc
# takes only the latter).
set(ct_zig_wrap ${CMAKE_BINARY_DIR}/zig-wrap)
foreach(t cc c++ ar ranlib)
    set(ct_args ${t})
    if(t STREQUAL "cc" OR t STREQUAL "c++")
        set(ct_args "${t} -target ${CT_ZIG_TARGET}")
    endif()
    file(WRITE ${ct_zig_wrap}/zig-${t}
        "#!/bin/sh\nexec \"${CMAKE_CURRENT_LIST_DIR}/zig-wrap.sh\" \"${CT_ZIG}\" ${ct_args} \"$@\"\n")
    file(CHMOD ${ct_zig_wrap}/zig-${t} PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
        GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
endforeach()
set(CMAKE_C_COMPILER ${ct_zig_wrap}/zig-cc)
set(CMAKE_CXX_COMPILER ${ct_zig_wrap}/zig-c++)
set(CMAKE_AR ${ct_zig_wrap}/zig-ar CACHE FILEPATH "" FORCE)
set(CMAKE_RANLIB ${ct_zig_wrap}/zig-ranlib CACHE FILEPATH "" FORCE)
# No ccache: the wrappers are regenerated per build tree.
set(CT_CCACHE CT_CCACHE-NOTFOUND CACHE FILEPATH "" FORCE)
# zig cc rejects --push-state/--pop-state, which CMake's GNU rules wrap
# around WHOLE_ARCHIVE libraries (the engine programs); plain
# --whole-archive ... --no-whole-archive instead.
set(CMAKE_LINKER_PUSHPOP_STATE_SUPPORTED FALSE)
set(CMAKE_C_LINKER_PUSHPOP_STATE_SUPPORTED FALSE)
set(CMAKE_CXX_LINKER_PUSHPOP_STATE_SUPPORTED FALSE)

if(NOT CT_QEMU)
    set(CT_QEMU qemu-aarch64-static)
endif()
if(CT_QEMU_SYSROOT)
    set(CMAKE_CROSSCOMPILING_EMULATOR ${CT_QEMU} -L ${CT_QEMU_SYSROOT})
else()
    set(CMAKE_CROSSCOMPILING_EMULATOR ${CT_QEMU})
endif()

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
