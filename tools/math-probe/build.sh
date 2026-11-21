#!/bin/bash
# Builds the probe on Linux with every implementation. Paths of the third-party sources:
#   LLVM_LIBC, LLVM_LIBC_MAIN  llvm-project/libc at the release tag and at main (sparse clones)
#   COREMATH                   a CORE-MATH source tree
#   OPENLIBM                   JuliaMath/openlibm
set -e
cd "$(dirname "$0")"
LLVM_LIBC=${LLVM_LIBC:-deps/llvm-23/libc}            # release tag llvmorg-23.1.6
LLVM_LIBC_MAIN=${LLVM_LIBC_MAIN:-deps/llvm-main/libc}  # main, to check versions agree
OPENLIBM=${OPENLIBM:-deps/openlibm}
COREMATH=${COREMATH:-deps/core-math-2024}   # upstream as of 2024-04-08 (c4218934), plain C
B=${B:-build}
mkdir -p $B
STRICT="-O2 -ffp-contract=off"
NATIVE="-O3 -march=native -ffp-contract=fast"
CXX="g++ -std=c++20"

# LLVM-libc, three ways: baseline x86-64 with GCC, native (FMA, contraction allowed) with GCC, -O0 with Clang.
$CXX $STRICT -I$LLVM_LIBC -Isrc -DLIBC_NAMESPACE=probe_llvm_a -DPROBE_NAME=llvm -DPROBE_ENTRY=ImplLlvm -c src/impl_llvm.cpp -o $B/impl_llvm.o
$CXX $STRICT -I$LLVM_LIBC_MAIN -Isrc -DLIBC_NAMESPACE=probe_llvm_m -DPROBE_NAME=llvm-main -DPROBE_ENTRY=ImplLlvmMain -c src/impl_llvm.cpp -o $B/impl_llvm_main.o
$CXX $NATIVE -I$LLVM_LIBC -Isrc -DLIBC_NAMESPACE=probe_llvm_b -DPROBE_NAME=llvm-native -DPROBE_ENTRY=ImplLlvmNative -c src/impl_llvm.cpp -o $B/impl_llvm_native.o
clang++ -std=c++20 -O0 -I$LLVM_LIBC -Isrc -DLIBC_NAMESPACE=probe_llvm_c -DPROBE_NAME=llvm-clang-O0 -DPROBE_ENTRY=ImplLlvmO0 -c src/impl_llvm.cpp -o $B/impl_llvm_o0.o

# CORE-MATH (C, GNU dialect), from a source tree: baseline and native.
for variant in base native; do
  if [ $variant = base ]; then flags="-O2"; suffix=""; else flags="-O3 -march=native"; suffix="_native"; fi
  renames=""
  for f in sin cos tan acos log10 sinf cosf tanf acosf; do renames="$renames -Dcr_$f=cr_$f$suffix"; done
  for f in sin cos tan acos log10; do
    gcc $flags $renames -w -I$COREMATH/src/binary64/$f -c $COREMATH/src/binary64/$f/$f.c -o $B/cm_${f}$suffix.o
  done
  for f in sin cos tan acos; do
    gcc $flags $renames -w -I$COREMATH/src/binary32/$f -c $COREMATH/src/binary32/$f/${f}f.c -o $B/cm_${f}f$suffix.o
  done
done
$CXX $STRICT -Isrc -DPROBE_SUFFIX= -DPROBE_NAME=coremath -DPROBE_ENTRY=ImplCoreMath -c src/impl_cm.cpp -o $B/impl_cm.o
$CXX $STRICT -Isrc -DPROBE_SUFFIX=_native -DPROBE_NAME=coremath-native -DPROBE_ENTRY=ImplCoreMathNative -c src/impl_cm.cpp -o $B/impl_cm_native.o

# openlibm (FreeBSD msun / fdlibm): strict, and with FMA contraction.
FD_FILES="s_sin s_cos s_tan k_sin k_cos k_tan k_rem_pio2 e_rem_pio2 e_acos e_log10 s_sinf s_cosf s_tanf k_sinf k_cosf k_tanf e_rem_pio2f e_acosf"
FD_INC="-I$OPENLIBM -I$OPENLIBM/include -I$OPENLIBM/amd64 -I$OPENLIBM/src -I$OPENLIBM/ld80 -DASSEMBLER -D__BSD_VISIBLE -Wno-implicit-function-declaration"
for variant in strict fma; do
  if [ $variant = strict ]; then flags="$STRICT"; prefix=fd_; else flags="$NATIVE"; prefix=fdfma_; fi
  for f in $FD_FILES; do
    gcc -std=c99 $flags -fno-builtin -fno-strict-aliasing $FD_INC -DPROBE_PREFIX=$prefix -include src/fd_rename.h -c $OPENLIBM/src/$f.c -o $B/${prefix}$f.o
  done
done
$CXX $STRICT -Isrc -DPROBE_PREFIX=fd_ -DPROBE_NAME=fdlibm -DPROBE_ENTRY=ImplFdlibm -c src/impl_fd.cpp -o $B/impl_fd.o
$CXX $STRICT -Isrc -DPROBE_PREFIX=fdfma_ -DPROBE_NAME=fdlibm-fma -DPROBE_ENTRY=ImplFdlibmFma -c src/impl_fd.cpp -o $B/impl_fd_fma.o

$CXX $STRICT -Isrc -fno-builtin -c src/impl_libm.cpp -o $B/impl_libm.o
$CXX $STRICT -Isrc -c src/impl_mpfr.cpp -o $B/impl_mpfr.o
$CXX $STRICT -Isrc -I/usr/include/x86_64-linux-gnu -c src/impl_sleef.cpp -o $B/impl_sleef.o
$CXX $STRICT -Isrc -DPROBE_HAS_LLVM=1 -DPROBE_HAS_LLVM_MAIN=1 -DPROBE_HAS_LLVM_VARIANTS=1 -DPROBE_HAS_COREMATH=1 -DPROBE_HAS_FDLIBM=1 -DPROBE_HAS_FDLIBM_FMA=1 \
  -DPROBE_HAS_MPFR=1 -DPROBE_HAS_SLEEF=1 -c src/probe.cpp -o $B/probe.o
$CXX -o $B/probe $B/probe.o $B/impl_*.o $B/cm_*.o $B/fd_*.o $B/fdfma_*.o -lmpfr -lgmp -lsleef -lm -lpthread
echo built $B/probe
