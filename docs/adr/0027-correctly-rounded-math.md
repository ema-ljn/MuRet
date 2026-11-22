# 0027. The client's math is correctly rounded

**Status** · Accepted. Emanuel decided it on 2026-11-21, on the analysis of that day. The behavior golden then gave
the same bits on Windows and on Linux.

## Context

The behavior golden of the refactor (AR0.3) is recorded on Linux and CI replays it on Windows. Its first scene, the
login up to Lorencia, failed on Windows with MSVC at frame 11, in the hash of what is drawn: the first frame of the
login with 3D models. Measured on GitHub's Windows and Linux machines and on an Intel Xeon:

- **Who decides the bits.** IEEE 754 fixes `+`, `-`, `*`, `/` and the square root, so they are the same everywhere. It
  does not fix the sine, the cosine, the tangent, the arc cosine or the logarithm, and each C library rounds them in
  its own way. The double ones differ between the UCRT and glibc in 3% of the calls.
- **What moves the pixels.** The float sine and cosine that glm calls inside `glm::quat(vec3)`, `glm::rotate` and
  `glm::perspective`, not the double `std::` calls of the client. In a pose experiment, replacing only glibc's float
  functions with correctly rounded ones changes 7% of the quaternions, 11% of the slerps and 40% of the bone chains.
  Replacing only the double ones changes none: the difference is gone once the result is stored in a float (0 of
  2,000,000 matrices of `OriginalAngleMatrix`).
- **Correct rounding has one answer.** A correctly rounded function returns the float or the double nearest to the
  exact value. LLVM-libc 23.1.6, CORE-MATH and MPFR 4.2.1 agree bit for bit: on the 2^32 floats for `sinf`, `cosf`,
  `tanf` and `acosf`, on 400,000,000 doubles per function, and on 18 columns of 2,000,000 inputs of the sizes the game
  uses. The client's pose code built on it gives one hash on ten builds: GCC 13 at `-O2` and `-O0`, Clang 18, and MSVC
  19.44 and 19.52 in Release, in Debug and with `/arch:AVX2`.
- **The original.** `main.exe` takes its sine from the x87 `FSIN`, whose result depends on the CPU: between an AMD
  Zen 5 and an Intel, 3% of the 64-bit values change, one double in 300,000 and no float. A correctly rounded sine
  stored in a float equals `FSIN`'s in 2,000,000 of 2,000,000.
- **The libraries.** CORE-MATH does not compile with MSVC (0 of 10 files). The fdlibm family (openlibm) changes its
  bits with the compiler flags. Boost.Math does not give these functions correctly rounded. MPFR takes 500 to 2,700 ns
  per call. LLVM-libc's math is header-only, compiles with GCC, Clang and MSVC, and takes 7 to 29 ns per call at `-O2`
  against glibc's 5 to 19. Unoptimized it is 10 to 40 times slower, and with MSVC `/fp:fast`, or `/fp:contract` with
  `/arch:AVX2`, changes its bits. Its vcpkg port, `llvm[libc]`, builds all of LLVM.

## Decision

- **The contract is the rounding, not the library.** Every function of the client that IEEE 754 does not fix returns
  the float or the double nearest to the exact value. Any correctly rounded implementation gives the same bits, and
  MPFR is the referee.
- **One door.** [`Render/Models/RoundedMath.h`](../../Client/src/Render/Models/RoundedMath.h) (module `engine/Math`)
  gives `RoundedSin`, `RoundedCos`, `RoundedTan` and `RoundedAcos` in float and double, and `RoundedLog10` in double.
  [`RoundedTransforms.h`](../../Client/src/Render/Models/RoundedTransforms.h) gives `RoundedRotate` and
  `RoundedPerspective`, glm's formulas with those functions, and `AngleQuaternion` writes the formula of
  `glm::quat(vec3)` by hand.
- **LLVM-libc behind it.** The 208 headers those functions need are copied unmodified from llvm-project
  `llvmorg-23.1.6` into [`third_party/llvm-libc/`](../../third_party/llvm-libc/README.md) by its `vendor.py`. Only
  `RoundedMath.cpp` includes them. It is its own library, `ClientRoundedMath`, built with `/O2 /fp:precise` or
  `-O2 -ffp-contract=off` in every configuration.
- **One case fixed by hand.** Without FMA instructions, LLVM-libc rounds the sine and the tangent of a double below
  2^-968 one unit in the last place off. `RoundedMath.cpp` returns `x` there, which is the nearest double.
- **No other source.** `Client/src` and `Client/tests` do not call the `std::` functions the C library rounds, or the
  glm functions that call them ([CODING_RULES §10](../CODING_RULES.md#10-pensar-antes-de-agregar)). A function the
  module lacks is added to it, with its headers and its MPFR hash.

## Consequences

- One golden serves Windows and Linux. `LoginToLorencia.tsv` was recorded once more with the new math: only the draw
  hash changed, on 91 of its 139 frames, from frame 11 on.
- The tests that fix it: `RoundedMathTest` hashes 200,000 results per function, over the sizes the game uses and
  every exponent of the type, against MPFR
  ([`Client/tests/vectors/rounded-math/`](../../Client/tests/vectors/rounded-math/README.md));
  `RoundedTransformsTest` and `HalfLifeMathTest.ThePoseGivesTheSameBitsOnEverySystem` fix the bits of a rotation, a
  projection and a pose. The three also run in the MSVC Release job, where the optimizer could change them.
- `Client/tools/architecture/check_rounded_math.py` fails CI on a call by name: `std::sin` and its kind, the same
  names unqualified, and glm's `rotate`, `perspective`, `slerp`, `angleAxis` and trigonometry. It cannot see a
  `glm::quat` built from a variable that holds Euler angles, or a call inside another library; the bit tests and the
  golden on two systems are the proof. Its one exception is the `std::pow` of a volume in `Platform/SdlAudio.cpp`,
  which is heard and not drawn, sent or hashed.
- What is given up: glm's own rotations and projections. A transform that needs trigonometry is written in
  `RoundedTransforms`.
- The copy is 23,000 lines of headers to keep. Moving it to another tag is `vendor.py` and the tests, which must pass
  unchanged: a correctly rounded result does not depend on the version.
- [`OriginalRetainedMath`](0014-perfil-matematico-retenido.md) keeps its own arithmetic. It models values the x87
  retains at 64 bits of mantissa, which is another contract. `labs/` is outside the rule.
- Not measured: ARM and macOS, which CI does not build, and the AMD Ryzen of the machine that runs the original (an
  AMD EPYC of the same Zen 5 family was).
- What would reopen it: a platform where LLVM-libc's headers do not compile, a frame whose time is in these functions,
  or a C library that ships correctly rounded functions (C23 reserves the `cr_` names), which would give the same
  bits without the copy.
