#!/usr/bin/env python3
r"""Corre cada caso de prueba en el oráculo (emulate_cloth.py, el código real) y en la
reimplementación del contrato (cloth_reference.py), y los compara bit a bit.

    python run_cloth_cases.py      # escribe cloth-cases/<caso>.json (o MURET_CLOTH_CASES) y el resultado

Los casos salen del ejemplo del login (los mismos huesos) cambiando lo que cada rama de 0x50ba30 y
de la tela mira.
"""

import copy
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import cloth_reference  # noqa: E402
import emulate_cloth  # noqa: E402


def frames_from(base, count):
    frames = []
    for k in range(count):
        f = copy.deepcopy(base['frames'][k % len(base['frames'])])
        f['frame_counter_7d3a0f0'] = 1000 + k * 7
        f['globals']['world_time_5767b88'] = 40000.0 + 40.0 * k
        frames.append(f)
    return frames


def cases():
    base = emulate_cloth.example_input(frames=3)
    # Los nombres de GL de las capas de la clase 3 y del monstruo 0x37 (Robe01 a 03), para que el
    # vector distinga la cara de adelante de la de atrás.
    base['texture_gl_names'].update({'0x250': 0x250, '0x251': 0x251, '0x252': 0x252})
    out = {}

    # A. el ejemplo: el Dark Lord del login con las tres telas
    out['a_login_dark_lord'] = base

    # B. la cuarta tela (ArmorClass05, 0x221e): se arma al crear pero la cuenta queda en 3; en el frame 2
    #    se vuelve a armar encima (banderas 0x1406 | 0x1407 y la esfera dos veces) y la cuenta pasa a 4;
    #    con otra armadura se suelta (vtable + 8) y vuelve a 3; al volver, se arma limpia. Sin las alas
    #    0x1c30 la tercera no se simula, salvo con el color de un duelo (1; 0,2; 0) en el último frame.
    b = copy.deepcopy(base)
    b['frames'] = frames_from(base, 6)
    for k, f in enumerate(b['frames']):
        f['character']['armor_238'] = [0x221e, 0x221e, 0x221e, 0x122e, 0x221e, 0x221e][k]
        f['character']['wings_2f8'] = -1
        f['color'] = [1.0, 0.2, 0.0] if k == 5 else [1.0, 1.0, 1.0]
    out['b_fourth_cloth_and_wings'] = b

    # C. la clase 3 (una sola tela, Robe01/Robe02, 0x405: la gravedad por 180), fuera del mapa 0x37,
    #    con el viento base que pasea 0x456b10, y la protección con otro acceso abierto (sin sus rand())
    c = copy.deepcopy(base)
    c['frames'] = frames_from(base, 3)
    for f in c['frames']:
        f['character']['class_1d4'] = 3
        f['globals']['map_6c9dd0'] = 0
        f['frame_counter_lock_depth'] = 1
    c['frames'][0]['globals']['wind_base_7245f0'] = 0.9
    out['c_class3_robe'] = c

    # D. el monstruo 0x37 (Robe03, 0x1001), con la bandera 0x1000 del objeto (dt = 0) en el frame 2 y
    #    la 0x4000 (no simula ni dibuja) en el 3
    d = copy.deepcopy(base)
    d['frames'] = frames_from(base, 4)
    for k, f in enumerate(d['frames']):
        f['character']['class_1d4'] = 1
        f['character']['monster_376'] = 0x37
        f['character']['flags_78'] = [0, 0x1000, 0x4000, 0][k]
    out['d_monster_0x37'] = d

    # E. una tela que se rompe: en el frame 2 los huesos saltan 4000 unidades; un resorte de la bandera 4
    #    pasa de 20 veces su largo, el paso da 0, 0x4fb1a0 suelta las telas y el frame 3 las vuelve a armar
    e = copy.deepcopy(base)
    e['frames'] = frames_from(base, 3)
    for m in e['frames'][1]['bones']:
        m[3] += 4000.0
    out['e_broken'] = e

    # F. el modo directo: banderas que 0x50ba30 no usa
    f = copy.deepcopy(base)
    f['frames'] = frames_from(base, 2)
    for fr in f['frames']:
        fr['globals']['map_6c9dd0'] = 3
        fr['globals']['wind_z_7245fc'] = 0.25
    f['frames'][0]['globals']['wind_base_7245f0'] = 0.3
    f['frames'][1]['dt'] = 0.01
    f['direct'] = [
        # 0x100: el viento sube la z y los resortes x 3; sin la bandera 4 ni la 1 en los resortes;
        # 0x20000000: la fuerza del resorte por largo / 32; arco 2 (y = 0) y el ancho que crece
        {'init': {'bone': 20, 'x': 0.0, 'y': 2.0, 'z': 15.0, 'columns': 4, 'rows': 6, 'width': 40.0,
                  'height': 60.0, 'front': 0x256, 'back': 0x257, 'flags': 0x20000106}, 'wind_bytes': [5, 7],
         'spheres': [[0.0, 10.0, 10.0, 20.0, 17]]},
        # 0x200 con el brillo y la mezcla 0x3000 (ninguna), la gravedad 0x800 (por 100), sin arco ni ancho
        {'init': {'bone': 19, 'x': 3.0, 'y': 0.0, 'z': 10.0, 'columns': 5, 'rows': 4, 'width': 50.0,
                  'height': 50.0, 'front': 0x257, 'back': 0x256, 'flags': 0x40003a00}, 'wind_bytes': [3, 9]},
        # aditiva con el brillo: sin la cara de atrás; la gravedad 0x400 (por 180); arco 3 (y = 20)
        {'init': {'bone': 18, 'x': 0.0, 'y': 0.0, 'z': 0.0, 'columns': 3, 'rows': 3, 'width': 20.0,
                  'height': 20.0, 'front': 0x6ea, 'back': 0x6ea, 'flags': 0x40002407}},
    ]
    out['f_direct_flags'] = f

    # G. las ramas raras: columnas en el mismo punto (el largo mínimo 0,001 de 0x4555d0 y 0x4544d0), filas
    #    en el mismo punto (la restricción con el largo mínimo y el corte por 20 veces el largo), una esfera
    #    sobre una partícula anclada (la distancia mínima 0,01 de 0x4547f0), los huesos que bajan 10 en el
    #    frame 2 (la restricción que separa, largo < 0,8 · reposo), el color nulo (glColor3f(1, 1, 1)), una
    #    textura negativa (0x620780 ata -slot) y los cachés de GL de partida al revés. El frame 1 con dt = 0:
    #    las partículas que arrancan en el mismo punto siguen ahí cuando llegan las restricciones.
    g = copy.deepcopy(base)
    g['frames'] = frames_from(base, 3)
    identity_bone = len(g['frames'][0]['bones'])
    for fr in g['frames']:
        fr['globals']['map_6c9dd0'] = 0x37
        fr['character']['angle_1c'] = [0.0, 0.0, 0.0]
        fr['bones'].append([1.0, 0.0, 0.0, 10.0, 0.0, 1.0, 0.0, 20.0, 0.0, 0.0, 1.0, 30.0])
    g['frames'][0]['globals'].update({'gl_blend_7d366dc': 3, 'gl_alpha_test_7d366cd': 1, 'gl_texture_7d366d5': 0,
                                      'gl_fog_7d368a0': 1, 'gl_cull_7d366cc': 0, 'gl_depth_mask_7d36898': 0,
                                      'bound_texture_6d5d68': 0x256})
    for m in g['frames'][1]['bones']:
        m[11] -= 10.0
    g['frames'][1]['color'] = None
    g['frames'][0]['dt'] = 0.0
    g['direct'] = [
        {'init': {'bone': 20, 'x': 0.0, 'y': 0.0, 'z': 20.0, 'columns': 3, 'rows': 4, 'width': 0.0,
                  'height': 60.0, 'front': -7, 'back': 0x256, 'flags': 0x0000}},
        # todas las partículas en un punto: con los ángulos del objeto en 0 y un hueso que es la identidad
        # más una traslación, la fila 0 anclada cae donde la puso el armado
        {'init': {'bone': identity_bone, 'x': 0.0, 'y': -20.0, 'z': 0.0, 'columns': 3, 'rows': 3, 'width': 0.0,
                  'height': 0.0, 'front': 0x256, 'back': 0x256, 'flags': 0x0}},
        {'init': {'bone': 19, 'x': 0.0, 'y': 0.0, 'z': 0.0, 'columns': 3, 'rows': 5, 'width': 20.0,
                  'height': 40.0, 'front': 0x257, 'back': 0x257, 'flags': 0x2000},
         'spheres': [[0.0, 0.0, 0.0, 5.0, 19], [0.0, -10.0, 0.0, 30.0, 19]]},
    ]
    out['g_direct_edges'] = g

    # H. los ángulos x e y del objeto distintos de 0 (el cos del ángulo 2 de 0x5fc8f0, que queda en
    #    double, entra en las cuentas), con la capa del Dark Lord (ancho que crece y arco) y sus esferas.
    h = copy.deepcopy(base)
    h['frames'] = frames_from(base, 2)
    for fr in h['frames']:
        fr['character']['angle_1c'] = [55.0, 55.0, 188.0]
    h['direct'] = [
        {'init': {'bone': 19, 'x': 0.0, 'y': 8.0, 'z': 10.0, 'columns': 10, 'rows': 10, 'width': 180.0,
                  'height': 180.0, 'front': 0x257, 'back': 0x257, 'flags': 0x1005},
         'spheres': [[-10.0, -10.0, -10.0, 25.0, 17], [10.0, -10.0, -10.0, 25.0, 17],
                     [-10.0, -10.0, 20.0, 27.0, 17], [10.0, -10.0, 20.0, 27.0, 17]]},
    ]
    out['h_direct_angles'] = h

    # I. un resorte estirado entre 10 y 20 veces su largo: el hueso sube 150 de un frame al otro, y la
    #    restricción lo trae sin romper la tela (el corte es a 20 veces).
    i = copy.deepcopy(base)
    i['frames'] = frames_from(base, 2)
    for m in i['frames'][1]['bones']:
        m[11] += 150.0
    i['direct'] = [
        {'init': {'bone': 19, 'x': 0.0, 'y': 0.0, 'z': 0.0, 'columns': 2, 'rows': 3, 'width': 10.0,
                  'height': 20.0, 'front': 0x257, 'back': 0x257, 'flags': 0x1000}},
    ]
    out['i_direct_stretch'] = i
    return out


def main():
    out_dir = os.environ.get('MURET_CLOTH_CASES', 'cloth-cases')
    os.makedirs(out_dir, exist_ok=True)
    failures = 0
    for name, spec in cases().items():
        result = emulate_cloth.run(spec)
        result['input'] = spec
        path = os.path.join(out_dir, name + '.json')
        with open(path, 'w') as fh:
            json.dump(result, fh)
        checked, bad = cloth_reference.compare(result)
        summary = []
        for fr in result['frames']:
            steps = [ev['step'] for ev in fr['events'] if 'step' in ev]
            kinds = sorted({k for ev in fr['events'] for k in ev if k in ('init', 'destroy_0x4fb1a0', 'broken')})
            summary.append('f%d: %d pasos, %d rand(), +0x18c=%s %s' % (
                fr['frame'], len(steps), sum(1 for ev in fr['events'] if 'rand' in ev), fr['object_18c'],
                ','.join(kinds)))
        print('%-26s %6d valores, %d distintos  %s' % (name, checked, len(bad),
                                                      'IGUAL BIT A BIT' if not bad else 'DISTINTO'))
        for line in summary:
            print('      ' + line)
        for line in bad[:10]:
            print('      ! ' + line)
        failures += len(bad)
    sys.exit(1 if failures else 0)


if __name__ == '__main__':
    main()
