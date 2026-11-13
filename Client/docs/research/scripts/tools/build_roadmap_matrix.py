#!/usr/bin/env python3
"""CL.M: arma la matriz que asigna cada función de main.exe a una ola del roadmap del cliente.

Lee el inventario fijo del decompilado (5.648 funciones internas), el grafo de llamadas de Ghidra, el
PE c32bbd5f, los exports .c/.asm y el estado de MuRet en un commit (las citas de Client/src con la ola
de su commit por git blame, los cortes, los tests, los docs y el TSV del barrido de la CL8). Escribe
Client/docs/research/coverage/mainexe-roadmap.tsv: las columnas del inventario sin tocar y nueve más
(clasificación, ola, consumidoras, propósito, evidencia y estados). El método está en
Client/docs/research/10-cobertura-roadmap.md; el verificador, en check_roadmap_coverage.py.

Uso (rutas como en scripts/README.md, El entorno):
    python3 -I Client/docs/research/scripts/tools/build_roadmap_matrix.py \\
        --research "$RESEARCH" --repo . --ref origin/main \\
        --out Client/docs/research/coverage/mainexe-roadmap.tsv --summary /tmp/resumen.json

Solo biblioteca estándar. No ejecuta el PE: todo es lectura estática.
"""
import argparse
import bisect
import collections
import gzip
import hashlib
import json
import os
import re
import struct
import subprocess

INV_SHA256 = '42cc47579259c0184e3e921e93ac7817176093a31bb7fb07f2d77734f073194c'
EXE_SHA256 = 'c32bbd5f16ca400c7cfc71fa5f4cd573c57f146dd19a6da52fdd1007abaca26a'
BASE = 0x400000
TEXT_END = 0x6B8000
CRT_START = 0x69EFF0
LIBJPEG = (0x66B8F0, 0x67F0C0)
WZLIB = (0x67F530, 0x69EFF0)
DECOMP = 'client-research/mainexe-vmxp-c32bbd5f/decompiled'
GRAPH = 'client-research/analysis/mainexe/evidence/coverage/grafo-2f16a9c6'
RESEARCH_DOCS = 'client-research/analysis/mainexe'
WAVES = ['CL%d' % i for i in range(12)]
NEW_COLUMNS = ['classification', 'primary_wave', 'consumer_waves', 'purpose', 'evidence',
               'analysis_state', 'implementation_state', 'validation', 'next_action']

# El std::basic_string<char> de la STL de MSVC6 (refcount en ptr[-1], _Xlen/_Xran del CRT): MuRet
# usa std::string. Leídos en el .c de cada uno (10-cobertura-roadmap.md, Bibliotecas).
STL_STRING = {0x4017A0: '_Tidy', 0x4017F0: 'erase', 0x401910: '_Eos', 0x401930: 'assign',
              0x401A60: '_Copy', 0x4292E0: 'assign(const char*, n)', 0x4532F0: 'assign(str, pos, n)',
              0x453C20: 'max_size', 0x453C30: '_Split',
              # Segundas instancias, de la revisión independiente (10-cobertura-roadmap.md, La revisión).
              0x680FF0: 'append(const char*, n) (otra instancia)', 0x683780: 'append(n, c) (otra instancia)',
              0x66B830: 'compare(pos, n, s, m) (otra instancia)'}
# Otras instancias de la STL de MSVC6 (list, deque, vector y un segundo string), leídas en su .c: nodos
# centinela de 0xc bytes, bloques de deque, refcount en ptr[-1]. Sin llamadas directas en su mayoría.
STL_OTHER = {0x429410: 'deque: _Tidy', 0x437700: 'deque: _Tidy', 0x449890: 'list: clear', 0x448520: 'list: _Freenode',
             0x4483b0: 'list: destructor', 0x456990: 'list: constructor (centinela de 0xc)',
             0x456c00: 'list: push_back',
             0x456e10: 'list: _Buynode', 0x456da0: 'list: clear', 0x457c70: 'list: clear', 0x6827b0: 'list: _Buynode',
             0x67f570: 'string: _Tidy (otra instancia)', 0x471670: 'vector<string>: _Destroy',
             0x680f60: 'vector: _Tidy', 0x680fb0: 'vector: _Tidy',
             0x4879A0: 'copy de un rango de strings (asigna uno por uno con _Split y memmove)'}
# Lo que ninguna regla alcanza (casi todo código muerto): decidido leyendo su .c y su .asm el
# 2026-11-13. (ola o 'excluded', qué hace y por qué va ahí).
_HOOK = 'MessageBox centrado con un hook WH_CBT (SetWindowsHookExA(5), GetDesktopWindow, SetWindowPos)'
_GGLIB = ('método sin uso de la librería de GameGuard de 0x686550 (el mismo objeto: +0x10, +0x28 y las cadenas que '
          'descifra 0x68de80); anticheat de main.exe, Fuera de alcance')
_HASH = ('tabla hash que el inicializador 0x4acef0 arma con tamaños al azar (rand() % 0xcc7): el hash anti-tamper que '
         'describe 02-arranque-y-ciclo-de-vida.md (se rearma con tamaños al azar); anticheat, Fuera de alcance')
_ARB = 'parámetros de un programa ARB (0x8620 = GL_VERTEX_PROGRAM_ARB) por punteros de wglGetProcAddress, sin uso'
MANUAL = {}
for _a in (0x401BB0, 0x401BF0, 0x401C10, 0x401C60, 0x401C70, 0x401C80, 0x401C90, 0x401CC0, 0x401CE0, 0x401D30):
    MANUAL[_a] = ('CL3', _HOOK)
for _a in (0x6869A0, 0x688980, 0x68B960, 0x68B980, 0x68B9A0, 0x68B9D0, 0x68BE30, 0x68C5E0, 0x68C650):
    MANUAL[_a] = ('excluded', _GGLIB)
for _a in (0x447F80, 0x448060, 0x448090, 0x4480E0, 0x448100, 0x4481B0, 0x4481D0, 0x448240, 0x448270, 0x4482B0,
           0x448310):
    MANUAL[_a] = ('excluded', _HASH)
for _a in (0x66B1C0, 0x66B200, 0x66B230, 0x66B270, 0x66B2B0, 0x66B2E0, 0x66B310, 0x66B350):
    MANUAL[_a] = ('CL5', _ARB)
for _a in (0x680950, 0x680970, 0x680D00, 0x6808D0):
    MANUAL[_a] = ('CL3', 'destructor y limpieza del objeto del manejador de caídas (vtable 0x6bd86c, cuyos métodos '
                         '0x682860 y 0x682970 son CL3)')
for _a in (0x456A20, 0x456A50, 0x456B00, 0x456C20, 0x456CA0, 0x457C10, 0x4575C0):
    MANUAL[_a] = ('CL7', 'la clase de la vtable 0x6b8d38 (sus métodos 0x4573a0, 0x457550 y 0x457dc0 son CL7): '
                         'destructor, lista de objetos y búsqueda por (x, y, z)')
for _a in (0x643E70, 0x643EA0):
    MANUAL[_a] = ('CL8', 'destructor de la clase de la vtable 0x6ba970, cuyos métodos 0x643ed0..0x643f60 son CL8')
for _a in (0x685A40, 0x685A60, 0x685A80, 0x685A90):
    MANUAL[_a] = ('CL2', 'constructor y destructor del objeto de la vtable 0x6bd884, junto al descifrado propio '
                         '0x685ac6 (CL2)')
for _a in (0x40CEC0, 0x40D960, 0x40DAC0, 0x40DB80):
    MANUAL[_a] = ('CL7', 'recorren los conjuntos del registro propio (+0x9348..+0x938c), los campos de 0x40da20 '
                         '(CL7, T10b)')
for _a in (0x5FC6C0, 0x5FC740, 0x5FC860, 0x5FC880, 0x5FC8B0, 0x5FCC90):
    MANUAL[_a] = ('CL6', 'matemática de vectores: comparar, negar, la caja que contiene, trasladar un punto (la de '
                         '0x5fc9c0 y 0x5fcbd0)')
for _a in (0x640760, 0x640790, 0x646ED0, 0x644D00, 0x644D30, 0x644EA0, 0x644F10, 0x497950, 0x498460):
    MANUAL[_a] = ('CL8', 'setters y listas de las ventanas (los objetos de 0x640000..0x64f000 y 0x497000, CL8)')
for _a in (0x412AC0, 0x412AF0):
    MANUAL[_a] = ('CL8', 'copian los datos del objeto 0x6eed7c que llena 0x412b30 (el B1, CL8)')
for _a in (0x5132D0, 0x517920, 0x5178F0):
    MANUAL[_a] = ('CL9', 'buscan personajes del viewport (DAT_0743a16c, el nombre en +0x1d9, la distancia) o un id '
                         'en un arreglo')
for _a in (0x54E610, 0x54EBF0):
    MANUAL[_a] = ('CL9', 'atributos de un ítem según su nivel con la tabla DAT_07681ee0')
MANUAL.update({
    0x41E1C0: ('CL3', 'agrega una línea a un archivo de log (CreateFileA, SetFilePointer al final), sin uso'),
    0x41E220: ('CL3', 'log con formato (wvsprintfA) sobre 0x41e1c0, sin uso'),
    0x41E420: ('CL3', 'lee el buffer de un dispositivo de DirectInput (GetDeviceData, DIERR_INPUTLOST), sin uso'),
    0x4B95E0: ('CL3', 'termina un proceso por nombre (OpenProcess, TerminateProcess), sin uso'),
    0x4BE680: ('CL3', 'llama N veces a un callback, sin uso'),
    0x6816E0: ('CL3', 'agrega un componente a una ruta (lista de strings, la barra final) en la librería de Webzen, '
                      'sin uso'),
    0x68F4B0: ('CL2', 'cifrado por bloques de 16 bytes (cargas big-endian, rondas en 0x68f540), sin uso'),
    0x68F540: ('CL2', 'las rondas del cifrado por bloques de 0x68f4b0, sin uso'),
    0x41F380: ('CL10', 'escribe los datos de un WAV con mmioWrite, sin uso'),
    0x41F3E0: ('CL10', 'abre un WAV de salida con mmio y escribe el RIFF, sin uso'),
    0x4E9920: ('CL7', 'arma el byte de dirección (dx + 8) << 4 | (dy + 8) de un paso'),
    0x5AE300: ('CL7', 'la dirección de un paso con dx y dy acotados a ±8'),
    0x4EA090: ('CL7', 'distancia 2D de un objeto a un punto contra un radio'),
    0x5F9670: ('CL7', 'recorre celdas del terreno alrededor de (x, y) (las funciones del terreno de '
                      '0x5f8390..0x5fac90)'),
    0x5FD2C0: ('CL7', 'prueba de celdas del terreno (las de 0x5f8390..0x5fac90)'),
    0x450376: ('CL4', 'vacía: la llama el WM_TIMER 1000 antes del keepalive (02-arranque-y-ciclo-de-vida.md)'),
    0x57CF90: ('CL9', 'contador de 1 a 50 (DAT_077f3bad) que llama 0x57cfc0 (CL9)'),
    0x5769E0: ('CL8', 'compara dos textos con un comodín, junto al filtro de palabras 0x576eb0 (CL8)'),
    0x5AECD0: ('CL8', 'color de un número según el nivel (umbrales 300, 200, 100 y 50)'),
    0x5AF940: ('CL8', 'bits de un arreglo de banderas de bytes (el formato de un flag de UI)'),
    0x5A8CA0: ('CL8', 'prueba si un tipo está entre 0x29 y 0x2a'),
    0x6200E0: ('CL8', 'abre una URL con ShellExecuteA("open"), sin uso'),
    0x4546B0: ('CL7', 'copia tres floats de +0x14 (una posición)'),
    0x40BA40: ('CL8', 'prueba si un valor más +0x1c llega a +0x20 (un límite de una barra)'),
    0x4ACD50: ('CL3', 'parte un texto por separadores (la línea de comandos o la config), sin uso'),
    0x4ACE00: ('CL3', 'prueba los caracteres de un texto (la línea de comandos o la config), sin uso'),
})
# Por qué no se porta cada grupo de exclusiones (Fuera de alcance del roadmap). El grupo va al principio
# del propósito de cada fila y el motivo, en next_action.
WHY_NOT = {
    'GameGuard': 'anticheat (nProtect GameGuard): MuRet no corre GameGuard; sus avisos de Text.bmd (790..800) '
                 'solo los muestra su callback',
    'ResourceGuard': 'anticheat: el chequeo de los recursos al entrar al mundo no tiene efecto en este build',
    'hash anti-tamper': 'anticheat: la tabla hash de tamaños al azar no cambia el juego; el rand() de la protección '
                        'se cuenta aparte (07 Q-C125, 08 P-C22)',
    'launcher': 'es del launcher (el auto-update y el servidor que deja en el registro); queda para más adelante, '
                'si MuRet tiene un launcher propio o una reinterpretación',
    'detector de trampas': 'anticheat: busca por título ventanas de programas de trampa (Speed Hack, GameHack, '
                           'ArtMoney) y cierra el cliente; MuRet no lo hace (04-protocolo.md)',
}
GG_WHERE = ('docs/superpowers/specs/2026-10-30-cliente-design.md#fuera-de-alcance ("el anticheat del propio '
            'main.exe"); Client/docs/research/10-cobertura-roadmap.md#gameguard')
# Exclusiones ya decididas en el roadmap (Fuera de alcance) con la función que las abre: qué hace, dónde
# está la decisión y su grupo de WHY_NOT.
EXCLUDED_ROOTS = {
    0x4B96B0: ('fin del auto-update: copia mu._xe, mumsg._ll, wz_zp._ll y message._tf',
               'Client/docs/research/02-arranque-y-ciclo-de-vida.md (tabla de WinMain: "no se porta: es del '
               'launcher")', 'launcher'),
    0x6922C0: ('wzPopLaunchInfo: lee del registro el servidor que dejó el launcher',
               'docs/superpowers/specs/2026-10-30-cliente-design.md#fuera-de-alcance; 02-arranque-y-ciclo-de-vida.md',
               'launcher'),
    0x681EC0: ('chequeo de los recursos al entrar al mundo, sin efecto (el jmp de 0x4c4ad5)',
               'Client/docs/research/07-quirks-y-bugs.md Q-C147; anticheat de main.exe, Fuera de alcance',
               'ResourceGuard'),
    # nProtect GameGuard, enlazado en el exe (10-cobertura-roadmap.md, GameGuard).
    0x686550: ('arranque de la librería: lo llama un inicializador estático (0x68653e, fuera de funciones); filtro '
               'de excepciones temporal 0x689dd0, el objeto de 0x128c bytes de DAT_07d5c9f0 y 0x6869f0 (CryptoAPI '
               'con CryptVerifySignatureA, registro, hilos y los LoadLibrary de nombres cifrados de '
               '0x688b60..0x68bbb0)', GG_WHERE + '; 05-formatos-de-datos.md (el bloque de 0x688b60)', 'GameGuard'),
    0x4505D0: ('el callback de GameMon: los mensajes 0x3e9..0x3f8 (error y cierre de la comunicación, speedhack, '
               'hack detectado, autenticación) con un MessageBoxA "Error" y los textos 792, 793, 794 y 799 de '
               'Text.bmd; lo llaman 0x688590 y 0x6891d0', GG_WHERE, 'GameGuard'),
    0x450380: ('el aviso de un código de error ("GameGuard Error : %lu", los códigos 0x6e..0xdc, con los textos 792, '
               '795 a 798 y 800 de Text.bmd), sin uso', GG_WHERE, 'GameGuard'),
    # De la revisión independiente (10-cobertura-roadmap.md, La revisión independiente).
    0x4AD070: ('el cierre por trampa: MessageBoxA "Error" con un texto de Text.bmd, WM_DESTROY al cliente y WM_CLOSE '
               'a la ventana detectada; lo llaman el callback de GameGuard 0x4505d0 y la búsqueda de ventanas de '
               '0x4ad100', 'Client/docs/research/04-protocolo.md (la búsqueda de ventanas no se porta); '
               'anticheat de main.exe, Fuera de alcance', 'detector de trampas'),
    0x681480: ('constructor del objeto de ResourceGuard (el new de 0x2c en 0x4c4a1b; el mismo objeto que recibe '
               '0x681ec0)', 'Client/docs/research/07-quirks-y-bugs.md Q-C147; anticheat de main.exe, Fuera de alcance',
               'ResourceGuard'),
    0x6814D0: ('destructor del objeto de ResourceGuard (0x4c4b14, después del jmp de 0x4c4ad5)',
               'Client/docs/research/07-quirks-y-bugs.md Q-C147; anticheat de main.exe, Fuera de alcance',
               'ResourceGuard'),
    0x448170: ('fija el multiplicador del hash (+0x1c) con rand(); lo llama WinMain en 0x4ba65f, antes del srand de '
               '0x4bad9a, así que no mueve el azar del juego', 'Client/docs/research/02-arranque-y-ciclo-de-vida.md; '
               'anticheat de main.exe, Fuera de alcance', 'hash anti-tamper'),
}
# Decisiones de la revisión independiente (10-cobertura-roadmap.md, La revisión independiente): ola
# responsable y por qué, leídas en el .asm. Pesan como una semilla de MuRet (paso 1).
REVIEW = {
    0x4AD100: ('CL4', 'el keepalive inmediato después del login (0x4bee60, CL4.5); la búsqueda de ventanas de trampas '
                      'que hace antes es anticheat y no se porta (04-protocolo.md)'),
    0x4BEE60: ('CL4', 'arma y manda el keepalive C1 0C 0E (CL4.5, 04-protocolo.md); la CL7 solo le sumó las '
                      'velocidades del personaje'),
    0x40B3F0: ('CL2', 'cierra la entrada protegida del serial de los C3 (0x5767913) con un rand(); es la pareja de '
                      '0x40b3b0 (CL2)'),
    0x57CFC0: ('CL9', 'las acciones del héroe al final de MoveHero: arma los paquetes de habilidad 0x19, 0x1E y 0x26 '
                      '(CL9.2); la CL7 porta su acceso al registro propio (LogicalAccess)'),
    0x5650C0: ('CL9', 'la interacción al llegar: levantar un ítem (0x22), hablar con un NPC (0x30), la tienda personal '
                      'y los demás (0x18, 0xA0, 0xD3, 0xD7); el TSV de la CL8 la da a la CL9.3 y la CL9.4, y MuRet '
                      'solo porta la salida del tipo 0'),
    0x517A20: ('CL9', 'el ataque a varios blancos (CL9.2): hasta 5 del viewport en un radio, el C1 .. 10 con el '
                      'serial de los C3 y send (0x51867d, 0x5187ee); lo llaman los efectos 0x518cf0, 0x535840 y '
                      '0x520170'),
    0x612270: ('CL7', 'carga los modelos de los NPC (61 cargas de modelos y 3 de sonidos, "Npc\\", "NpcClerk"), como '
                      'su par 0x612d90 (CL7)'),
    0x4F97B0: ('CL11', 'la habilidad del personaje (CL11.4): 23 altas de efectos (0x518cf0, 0x530860, 0x53fe70), 7 '
                       'sonidos y 11 rand(), sin paquetes; 06 la deja "sin leer: solo los llama la habilidad"'),
    0x41ED10: ('CL10', 'toca un sonido: el buffer de DirectSound del id (0x71d2c4)'),
    0x41EE00: ('CL10', 'para un sonido (el buffer de DirectSound del id)'),
    0x41E520: ('CL10', 'arranca DirectSound (CoInitialize y DirectSoundCreate, DSOUND#1) desde WinMain'),
    0x41FF30: ('CL10', 'el log de los drivers de sonido ("Sound Card Driver", DirectSoundEnumerate, DSOUND#2) desde '
                       'WinMain'),
    0x4ACF60: ('CL10', 'para la música (wzAudioStop)'),
    0x4ACFD0: ('CL10', 'toca la música (wzAudioPlay)'),
    0x4AD060: ('CL10', 'la posición del stream de la música (wzAudioGetStreamOffsetRange)'),
    0x498820: ('CL9', 'la ventana de las cartas (CL9.4, amigos): el único que manda un C2 (03-red-y-cifrado.md); '
                      'MuRet la cita solo como ejemplo del encuadre'),
    0x4D2ED0: ('CL9', 'F3 05: la subida de nivel (los stats del personaje propio, 15 efectos y un sonido)'),
    0x4D3B50: ('CL9', 'F3 08: el cambio de clase'),
    0x4D9F00: ('CL9', 'F3 23: un efecto en la posición del héroe que pide el servidor'),
    0x4DA070: ('CL9', 'F3 40: un aviso del servidor, que dibujan los avisos de la CL8 (0x632e30, 0x633300)'),
    0x459A70: ('CL3', 'el temporizador de alta resolución (QueryPerformanceCounter, timeBeginPeriod) que arma WinMain'),
    0x4B8FF0: ('CL3', 'registra la clase "MU" y crea la ventana (CreateWindowExA) desde WinMain'),
    0x549EC0: ('CL6', 'carga FilterName.bmd, como su par 0x549c60 (Filter.bmd, CL6)'),
}
# Citas de Client/src que nombran la función sin portarla (revisión independiente): no cuentan como
# implementación. MuRet no tiene audio hasta la CL10: sus citas de las funciones de sonido marcan dónde
# se pide uno.
MENTION_ONLY = {
    0x4F97B0: 'CharacterStep.cpp la nombra como quien sube +0x1a0',
    0x498820: 'EncryptedPacket.h la nombra como el único que manda un C2',
    0x41ED10: 'sin audio hasta la CL10', 0x41EE00: 'sin audio hasta la CL10',
    0x4ACF60: 'sin audio hasta la CL10', 0x4ACFD0: 'sin audio hasta la CL10',
    0x435BD0: 'GameUi.cpp nombra la cadena de Crywolf del paso, sin portarla',
    0x44F810: 'GameUi.cpp la nombra en el corte de la CL9.5',
    0x40AD90: 'LogicalAccess.h: no hace el alta de la entrada', 0x40B240: 'LogicalAccess.h: no hace la búsqueda',
}
# Citas que portan solo una parte (revisión independiente).
PARTIAL_ONLY = {
    0x4365C0: 'GameHud.cpp porta el 0x620a00(1) del principio; la UI de los eventos es de la CL9.5',
}
# Los gigantes de los personajes: el paso (0x4fd5f0), el dibujo (0x50ba30, 0x505de0) y el alta de un
# monstruo (0x5141e0). Lo que solo ellos alcanzan y la CL7 no cita son las ramas de los tipos y de la
# habilidad que la CL7 no alcanza (paso 4e).
TYPE_GIANTS = {0x4FD5F0, 0x50BA30, 0x505DE0, 0x5141E0}
# Lo que escribe CL.M no es una fuente de CL.M: medido sobre un commit que ya los tiene, el informe y los
# scripts le darían a las funciones la ola que les dio la corrida anterior.
CLM_OUTPUTS = {'Client/docs/research/10-cobertura-roadmap.md',
               'Client/docs/research/scripts/tools/build_roadmap_matrix.py',
               'Client/docs/research/scripts/tools/check_roadmap_coverage.py',
               'Client/docs/research/scripts/tools/test_check_roadmap_coverage.py'}
TYPE_CREATE = 0x5141E0
EFFECT_CALLS = {0x518CF0, 0x530860, 0x53FE70, 0x5491E0, 0x41ED10}
# Gigantes con ramas de varias olas: la ola responsable sale de su papel, no de quién lo cita más.
ROLE_OVERRIDES = {
    0x4B9B80: ('CL3', 'WinMain: arranque, ventana y bucle de mensajes (02-arranque-y-ciclo-de-vida.md, CL3)'),
    0x4AF4E0: ('CL3', 'WndProc: la tabla de mensajes de la ventana y el input (02-arranque-y-ciclo-de-vida.md, CL3.3)'),
    0x4E2B10: ('CL4', 'dispatcher de recv: un caso por opcode (04-protocolo.md#el-dispatcher-0x4e2b10, CL4.2)'),
    0x636290: ('CL3', 'el paso por frame de las escenas (02-arranque-y-ciclo-de-vida.md, CL3.2)'),
    # De la revisión independiente: llama a la limpieza de cada sistema, que es de la ola del sistema.
    0x4AEDA0: ('CL3', 'el cierre por WM_QUIT desde WinMain (0x4be644): guarda la config en HKCU\\SOFTWARE\\Webzen\\Mu\\'
                      'Config, cierra "MuPlayer", restaura SystemParametersInfo y libera cada sistema '
                      '(02-arranque-y-ciclo-de-vida.md, CL3)'),
}
MULTI_WAVE_GIANTS = set(ROLE_OVERRIDES)
DISPATCHER = 0x4E2B10
# Opcode S->C del dispatcher -> ola y sub-ola (04-protocolo.md y paso5 del research; la CL8 ya
# portó el chat, la vida y el maná y los ítems del inventario; la CL7, el movimiento y el clima).
OPCODE_WAVE = {}
for ops, w in [((0x00, 0x03, 0x71, 0xEB, 0xF1, 0xF4), 'CL4 (conexión y keepalive)'),
               ((0x0F, 0x1C, 0x46, 0xBD, 0xD3, 0xD7), 'CL7 (mundo y movimiento)'),
               ((0x01, 0x02, 0x0C, 0x0D, 0x26, 0x27, 0x24, 0x28, 0x2A), 'CL8 (chat, vida y maná, inventario)'),
               ((0x12, 0x13, 0x14, 0x18, 0x1A, 0x1F, 0x20, 0x25, 0x45, 0x5D, 0x68, 0x9C, 0xCA, 0xDF),
                'CL9.1 (viewport)'),
               ((0x07, 0x0B, 0x16, 0x17, 0x19, 0x1B, 0x1E, 0x29, 0x2C, 0x48, 0x69, 0x73, 0xA3, 0xA7, 0xA8, 0xB7),
                'CL9.2 (combate y skills)'),
               ((0x21, 0x22, 0x23, 0x30, 0x31, 0x32, 0x33, 0x34, 0x81, 0x82, 0x83, 0x86, 0x87, 0xA9),
                'CL9.3 (ítems, tiendas, baúl y caos)'),
               (tuple(range(0x36, 0x3E)) + tuple(range(0x40, 0x45)) + tuple(range(0x50, 0x57))
                + tuple(range(0x60, 0x68)) + (0xAA, 0xAB, 0xAC, 0xAD) + tuple(range(0xC0, 0xCC))
                + (0xE1, 0xE5, 0xE6, 0xE9), 'CL9.4 (comercio, party, gremio, amigos y duelo)'),
               ((0x3F, 0x47, 0x51, 0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x99, 0x9A, 0x9B, 0x9D, 0x9E, 0x9F,
                 0xAF, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB8, 0xB9, 0xBA, 0xBB, 0xBC), 'CL9.5 (eventos y asedio)'),
               ((0xA0, 0xA1, 0xA2), 'CL9 (misiones: sin sub-ola, ver 10-cobertura-roadmap.md)')]:
    for op in ops:
        OPCODE_WAVE[op] = w
# Raíces de lectura de CL11 (roadmap, La ola de efectos visuales): efectos con modelo, joints, cintas,
# sprites y partículas.
EFFECT_ROOTS = [0x518CF0, 0x5208A0, 0x52D620, 0x530860, 0x535840, 0x53C6A0, 0x52E590, 0x5491E0, 0x549510,
                0x549440, 0x53FE70, 0x5446E0, 0x548550]
STRING_FAMILY = [(re.compile(r'Data\\(Sound|Music)|\.(wav|mp3|ogg)\b', re.I), 'CL10', 'archivos de audio')]
IMPORT_FAMILY = [(re.compile(r'^(gl|glu|wgl)[A-Z]'), 'CL5', 'OpenGL'),
                 (re.compile(r'^Imm'), 'CL8', 'IME del chat'),
                 (re.compile(r'^wzAudio'), 'CL10', 'wzAudio.dll'),
                 (re.compile(r'^(timeGetTime|QueryPerformance)'), 'CL3', 'reloj')]

ADDR = re.compile(r'0[xX](?:00)?([4-6][0-9a-fA-F]{5})\b')
ADDR_RESEARCH = re.compile(r'(?:0[xX]|FUN_)(?:00)?([4-6][0-9a-fA-F]{5})\b')
CUT = re.compile(r'cut\(|Unexpected\(|\bCL ?(9|10|11)\b|\bCL(9|10|11)\.|es de la CL|es del hito|\bcorte\b|'
                 r'cortad|sin portar|no se porta|queda para|llegan? con|\bstub\b|gancho|'
                 r'\btodavía no\b|\bMuRet no\b|\bno (?:lo |la |los |las )?porta', re.I)
WTOK = re.compile(r'\bCL ?(\d{1,2})(?:\.\d+|\.R\d?)?\b')
SUBWAVE = re.compile(r'\bCL ?(\d{1,2}\.(?:\d+|R\d?))\b')


def wnum(w):
    return int(w[2:])


def read_bytes(path):
    with open(path, 'rb') as fh:
        return fh.read()


def read_lines(path):
    with open(path, encoding='utf-8', errors='ignore') as fh:
        return fh.read().split('\n')


def clean(text, n=170):
    text = re.sub(r'[`*|]', '', text).replace('\t', ' ')
    text = re.sub(r'\s+', ' ', text).strip()
    return text if len(text) <= n else text[:n - 1] + '…'


class Binary:
    def __init__(self, exe_path):
        self.d = read_bytes(exe_path)
        if hashlib.sha256(self.d).hexdigest() != EXE_SHA256:
            raise SystemExit(f'{exe_path}: el SHA-256 no es el del build c32bbd5f')
        d = self.d
        pe = struct.unpack_from('<I', d, 0x3C)[0]
        nsec = struct.unpack_from('<H', d, pe + 6)[0]
        optsz = struct.unpack_from('<H', d, pe + 20)[0]
        self.opt = pe + 24
        self.secs = []
        for i in range(nsec):
            name, vs, va, rs, ra = struct.unpack_from('<8sIIII', d, self.opt + optsz + 40 * i)
            self.secs.append((name.rstrip(b'\0').decode('latin1'), va, vs, ra, rs))
        self.iat = self._imports()

    def off(self, va):
        r = va - BASE
        for _, sva, _, ra, rs in self.secs:
            if sva <= r < sva + rs:
                return r - sva + ra
        return None

    def _imports(self):
        d = self.d
        rva = struct.unpack_from('<I', d, self.opt + 96 + 8)[0]
        o = self.off(BASE + rva)
        iat = {}
        while True:
            oft, _, _, nm, ft = struct.unpack_from('<IIIII', d, o)
            if nm == 0:
                break
            dll = d[self.off(BASE + nm):].split(b'\0')[0].decode()
            th = oft or ft
            k = 0
            while True:
                v = struct.unpack_from('<I', d, self.off(BASE + th) + 4 * k)[0]
                if v == 0:
                    break
                name = f'{dll}#{v & 0xFFFF}' if v & 0x80000000 else \
                    d[self.off(BASE + v) + 2:].split(b'\0')[0].decode()
                iat[BASE + ft + 4 * k] = name
                k += 1
            o += 20
        return iat

    def cstr(self, va):
        o = self.off(va)
        if o is None:
            return None
        s = self.d[o:o + 160].split(b'\0')[0]
        if len(s) >= 4 and all(32 <= c < 127 for c in s):
            return s.decode('latin1')
        return None

    def data_dwords(self):
        for name, va, _, ra, rs in self.secs:
            if name not in ('.rdata', '.data'):
                continue
            for off in range(0, rs - 3, 4):
                yield BASE + va + off, struct.unpack_from('<I', self.d, ra + off)[0]


def load_inventory(path):
    raw = read_bytes(path)
    if hashlib.sha256(raw).hexdigest() != INV_SHA256:
        raise SystemExit(f'{path}: el SHA-256 no es el del inventario fijado en el plan de CL.M')
    lines = raw.decode('utf-8').split('\n')
    header = lines[0].split('\t')
    rows = [ln.split('\t') for ln in lines[1:] if ln]
    for r in rows:
        r.extend([''] * (len(header) - len(r)))
    return header, rows


def git(repo, *args):
    return subprocess.run(['git', '-C', repo, *args], capture_output=True, text=True,
                          errors='ignore', check=False).stdout


class Facts:
    """Lo que dice el binario: callers, punteros, tablas, imports y strings por función."""

    def __init__(self, research, inv_rows):
        self.rows = {int(r[0], 16): r for r in inv_rows}
        self.entries = sorted(self.rows)
        self.size = {a: int(r[1]) for a, r in self.rows.items()}
        self.name = {a: r[2] for a, r in self.rows.items()}
        self.bin = Binary(os.path.join(research, 'client-research/mainexe-vmxp-c32bbd5f/original/main.exe'))
        fn_dir = os.path.join(research, DECOMP, 'functions')
        self.callers = collections.defaultdict(set)
        self.callees = collections.defaultdict(set)
        self.ptr_by = collections.defaultdict(set)
        self.data_refs = collections.defaultdict(set)
        for line in gzip.open(os.path.join(research, GRAPH, 'edges.txt.gz'), 'rt'):
            a, b, k = line.rstrip().split('|')
            try:
                a, b = int(a, 16), int(b, 16)
            except ValueError:
                continue
            if b in self.rows:
                if k in ('C', 'J'):     # J: un salto de cola a la entrada de otra función
                    self.callers[b].add(a)
                    self.callees[a].add(b)
                elif k == 'D':
                    self.ptr_by[b].add(a)
            elif k == 'D':
                self.data_refs[a].add(b)
        self.imports = collections.defaultdict(set)
        self.strings = collections.defaultdict(set)
        self.calls_at = collections.defaultdict(list)   # función -> [(sitio, destino)]
        lit = re.compile(r'0x([0-9a-f]{6,8})\b')
        call = re.compile(r'^([0-9a-f]{8})\s.*\bCALL 0x([0-9a-f]{8})$')
        for a, r in self.rows.items():
            for line in read_lines(os.path.join(fn_dir, r[8] + '.asm')):
                m = call.match(line.rstrip())
                if m:
                    self.calls_at[a].append((int(m.group(1), 16), int(m.group(2), 16)))
                for h in lit.findall(line):
                    v = int(h, 16)
                    if not 0x6B8000 <= v < 0x7D5F000:
                        continue
                    if v in self.bin.iat:
                        self.imports[a].add(self.bin.iat[v])
                    else:
                        s = self.bin.cstr(v)
                        if s:
                            self.strings[a].add(s)
        self._tables()
        self._hidden_callers()
        self._funclet_parents()
        self.globals = collections.defaultdict(set)   # dato global -> funciones que lo usan
        for a, ds in self.data_refs.items():
            for t in ds:
                if 0x6C6000 <= t < 0x7D5F000 and self.bin.cstr(t) is None:
                    self.globals[t].add(a)

    def containing(self, x):
        i = bisect.bisect_right(self.entries, x) - 1
        if i >= 0 and self.entries[i] <= x < self.entries[i] + max(self.size[self.entries[i]], 1):
            return self.entries[i]
        return None

    def _tables(self):
        """Tablas de punteros a código en .rdata/.data, cortadas donde el código apunta (vtables)."""
        code = collections.OrderedDict((loc, v) for loc, v in self.bin.data_dwords() if 0x401000 <= v < TEXT_END)
        runs, seen = [], set()
        for loc in code:
            if loc in seen:
                continue
            en = loc
            while en + 4 in code:
                en += 4
            seen.update(range(loc, en + 4, 4))
            runs.append((loc, en + 4))
        starts = [r[0] for r in runs]
        targets = collections.defaultdict(set)
        for fa, ds in self.data_refs.items():
            for t in ds:
                i = bisect.bisect_right(starts, t) - 1
                if i >= 0 and runs[i][0] <= t < runs[i][1]:
                    targets[t].add(fa)
        self.table_of = collections.defaultdict(list)   # función -> [(inicio de tabla, quién la usa)]
        self.in_data = collections.defaultdict(list)
        for st, en in runs:
            cuts = sorted(t for t in targets if st <= t < en and (t - st) % 4 == 0)
            bounds = [st] + [c for c in cuts if c != st] + [en]
            for i in range(len(bounds) - 1):
                s0, s1 = bounds[i], bounds[i + 1]
                for loc in range(s0, s1, 4):
                    if code[loc] in self.rows:
                        self.table_of[code[loc]].append((s0, targets.get(s0, set())))
        for loc, v in self.bin.data_dwords():
            if v in self.rows:
                self.in_data[v].append(loc)

    def _funclet_parents(self):
        """El padre de cada funclet de EH: la función que empuja el handler que sigue al funclet."""
        stubs = collections.defaultdict(set)
        for fa, ds in self.data_refs.items():
            for t in ds:
                if CRT_START <= t < TEXT_END and t not in self.rows:
                    stubs[t].add(fa)
        order = sorted(stubs)
        self.parent = {}
        for a, n in self.name.items():
            if n.startswith(('Unwind@', 'Catch@')):
                i = bisect.bisect_right(order, a)
                if i < len(order):
                    self.parent[a] = (order[i], stubs[order[i]])

    def _hidden_callers(self):
        """CALL/JMP rel32 desde código que Ghidra no puso en ninguna función (inicializadores estáticos)."""
        d = self.bin.d
        self.hidden = collections.defaultdict(list)
        lo, hi = self.bin.off(0x401000), self.bin.off(TEXT_END - 1)
        for off in range(lo, hi - 4):
            if d[off] not in (0xE8, 0xE9):
                continue
            src = BASE + off
            t = src + 5 + struct.unpack_from('<i', d, off + 1)[0]
            if t in self.rows and self.containing(src) is None:
                self.hidden[t].append(src)

    def kind(self, a):
        n = self.name[a]
        if self.rows[a][5] == 'true':
            return 'thunk'
        if n.startswith(('Unwind@', 'Catch@')):
            return 'eh'
        if a in STL_STRING or a in STL_OTHER:
            return 'stl'
        if (self.hidden[a] and not self.callers[a] and self.size[a] <= 16 and len(self.callees[a]) == 1):
            return 'glue'
        if a >= CRT_START:
            own = [c for c in self.callees[a] if c < CRT_START and c not in STL_STRING]
            return 'crt-hook' if own and n != 'entry' else 'crt'
        if LIBJPEG[0] <= a < LIBJPEG[1]:
            return 'libjpeg'
        if WZLIB[0] <= a < WZLIB[1]:
            return 'wzlib'
        return 'game'


class MuRet:
    """Lo que dice MuRet en un commit: implementación (blame), cortes, tests, docs y el TSV de la CL8."""

    def __init__(self, repo, ref, facts):
        self.repo, self.ref, self.F = repo, ref, facts
        self.commit = git(repo, 'rev-parse', ref).strip()
        self.commit_wave = self._commit_waves()
        self.impl = collections.defaultdict(collections.Counter)
        self.impl_loc = collections.defaultdict(list)
        self.cut = collections.defaultdict(list)
        self.tests = collections.defaultdict(list)
        self.doc = collections.defaultdict(list)
        self.doc_wave = collections.defaultdict(collections.Counter)
        self.addr_wave = collections.defaultdict(collections.Counter)   # dirección exacta -> olas
        self.cl8 = {}
        self._scan()

    def _commit_waves(self):
        out, last = {}, None
        log = git(self.repo, 'log', '--reverse', '--format=%H%x00%s%x00%b%x01', self.ref)
        for rec in log.split('\x01'):
            if not rec.strip():
                continue
            h, subj, body = (rec.strip('\n').split('\x00') + ['', ''])[:3]
            m = WTOK.search(subj) or WTOK.search(body)
            w = f'CL{int(m.group(1))}' if m and int(m.group(1)) <= 11 else (last or 'CL0')
            last = w
            out[h] = w
        return out

    def fn(self, h):
        return self.F.containing(int(h, 16))

    def _scan(self):
        files = git(self.repo, 'ls-tree', '-r', '--name-only', self.ref).split('\n')
        for f in files:
            if not f.startswith(('Client/', 'docs/')) or f.endswith('Input/Key.h'):
                continue
            if f.startswith('Client/docs/research/coverage/') or f in CLM_OUTPUTS:
                continue
            if not f.endswith(('.h', '.cpp', '.md', '.tsv', '.py', '.txt', '.inc')):
                continue
            if f.startswith('Client/src/'):
                self._blame(f)
                continue
            text = git(self.repo, 'show', f'{self.ref}:{f}')
            for i, line in enumerate(text.splitlines(), 1):
                hs = ADDR.findall(line)
                if not hs:
                    continue
                cut = bool(CUT.search(line))
                ws = {f'CL{int(x)}' for x in WTOK.findall(line) if int(x) <= 11}
                for h in hs:
                    a = self.fn(h)
                    if a is None:
                        continue
                    if f.startswith('Client/tests/'):
                        if not cut and len(self.tests[a]) < 3:
                            self.tests[a].append(f'{f}:{i}')
                        continue
                    if len(self.doc[a]) < 8:
                        self.doc[a].append((f'{f}:{i}', sorted(ws, key=wnum), line.strip()))
                    if len(ws) == 1:
                        w = next(iter(ws))
                        self.doc_wave[a][w] += 1
                        self.addr_wave[int(h, 16)][w] += 1
        tsv = git(self.repo, 'show', f'{self.ref}:docs/superpowers/plans/2026-11-12-ola-cl8-funciones.tsv')
        for line in tsv.splitlines()[1:]:
            p = line.split('\t')
            if p and p[0].startswith('0x'):
                a = self.fn(p[0][2:])
                if a is not None:
                    self.cl8[a] = p[2] if len(p) > 2 else ''

    def _blame(self, f):
        out = git(self.repo, 'blame', '--line-porcelain', self.ref, '--', f)
        cur = ln = None
        for line in out.splitlines():
            if re.match(r'^[0-9a-f]{40} ', line):
                p = line.split()
                cur, ln = p[0], int(p[2])
            elif line.startswith('\t'):
                text = line[1:]
                hs = ADDR.findall(text)
                if not hs:
                    continue
                w = self.commit_wave.get(cur, 'CL0')
                cut = bool(CUT.search(text))
                ws = {f'CL{int(x)}' for x in WTOK.findall(text) if int(x) <= 11}
                for h in hs:
                    a = self.fn(h)
                    if a is None:
                        continue
                    if cut:
                        if len(self.cut[a]) < 6:
                            self.cut[a].append((f'{f}:{ln}', sorted(ws, key=wnum), text.strip()))
                        continue
                    self.impl[a][w] += 1
                    self.addr_wave[int(h, 16)][w] += 1
                    if len(self.impl_loc[a]) < 3:
                        self.impl_loc[a].append((f'{f}:{ln}', cur[:7], w, text.strip()))


def research_mentions(research, facts):
    out = collections.defaultdict(list)
    root = os.path.join(research, RESEARCH_DOCS)
    for dirpath, _, files in os.walk(root):
        for name in sorted(files):
            if not name.endswith('.md'):
                continue
            path = os.path.join(dirpath, name)
            rel = os.path.relpath(path, research)
            for i, line in enumerate(read_lines(path), 1):
                for h in ADDR_RESEARCH.findall(line):
                    a = facts.containing(int(h, 16))
                    if a is not None and len(out[a]) < 4:
                        out[a].append((f'{rel}:{i}', line.strip()))
    return out


def registry_levels(research, facts):
    path = os.path.join(research, 'client-research/analysis/mainexe/evidence/coverage/registro-niveles.tsv')
    out = {}
    for line in read_lines(path):
        if line.startswith('#') or not line.strip():
            continue
        va, level, *rest = line.rstrip('\n').split('\t')
        a = facts.containing(int(va, 16))
        if a is not None:
            out[a] = (level, clean(' '.join(rest), 120))
    return out


def lowest(ws):
    ws = sorted(set(ws), key=wnum)
    return ws[0] if ws else None


def build(args):
    inv_path = os.path.join(args.research, DECOMP, 'inventory.tsv')
    header, rows = load_inventory(inv_path)
    F = Facts(args.research, rows)
    M = MuRet(args.repo, args.ref, F)
    for a, why in MENTION_ONLY.items():   # la nombran sin portarla: la cita queda como mención
        M.impl.pop(a, None)
        for loc, _, _, text in M.impl_loc.pop(a, [])[::-1]:
            M.doc[a].insert(0, (f'{loc} (la nombra: {why})', [], text))
    R = research_mentions(args.research, F)
    REG = registry_levels(args.research, F)
    kinds = {a: F.kind(a) for a in F.rows}
    own = {a for a, k in kinds.items() if k in ('game', 'wzlib', 'crt-hook')}

    wave, basis, ev, cons = {}, {}, collections.defaultdict(list), collections.defaultdict(set)
    ev_purpose = {}

    def mention_waves(a):
        ws = set(M.impl[a])
        for _, w, _ in M.cut[a]:
            ws.update(w)
        ws.update(f'CL{int(x)}' for x in re.findall(r'CL(\d{1,2})', M.cl8.get(a, '')) if int(x) <= 11)
        ws.update(M.doc_wave[a])
        return ws

    # 1) Semillas: lo que MuRet porta o asigna explícitamente.
    for a in own:
        cutw = {w for _, ws, _ in M.cut[a] for w in ws if wnum(w) >= 9}
        tsvw = {f'CL{int(x)}' for x in re.findall(r'CL(\d{1,2})', M.cl8.get(a, '')) if int(x) <= 11}
        if a in ROLE_OVERRIDES:
            wave[a], why = ROLE_OVERRIDES[a]
            basis[a] = 'papel'
            ev[a].append(why)
        elif a in REVIEW:
            wave[a], why = REVIEW[a]
            basis[a] = 'revisión'
            ev[a].append(f'revisión independiente: {why}')
            ev_purpose[a] = why
        elif M.impl[a]:
            c = M.impl[a]
            wave[a] = max(c, key=lambda w: (c[w], -wnum(w)))
            basis[a] = 'src'
        elif cutw:
            wave[a] = lowest(cutw)
            basis[a] = 'corte'
        elif tsvw:
            wave[a] = lowest(tsvw)
            basis[a] = 'tsv-cl8'
            ev[a].append(f'docs/superpowers/plans/2026-11-12-ola-cl8-funciones.tsv: {clean(M.cl8[a], 80)}')
        elif M.doc_wave[a]:
            c = M.doc_wave[a]
            wave[a] = max(c, key=lambda w: (c[w], -wnum(w)))
            basis[a] = 'doc'
        if a in wave:
            cons[a] = mention_waves(a) - {wave[a]}
    def refs_of(a):   # quién la alcanza: un funclet de EH cuenta como la función que lo usa
        cs = set()
        for c in F.callers[a]:
            cs |= F.parent[c][1] if c in F.parent else {c}
        return (cs | F.ptr_by[a] | {o for _, us in F.table_of[a] for o in us}) - {a}   # sin la recursión

    # 2) Exclusiones ya decididas y lo que solo ellas alcanzan.
    excluded = {}
    for a, (what, where, group) in EXCLUDED_ROOTS.items():
        excluded[a] = (what, where, group)
    # Una llamada desde código que Ghidra no puso en ninguna función (un inicializador estático, un hueco
    # entre funciones) cuenta como de la función excluida que tenga al lado (0x68653e, antes de 0x686550;
    # el hueco que sigue a 0x68585b) o, si no hay ninguna, de la que sigue al hueco.
    starts = sorted(F.rows)

    def refs_x(a):
        out = set(refs_of(a))
        for site in F.hidden[a]:
            i = bisect.bisect_right(starts, site)
            near = [starts[j] for j in (i - 1, i) if 0 <= j < len(starts)]
            side = [x for x in near if x in excluded]
            out.add(side[0] if side else near[-1])
        for t in [x for x in out if kinds.get(x) == 'thunk']:   # un thunk vale por quien lo llama
            out.discard(t)
            out |= refs_of(t)
        return out

    glue = {a for a, k in kinds.items() if k == 'glue'}

    def close_exclusions(neutral):
        # Las candidatas son las que solo alcanzan otras candidatas, una exclusión o algo neutral (el
        # código muerto); los ciclos, como los métodos de una clase que se llaman entre sí, también.
        cand = {a for a in own if a not in excluded and a not in wave}
        neutral = neutral | glue   # el código que construye un global no la hace viva: el global es de su clase
        grew = True
        while grew:
            grew = False
            for a in sorted(cand):
                refs = refs_x(a)
                if not refs or not refs <= cand | set(excluded) | neutral:
                    cand.discard(a)
                    grew = True
        # De esas, se excluyen las que una exclusión alcanza, directa o indirectamente.
        grew = True
        while grew:
            grew = False
            for a in sorted(cand - set(excluded)):
                hit = sorted(refs_x(a) & set(excluded))
                if hit:
                    root = hit[0]
                    excluded[a] = (f'solo la alcanza {root:#x}',) + excluded[root][1:]
                    grew = True

    close_exclusions(set())
    for a in excluded:   # una exclusión con citas de MuRet (0x681ec0) no le pasa su ola a nadie
        wave.pop(a, None)
        basis.pop(a, None)
    # 3) Propagación por el grafo, en rondas (el resultado no depende del orden).
    giants = {a for a in own if len(F.callees[a]) > 60 or F.size[a] > 20000} | MULTI_WAVE_GIANTS

    def sources(a, with_giants):
        src = []
        for c0 in F.callers[a]:
            for c in (F.parent[c0][1] if c0 in F.parent else [c0]):
                if c in wave and (with_giants or c not in giants):
                    src.append((c, 'llamada' if c == c0 else f'funclet {c0:#x}'))
        for c in F.ptr_by[a]:
            if c in wave:
                src.append((c, 'puntero'))
        for st, users in F.table_of[a]:
            for c in users:
                if c in wave:
                    src.append((c, f'vtable {st:#x}'))
        return src

    def propagate(with_giants, tag):
        while True:
            new = {}
            for a in own:
                if a in wave or a in excluded:
                    continue
                src = sources(a, with_giants)
                if with_giants:
                    src = [(c, k) for c, k in src if c not in MULTI_WAVE_GIANTS]
                if not src:
                    continue
                ws = collections.Counter(wave[c] for c, _ in src)
                w = lowest(ws)
                shown = ', '.join(f'{c:#x} ({wave[c]}, {k})' for c, k in sorted(src)[:3])
                more = f' y {len(src) - 3} más' if len(src) > 3 else ''
                new[a] = (w, set(ws) - {w}, f'{tag}: {shown}{more}')
            if not new:
                return
            for a, (w, cw, e) in new.items():
                wave[a], basis[a] = w, tag
                cons[a] |= cw
                ev[a].append(e)

    propagate(False, 'grafo')
    # 3b) El dispatcher: cada llamada va con el opcode de su caso (paso5 del research).
    labels = {}
    paso5 = os.path.join(args.research, RESEARCH_DOCS, 'functions/paso5-protocolo-handlers.md')
    for line in read_lines(paso5):
        m = re.match(r'^\| 0x([0-9A-Fa-f]{2}) \| (?:0x[0-9a-f]+→)?0x([0-9a-f]{6})', line)
        if m:
            labels[int(m.group(2), 16)] = int(m.group(1), 16)
    lab_sorted = sorted(labels)
    disp_wave = {}
    for site, tgt in F.calls_at[DISPATCHER]:
        i = bisect.bisect_right(lab_sorted, site) - 1
        if i < 0 or tgt not in own:
            continue
        op = labels[lab_sorted[i]]
        if op in OPCODE_WAVE:
            disp_wave.setdefault(tgt, []).append((op, lab_sorted[i], site))
    while True:
        new = {}
        for a, lst in disp_wave.items():
            if a in wave or a in excluded:
                continue
            ws = [OPCODE_WAVE[op].split(' ')[0].split('.')[0] for op, _, _ in lst]
            w = lowest(ws)
            op, lab, site = lst[0]
            new[a] = (w, set(ws) - {w}, f'dispatcher 0x4e2b10: el caso del opcode {op:#04x} (etiqueta {lab:#x}, '
                                        f'llamada en {site:#x}) es {OPCODE_WAVE[op]}; paso5-protocolo-handlers.md')
        if not new:
            break
        for a, (w, cw, e) in new.items():
            wave[a], basis[a] = w, 'opcode'
            cons[a] |= cw
            ev[a].append(e)
        propagate(False, 'grafo')
    # 3c) Los otros gigantes de varias olas: la ola de la cita más cercana a la llamada.
    cited = sorted(M.addr_wave)
    for g in MULTI_WAVE_GIANTS - {DISPATCHER}:
        for site, tgt in F.calls_at[g]:
            if tgt not in own or tgt in wave or tgt in excluded:
                continue
            i = bisect.bisect_left(cited, site)
            best = None
            for j in (i - 1, i):
                if 0 <= j < len(cited) and g <= cited[j] < g + F.size[g] and abs(cited[j] - site) <= 0x200:
                    if best is None or abs(cited[j] - site) < abs(best - site):
                        best = cited[j]
            if best is not None:
                c = M.addr_wave[best]
                w = max(c, key=lambda x: (c[x], -wnum(x)))
                wave[tgt], basis[tgt] = w, 'rama'
                ev[tgt].append(f'la llamada {site:#x} de {g:#x} está a {abs(best - site):#x} bytes de {best:#x}, '
                               f'que MuRet cita en la {w}')
    propagate(False, 'grafo')
    propagate(True, 'gigante')
    # 3d) Los casos de efectos que solo alcanzan los efectos y que MuRet no porta: CL11.
    eff = set(EFFECT_ROOTS)
    grew = True
    while grew:
        grew = False
        for a in own:
            if a in eff or basis.get(a) not in ('grafo', 'gigante'):
                continue
            refs = refs_of(a)
            if refs and refs <= eff:
                eff.add(a)
                grew = True
    for a in sorted(eff - set(EFFECT_ROOTS)):
        if a in wave and wave[a] != 'CL11':
            cons[a].add(wave[a])
            wave[a], basis[a] = 'CL11', 'efectos'
            ev[a].insert(0, 'solo la alcanzan los efectos (las raíces de CL11 del roadmap) y MuRet no la porta: '
                            'caso restante de CL11')
    # Las raíces conservan su ola (las obligaciones de CL7.6) y suman CL11, que completa sus casos.
    for a in EFFECT_ROOTS:
        if a in wave and wave[a] != 'CL11':
            cons[a].add('CL11')
            ev[a].insert(0, f'raíz de efectos del roadmap: la {wave[a]} conserva lo que ya porta y CL11.R '
                            f'reparte los casos que faltan')
    # 4) Código muerto: nadie la llama, la apunta ni la inicializa, o solo la alcanza código muerto.
    dead = set()
    grew = True
    while grew:
        grew = False
        for a in own:
            if a in dead or a in excluded or basis.get(a) in ('src', 'papel'):
                continue
            if F.hidden[a] or F.in_data[a]:
                continue
            if refs_of(a) <= dead:
                dead.add(a)
                grew = True
    for _ in range(4):
        new = {}
        for a in own:
            if a in wave or a in excluded:
                continue
            outs = [c for c in F.callees[a] if c in wave]
            if not outs:
                continue
            ws = collections.Counter(wave[c] for c in outs)
            w = max(ws, key=lambda x: (ws[x], -wnum(x)))
            shown = ', '.join(f'{c:#x} ({wave[c]})' for c in sorted(outs)[:3])
            new[a] = (w, set(ws) - {w}, f'usa: {shown}')
        if not new:
            break
        for a, (w, cw, e) in new.items():
            wave[a], basis[a] = w, 'usa'
            cons[a] |= cw
            ev[a].append(e)
    # 4a) Instala una vtable cuyos métodos ya tienen ola (constructores y destructores).
    seg_members = collections.defaultdict(set)
    for m, lst in F.table_of.items():
        for st, _ in lst:
            seg_members[st].add(m)
    for _ in range(3):
        new = {}
        for a in own:
            if a in wave or a in excluded:
                continue
            score = collections.Counter()
            seen_t = None
            for t in F.data_refs[a]:
                ms = [m for m in seg_members.get(t, ()) if m != a and m in wave]
                for m in ms:
                    score[wave[m]] += 1
                    seen_t = seen_t or t
            if score:
                w = max(score, key=lambda x: (score[x], -wnum(x)))
                new[a] = (w, f'instala la vtable {seen_t:#x}, cuyos métodos son de la {w}')
        if not new:
            break
        for a, (w, e) in new.items():
            wave[a], basis[a] = w, 'vtable'
            ev[a].append(e)
    # 4a) Comparte datos globales con funciones que ya tienen ola (las de pocos usuarios pesan más).
    for _ in range(3):
        new = {}
        for a in own:
            if a in wave or a in excluded:
                continue
            score, why = collections.Counter(), {}
            for t in F.data_refs[a]:
                users = [u for u in F.globals.get(t, ()) if u != a and u in wave]
                if not users or len(F.globals[t]) > 40:
                    continue
                for u in users:
                    score[wave[u]] += 1 / len(F.globals[t])
                    why.setdefault(wave[u], (t, u))
            if score:
                w = max(score, key=lambda x: (score[x], -wnum(x)))
                t, u = why[w]
                new[a] = (w, f'comparte el dato {t:#x} con {u:#x} ({w})')
        if not new:
            break
        for a, (w, e) in new.items():
            wave[a], basis[a] = w, 'datos'
            ev[a].append(e)
    for a in own:
        if a in wave or a in excluded:
            continue
        fam = [(w, label) for imp in sorted(F.imports[a]) for rx, w, label in IMPORT_FAMILY if rx.match(imp)]
        fam += [(w, label) for st in sorted(F.strings[a]) for rx, w, label in STRING_FAMILY if rx.search(st)]
        if fam:
            wave[a], basis[a] = fam[0][0], 'dominio'
            shown = sorted(F.imports[a])[:3] + [f'"{x[:30]}"' for x in sorted(F.strings[a])[:2]]
            ev[a].append(f'usa {fam[0][1]}: ' + ', '.join(shown))
    # 4b) Lo que solo alcanzan los gigantes de varias olas, sin cita cerca: la ola de su papel.
    propagate(True, 'gigante')
    for a in own:
        if a in wave or a in excluded:
            continue
        gs = [g for g in F.callers[a] if g in MULTI_WAVE_GIANTS]
        if gs:
            wave[a], basis[a] = ROLE_OVERRIDES[gs[0]][0], 'gigante'
            ev[a].append(f'solo la llama {gs[0]:#x} ({ROLE_OVERRIDES[gs[0]][0]}), sin una cita de MuRet cerca de '
                         f'la llamada')
    propagate(False, 'grafo')

    # 4c) Lo leído a mano.
    for a, (w, why) in MANUAL.items():
        if a not in own or a in wave:
            continue
        if w == 'excluded':
            group = 'GameGuard' if why == _GGLIB else 'hash anti-tamper'
            excluded[a] = (why, 'lectura del .c y el .asm (10-cobertura-roadmap.md, Lo leído a mano)', group)
            continue
        wave[a], basis[a] = w, 'lectura'
        ev[a].append(f'leída a mano: {why}')
        ev_purpose[a] = why
    propagate(False, 'grafo')
    # 4d) Lo que sigue sin ola y solo alcanzan las exclusiones o el código muerto (las clases de
    # ResourceGuard y de GameGuard), y el código muerto que solo usa funciones excluidas.
    close_exclusions(dead)
    loose = {a for a in dead if a not in wave and a not in excluded}
    grew = True
    while grew:   # las que usan algo fuera de las exclusiones y de las otras sueltas no entran
        grew = False
        for a in sorted(loose):
            outs = {c for c in F.callees[a] if c in own}
            if not outs or not outs <= loose | set(excluded):
                loose.discard(a)
                grew = True
    grew = True
    while grew:
        grew = False
        for a in sorted(loose - set(excluded)):
            hit = sorted({c for c in F.callees[a] if c in excluded})
            if hit:
                excluded[a] = (f'código muerto que solo usa funciones excluidas, como {hit[0]:#x}',) \
                    + excluded[hit[0]][1:]
                grew = True
    close_exclusions(dead)
    # 4e) Las ramas de los tipos y de la habilidad que la CL7 no alcanza (revisión independiente): lo que
    # la CL7 tiene solo por el grafo, sin citas de MuRet, y alcanzan únicamente los gigantes de los
    # personajes (o la habilidad 0x4f97b0, CL11). Son los monstruos de los mapas de evento, cuyos módulos
    # 06 da a la CL9, y la habilidad (06: "no se llega", "solo los llama la habilidad"). El alta de un
    # tipo (lo que solo alcanza 0x5141e0) va a la CL9; lo que crea efectos o toca sonidos, a la CL11.4,
    # con lo que solo eso alcanza; el resto, a la CL9.
    reach_ok = TYPE_GIANTS | {a for a in REVIEW if REVIEW[a][0] == 'CL11'}
    branch = {a for a in own if wave.get(a) == 'CL7' and basis.get(a) in ('grafo', 'gigante') and a not in dead}
    grew = True
    while grew:
        grew = False
        for a in sorted(branch):
            refs = refs_of(a)
            if not refs or not refs <= reach_ok | branch:
                branch.discard(a)
                grew = True
    create = set()
    grew = True
    while grew:
        grew = False
        for a in sorted(branch - create):
            refs = refs_of(a)
            if refs <= {TYPE_CREATE} | create:
                create.add(a)
                grew = True
    fx = {a for a in branch - create if F.callees[a] & EFFECT_CALLS}
    grew = True
    while grew:
        grew = False
        for a in sorted(branch - create - fx):
            refs = refs_of(a)
            if refs & fx and refs <= fx | (reach_ok - {TYPE_CREATE}):
                fx.add(a)
                grew = True
    for a in sorted(branch):
        w = 'CL11' if a in fx else 'CL9'
        direct = bool(F.callees[a] & EFFECT_CALLS)
        what = ('alta de un tipo' if a in create else 'crea efectos o toca sonidos' if direct
                else 'la usan solo ramas que crean efectos' if a in fx else 'lógica de un tipo o de la habilidad')
        reach = ', '.join(f'{c:#x}' for c in sorted(refs_of(a))[:3])
        cons[a].add('CL7')
        if a in fx:
            cons[a].add('CL9')   # el disparador (CL11.4 conecta los efectos con la CL9)
        elif direct:
            cons[a].add('CL11')
        wave[a], basis[a] = w, 'tipo'
        ev[a].insert(0, f'rama que la CL7 no alcanza ({what}): solo la alcanzan {reach}; '
                        f'06-render-escenas-y-ui.md:6389 y :9581 (los módulos de los mapas de evento son de la CL9)')
    # 4f) Las consumidoras: las olas de quienes la alcanzan y, en un gigante, las olas abiertas (CL9 a CL11)
    # de lo que llama, que son las ramas que le faltan.
    for a in own:
        if a not in wave:
            continue
        cons[a] |= {wave[c] for c in refs_of(a) if c in wave and c not in dead}
        if a in giants:
            cons[a] |= {wave[c] for c in F.callees[a] if c in wave and wnum(wave[c]) >= 9}
        cons[a].discard(wave[a])

    # 5) Filas.
    out_rows = []
    stats = {'kinds': collections.Counter(), 'kind_bytes': collections.Counter()}
    for r in rows:
        a = int(r[0], 16)
        k = kinds[a]
        size = F.size[a]
        stats['kinds'][k] += 1
        stats['kind_bytes'][k] += size
        row = dict(zip(NEW_COLUMNS, [''] * len(NEW_COLUMNS)))
        callers = sorted(F.callers[a])
        caller_txt = ', '.join(f'{c:#x}' for c in callers[:3])
        caller_txt += f' y {len(callers) - 3} más' if len(callers) > 3 else ''
        asm = f'{DECOMP}/functions/{r[8]}.asm'
        row['validation'] = 'none'
        if k == 'thunk':
            target = [ln for ln in read_lines(os.path.join(args.research, asm)) if 'JMP' in ln]
            tgt = target[0].split('JMP')[-1].strip() if target else '?'
            imp = ''
            m = re.search(r'0x([0-9a-f]{6,8})', tgt)
            if m and int(m.group(1), 16) in F.bin.iat:
                imp = F.bin.iat[int(m.group(1), 16)]
            elif m and int(m.group(1), 16) in F.rows:
                imp = F.name[int(m.group(1), 16)]
            cw = collections.Counter(wave[c] for c in callers if c in wave)
            fam = [(w, label) for rx, w, label in IMPORT_FAMILY
                   if rx.match(r[2].replace('thunk_', '')) or rx.match(imp)]
            row['classification'] = 'thunk'
            if cw:
                row['primary_wave'] = lowest(cw)
                row['consumer_waves'] = ';'.join(sorted(set(cw) - {row['primary_wave']}, key=wnum))
                why = f'hereda la ola de quien lo llama: {caller_txt}'
            elif m and int(m.group(1), 16) in excluded:
                row['primary_wave'] = ''
                why = (f'sin llamadas desde código de juego; el destino está excluido '
                       f'({excluded[int(m.group(1), 16)][2]})')
            elif fam:
                row['primary_wave'] = fam[0][0]
                why = f'sin llamadas desde código de juego; el destino es {fam[0][1]}'
            else:
                row['primary_wave'] = ''
                why = 'sin llamadas desde código de juego ni familia de juego: destino del sistema, sin ola'
            row['purpose'] = clean(f'thunk de {r[2]} -> {imp or tgt}')
            row['evidence'] = clean(f'{asm}: JMP {tgt}; {why}', 300)
            row['analysis_state'] = 'static'
            row['implementation_state'] = 'not-applicable'
            row['next_action'] = 'nada: es un salto al destino; lo cubre la ola del destino'
            if m and int(m.group(1), 16) in excluded:
                row['next_action'] = f'nada: no se porta; {WHY_NOT[excluded[int(m.group(1), 16)][2]]}'
        elif k in ('eh', 'crt', 'libjpeg', 'stl', 'glue'):
            row['classification'] = 'library'
            row['analysis_state'] = 'static' if k in ('stl', 'libjpeg') else 'inferred'
            row['implementation_state'] = 'not-applicable'
            if k == 'eh':
                tabs = ', '.join(f'{x:#x}' for x in F.in_data[a][:2])
                row['purpose'] = 'funclet de excepciones de C++ que genera MSVC (destructor de un local al desenrollar)'
                row['evidence'] = clean(f'{asm}; nombre {r[2]} de Ghidra; lo apunta la tabla de EH {tabs}; '
                                        f'MuRet usa las excepciones y RAII de su compilador', 300)
                row['next_action'] = 'nada: lo reemplaza el compilador'
            elif k == 'crt':
                own_callers = [c for c in callers if c < CRT_START]
                imps = ', '.join(sorted(F.imports[a])[:3])
                row['purpose'] = clean('rutina del CRT estático de MSVC' + (f' (usa {imps})' if imps else ''))
                row['evidence'] = clean(
                    f'{asm}; rango del CRT (>= 0x69eff0, research analysis/mainexe/functions/criterio-cobertura.md); '
                    f'no llama código del juego; la llaman {len(own_callers)} funciones del juego'
                    + ('; entry y startup: mainexe-vmxp-c32bbd5f/reports/entry-006a2444.md' if a == 0x6A2444
                       else ''), 300)
                row['next_action'] = 'nada: lo reemplaza la biblioteca estándar de C++'
            elif k == 'libjpeg':
                row['purpose'] = 'libjpeg de IJG (decodificar los JPEG de los OZJ)'
                row['evidence'] = clean(f'{asm}; rango de libjpeg (0x66b8f0..0x67f0c0, criterio-cobertura.md); '
                                        f'MuRet usa libjpeg-turbo (CL1.4, Client/src/Data/Textures)', 300)
                row['next_action'] = 'nada: lo reemplaza libjpeg-turbo (CL1.4)'
            elif k == 'glue':
                tgt = next(iter(F.callees[a]))
                what = 'registra en atexit el destructor de un objeto global' if tgt == 0x69F8E4 else \
                    f'construye un objeto global ({tgt:#x})'
                row['purpose'] = f'código que genera MSVC para los globales: {what}'
                row['evidence'] = clean(f'{DECOMP}/functions/{r[8]}.c: un solo CALL ({tgt:#x}); lo llama la tabla de '
                                        f'inicializadores del CRT desde '
                                        f'{", ".join(f"{x:#x}" for x in F.hidden[a][:2])} '
                                        f'(código fuera de toda función)', 300)
                row['analysis_state'] = 'static'
                row['next_action'] = 'nada: lo genera el compilador; el objeto global es de la ola de su clase'
            elif a in STL_OTHER:
                row['purpose'] = f'STL de MSVC6: {STL_OTHER[a]}'
                row['evidence'] = clean(f'{DECOMP}/functions/{r[8]}.c: patrón de la STL de MSVC6 (nodos centinela, '
                                        f'bloques, refcount en ptr[-1]); la llaman {len(callers)} funciones', 300)
                row['next_action'] = 'nada: MuRet usa la STL de su compilador'
            else:
                row['purpose'] = f'std::string de la STL de MSVC6: {STL_STRING[a]}'
                row['evidence'] = clean(f'{DECOMP}/functions/{r[8]}.c: refcount en ptr[-1] y _Xlen/_Xran '
                                        f'(0x69effb "string too long", 0x69f227 "invalid string position"); '
                                        f'la llaman {len(callers)} funciones', 300)
                row['next_action'] = 'nada: MuRet usa std::string'
        elif a in excluded:
            what, where, group = excluded[a]
            row['classification'] = 'excluded'
            row['purpose'] = clean(f'{group}: {what}', 300)
            row['evidence'] = clean(f'{asm}; {where}', 300)
            row['analysis_state'] = 'static' if a in EXCLUDED_ROOTS or a in MANUAL else 'inferred'
            row['implementation_state'] = 'not-applicable'
            row['next_action'] = f'{"por ahora nada" if group == "launcher" else "nada"}: no se porta; {WHY_NOT[group]}'
        elif a in wave:
            w = wave[a]
            row['classification'] = 'game'
            row['primary_wave'] = w
            row['consumer_waves'] = ';'.join(sorted(cons[a] - {w}, key=wnum))
            parts = []
            for loc, c7, cw, text in M.impl_loc[a][:2]:
                parts.append(f'{loc}@{c7} ({cw})')
            for loc, ws, text in M.cut[a][:1]:
                parts.append(f'corte {loc}' + (f' ({"/".join(ws)})' if ws else ''))
            for loc, ws, text in M.doc[a][:1]:
                parts.append(loc + (f' ({"/".join(ws)})' if ws else ''))
            for loc, text in R[a][:1]:
                parts.append(loc)
            parts += ev[a][:2]
            if basis[a] == 'src' and len(M.impl[a]) > 1:
                per_wave = sorted(M.impl[a].items(), key=lambda kv: wnum(kv[0]))
                parts.append('citas por ola: ' + ', '.join(f'{x} {n}' for x, n in per_wave))
            row['evidence'] = clean(f'{asm}; base={basis[a]}; ' + '; '.join(parts), 420)
            # propósito: la mejor línea que lo describe
            purpose = ''
            docs = [d for d in M.doc[a] if d[0].startswith('Client/docs/research/')] + M.doc[a]
            if a in ev_purpose:
                purpose = ev_purpose[a]
            elif docs:
                purpose = docs[0][2]
            elif R[a]:
                purpose = R[a][0][1]
            elif M.impl_loc[a]:
                purpose = M.impl_loc[a][0][3]
            else:
                bits = []
                if F.strings[a]:
                    bits.append('textos: ' + ', '.join(f'"{s[:30]}"' for s in sorted(F.strings[a])[:2]))
                if F.imports[a]:
                    bits.append('usa ' + ', '.join(sorted(F.imports[a])[:3]))
                if callers:
                    bits.append(f'la llama {caller_txt}')
                elif a in dead:
                    bits.append('nadie la llama ni la apunta')
                purpose = '; '.join(bits) or 'sin texto que la describa'
            if a in dead:
                purpose = 'código muerto: ' + purpose
            row['purpose'] = clean(purpose)
            implemented = bool(M.impl[a])
            if a in dead:
                row['implementation_state'] = 'not-applicable'
            elif implemented and (M.cut[a] or a in giants or a in PARTIAL_ONLY):
                row['implementation_state'] = 'partial'
            elif implemented:
                row['implementation_state'] = 'implemented'
            elif wnum(w) >= 9:
                row['implementation_state'] = 'not-started'
            else:
                row['implementation_state'] = 'unknown'
            if a in REG:
                row['analysis_state'] = 'verified'
                row['evidence'] = clean(row['evidence'] + f'; registro-niveles.tsv {REG[a][0]}: {REG[a][1]}', 480)
            elif basis[a] in ('src', 'papel', 'corte', 'doc', 'lectura', 'opcode', 'revisión') or R[a]:
                row['analysis_state'] = 'static'
            else:
                row['analysis_state'] = 'inferred'
            if M.tests[a]:
                row['validation'] = '; '.join(M.tests[a][:2])
            st = row['implementation_state']
            if a in dead:
                row['next_action'] = (f'confirmar en la {w} que nadie la alcanza (ni por un puntero armado en '
                                      f'ejecución)')
            elif st == 'implemented':
                row['next_action'] = 'nada para el mapa; QA y VM aparte'
            elif st == 'partial':
                row['next_action'] = f'completar las ramas cortadas en su ola ({row["consumer_waves"] or w})'
            elif st == 'not-started':
                row['next_action'] = f'portar en la {w}'
            else:
                row['next_action'] = (f'comprobar si la {w} la porta sin citarla; si no, portarla o moverla con '
                                      f'evidencia')
            if row['analysis_state'] == 'inferred':
                row['next_action'] += '; leer el ASM del llamador para confirmar la ola'
        else:
            row['classification'] = 'pending'
            row['primary_wave'] = ''
            row['purpose'] = clean('sin señal: ' + ('nadie la llama' if a in dead else f'la llama {caller_txt}'))
            row['evidence'] = clean(asm, 300)
            row['analysis_state'] = 'pending'
            row['implementation_state'] = 'unknown'
            row['next_action'] = 'leer el .c y el .asm y buscar quién la alcanza (tablas en .data, inicializadores)'
        out_rows.append(r + [row[c] for c in NEW_COLUMNS])
    with open(args.out, 'w', encoding='utf-8', newline='\n') as fh:
        fh.write('\t'.join(header + NEW_COLUMNS) + '\n')
        for r in out_rows:
            fh.write('\t'.join(x.replace('\t', ' ').replace('\n', ' ') for x in r) + '\n')
    # Avance aproximado del código de juego vivo (10-cobertura-roadmap.md, El avance real). En una
    # función partial, la fracción que falta es la de sus citas de corte sobre todas sus citas, y se
    # reparte entre las olas que nombran esos cortes (CL9 en adelante) o queda en la suya.
    prog = {'live_bytes': 0, 'implemented': 0, 'partial_done': 0.0, 'partial_left': collections.Counter(),
            'not_started': collections.Counter(), 'unknown': collections.Counter()}
    for r in out_rows:
        a = int(r[0], 16)
        row = dict(zip(NEW_COLUMNS, r[len(header):]))
        if row['classification'] != 'game' or a in dead:
            continue
        size, st, w = F.size[a], row['implementation_state'], row['primary_wave']
        prog['live_bytes'] += size
        if st == 'implemented':
            prog['implemented'] += size
        elif st == 'not-started':
            prog['not_started'][w] += size
        elif st == 'unknown':
            prog['unknown'][w] += size
        elif st == 'partial':
            ncut, nimp = len(M.cut[a]), sum(M.impl[a].values())
            left = ncut / (ncut + nimp) if ncut + nimp and a not in PARTIAL_ONLY else 0.5
            later = sorted({x for _, ws, _ in M.cut[a] for x in ws if wnum(x) >= 9}, key=wnum) or [w]
            for x in later:
                prog['partial_left'][x] += size * left / len(later)
            prog['partial_done'] += size * (1 - left)
    owed = sum(prog['not_started'].values()) + sum(prog['partial_left'].values())
    unknown = sum(prog['unknown'].values())
    prog['done_pct'] = {f'sin cita x{u}': round(100 * (1 - (owed + u * unknown) / prog['live_bytes']), 1)
                        for u in (0, 0.5, 1)}
    summary = {
        'muret_commit': M.commit,
        'progress': prog,
        'research_inventory_sha256': INV_SHA256,
        'kinds': {k: [stats['kinds'][k], stats['kind_bytes'][k]] for k in stats['kinds']},
        'dead': sorted(f'{a:#x}' for a in dead),
        'giants': sorted(f'{a:#x}' for a in giants),
        'basis': collections.Counter(basis.values()),
    }
    if args.summary:
        with open(args.summary, 'w', encoding='utf-8') as fh:
            json.dump(summary, fh, indent=1, default=list)
    return summary


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--research', required=True, help='clon de EmaNLJN/mu-server-repack-research-102i')
    ap.add_argument('--repo', default='.', help='checkout de MuRet')
    ap.add_argument('--ref', default='HEAD', help='commit de MuRet que se mide')
    ap.add_argument('--out', required=True)
    ap.add_argument('--summary')
    s = build(ap.parse_args())
    print(json.dumps({k: s[k] for k in ('muret_commit', 'kinds', 'basis')}, default=list))


if __name__ == '__main__':
    main()
