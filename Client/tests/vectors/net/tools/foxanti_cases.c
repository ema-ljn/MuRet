/*
 * foxanti_cases.c: vectores de referencia de la capa de paquetes de FoxAnti.
 *
 * Qué es: un driver que llama a la "FoxAnti mínima" (packet.c y packet.h, los
 * puertos fieles de las funciones de FoxAnti.dll, cada una anotada con su VA)
 * y vuelca en hex la entrada y la salida de cada caso. Sirve para sacar los
 * resultados esperados de los tests del cliente de MuRet (CL2.4) sin
 * reimplementar nada: toda la lógica de paquetes la pone packet.c. Lo único
 * propio de este archivo es armar las entradas y el modelo del "juego"
 * (game_encode), que es el mismo de mu-client/tests/packet/test_packet.c.
 *
 * Dónde está la FoxAnti mínima: en el repo mu-server-repack-research-102i,
 * carpeta client-research/analysis/foxanti/minimal/ (src/packet.c y
 * src/packet.h). Este archivo no la incluye por ruta: la ruta va por la
 * línea de compilación.
 *
 * Cómo compilarlo y correrlo (MIN = la carpeta de la FoxAnti mínima):
 *
 *   gcc -std=c11 -O0 -Wall -Wextra -I"$MIN/src" \
 *       foxanti_cases.c "$MIN/src/packet.c" -o foxanti_cases
 *   ./foxanti_cases > cases.txt
 *
 * Formato de la salida: una línea por caso,
 *
 *   nombre: entrada -> salida (largo)
 *
 * en hex con espacios. "largo" es lo que devuelve la función (con los mocks
 * de send, que devuelven el largo que reciben, es el largo del paquete; en
 * fox_reset_queue es out_len). Las líneas que no son casos (contexto,
 * chequeos, tablas) arrancan con dos espacios o con "#".
 *
 * Convenciones de packet.c que usa este driver (verificadas en el código):
 *   - fox_crypt(a1, buf, mode, k0): a1 es el índice del ÚLTIMO byte, no un
 *     largo. Con k0 = 3 los dos modos tocan buf[2..a1] y la semilla es buf[1].
 *   - El juego cifra un C1 con XOR32 así: fox_crypt(len - 2, pkt + 1,
 *     FOX_CRYPT_ENCODE, 3), o sea pkt[3..len-1] con semilla pkt[2] (opcode).
 *   - fox_send_forward(main_send, a, pkt, n) con a == NULL descifra y vuelve
 *     a cifrar pkt[2..n] con fox_crypt(n, pkt, ..., 3): el buffer mide n+1
 *     y pkt[n] es el byte de relleno. pkt[0] es el serial (no entra al
 *     cifrado) y pkt[1] es el opcode (semilla, y es lo que se remapea).
 *   - fox_decode_inbound sobre una lista C2 F4 02 la reescribe con un byte
 *     más (devuelve len + 1): el buffer tiene que medir len + 1.
 *   - fox_reset_queue escribe una cola de 4000 bytes (memset completo).
 *
 * De dónde sale cada caso:
 *   A  fox_encode_outbound (VA 0x341540, el hook del send del IAT):
 *      A1  la rama "C1 06 F4 03" (selección de servidor): un código que cae
 *          en un escalón (0x190) y uno que no cae en ninguno (0).
 *      A2  los bordes de los 8 escalones de la escalera de tamaño, más un
 *          código por debajo y uno por encima. Es la misma tabla que
 *          test_encode_outbound_ladder de mu-client/tests/packet/test_packet.c.
 *      A3  la ida y vuelta lista -> selección (pregunta Q-C17): para cada
 *          código w de 0 a 110, la lista "C2 00 0A F4 02 01 <w> 00 CC" pasa
 *          por fox_decode_inbound, el código que queda en la lista
 *          (out[7..8]) se manda en "C1 06 F4 03 <código>" por
 *          fox_encode_outbound, y se anota qué llega al cable.
 *      A4  los remapeos de opcode D3 -> A3 y D7 -> A4 (y 20 como control,
 *          sin remapeo) sobre C1 de largo 3, 4, 8 y 40, con los bytes
 *          pkt[i] = i*7+3 (mod 256) para i = 3..L-1: forma plana, con el
 *          XOR32 del juego y al cable.
 *      A5  un C1 corto que no es la rama mágica ni se remapea: C1 04 F4 02.
 *      A6  los dos C1 con D3 de test_net_foxanti.c (mu-client), que pasan por
 *          fox_encode_outbound tal cual, sin el XOR32 del juego.
 *   B  fox_send_forward (VA 0x3416A0, el slot de send de la tabla de
 *      interfaz) con a == NULL, para los opcodes 10 (-> 1D), 19 (-> A5),
 *      1E (-> A6) y los controles 1C y F1. El "sealed" es lo que después va a
 *      cifrarse en C3/C4: [serial][opcode][bytes...][relleno].
 *      (i)   serial 0, sin bytes después del opcode (n = 2), relleno A5.
 *      (ii)  serial 0, un byte 00 (n = 3, la forma de C1 04 <op> 00),
 *            relleno 00 y relleno A5.
 *      (iii) serial 7: el C1 plano de 13 bytes "C1 0D <op> <i*13+5 para
 *            i = 0..9>" con el XOR32 del juego; el sealed es pkt[1..12] de
 *            ese C1 con pkt[1] reemplazado por el serial (n = 12), relleno 5C.
 *      (iv)  solo para 1E: el C1 plano "C1 05 1E 29 00" con el XOR32 del
 *            juego, serial 3, relleno A5 (n = 4).
 *   C  fox_decode_inbound (VA 0x3414D0) y, adentro, fox_parse_f4_02
 *      (VA 0x341170):
 *      C1  la lista del cable con una entrada de código 0.
 *      C2  una lista de 3 entradas (códigos 9, 10 y 21; cargas 11, 22, 33;
 *          cuarto byte EE).
 *      C3  una lista con cantidad 0.
 *      C4  una lista con 2 bytes de más detrás de su única entrada.
 *      C5  códigos 97, 98 y FFFF (carga 44, cuarto byte EE): el último
 *          escalón y dos que caen fuera.
 *      C6  las exenciones del XOR AA (C1+00, C1+F4, C2+F4 que no es lista)
 *          y un paquete que sí se desenmascara. Se omite "C2 00 05 F4 02"
 *          porque lee pkt[5] fuera del paquete (count sin validar).
 *   D  fox_reset_queue (VA 0x3412C0): el reencolado de F3 11 (la muestra
 *      viva de s19 "C1 08 F3 11 01 00 2C 04" y otras), con la cola de 4000.
 *      El de 20 bytes lleva los 15 bytes pkt[5 + i] = i*11+1 para i = 0..14.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "packet.h"

/* Tamaño de los buffers de trabajo: alcanza para el paquete más largo (40)
 * más el byte que agregan fox_send_forward y fox_parse_f4_02. */
#define BUF_SIZE 64

/* ------------------------------------------------------------------ */
/* Impresión                                                          */
/* ------------------------------------------------------------------ */

static void put_hex(const uint8_t *p, int n)
{
    for (int i = 0; i < n; i++)
        printf(i == 0 ? "%02X" : " %02X", p[i]);
}

/* Una línea "nombre: entrada -> salida (largo)". */
static void print_case(const char *name, const uint8_t *in, int in_len,
                       const uint8_t *out, int out_len, int ret)
{
    printf("%s: ", name);
    put_hex(in, in_len);
    printf(" -> ");
    put_hex(out, out_len);
    printf(" (%d)\n", ret);
}

/* Una línea de contexto (no es un caso): "  etiqueta: bytes". */
static void print_note(const char *label, const uint8_t *p, int n)
{
    printf("  %s: ", label);
    put_hex(p, n);
    printf("\n");
}

/* ------------------------------------------------------------------ */
/* Mocks de los extremos (no tocan el paquete, devuelven el largo)    */
/* ------------------------------------------------------------------ */

/* El send real de ws2_32 que recibe fox_encode_outbound. */
static int FOX_CALL mock_send(uintptr_t sock, uint8_t *pkt, int len, int flags)
{
    (void)sock;
    (void)pkt;
    (void)flags;
    return len;
}

/* El trampolín de send de main.exe que recibe fox_send_forward. */
static int FOX_CALL mock_main_send(void *a, uint8_t *pkt, int n)
{
    (void)a;
    (void)pkt;
    return n;
}

/* ------------------------------------------------------------------ */
/* Modelo del juego                                                   */
/* ------------------------------------------------------------------ */

/* El XOR32 que main.exe aplica a un C1 antes del send: pkt[3..len-1] con
 * semilla pkt[2]. Igual que game_encode de mu-client/tests/packet. */
static void game_encode(uint8_t *pkt, int len)
{
    fox_crypt(len - 2, pkt + 1, FOX_CRYPT_ENCODE, 3);
}

/* El opcode que esperamos después del remapeo de fox_encode_outbound
 * (packet.h: D3 -> A3, D7 -> A4). Solo se usa para el chequeo de A4. */
static uint8_t outbound_remap(uint8_t op)
{
    if (op == 0xD3)
        return 0xA3;
    if (op == 0xD7)
        return 0xA4;
    return op;
}

/* ------------------------------------------------------------------ */
/* Corredores de cada función                                         */
/* ------------------------------------------------------------------ */

/* fox_encode_outbound sobre una copia de `in`; deja el resultado en `out`. */
static int run_outbound(const uint8_t *in, int len, uint8_t *out)
{
    memcpy(out, in, (size_t)len);
    return fox_encode_outbound(mock_send, 0, out, len, 0);
}

static void case_outbound(const char *name, const uint8_t *in, int len)
{
    uint8_t out[BUF_SIZE];
    int ret = run_outbound(in, len, out);
    print_case(name, in, len, out, len, ret);
}

/* fox_send_forward con a == NULL: `sealed` mide n+1 (relleno en [n]).
 * Imprime los n+1 bytes antes y después, y chequea el patrón que se ve en
 * el código: decode y encode con la misma semilla se cancelan, así que si
 * el opcode cambia, todo pkt[2..n] queda XOR (op viejo ^ op nuevo). */
static void case_forward(const char *name, const uint8_t *sealed, int n)
{
    uint8_t buf[BUF_SIZE];
    memcpy(buf, sealed, (size_t)n + 1);
    int ret = fox_send_forward(mock_main_send, NULL, buf, n);
    print_case(name, sealed, n + 1, buf, n + 1, ret);

    uint8_t delta = (uint8_t)(sealed[1] ^ buf[1]);
    int ok = (buf[0] == sealed[0]);
    for (int i = 2; i <= n; i++)
        ok = ok && (buf[i] == (uint8_t)(sealed[i] ^ delta));
    printf("  chequeo: pkt[0] igual y pkt[2..%d] = antes ^ %02X: %s\n", n, delta,
           ok ? "si" : "NO");
}

/* fox_decode_inbound sobre una copia en un buffer con lugar para len+1. */
static int run_inbound(const uint8_t *in, int len, uint8_t *out)
{
    memset(out, 0, BUF_SIZE);
    memcpy(out, in, (size_t)len);
    return fox_decode_inbound(len, out);
}

static void case_inbound(const char *name, const uint8_t *in, int len)
{
    uint8_t out[BUF_SIZE];
    int ret = run_inbound(in, len, out);
    print_case(name, in, len, out, ret, ret);
}

/* La cola fija del original (0x34C5C0, 4000 bytes). */
static uint8_t g_queue[4000];

static void case_reset(const char *name, const uint8_t *in, int len)
{
    uint8_t buf[BUF_SIZE];
    uint8_t *p = buf;
    uint32_t out_len = 0;

    memcpy(buf, in, (size_t)len);
    fox_reset_queue(g_queue, &p, len, &out_len);
    print_case(name, in, len, p, (int)out_len, (int)out_len);
    if (p != g_queue)
        printf("  OJO: el puntero no quedó apuntando a la cola\n");
}

/* ------------------------------------------------------------------ */
/* A: fox_encode_outbound                                             */
/* ------------------------------------------------------------------ */

/* Arma "C1 06 F4 03 <código LE>". */
static void make_select(uint8_t *pkt, uint16_t code)
{
    static const uint8_t head[4] = { 0xC1, 0x06, 0xF4, 0x03 };
    memcpy(pkt, head, sizeof(head));
    fox_wr16(pkt + 4, code);
}

static void cases_a1(void)
{
    static const uint8_t a1_1[] = { 0xC1, 0x06, 0xF4, 0x03, 0x90, 0x01 };
    static const uint8_t a1_2[] = { 0xC1, 0x06, 0xF4, 0x03, 0x00, 0x00 };

    printf("# A1: fox_encode_outbound, rama C1 06 F4 03\n");
    case_outbound("A1.190", a1_1, (int)sizeof(a1_1));
    case_outbound("A1.000", a1_2, (int)sizeof(a1_2));
}

static void cases_a2(void)
{
    static const uint16_t codes[] = {
        0x103, 0x104, 0x117, 0x118, 0x12B, 0x12C, 0x13F, 0x140, 0x153,
        0x154, 0x167, 0x168, 0x17B, 0x17C, 0x18F, 0x190, 0x1A3, 0x1A4
    };

    printf("\n# A2: fox_encode_outbound, escalera de C1 06 F4 03\n");
    for (size_t i = 0; i < sizeof(codes) / sizeof(codes[0]); i++) {
        uint8_t in[6], out[6];
        char name[32];
        make_select(in, codes[i]);
        int ret = run_outbound(in, 6, out);
        snprintf(name, sizeof(name), "A2.%03X", codes[i]);
        print_case(name, in, 6, out, 6, ret);
        printf("  código: 0x%03X -> 0x%03X\n", codes[i], fox_rd16(out + 4));
    }
}

#define A3_MAX_W 110

static void cases_a3(void)
{
    uint16_t list_code[A3_MAX_W + 1];
    uint16_t wire_code[A3_MAX_W + 1];

    printf("\n# A3: ida y vuelta (Q-C17): w -> lista (fox_decode_inbound, out[7..8])"
           " -> cable (fox_encode_outbound de C1 06 F4 03)\n");
    for (int w = 0; w <= A3_MAX_W; w++) {
        uint8_t list[11] = { 0xC2, 0x00, 0x0A, 0xF4, 0x02, 0x01, 0, 0, 0x00, 0xCC, 0 };
        uint8_t sel[6];

        fox_wr16(list + 6, (uint16_t)w);
        fox_decode_inbound(10, list);
        list_code[w] = fox_rd16(list + 7);

        make_select(sel, list_code[w]);
        fox_encode_outbound(mock_send, 0, sel, 6, 0);
        wire_code[w] = fox_rd16(sel + 4);

        printf("A3 w=%3d (0x%04X): lista 0x%04X -> cable 0x%04X (%3d)%s\n", w, w,
               list_code[w], wire_code[w], wire_code[w],
               wire_code[w] == w ? "" : "  distinto de w");
    }

    /* Resumen por rangos: w consecutivos con la misma relación
     * (lista - w, lista - cable); el slot en cero va aparte. */
    printf("\n# A3 resumen por rangos de w\n");
    int start = 0;
    for (int w = 1; w <= A3_MAX_W + 1; w++) {
        int same = 0;
        if (w <= A3_MAX_W) {
            int zero_prev = (list_code[w - 1] == 0);
            int zero_cur = (list_code[w] == 0);
            if (zero_prev && zero_cur) {
                same = (wire_code[w] == wire_code[w - 1]);
            } else if (!zero_prev && !zero_cur) {
                same = ((uint16_t)(list_code[w] - w) == (uint16_t)(list_code[w - 1] - (w - 1))) &&
                       ((uint16_t)(list_code[w] - wire_code[w]) ==
                        (uint16_t)(list_code[w - 1] - wire_code[w - 1]));
            }
        }
        if (same)
            continue;

        int end = w - 1;
        printf("A3 w=%d..%d: lista 0x%04X..0x%04X, cable 0x%04X..0x%04X", start, end,
               list_code[start], list_code[end], wire_code[start], wire_code[end]);
        if (list_code[start] == 0)
            printf(" (slot de la lista en cero)\n");
        else if (wire_code[start] == start)
            printf(" (lista = w + 0x%03X, cable = w: ida y vuelta OK)\n",
                   (uint16_t)(list_code[start] - start));
        else
            printf(" (lista = w + 0x%03X, cable = w + %d: NO vuelve)\n",
                   (uint16_t)(list_code[start] - start), wire_code[start] - start);
        start = w;
    }
}

static void cases_a4(void)
{
    static const uint8_t ops[] = { 0xD3, 0xD7, 0x20 };
    static const int lens[] = { 3, 4, 8, 40 };

    printf("\n# A4: fox_encode_outbound, remapeos D3 -> A3 y D7 -> A4 (20 sin remapeo)\n");
    printf("# entrada = el C1 con XOR32 del juego; plano = antes del XOR32\n");
    for (size_t o = 0; o < sizeof(ops); o++) {
        for (size_t l = 0; l < sizeof(lens) / sizeof(lens[0]); l++) {
            int len = lens[l];
            uint8_t plain[BUF_SIZE], xored[BUF_SIZE], wire[BUF_SIZE], ref[BUF_SIZE];
            char name[32];

            plain[0] = 0xC1;
            plain[1] = (uint8_t)len;
            plain[2] = ops[o];
            for (int i = 3; i < len; i++)
                plain[i] = (uint8_t)(i * 7 + 3);

            memcpy(xored, plain, (size_t)len);
            game_encode(xored, len);
            int ret = run_outbound(xored, len, wire);

            snprintf(name, sizeof(name), "A4.%02X.L%d", ops[o], len);
            print_note("plano", plain, len);
            print_case(name, xored, len, wire, len, ret);

            /* El cable tendría que ser el XOR32 del plano con el opcode ya
             * remapeado (lo mismo que chequea test_packet.c). */
            memcpy(ref, plain, (size_t)len);
            ref[2] = outbound_remap(ref[2]);
            game_encode(ref, len);
            printf("  chequeo: cable == XOR32(plano con opcode %02X): %s\n", ref[2],
                   memcmp(ref, wire, (size_t)len) == 0 ? "si" : "NO");
        }
    }
}

static void cases_a5(void)
{
    static const uint8_t a5[] = { 0xC1, 0x04, 0xF4, 0x02 };

    printf("\n# A5: fox_encode_outbound, C1 corto sin remapeo\n");
    case_outbound("A5", a5, (int)sizeof(a5));
}

static void cases_a6(void)
{
    static const uint8_t c1[] = { 0xC1, 0x04, 0xD3, 0x00 };
    static const uint8_t partial[] = { 0xC1, 0x0A, 0xD3, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07 };

    printf("\n# A6: fox_encode_outbound, los C1 de test_net_foxanti.c sin XOR32\n");
    case_outbound("A6.net.c1", c1, (int)sizeof(c1));
    case_outbound("A6.net.partial", partial, (int)sizeof(partial));
}

/* ------------------------------------------------------------------ */
/* B: fox_send_forward con a == NULL                                  */
/* ------------------------------------------------------------------ */

/* Arma el sealed de un C1 (con XOR32 del juego ya aplicado): pkt[1..L-1]
 * del C1 con pkt[1] reemplazado por el serial, y el relleno detrás.
 * Devuelve n = L - 1 (el índice del relleno). */
static int make_sealed(uint8_t *sealed, const uint8_t *c1, int len, uint8_t serial,
                       uint8_t pad)
{
    memcpy(sealed, c1 + 1, (size_t)len - 1);
    sealed[0] = serial;
    sealed[len - 1] = pad;
    return len - 1;
}

static void cases_b(void)
{
    static const uint8_t ops[] = { 0x10, 0x19, 0x1E, 0x1C, 0xF1 };

    printf("\n# B: fox_send_forward(mock, NULL, sealed, n); sealed = [serial][op][bytes][relleno],"
           " se imprimen los n+1 bytes\n");
    for (size_t o = 0; o < sizeof(ops); o++) {
        uint8_t op = ops[o];
        char name[32];

        /* (i) serial 0, solo el opcode, n = 2, relleno A5. */
        {
            uint8_t s[3] = { 0x00, op, 0xA5 };
            snprintf(name, sizeof(name), "B.%02X.i", op);
            case_forward(name, s, 2);
        }

        /* (ii) serial 0, opcode, 00, n = 3, relleno 00 y A5. */
        {
            uint8_t s[4] = { 0x00, op, 0x00, 0x00 };
            snprintf(name, sizeof(name), "B.%02X.ii.pad00", op);
            case_forward(name, s, 3);
            s[3] = 0xA5;
            snprintf(name, sizeof(name), "B.%02X.ii.padA5", op);
            case_forward(name, s, 3);
        }

        /* (iii) serial 7, el C1 plano de 13 bytes con XOR32, relleno 5C. */
        {
            uint8_t c1[13], s[13];
            c1[0] = 0xC1;
            c1[1] = 0x0D;
            c1[2] = op;
            for (int i = 0; i < 10; i++)
                c1[3 + i] = (uint8_t)(i * 13 + 5);
            print_note("C1 plano", c1, 13);
            game_encode(c1, 13);
            print_note("C1 xor32", c1, 13);
            int n = make_sealed(s, c1, 13, 0x07, 0x5C);
            snprintf(name, sizeof(name), "B.%02X.iii", op);
            case_forward(name, s, n);
        }

        /* (iv) solo 1E: C1 05 1E 29 00 con XOR32, serial 3, relleno A5. */
        if (op == 0x1E) {
            uint8_t c1[5] = { 0xC1, 0x05, 0x1E, 0x29, 0x00 };
            uint8_t s[5];
            print_note("C1 plano", c1, 5);
            game_encode(c1, 5);
            print_note("C1 xor32", c1, 5);
            int n = make_sealed(s, c1, 5, 0x03, 0xA5);
            snprintf(name, sizeof(name), "B.%02X.iv", op);
            case_forward(name, s, n);
        }
    }
}

/* ------------------------------------------------------------------ */
/* C: fox_decode_inbound                                              */
/* ------------------------------------------------------------------ */

static void cases_c(void)
{
    static const uint8_t c1[] = { 0xC2, 0x00, 0x0A, 0xF4, 0x02, 0x01, 0x00, 0x00, 0x00, 0xCC };
    static const uint8_t c2[] = { 0xC2, 0x00, 0x12, 0xF4, 0x02, 0x03,
                                  0x09, 0x00, 0x11, 0xEE,
                                  0x0A, 0x00, 0x22, 0xEE,
                                  0x15, 0x00, 0x33, 0xEE };
    static const uint8_t c3[] = { 0xC2, 0x00, 0x06, 0xF4, 0x02, 0x00 };
    static const uint8_t c4[] = { 0xC2, 0x00, 0x0C, 0xF4, 0x02, 0x01,
                                  0x05, 0x00, 0x07, 0xCC, 0x99, 0x88 };
    static const uint8_t c5[] = { 0xC2, 0x00, 0x12, 0xF4, 0x02, 0x03,
                                  0x61, 0x00, 0x44, 0xEE,
                                  0x62, 0x00, 0x44, 0xEE,
                                  0xFF, 0xFF, 0x44, 0xEE };
    static const uint8_t c6a[] = { 0xC1, 0x04, 0x00, 0x01 };
    static const uint8_t c6b[] = { 0xC1, 0x04, 0xF4, 0x05 };
    static const uint8_t c6c[] = { 0xC2, 0x00, 0x06, 0xF4, 0x03, 0x07 };
    static const uint8_t c6d[] = { 0x6B, 0x04, 0xE8, 0xAB };
    /* C1 04 42 01 enmascarado entero (el byte de tamaño también). */
    static const uint8_t c6e[] = { 0x6B, 0xAE, 0xE8, 0xAB };

    printf("\n# C: fox_decode_inbound (la salida mide lo que devuelve)\n");
    case_inbound("C1", c1, (int)sizeof(c1));
    case_inbound("C2", c2, (int)sizeof(c2));
    case_inbound("C3", c3, (int)sizeof(c3));
    case_inbound("C4", c4, (int)sizeof(c4));
    case_inbound("C5", c5, (int)sizeof(c5));
    case_inbound("C6.C1-00", c6a, (int)sizeof(c6a));
    case_inbound("C6.C1-F4", c6b, (int)sizeof(c6b));
    case_inbound("C6.C2-F4-03", c6c, (int)sizeof(c6c));
    case_inbound("C6.mask", c6d, (int)sizeof(c6d));
    case_inbound("C6.mask-full", c6e, (int)sizeof(c6e));
}

/* ------------------------------------------------------------------ */
/* D: fox_reset_queue                                                 */
/* ------------------------------------------------------------------ */

static void cases_d(void)
{
    static const uint8_t d1[] = { 0xC1, 0x08, 0xF3, 0x11, 0xFE, 0x04, 0x16, 0x68 };
    static const uint8_t d2[] = { 0xC1, 0x08, 0xF3, 0x11, 0x01, 0x00, 0x2C, 0x04 };
    static const uint8_t d3[] = { 0xC1, 0x05, 0xF3, 0x11, 0x07 };
    uint8_t d4[20] = { 0xC1, 0x14, 0xF3, 0x11, 0x05 };

    for (int i = 0; i < 15; i++)
        d4[5 + i] = (uint8_t)(i * 11 + 1);

    printf("\n# D: fox_reset_queue (la salida son los out_len bytes de la cola)\n");
    case_reset("D1", d1, (int)sizeof(d1));
    case_reset("D2", d2, (int)sizeof(d2));
    case_reset("D3", d3, (int)sizeof(d3));
    case_reset("D4", d4, (int)sizeof(d4));
}

int main(void)
{
    cases_a1();
    cases_a2();
    cases_a3();
    cases_a4();
    cases_a5();
    cases_a6();
    cases_b();
    cases_c();
    cases_d();
    return 0;
}
