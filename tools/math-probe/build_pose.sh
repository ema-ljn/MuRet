#!/bin/bash
# Builds the pose experiment on Linux: the system math library, and LLVM-libc with GCC and with Clang.
set -e
cd "$(dirname "$0")"
LLVM_LIBC=${LLVM_LIBC:-deps/llvm-23/libc}
GLM=${GLM:-deps/glm}
CLIENT=${CLIENT:-../../Client/src}
B=${B:-build}
mkdir -p $B
SOURCES="pose/pose.cpp $CLIENT/Render/Models/HalfLifeMath.cpp $CLIENT/Render/Models/OriginalRotation.cpp"
INCLUDES="-I$CLIENT -I$GLM"
g++ -std=c++20 -O2 -ffp-contract=off $INCLUDES $SOURCES -o $B/pose-system
g++ -std=c++20 -O2 -ffp-contract=off -I$LLVM_LIBC -DLIBC_NAMESPACE=pose_cr -c pose/cr_impl.cpp -o $B/pose_cr_impl.o
g++ -std=c++20 -O2 -ffp-contract=off $INCLUDES -include pose/cr_redirect.h $SOURCES $B/pose_cr_impl.o -o $B/pose-cr
g++ -std=c++20 -O0 -ffp-contract=off $INCLUDES -include pose/cr_redirect.h $SOURCES $B/pose_cr_impl.o -o $B/pose-cr-O0
clang++ -std=c++20 -O2 -ffp-contract=off -I$LLVM_LIBC -DLIBC_NAMESPACE=pose_cr -c pose/cr_impl.cpp -o $B/pose_cr_impl_clang.o
clang++ -std=c++20 -O2 -ffp-contract=off $INCLUDES -include pose/cr_redirect.h $SOURCES $B/pose_cr_impl_clang.o -o $B/pose-cr-clang
echo built the pose binaries in $B
