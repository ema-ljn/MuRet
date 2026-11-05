#!/usr/bin/env python3
"""Lo que hace el cuervo del login en muchas semillas, con raven_reference.py (que da bit a bit lo mismo
que el paso real emulado): en qué paso salta junto al dueño y, después, a qué distancia, a qué altura,
a qué velocidad y en qué estado vuela. Uso: raven_stats.py [primera semilla] [semillas] [pasos]."""
import math
import os
import statistics
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import raven_reference as rr  # noqa: E402

OWNER = (10603.0, 14112.0, 580.0)


def percentile(values, q):
    ordered = sorted(values)
    return ordered[int(q * (len(ordered) - 1))]


def main():
    first = int(sys.argv[1]) if len(sys.argv) > 1 else 1
    seeds = int(sys.argv[2]) if len(sys.argv) > 2 else 40
    steps = int(sys.argv[3]) if len(sys.argv) > 3 else 5000
    jumps = []
    distance, height, speed, slow = [], [], [], 0
    after = 0
    for seed in range(first, first + seeds):
        rand = rr.Rand(seed)
        o = {'pos': [50.0, 50.0, rr.START_Z], 'ang': [0.0, 0.0, 0.0], 'dir': [0.0, 0.0, 0.0], 'state': 0,
             'action': 0, 'frame': 0.0, 'counter': 0}
        jump = None
        previous = list(o['pos'])
        for k in range(1, steps + 1):
            rr.step(o, OWNER, rand, 0x12345677, [], [])
            p = o['pos']
            if jump is None and math.hypot(p[0] - OWNER[0], p[1] - OWNER[1]) < 1 and abs(p[2] - 830.0) < 1e-3:
                jump = k
            elif jump is not None and k > jump + 50:
                distance.append(math.hypot(p[0] - OWNER[0], p[1] - OWNER[1]))
                height.append(p[2] - OWNER[2])
                speed.append(math.dist(p, previous))
                slow += o['state'] == 1
                after += 1
            previous = list(p)
        jumps.append(jump)
    print('semillas', first, 'a', first + seeds - 1, 'pasos', steps)
    print('salto: pasos %d a %d (%.1f a %.1f s), mediana %d' % (
        min(jumps), max(jumps), min(jumps) * 0.04, max(jumps) * 0.04, statistics.median(jumps)))
    print('distancia horizontal: mediana %.0f, p5 %.0f, p95 %.0f, máximo %.0f' % (
        statistics.median(distance), percentile(distance, 0.05), percentile(distance, 0.95), max(distance)))
    print('altura sobre el dueño: mediana %.0f, p5 %.0f, p95 %.0f' % (
        statistics.median(height), percentile(height, 0.05), percentile(height, 0.95)))
    print('velocidad por paso: mediana %.1f, p95 %.1f' % (statistics.median(speed), percentile(speed, 0.95)))
    print('estado lento (1): %.0f %%' % (100.0 * slow / after))


if __name__ == '__main__':
    main()
