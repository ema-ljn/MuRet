# Rounded math reference

`reference.cpp` prints the values that `render/test_rounded_math.cpp` expects. It computes them with
[MPFR](https://www.mpfr.org/): each function over the same inputs, the result rounded to nearest at 24 bits (float)
or 53 bits (double). MPFR is not the library the client uses (LLVM-libc, `third_party/llvm-libc`), so the test
checks the contract, the float or double nearest to the exact value, and not one library against itself.

```sh
sudo apt-get install libmpfr-dev
g++ -std=c++20 -O2 reference.cpp -lmpfr -lgmp -o reference && ./reference
```

With MPFR 4.2.1:

```text
sinf(1)   3f576aa4
cosf(1)   3f0a5140
tanf(1)   3fc75923
acosf(.5) 3f860a92
sin(1)    3feaed548f090cee
cos(1)    3fe14a280fb5068c
tan(1)    3ff8eb245cbee3a6
acos(.5)  3ff0c152382d7366
log10(2)  3fd34413509f79ff
sinf   c1d51bb67356e490
cosf   b461a2e1923ed20f
tanf   ff5b9223181d53c9
acosf  8cbd2f0f4d5ae299
sin    437149241e2d9310
cos    e0c4241d235ab65e
tan    32c3200dc8d22a17
acos   70b6b290d450f67d
log10  7e8a47e44860acf3
```

The last nine are the FNV-1a hash of 200,000 results each: 100,000 inputs with the sizes the game uses and 100,000
over every exponent of the type.
