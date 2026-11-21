#!/bin/bash
# Fetches the third-party sources at fixed revisions into deps/ (for CI; locally they live in /home/user/deps-src).
set -e
mkdir -p deps && cd deps
sparse_libc() { # dir ref
  mkdir -p "$1" && cd "$1" && git init -q && git remote add origin https://github.com/llvm/llvm-project.git
  git sparse-checkout init --cone && git sparse-checkout set libc
  git fetch -q --depth 1 --filter=blob:none origin "$2" && git checkout -q FETCH_HEAD && cd ..
}
[ -d llvm-23 ] || sparse_libc llvm-23 refs/tags/llvmorg-23.1.6
[ -d llvm-main ] || sparse_libc llvm-main b6f762dad0298dfcc44c2e9f0268c9d088e40655
if [ "$1" = "all" ]; then
  [ -d openlibm ] || git clone -q --depth 1 --branch v0.8.8 https://github.com/JuliaMath/openlibm.git
  if [ ! -d core-math-2024 ]; then
    git clone -q https://github.com/mockingbirdnest/core-math.git core-math-mirror
    git -C core-math-mirror worktree add -f ../core-math-2024 c421893441e279e44fc6f7dc5f0e589dd05d77d7
  fi
fi
# The upstream of CORE-MATH (Inria GitLab), when the network allows it.
[ -d core-math-upstream ] || git clone -q --depth 1 https://gitlab.inria.fr/core-math/core-math.git core-math-upstream || echo "core-math upstream not reachable"
ls
