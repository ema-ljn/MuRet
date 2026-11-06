#!/usr/bin/env python3
"""Vectores de 0x635ae0 real: 60 casos de 50 frames, con cinco semillas.

Toda expectativa sale de CameraEmu sobre main.exe c32bbd5f; no se importa
camera_reference. Formato: Client/tests/vectors/game/README.md. Uso:
    python -I /ruta/al/repo/Client/docs/research/scripts/game/camera/make_camera_vectors.py salida
Requiere MURET_MAIN_EXE, unicorn y pefile (scripts/README.md, El entorno).
"""
import argparse
import hashlib
import os
from pathlib import Path
import random
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from emu_camera import (  # noqa: E402
    CameraEmu, EXE, CAM_ANG, DIST, DIST_TARGET, FAR, LEVEL, MAP, SCENE,
    SHAKE, SPECIAL_H, TIME, ZOOM, bits, f32,
)

EXE_SHA256 = 'c32bbd5f16ca400c7cfc71fa5f4cd573c57f146dd19a6da52fdd1007abaca26a'
SEEDS = (1, 2, 3, 4, 5)
MAPS = (0, 2, 3, 5, 6, 30)
SCENES = (2, 4, 5)
FRAMES = 50
RECTANGLE_BORDER_FRAMES = 12
LONG_CLOCKS = tuple(struct.unpack('<f', struct.pack('<I', value))[0]
                    for value in (0x4f0d7740, 0x4f2f0344, 0x4f5ac415))
ATTRIBUTES = (0, 0x01, 0x40, 0x80, 0xc0)
EFFECT_TARGET = 0x724588  # 06 § La cámara del juego: el desvío que lee 0x44dc60.
CELL = (143, 120)
DISTANCES = (0.0, 1000.0, 999.99994, 1400.0, 3200.0, 3500.0)
ZOOMS = (-10.0, 0.0, 10.0, 199.99, 200.0, 200.01, 210.0, 220.0)
YAWS = (-180.0, -90.0, -45.0, -36.0, 0.0, 47.25, 135.0)
LEVELS = (0, 1, 2, 3, 4, 5)
FARS = (1750.0, 2000.0, 2750.0, 4000.0)
FIXED_HEIGHTS = (700.0, 1200.0, 1201.0)


def verify_exe():
    digest = hashlib.sha256(Path(EXE).read_bytes()).hexdigest()
    if digest != EXE_SHA256:
        raise SystemExit('MURET_MAIN_EXE: SHA-256 distinto del build c32bbd5f: ' + digest)


def cases():
    for seed in SEEDS:
        for map_index, map_id in enumerate(MAPS):
            for variant in range(2):
                index = map_index * 2 + variant
                # Cada mapa recorre las tres escenas; cada semilla usa los seis mapas.
                yield seed, map_id, SCENES[(seed + index) % len(SCENES)], index


def prepare_camera(camera, rnd, map_id, scene, index):
    e = camera.emu
    e.w32(MAP, map_id)
    e.w32(SCENE, scene)
    values = (DISTANCES[index % len(DISTANCES)], ZOOMS[index % len(ZOOMS)],
              YAWS[index % len(YAWS)], rnd.choice(FARS), rnd.choice(FIXED_HEIGHTS),
              rnd.choice(DISTANCES))
    for address, value in zip((DIST, ZOOM, CAM_ANG + 8, FAR, SPECIAL_H, DIST_TARGET), values):
        e.wf(address, value)
    level = LEVELS[index % len(LEVELS)]
    e.w32(LEVEL, level)
    camera.hero(14350.0, 12050.0, 165.0, *CELL)
    fields = [str(v) for v in (map_id, scene)]
    fields += [bits(v) for v in values[:3]] + [str(level)] + [bits(v) for v in values[3:]]
    return ' '.join(fields)


def hero_position(rnd, frame):
    if frame < RECTANGLE_BORDER_FRAMES:
        # Los cuatro bordes, exactamente sobre ellos y a ±0,01, sin depender del azar.
        edge, offset_index = divmod(frame, 3)
        offset = (-0.01, 0.0, 0.01)[offset_index]
        x, y = ((16100.0 + offset, 20000.0), (19000.0 + offset, 20000.0),
                (17500.0, 18900.0 + offset), (17500.0, 21700.0 + offset))[edge]
    elif rnd.random() < 0.25:
        x, y = rnd.uniform(16100.0, 19000.0), rnd.uniform(18900.0, 21700.0)
    else:
        x, y = rnd.uniform(0.0, 25599.0), rnd.uniform(0.0, 25599.0)
    return tuple(f32(v) for v in (x, y, rnd.uniform(-200.0, 1500.0)))


def frame_input(rnd, frame):
    effect = (0.0, 0.0, 0.0)
    if frame % 3 == 1:
        effect = tuple(f32(rnd.uniform(-500.0, 500.0)) for _ in range(3))
    elif frame % 3 == 2:
        effect = (0.0, 125.0, -75.0)  # Cubre x = 0 con otras componentes distintas de cero.
    hero = hero_position(rnd, frame)
    shake = f32(rnd.choice((0.0, rnd.uniform(-3.0, 3.0))))
    clock = f32(rnd.uniform(0.0, 2e6))
    clock_index = frame - RECTANGLE_BORDER_FRAMES
    if 0 <= clock_index < len(LONG_CLOCKS):
        # P-C29: estos relojes largos comparan contra Unicorn; FSIN x87 nativo
        # puede diferir de su emulación y de std::sin por un ULP.
        clock = LONG_CLOCKS[clock_index]
    return hero, ATTRIBUTES[frame % len(ATTRIBUTES)], shake, clock, frame % 2, effect


def emulate_frame(camera, input_):
    hero, attr, shake, clock, webzen, effect = input_
    # La celda lógica fija aísla el atributo de la posición visual; el arnés conserva
    # su registro protegido entre frames y ejecuta los accesos originales.
    camera.hero(*hero, *CELL, name=b'webzen' if webzen else b'')
    camera.att(*CELL, attr)
    e = camera.emu
    e.wf(SHAKE, shake)
    e.wf(TIME, clock)
    e.wfv(EFFECT_TARGET, effect)
    return camera.frame()


def frame_line(input_, result):
    hero, attr, shake, clock, webzen, effect = input_
    values = [bits(v) for v in hero]
    values += ['%02x' % attr, bits(shake), bits(clock), str(webzen)]
    values += [bits(v) for v in effect]
    values += [bits(v) for v in result['pos']] + [bits(v) for v in result['ang']]
    values += [bits(result[key]) for key in ('zoom', 'dist', 'dist_target')]
    values += [str(result['level'])]
    values += [bits(result[key]) for key in ('far', 'special_h', 'fov', 'near')]
    return 'frame ' + ' '.join(values)


def generate():
    yield 'camera 2'
    for seed, map_id, scene, index in cases():
        rnd = random.Random(seed * 1000 + index)
        camera = CameraEmu()
        initial = prepare_camera(camera, rnd, map_id, scene, index)
        yield 'case %d %s' % (seed, initial)
        for frame in range(FRAMES):
            input_ = frame_input(rnd, frame)
            yield frame_line(input_, emulate_frame(camera, input_))
    yield 'end'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path, help='archivo camera.txt que se genera')
    args = parser.parse_args()
    verify_exe()
    # Un fallo del oráculo no deja vectores parciales.
    text = '\n'.join(generate()) + '\n'
    args.output.write_text(text, encoding='utf-8', newline='\n')
    print('casos 60, frames 3000, SHA-256 ' + hashlib.sha256(text.encode('utf-8')).hexdigest())


if __name__ == '__main__':
    main()
