// Probe for the deterministic-math analysis (2026-11-21). See README.md next to it.
#include "domains.h"
#include "impls.h"
#include "x87.h"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cinttypes>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#if PROBE_HAS_MPFR
float MpfrSinToFloat(double x);
long double MpfrSinTo64(double x);
long double MpfrCosTo64(double x);
#endif

#if defined(_MSC_VER) && defined(_M_X64)
#include <math.h>
#endif

namespace
{
// Which compiler and switches built this binary, so a log line says what its numbers belong to.
void PrintBuild()
{
#if defined(_MSC_VER) && !defined(__clang__)
    std::printf("# build: MSVC %d", _MSC_FULL_VER);
#elif defined(__clang__)
    std::printf("# build: Clang %s", __clang_version__);
#else
    std::printf("# build: GCC %s", __VERSION__);
#endif
#if defined(__FMA__) || defined(__AVX2__)
    std::printf(", FMA/AVX2 at compile time");
#endif
#if defined(_DEBUG)
    std::printf(", debug runtime");
#endif
#if defined(_MSC_VER) && defined(_M_X64)
    if (std::getenv("PROBE_NO_FMA3") != nullptr)
    {
        _set_FMA3_enable(0);
        std::printf(", UCRT FMA3 paths disabled");
    }
#endif
    std::printf("\n");
}

std::uint64_t Fnv(const void* data, std::size_t size)
{
    std::uint64_t h = 0xcbf29ce484222325ull;
    const auto* p = static_cast<const unsigned char*>(data);
    for (std::size_t i = 0; i < size; ++i)
    {
        h = (h ^ p[i]) * 0x100000001b3ull;
    }
    return h;
}

std::int64_t Ordered(double v)
{
    const auto bits = std::bit_cast<std::int64_t>(v);
    return bits < 0 ? std::int64_t{INT64_MIN} - bits : bits;
}
std::int64_t Ordered(float v)
{
    const auto bits = std::bit_cast<std::int32_t>(v);
    return bits < 0 ? std::int64_t{INT32_MIN} - bits : std::int64_t{bits};
}

struct Registry
{
    std::vector<const Impl*> impls;
    const Impl* Find(const std::string& name) const
    {
        for (const Impl* impl : impls)
        {
            if (name == impl->name)
            {
                return impl;
            }
        }
        std::fprintf(stderr, "no implementation named %s\n", name.c_str());
        std::exit(2);
    }
};

Registry MakeRegistry()
{
    Registry r;
    r.impls.push_back(ImplLibm());
#if PROBE_HAS_LLVM
    r.impls.push_back(ImplLlvm());
#endif
#if PROBE_HAS_LLVM_MAIN
    r.impls.push_back(ImplLlvmMain());
#endif
#if PROBE_HAS_LLVM_VARIANTS
    r.impls.push_back(ImplLlvmNative());
    r.impls.push_back(ImplLlvmO0());
#endif
#if PROBE_HAS_COREMATH
    r.impls.push_back(ImplCoreMath());
    r.impls.push_back(ImplCoreMathNative());
#endif
#if PROBE_HAS_FDLIBM
    r.impls.push_back(ImplFdlibm());
#endif
#if PROBE_HAS_FDLIBM_FMA
    r.impls.push_back(ImplFdlibmFma());
#endif
#if PROBE_HAS_MPFR
    r.impls.push_back(ImplMpfr());
#endif
#if PROBE_HAS_SLEEF
    r.impls.push_back(ImplSleef());
    r.impls.push_back(ImplSleefDet());
#endif
    return r;
}

// One (function, domain) column of the tables.
struct Column
{
    std::string label;
    bool isFloat;
    std::function<double (*(const Impl&))(double)> pickD;
    std::function<float (*(const Impl&))(float)> pickF;
    std::vector<double> inD;
    std::vector<float> inF;
};

std::vector<Column> MakeColumns(std::size_t n)
{
    std::vector<Column> columns;
    const auto addD = [&columns](const char* label, double (*Impl::* member)(double), std::vector<double> in) {
        Column c;
        c.label = label;
        c.isFloat = false;
        c.pickD = [member](const Impl& impl) { return impl.*member; };
        c.inD = std::move(in);
        columns.push_back(std::move(c));
    };
    const auto addF = [&columns](const char* label, float (*Impl::* member)(float), std::vector<float> in) {
        Column c;
        c.label = label;
        c.isFloat = true;
        c.pickF = [member](const Impl& impl) { return impl.*member; };
        c.inF = std::move(in);
        columns.push_back(std::move(c));
    };
    std::vector<double> halfDegrees = DomainDegrees(n);
    for (double& x : halfDegrees)
    {
        x *= 0.5;
    }
    addD("sin/grados", &Impl::sin, DomainDegrees(n));
    addD("sin/radianes", &Impl::sin, DomainRadians(n));
    addD("sin/reloj", &Impl::sin, DomainClock(n));
    addD("sin/sonda415", &Impl::sin, DomainProbe415Angles());
    addD("cos/grados", &Impl::cos, DomainDegrees(n));
    addD("cos/radianes", &Impl::cos, DomainRadians(n));
    addD("cos/reloj", &Impl::cos, DomainClock(n));
    addD("cos/sonda415", &Impl::cos, DomainProbe415Angles());
    addD("tan/mediogrado", &Impl::tan, std::move(halfDegrees));
    addD("acos/cosenos", &Impl::acos, DomainCosines(n));
    addD("acos/sonda415", &Impl::acos, DomainProbe415Cosines());
    addD("log10/positivos", &Impl::log10, DomainPositive(n));
    addF("sinf/medioangulo", &Impl::sinf, DomainHalfAnglesF(n));
    addF("sinf/radianes", &Impl::sinf, DomainRadiansF(n));
    addF("cosf/medioangulo", &Impl::cosf, DomainHalfAnglesF(n));
    addF("cosf/radianes", &Impl::cosf, DomainRadiansF(n));
    addF("tanf/medioangulo", &Impl::tanf, DomainHalfAnglesF(n));
    addF("acosf/cosenos", &Impl::acosf, DomainCosinesF(n));
    return columns;
}

struct Output
{
    std::vector<double> d;
    std::vector<float> f;
};

Output Evaluate(const Impl& impl, const Column& column)
{
    Output out;
    if (column.isFloat)
    {
        float (*fn)(float) = column.pickF(impl);
        out.f.resize(column.inF.size());
        for (std::size_t i = 0; i < column.inF.size(); ++i)
        {
            out.f[i] = fn(column.inF[i]);
        }
    }
    else
    {
        double (*fn)(double) = column.pickD(impl);
        out.d.resize(column.inD.size());
        for (std::size_t i = 0; i < column.inD.size(); ++i)
        {
            out.d[i] = fn(column.inD[i]);
        }
    }
    return out;
}

std::uint64_t HashOf(const Output& out)
{
    return out.d.empty() ? Fnv(out.f.data(), out.f.size() * sizeof(float)) : Fnv(out.d.data(), out.d.size() * sizeof(double));
}

struct Difference
{
    std::size_t total = 0;
    std::size_t bits = 0;      // results whose bits differ
    std::size_t asFloat = 0;   // doubles whose float store differs
    std::int64_t maxUlp = 0;
};

Difference Compare(const Output& a, const Output& b)
{
    Difference diff;
    if (!a.d.empty())
    {
        diff.total = a.d.size();
        for (std::size_t i = 0; i < a.d.size(); ++i)
        {
            if (std::bit_cast<std::uint64_t>(a.d[i]) != std::bit_cast<std::uint64_t>(b.d[i]))
            {
                if (std::isnan(a.d[i]) && std::isnan(b.d[i]))
                {
                    continue;
                }
                ++diff.bits;
                diff.maxUlp = std::max<std::int64_t>(diff.maxUlp, std::llabs(Ordered(a.d[i]) - Ordered(b.d[i])));
                if (std::bit_cast<std::uint32_t>(static_cast<float>(a.d[i])) != std::bit_cast<std::uint32_t>(static_cast<float>(b.d[i])))
                {
                    ++diff.asFloat;
                }
            }
        }
    }
    else
    {
        diff.total = a.f.size();
        for (std::size_t i = 0; i < a.f.size(); ++i)
        {
            if (std::bit_cast<std::uint32_t>(a.f[i]) != std::bit_cast<std::uint32_t>(b.f[i]))
            {
                if (std::isnan(a.f[i]) && std::isnan(b.f[i]))
                {
                    continue;
                }
                ++diff.bits;
                ++diff.asFloat;
                diff.maxUlp = std::max<std::int64_t>(diff.maxUlp, std::llabs(Ordered(a.f[i]) - Ordered(b.f[i])));
            }
        }
    }
    return diff;
}

int CommandHash(const Registry& registry, std::size_t n, const std::vector<std::string>& names)
{
    const std::vector<Column> columns = MakeColumns(n);
    std::printf("%-18s", "columna");
    for (const std::string& name : names)
    {
        std::printf(" %-16s", name.c_str());
    }
    std::printf("\n");
    for (const Column& column : columns)
    {
        std::printf("%-18s", column.label.c_str());
        for (const std::string& name : names)
        {
            std::printf(" %016" PRIx64, HashOf(Evaluate(*registry.Find(name), column)));
        }
        std::printf("\n");
    }
    return 0;
}

// Compares one implementation's hashes with a file of "column hash" lines (the correctly rounded ones).
int CommandCheck(const Registry& registry, std::size_t n, const std::string& name, const std::string& path)
{
    std::FILE* file = std::fopen(path.c_str(), "r");
    if (file == nullptr)
    {
        std::perror(path.c_str());
        return 1;
    }
    std::vector<std::pair<std::string, std::string>> expected;
    char label[64];
    char hash[64];
    char line[1024];
    while (std::fgets(line, sizeof line, file) != nullptr)
    {
        if (line[0] != '#' && std::sscanf(line, "%63s %63s", label, hash) == 2 && std::string(label) != "columna")
        {
            expected.emplace_back(label, hash);
        }
    }
    std::fclose(file);
    const std::vector<Column> columns = MakeColumns(n);
    int different = 0;
    for (const Column& column : columns)
    {
        char mine[32];
        std::snprintf(mine, sizeof mine, "%016" PRIx64, HashOf(Evaluate(*registry.Find(name), column)));
        std::string want = "(sin dato)";
        for (const auto& [expectedLabel, expectedHash] : expected)
        {
            if (expectedLabel == column.label)
            {
                want = expectedHash;
            }
        }
        const bool same = want == mine;
        different += same ? 0 : 1;
        std::printf("%-18s %s %s %s\n", column.label.c_str(), mine, same ? "==" : "!=", want.c_str());
    }
    std::printf("%s: %d de %zu columnas distintas de %s\n", name.c_str(), different, columns.size(), path.c_str());
    return different == 0 ? 0 : 3;
}

int CommandTable(const Registry& registry, std::size_t n, const std::string& reference, const std::vector<std::string>& names)
{
    const std::vector<Column> columns = MakeColumns(n);
    std::printf("# diferencias contra %s; por columna: bits distintos / distintos al guardar en float / ulp maximo\n", reference.c_str());
    std::printf("%-18s %9s", "columna", "n");
    for (const std::string& name : names)
    {
        std::printf(" | %-26s", name.c_str());
    }
    std::printf("\n");
    for (const Column& column : columns)
    {
        const Output ref = Evaluate(*registry.Find(reference), column);
        std::printf("%-18s %9zu", column.label.c_str(), column.isFloat ? column.inF.size() : column.inD.size());
        for (const std::string& name : names)
        {
            const Difference diff = Compare(Evaluate(*registry.Find(name), column), ref);
            char cell[64];
            std::snprintf(cell, sizeof cell, "%zu / %zu / %lld", diff.bits, diff.asFloat, static_cast<long long>(diff.maxUlp));
            std::printf(" | %-26s", cell);
        }
        std::printf("\n");
        std::fflush(stdout);
    }
    return 0;
}

int CommandDump(const Registry& registry, std::size_t n, const std::string& name, const std::string& directory)
{
    const std::vector<Column> columns = MakeColumns(n);
    const Impl& impl = *registry.Find(name);
    for (std::size_t c = 0; c < columns.size(); ++c)
    {
        const Output out = Evaluate(impl, columns[c]);
        const std::string path = directory + "/" + std::to_string(c) + ".bin";
        std::FILE* file = std::fopen(path.c_str(), "wb");
        if (file == nullptr)
        {
            std::perror(path.c_str());
            return 1;
        }
        if (out.d.empty())
        {
            std::fwrite(out.f.data(), sizeof(float), out.f.size(), file);
        }
        else
        {
            std::fwrite(out.d.data(), sizeof(double), out.d.size(), file);
        }
        std::fclose(file);
    }
    return 0;
}

std::vector<unsigned char> ReadAll(const std::string& path)
{
    std::vector<unsigned char> bytes;
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr)
    {
        std::perror(path.c_str());
        std::exit(1);
    }
    unsigned char buffer[1 << 16];
    std::size_t got;
    while ((got = std::fread(buffer, 1, sizeof buffer, file)) > 0)
    {
        bytes.insert(bytes.end(), buffer, buffer + got);
    }
    std::fclose(file);
    return bytes;
}

int CommandDiff(std::size_t n, const std::string& a, const std::string& b)
{
    const std::vector<Column> columns = MakeColumns(n);
    std::printf("# %s contra %s: bits distintos / distintos al guardar en float / ulp maximo\n", a.c_str(), b.c_str());
    for (std::size_t c = 0; c < columns.size(); ++c)
    {
        const auto bytesA = ReadAll(a + "/" + std::to_string(c) + ".bin");
        const auto bytesB = ReadAll(b + "/" + std::to_string(c) + ".bin");
        Output outA, outB;
        if (columns[c].isFloat)
        {
            outA.f.resize(bytesA.size() / 4);
            outB.f.resize(bytesB.size() / 4);
            std::memcpy(outA.f.data(), bytesA.data(), bytesA.size());
            std::memcpy(outB.f.data(), bytesB.data(), bytesB.size());
        }
        else
        {
            outA.d.resize(bytesA.size() / 8);
            outB.d.resize(bytesB.size() / 8);
            std::memcpy(outA.d.data(), bytesA.data(), bytesA.size());
            std::memcpy(outB.d.data(), bytesB.data(), bytesB.size());
        }
        const Difference diff = Compare(outA, outB);
        std::printf("%-18s %9zu  %zu / %zu / %lld\n", columns[c].label.c_str(), diff.total, diff.bits, diff.asFloat,
                    static_cast<long long>(diff.maxUlp));
    }
    return 0;
}

int CommandTime(const Registry& registry, std::size_t n, const std::vector<std::string>& names)
{
    const std::vector<Column> columns = MakeColumns(n);
    std::printf("# nanosegundos por llamada (mejor de 5 pasadas)\n%-18s", "columna");
    for (const std::string& name : names)
    {
        std::printf(" %12s", name.c_str());
    }
    std::printf("\n");
    for (const Column& column : columns)
    {
        std::printf("%-18s", column.label.c_str());
        for (const std::string& name : names)
        {
            const Impl& impl = *registry.Find(name);
            double best = 1e30;
            const std::size_t count = column.isFloat ? column.inF.size() : column.inD.size();
            for (int pass = 0; pass < 5; ++pass)
            {
                const auto start = std::chrono::steady_clock::now();
                double sink = 0.0;
                if (column.isFloat)
                {
                    float (*fn)(float) = column.pickF(impl);
                    for (const float x : column.inF)
                    {
                        sink += static_cast<double>(fn(x));
                    }
                }
                else
                {
                    double (*fn)(double) = column.pickD(impl);
                    for (const double x : column.inD)
                    {
                        sink += fn(x);
                    }
                }
                const auto stop = std::chrono::steady_clock::now();
                volatile double keep = sink;
                (void)keep;
                best = std::min(best, std::chrono::duration<double, std::nano>(stop - start).count() / static_cast<double>(count));
            }
            std::printf(" %12.1f", best);
        }
        std::printf("\n");
        std::fflush(stdout);
    }
    return 0;
}

// Every float bit pattern through two implementations, split across threads.
int CommandExhaustive(const Registry& registry, const std::string& function, const std::string& a, const std::string& b, unsigned threads)
{
    const auto pick = [&function](const Impl& impl) -> float (*)(float) {
        if (function == "sinf") return impl.sinf;
        if (function == "cosf") return impl.cosf;
        if (function == "tanf") return impl.tanf;
        if (function == "acosf") return impl.acosf;
        std::fprintf(stderr, "unknown function %s\n", function.c_str());
        std::exit(2);
    };
    float (*fa)(float) = pick(*registry.Find(a));
    float (*fb)(float) = pick(*registry.Find(b));
    std::vector<std::uint64_t> different(threads, 0);
    std::vector<std::uint64_t> maxUlp(threads, 0);
    std::vector<std::uint32_t> firstBits(threads, 0);
    std::vector<std::thread> pool;
    for (unsigned t = 0; t < threads; ++t)
    {
        pool.emplace_back([&, t] {
            const std::uint64_t begin = (std::uint64_t{1} << 32) * t / threads;
            const std::uint64_t end = (std::uint64_t{1} << 32) * (t + 1) / threads;
            for (std::uint64_t bits = begin; bits < end; ++bits)
            {
                const float x = std::bit_cast<float>(static_cast<std::uint32_t>(bits));
                const float ra = fa(x);
                const float rb = fb(x);
                if (std::bit_cast<std::uint32_t>(ra) != std::bit_cast<std::uint32_t>(rb) && !(std::isnan(ra) && std::isnan(rb)))
                {
                    if (different[t] == 0)
                    {
                        firstBits[t] = static_cast<std::uint32_t>(bits);
                    }
                    ++different[t];
                    maxUlp[t] = std::max<std::uint64_t>(maxUlp[t], static_cast<std::uint64_t>(std::llabs(Ordered(ra) - Ordered(rb))));
                }
            }
        });
    }
    std::uint64_t total = 0;
    std::uint64_t ulp = 0;
    for (unsigned t = 0; t < threads; ++t)
    {
        pool[t].join();
        total += different[t];
        ulp = std::max(ulp, maxUlp[t]);
    }
    std::uint32_t first = 0;
    for (unsigned t = 0; t < threads; ++t)
    {
        if (different[t] != 0)
        {
            first = firstBits[t];
            break;
        }
    }
    std::printf("%s: %s contra %s sobre los 4294967296 floats: %" PRIu64 " distintos (%.4f %%), ulp maximo %" PRIu64 ", primero 0x%08x\n",
                function.c_str(), a.c_str(), b.c_str(), total, 100.0 * static_cast<double>(total) / 4294967296.0, ulp, first);
    return 0;
}

// Two implementations over many more inputs than the tables, without keeping the results.
int CommandBig(const Registry& registry, const std::string& function, const std::string& a, const std::string& b, std::uint64_t count, unsigned threads)
{
    const auto pick = [&function](const Impl& impl) -> double (*)(double) {
        if (function == "sin") return impl.sin;
        if (function == "cos") return impl.cos;
        if (function == "tan") return impl.tan;
        if (function == "acos") return impl.acos;
        if (function == "log10") return impl.log10;
        std::fprintf(stderr, "unknown function %s\n", function.c_str());
        std::exit(2);
    };
    double (*fa)(double) = pick(*registry.Find(a));
    double (*fb)(double) = pick(*registry.Find(b));
    std::vector<std::uint64_t> different(threads, 0);
    std::vector<std::uint64_t> asFloat(threads, 0);
    std::vector<std::thread> pool;
    for (unsigned t = 0; t < threads; ++t)
    {
        pool.emplace_back([&, t] {
            SplitMix64 rng{0xB16B00B5ull + t};
            for (std::uint64_t i = 0; i < count / threads; ++i)
            {
                double x;
                if (function == "acos")
                {
                    x = static_cast<double>(rng.UnitF() * 2.0f - 1.0f);
                }
                else if (function == "log10")
                {
                    x = rng.Unit() * 1000.0 + 0x1.0p-20;
                }
                else if (i % 3 == 0)
                {
                    x = static_cast<double>(rng.UnitF() * 720.0f - 360.0f) * static_cast<double>(DegreesF);
                }
                else if (i % 3 == 1)
                {
                    x = rng.Unit() * 12.566370614359172 - 6.283185307179586;
                }
                else
                {
                    x = static_cast<double>(static_cast<float>(rng.Next() % (1u << 27))) * static_cast<double>(0.001f);
                }
                const double ra = fa(x);
                const double rb = fb(x);
                if (std::bit_cast<std::uint64_t>(ra) != std::bit_cast<std::uint64_t>(rb))
                {
                    ++different[t];
                    if (std::bit_cast<std::uint32_t>(static_cast<float>(ra)) != std::bit_cast<std::uint32_t>(static_cast<float>(rb)))
                    {
                        ++asFloat[t];
                    }
                }
            }
        });
    }
    std::uint64_t total = 0;
    std::uint64_t totalFloat = 0;
    for (unsigned t = 0; t < threads; ++t)
    {
        pool[t].join();
        total += different[t];
        totalFloat += asFloat[t];
    }
    std::printf("%s: %s contra %s sobre %" PRIu64 " entradas: %" PRIu64 " distintos, %" PRIu64 " al guardar en float\n", function.c_str(),
                a.c_str(), b.c_str(), count / threads * threads, total, totalFloat);
    return 0;
}

#if PROBE_HAS_X87 && PROBE_HAS_MPFR
// How far each implementation is from the FSIN and FCOS of this CPU, at the two stores the original does.
int CommandX87(const Registry& registry, std::size_t n, const std::vector<std::string>& names)
{
    struct Set
    {
        const char* label;
        std::vector<double> xs;
    };
    const std::vector<Set> sets = {{"grados", DomainDegrees(n)}, {"radianes", DomainRadians(n)}, {"reloj", DomainClock(n)}};
    for (int which = 0; which < 2; ++which)
    {
        const char* fn = which == 0 ? "sin" : "cos";
        for (const Set& set : sets)
        {
            std::size_t notRounded64 = 0;
            std::size_t storeDoubleVsCr = 0;
            std::size_t storeFloatVsCr = 0;
            std::vector<std::size_t> asDouble(names.size(), 0);
            std::vector<std::size_t> asFloat(names.size(), 0);
            std::vector<std::size_t> asProduct(names.size(), 0);
            std::vector<double (*)(double)> fns;
            for (const std::string& name : names)
            {
                fns.push_back(which == 0 ? registry.Find(name)->sin : registry.Find(name)->cos);
            }
            const double factor = static_cast<double>(0.3f);
            for (const double x : set.xs)
            {
                const long double real = which == 0 ? X87Sin(x) : X87Cos(x);
                const long double exact64 = which == 0 ? MpfrSinTo64(x) : MpfrCosTo64(x);
                if (real != exact64)
                {
                    ++notRounded64;
                }
                const auto realDouble = static_cast<double>(real);
                const auto realFloat = static_cast<float>(real);
                // FSIN, then FMUL by a float constant in PC53: one rounding of the 64-bit value times the factor.
                const auto realProduct = static_cast<double>(real * static_cast<long double>(factor));
                if (realDouble != static_cast<double>(exact64))
                {
                    ++storeDoubleVsCr;
                }
                if (realFloat != static_cast<float>(exact64))
                {
                    ++storeFloatVsCr;
                }
                for (std::size_t k = 0; k < fns.size(); ++k)
                {
                    const double value = fns[k](x);
                    if (value != realDouble)
                    {
                        ++asDouble[k];
                    }
                    if (static_cast<float>(value) != realFloat)
                    {
                        ++asFloat[k];
                    }
                    if (value * factor != realProduct)
                    {
                        ++asProduct[k];
                    }
                }
            }
            std::printf("%s/%s n=%zu: FSIN distinto del valor exacto redondeado a 64 bits: %zu; su store a double distinto del de ese valor: %zu; "
                        "a float: %zu\n",
                        fn, set.label, set.xs.size(), notRounded64, storeDoubleVsCr, storeFloatVsCr);
            for (std::size_t k = 0; k < names.size(); ++k)
            {
                std::printf("    %-16s distinto de la x87 al guardar en double: %8zu (%.4f %%)   en float: %6zu   tras fmul por 0,3f en PC53: %8zu (%.4f %%)\n",
                            names[k].c_str(), asDouble[k], 100.0 * static_cast<double>(asDouble[k]) / static_cast<double>(set.xs.size()), asFloat[k],
                            asProduct[k], 100.0 * static_cast<double>(asProduct[k]) / static_cast<double>(set.xs.size()));
            }
            std::fflush(stdout);
        }
    }
    return 0;
}

// What this CPU's FSIN and FCOS return, as distances from the exactly rounded value, to compare two CPUs.
struct X87Record
{
    std::int32_t ulps64; // distance in units of the 64-bit significand; INT32_MAX or INT32_MIN when out of range
    std::int8_t ulpsDouble; // distance of the double store from the double store of the exact value
    std::int8_t ulpsFloat;  // the same for the float store
};

std::int32_t Distance80(long double a, long double b)
{
    std::uint64_t ma = 0;
    std::uint64_t mb = 0;
    std::uint16_t ea = 0;
    std::uint16_t eb = 0;
    std::memcpy(&ma, &a, 8);
    std::memcpy(&mb, &b, 8);
    std::memcpy(&ea, reinterpret_cast<const char*>(&a) + 8, 2);
    std::memcpy(&eb, reinterpret_cast<const char*>(&b) + 8, 2);
    if (ea != eb)
    {
        return a > b ? INT32_MAX : INT32_MIN;
    }
    const auto delta = static_cast<std::int64_t>(ma - mb);
    return static_cast<std::int32_t>(std::clamp<std::int64_t>(delta, -(std::int64_t{1} << 30), std::int64_t{1} << 30));
}

std::vector<X87Record> X87Records(std::size_t n)
{
    std::vector<X87Record> records;
    const std::vector<std::vector<double>> sets = {DomainDegrees(n), DomainRadians(n), DomainClock(n)};
    for (int which = 0; which < 2; ++which)
    {
        for (const std::vector<double>& xs : sets)
        {
            for (const double x : xs)
            {
                const long double real = which == 0 ? X87Sin(x) : X87Cos(x);
                const long double exact = which == 0 ? MpfrSinTo64(x) : MpfrCosTo64(x);
                X87Record record{};
                record.ulps64 = Distance80(real, exact);
                record.ulpsDouble = static_cast<std::int8_t>(std::clamp<std::int64_t>(
                    Ordered(static_cast<double>(real)) - Ordered(static_cast<double>(exact)), -127, 127));
                record.ulpsFloat = static_cast<std::int8_t>(
                    std::clamp<std::int64_t>(Ordered(static_cast<float>(real)) - Ordered(static_cast<float>(exact)), -127, 127));
                records.push_back(record);
            }
        }
    }
    return records;
}

int CommandX87Dump(std::size_t n, const std::string& path)
{
    const std::vector<X87Record> records = X87Records(n);
    std::FILE* file = std::fopen(path.c_str(), "wb");
    if (file == nullptr)
    {
        std::perror(path.c_str());
        return 1;
    }
    for (const X87Record& record : records)
    {
        std::fwrite(&record.ulps64, 4, 1, file);
        std::fwrite(&record.ulpsDouble, 1, 1, file);
        std::fwrite(&record.ulpsFloat, 1, 1, file);
    }
    std::fclose(file);
    return 0;
}

int CommandX87Compare(std::size_t n, const std::string& path)
{
    const std::vector<X87Record> mine = X87Records(n);
    const std::vector<unsigned char> bytes = ReadAll(path);
    if (bytes.size() != mine.size() * 6)
    {
        std::fprintf(stderr, "the reference has %zu bytes, expected %zu\n", bytes.size(), mine.size() * 6);
        return 1;
    }
    static const char* const Labels[] = {"sin/grados", "sin/radianes", "sin/reloj", "cos/grados", "cos/radianes", "cos/reloj"};
    std::printf("# la x87 de esta CPU contra la del archivo: de n entradas, cuantas dan otro valor de 64 bits / otro double / otro float\n");
    for (std::size_t column = 0; column < 6; ++column)
    {
        std::size_t different64 = 0;
        std::size_t differentDouble = 0;
        std::size_t differentFloat = 0;
        std::size_t inexactHere = 0;
        std::size_t inexactThere = 0;
        for (std::size_t i = 0; i < n; ++i)
        {
            const std::size_t k = column * n + i;
            std::int32_t ulps64 = 0;
            std::memcpy(&ulps64, &bytes[k * 6], 4);
            const auto ulpsDouble = static_cast<std::int8_t>(bytes[k * 6 + 4]);
            const auto ulpsFloat = static_cast<std::int8_t>(bytes[k * 6 + 5]);
            different64 += ulps64 != mine[k].ulps64;
            differentDouble += ulpsDouble != mine[k].ulpsDouble;
            differentFloat += ulpsFloat != mine[k].ulpsFloat;
            inexactHere += mine[k].ulps64 != 0;
            inexactThere += ulps64 != 0;
        }
        std::printf("%-14s n=%zu  64 bits: %zu  double: %zu  float: %zu   (lejos del exacto: aca %zu, en el archivo %zu)\n", Labels[column], n,
                    different64, differentDouble, differentFloat, inexactHere, inexactThere);
    }
    return 0;
}
#endif
} // namespace

int main(int argc, char** argv)
{
    PrintBuild();
    const Registry registry = MakeRegistry();
    const std::string command = argc > 1 ? argv[1] : "";
    std::vector<std::string> rest;
    for (int i = 3; i < argc; ++i)
    {
        rest.emplace_back(argv[i]);
    }
    const std::size_t n = argc > 2 ? static_cast<std::size_t>(std::strtoull(argv[2], nullptr, 10)) : 0;
    if (command == "list")
    {
        for (const Impl* impl : registry.impls)
        {
            std::printf("%s\n", impl->name);
        }
        return 0;
    }
    if (command == "hash" && !rest.empty())
    {
        return CommandHash(registry, n, rest);
    }
    if (command == "check" && rest.size() == 2)
    {
        return CommandCheck(registry, n, rest[0], rest[1]);
    }
    if (command == "table" && rest.size() >= 2)
    {
        return CommandTable(registry, n, rest[0], {rest.begin() + 1, rest.end()});
    }
    if (command == "dump" && rest.size() == 2)
    {
        return CommandDump(registry, n, rest[0], rest[1]);
    }
    if (command == "diff" && rest.size() == 2)
    {
        return CommandDiff(n, rest[0], rest[1]);
    }
    if (command == "time" && !rest.empty())
    {
        return CommandTime(registry, n, rest);
    }
    if (command == "exhaustive" && rest.size() == 3)
    {
        return CommandExhaustive(registry, rest[0], rest[1], rest[2], static_cast<unsigned>(n));
    }
    if (command == "big" && rest.size() == 4)
    {
        return CommandBig(registry, rest[0], rest[1], rest[2], std::strtoull(rest[3].c_str(), nullptr, 10), static_cast<unsigned>(n));
    }
#if PROBE_HAS_X87 && PROBE_HAS_MPFR
    if (command == "x87" && !rest.empty())
    {
        return CommandX87(registry, n, rest);
    }
    if (command == "x87dump" && rest.size() == 1)
    {
        return CommandX87Dump(n, rest[0]);
    }
    if (command == "x87cmp" && rest.size() == 1)
    {
        return CommandX87Compare(n, rest[0]);
    }
#endif
    std::fprintf(stderr,
                 "usage: probe list | hash N impl... | check N impl file | table N reference impl... | dump N impl dir | diff N dirA dirB |\n"
                 "       time N impl... | exhaustive THREADS fn implA implB | big THREADS fn implA implB COUNT | x87 N impl...\n");
    return 2;
}
