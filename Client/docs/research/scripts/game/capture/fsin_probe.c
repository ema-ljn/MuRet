#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Prueba propia del cálculo de balanceo: no carga ni ejecuta main.exe. */
_Static_assert(sizeof(float) == sizeof(uint32_t), "Se requiere float de 32 bits");

enum
{
    PrecisionMask = 0x0300,
    RoundingMask = 0x0c00,
    Precision53 = 0x0200,
    Precision64 = 0x0300
};

static uint16_t ReadControlWord(void)
{
    uint16_t controlWord;
    __asm__ __volatile__("fnstcw %0" : "=m"(controlWord));
    return controlWord;
}

static void WriteControlWord(uint16_t controlWord)
{
    __asm__ __volatile__("fldcw %0" : : "m"(controlWord));
}

static float FloatFromBits(uint32_t bits)
{
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static uint32_t FloatBits(float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static uint32_t RockingY(uint32_t clockBits)
{
    const float clock = FloatFromBits(clockBits);
    const float rate = FloatFromBits(UINT32_C(0x3a51b717));
    const float factor = 2.5f;
    const float zero = 0.0f;
    float result;
    /* La secuencia queda entera en x87, sin sin() ni operaciones SSE intermedias. */
    __asm__ __volatile__("flds %[clock]\n\t"
                         "fmuls %[rate]\n\t"
                         "fsin\n\t"
                         "fmuls %[factor]\n\t"
                         "fadds %[zero]\n\t"
                         "fstps %[result]"
                         : [result] "=m"(result)
                         : [clock] "m"(clock), [rate] "m"(rate), [factor] "m"(factor), [zero] "m"(zero)
                         : "st");
    return FloatBits(result);
}

static void RunPrecision(uint16_t originalControlWord, unsigned int precision, uint16_t precisionBits)
{
    static const uint32_t clocks[] = {UINT32_C(0x4f0d7740), UINT32_C(0x4f2f0344), UINT32_C(0x4f5ac415)};
    static const uint32_t oracle[] = {UINT32_C(0x393da1cd), UINT32_C(0x33b96bb5), UINT32_C(0x33e7c6a2)};
    const uint16_t controlWord =
        (uint16_t)((originalControlWord & (uint16_t) ~(PrecisionMask | RoundingMask)) | precisionBits);
    unsigned int index;
    WriteControlWord(controlWord);
    for (index = 0; index < sizeof(clocks) / sizeof(clocks[0]); ++index)
    {
        const uint32_t result = RockingY(clocks[index]);
        printf("%u %04x %08lx %08lx %08lx %u\n", precision, (unsigned int)ReadControlWord(),
               (unsigned long)clocks[index], (unsigned long)oracle[index], (unsigned long)result,
               result == oracle[index] ? 1u : 0u);
    }
}

int main(int argc, char* argv[])
{
    const uint16_t originalControlWord = ReadControlWord();
    uint16_t restoredControlWord;
    if (argc != 1)
    {
        if (argc != 3 || strcmp(argv[1], "--output") != 0)
            return 2;
        if (freopen(argv[2], "w", stdout) == NULL)
            return 3;
    }
    printf("0 %04x\n", (unsigned int)originalControlWord);
    RunPrecision(originalControlWord, 53u, Precision53);
    RunPrecision(originalControlWord, 64u, Precision64);
    WriteControlWord(originalControlWord);
    restoredControlWord = ReadControlWord();
    printf("1 %04x\n", (unsigned int)restoredControlWord);
    /* La CW ya está restaurada aunque falle una escritura o el vaciado de stdout. */
    if (fflush(stdout) != 0 || ferror(stdout))
        return 4;
    /* Una discrepancia numérica con Unicorn no es un error de esta herramienta. */
    return restoredControlWord == originalControlWord ? 0 : 1;
}
