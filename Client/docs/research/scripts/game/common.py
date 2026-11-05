"""Lo que comparten los scripts del juego: dónde está cada entrada y los lectores del Data\\.

Cada entrada sale de una variable de entorno (ver ../README.md, El entorno):
- MURET_MAIN_EXE: el main.exe c32bbd5f;
- MURET_CLIENT_DATA: el Data\\ del cliente; sin ella, Client/assets/Data del repo;
- MURET_CLIENT_GL_TRACE_DIR: las trazas de GLIntercept (lorencia-Frame_016215.txt, ...);
- MURET_CLIENT_WIRE_DIR: el cable (muproxy.log) y las claves del servidor (Dec1.dat, Enc2.dat);
- MURET_CLIENT_MEMORY_DIR: los volcados de memoria, una carpeta por captura.

Importarlo agrega a sys.path la carpeta de scripts/ (emulate_cloth.py) y la de tools/ (cg.py). Con
`python -I` la carpeta del script no entra sola en sys.path: cada script agrega la de game/ antes de
importar este módulo.
"""
import os
import struct
import sys

GAME = os.path.dirname(os.path.abspath(__file__))
SCRIPTS = os.path.dirname(GAME)
LOGIN = os.path.join(SCRIPTS, 'login')
TOOLS = os.path.join(SCRIPTS, 'tools')
REPO = os.path.abspath(os.path.join(SCRIPTS, '..', '..', '..', '..'))
DEFAULT_DATA = os.path.join(REPO, 'Client', 'assets', 'Data')
LORENCIA_TRACE = 'lorencia-Frame_016215.txt'
LORENCIA_DUMP = '2026-10-09-lorencia'

for _path in (SCRIPTS, TOOLS):
    if _path not in sys.path:
        sys.path.insert(0, _path)


def _need(var, what):
    value = os.environ.get(var)
    if not value:
        sys.exit('falta %s (%s): ver Client/docs/research/scripts/README.md, El entorno' % (var, what))
    return value


def _need_file(path, var):
    if not os.path.isfile(path):
        sys.exit('no está %s (de %s)' % (path, var))
    return path


def main_exe():
    return _need_file(_need('MURET_MAIN_EXE', 'el main.exe c32bbd5f'), 'MURET_MAIN_EXE')


def data_dir():
    return os.environ.get('MURET_CLIENT_DATA') or DEFAULT_DATA


def gl_trace(name=LORENCIA_TRACE):
    return _need_file(os.path.join(_need('MURET_CLIENT_GL_TRACE_DIR', 'las trazas de GL'), name),
                      'MURET_CLIENT_GL_TRACE_DIR')


def wire_file(name='muproxy.log'):
    return _need_file(os.path.join(_need('MURET_CLIENT_WIRE_DIR', 'el cable y las claves'), name),
                      'MURET_CLIENT_WIRE_DIR')


def memory_file(name, capture=LORENCIA_DUMP):
    return _need_file(os.path.join(_need('MURET_CLIENT_MEMORY_DIR', 'los volcados de memoria'), capture, name),
                      'MURET_CLIENT_MEMORY_DIR')


def use_login_harness():
    """Deja importar los módulos de scripts/login (emu, draw_emu, world, bmd) con el mismo Data\\."""
    main_exe()
    os.environ.setdefault('MURET_CLIENT_DATA', data_dir())
    if LOGIN not in sys.path:
        sys.path.insert(0, LOGIN)


# --- el Data\ ------------------------------------------------------------------------------------
K16 = bytes([0xD1, 0x73, 0x52, 0xF6, 0xD2, 0x9A, 0xCB, 0x27, 0x3E, 0xAF, 0x59, 0x31, 0x37, 0xB3, 0xE7, 0xA2])
XOR3 = bytes([0xFC, 0xCF, 0xAB])


def k16(data):
    """El descifrado de los .obj, .map y .att (05, la clave de 16 bytes con el encadenado de 0x5E)."""
    out = bytearray(len(data))
    prev = 0x5E
    for i, c in enumerate(data):
        out[i] = ((K16[i % 16] ^ c) - prev) & 0xff
        prev = (c + 0x3D) & 0xff
    return bytes(out)


def xor3(data):
    return bytes(b ^ XOR3[i % 3] for i, b in enumerate(data))


def find(folder, name):
    """La ruta de Data\\<carpeta>\\<archivo> sin mirar mayúsculas (World7 y world8 conviven), o None."""
    root = data_dir()
    for f in os.listdir(root):
        if f.lower() == folder.lower():
            d = os.path.join(root, f)
            for g in os.listdir(d):
                if g.lower() == name.lower():
                    return os.path.join(d, g)
    return None


def need(folder, name):
    path = find(folder, name)
    if path is None:
        sys.exit('no está %s\\%s en %s' % (folder, name, data_dir()))
    return path


def load_obj(world):
    """EncTerrain<N>.obj: (versión, mapa, cantidad) y cada registro (tipo, posición, ángulo, escala)."""
    with open(need('world%d' % world, 'EncTerrain%d.obj' % world), 'rb') as f:
        b = k16(f.read())
    ver, w, n = b[0], b[1], struct.unpack_from('<h', b, 2)[0]
    objs = []
    for i in range(n):
        o = 4 + 30 * i
        t = struct.unpack_from('<h', b, o)[0]
        v = struct.unpack_from('<7f', b, o + 2)
        objs.append(dict(i=i, type=t, pos=v[0:3], ang=v[3:6], scale=v[6]))
    return (ver, w, n), objs


def load_att(world):
    """EncTerrain<N>.att (K16 y XOR3): la cabecera de 4 bytes y un valor por celda (WORD o byte)."""
    with open(need('world%d' % world, 'EncTerrain%d.att' % world), 'rb') as f:
        b = xor3(k16(f.read()))
    header, body = b[:4], b[4:]
    if len(body) == 65536:
        return header, list(body)
    return header, list(struct.unpack('<65536H', body[:0x20000]))


def att_cells(world):
    """El byte bajo de cada celda del .att, que es lo que lee el camino (0x01, 0x02, 0x04, 0x08...)."""
    header, values = load_att(world)
    return header, [v & 0xff for v in values]


def load_heights(world, scale=1.5):
    """TerrainHeight.OZB: los 65.536 bytes desde 0x43c, por la escala (1,5 en el juego; 3 en el 56)."""
    with open(need('world%d' % world, 'TerrainHeight.OZB'), 'rb') as f:
        raw = f.read()[0x43c:0x43c + 65536]
    return [b * scale for b in raw]


# --- las trazas de GL -------------------------------------------------------------------------------
def trace_frame(path, k=0):
    """El pase del mundo del frame k de una traza: desde el último glClearColor antes del primer
    glBegin del frame (el color del mapa, 0x637404; el primero es el negro de 0x637700) hasta el
    wglSwapBuffers que lo cierra, sin incluirlo. Con la de Lorencia y k = 0 son las líneas 59 a
    126.902 de la traza, el "frame1.txt" del barrido: los números de línea que dan los scripts son
    de ahí (la línea 1 es el glClearColor)."""
    frame = []
    index = 0
    with open(path, encoding='utf-8', errors='replace') as f:
        for line in f:
            line = line.rstrip('\n')
            if line.startswith('wglSwapBuffers'):
                if index == k:
                    break
                index += 1
                frame = []
                continue
            if index == k:
                frame.append(line)
    if index != k or not frame:
        sys.exit('la traza %s no tiene el frame %d' % (path, k))
    first_begin = next((i for i, l in enumerate(frame) if l.startswith('glBegin')), len(frame))
    clears = [i for i, l in enumerate(frame[:first_begin]) if l.startswith('glClearColor')]
    return frame[clears[-1] if clears else 0:]
