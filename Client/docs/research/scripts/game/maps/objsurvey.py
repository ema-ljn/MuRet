#!/usr/bin/env python3
"""Los objetos de cada mundo del Data\\: cuántos registros trae el .obj, cuántos tipos, en qué carpeta
Object<N> están sus modelos y qué tipos no tienen modelo.

El modelo de un tipo es el de Lorencia por nombre en el mundo 1 (../world0/lorencia_models.py) y
Object<k+1>.bmd en los demás (el lazo genérico de 0x61a0c4..0x61a124). Los tipos de 160 en adelante
salen aparte: no entran en la tabla de los 160 del mundo. Es la lista de "tipos sin modelo" de la
CL7.5 A (tests/vectors/worlds/missing-models.txt).

Uso: objsurvey.py [mundo ...]      (los números de carpeta; por defecto los del Data\\ del laboratorio)
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..'))
sys.path.insert(0, os.path.join(HERE, '..', 'world0'))
import common  # noqa: E402
import lorencia_models  # noqa: E402

WORLDS = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 19, 25, 31, 32, 34, 35, 55, 56]


def object_folder(world):
    root = common.data_dir()
    for f in os.listdir(root):
        if f.lower() == 'object%d' % world:
            return os.path.join(root, f)
    return None


def main():
    worlds = [int(w) for w in sys.argv[1:]] or WORLDS
    for w in worlds:
        if common.find('world%d' % w, 'EncTerrain%d.obj' % w) is None:
            print('World%-2d sin .obj' % w)
            continue
        _, objs = common.load_obj(w)
        types = sorted(set(o['type'] for o in objs))
        folder = object_folder(w)
        have = {x.lower() for x in os.listdir(folder)} if folder else set()
        missing = []
        for t in types:
            name = lorencia_models.bmd(t) if w == 1 else 'Object%02d.bmd' % (t + 1)
            if name is None or name.lower() not in have:
                missing.append(t)
        big = [t for t in types if t >= 160]
        print('World%-2d objetos=%4d tipos=%3d carpeta=%s sin_modelo=%s tipos>=160=%s' % (
            w, len(objs), len(types), os.path.basename(folder) if folder else None, missing, big))


if __name__ == '__main__':
    main()
