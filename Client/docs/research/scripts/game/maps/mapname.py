#!/usr/bin/env python3
"""El nombre de cada mapa, con el código real: 0x5f0b40(mapa) para los 256, que devuelve un puntero a un
texto de Text.bmd (la tabla de 0x76bd1dc, de a 300 bytes). Da el número de texto de cada mapa, o el
puntero si no cae en la tabla.

Es el nombre del mensaje de llegada ("<mapa>" + el texto 484, en 0x555190) que manda el cambio de mapa
(06, Los cambios de mapa; la CL7.5 D): el 30 + mapa del 0 al 9, el 55 en el 10, el 56 del 11 al 17, ...
Usa el arnés del login (scripts/login/emu.py); el texto mismo no se lee (Text.bmd no se carga).

Uso: mapname.py      (con MURET_MAIN_EXE)
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import common  # noqa: E402

common.use_login_harness()
from emu import Emu  # noqa: E402

MAP_NAME, TEXTS, TEXT_SIZE = 0x5f0b40, 0x76bd1dc, 300


def main():
    e = Emu()
    for m in range(256):
        p = e.call(MAP_NAME, [m])
        d = p - TEXTS
        print(m, d // TEXT_SIZE if d % TEXT_SIZE == 0 else ('?', hex(p)))


if __name__ == '__main__':
    main()
