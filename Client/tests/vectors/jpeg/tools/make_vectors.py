# Arma los vectores JPEG de CI (el README.md de los vectores): imágenes sintéticas, codificadas con
# el cjpeg de libjpeg-turbo 3.2.0, salvo las CMYK, que escribe cmyk6b, y decodificadas con hash6b,
# que decodifica con la 6b como el original. hash6b y cmyk6b salen de build6b.sh.
#
# Uso: python3 make_vectors.py <cjpeg de turbo> <hash6b> <cmyk6b> <carpeta de salida>
import os
import struct
import subprocess
import sys
import tempfile

CJPEG, HASH6B, CMYK6B, OUT = sys.argv[1:5]


def detailed(x, y):
    return ((47 * x + 13 * y) & 255, (x * x + 3 * y * y) & 255, (7 * x * y + 91) & 255)


def flat(x, y):
    return (128, 64, 192)


def blocks(x, y):
    return ((8 * (x // 8)) & 255, (8 * (y // 8)) & 255, (128 + x // 8 + y // 8) & 255)


def pnm(folder, name, width, height, pixel, gray=False):
    # Un PPM (o un PGM, con solo el rojo) de 8 bits, la entrada de cjpeg.
    path = os.path.join(folder, name + ('.pgm' if gray else '.ppm'))
    with open(path, 'wb') as out:
        out.write(f'{"P5" if gray else "P6"}\n{width} {height}\n255\n'.encode())
        for y in range(height):
            for x in range(width):
                rgb = pixel(x, y)
                out.write(bytes(rgb[:1] if gray else rgb))
    return path


def cjpeg(name, source, *options):
    path = os.path.join(OUT, name + '.jpg')
    subprocess.run([CJPEG, '-quality', '75', *options, '-outfile', path, source], check=True)
    return path


def hash6b(path, raw=None):
    # La línea de hash6b: "archivo ancho alto componentes fnv" o "archivo ERR código".
    command = [HASH6B] + (['-raw', raw] if raw else []) + [path]
    return subprocess.run(command, check=True, capture_output=True, text=True).stdout.split()[1:]


def data_middle(jpeg):
    # Dónde cortar `jpeg` en la mitad de sus datos: después de la cabecera del primer scan, la
    # mitad de lo que queda sin contar el FF D9 del final.
    i = 2
    while True:
        marker = jpeg[i + 1]
        length = struct.unpack('>H', jpeg[i + 2:i + 4])[0]
        if marker == 0xDA:
            start = i + 2 + length
            return start + (len(jpeg) - 2 - start) // 2
        i += 2 + length


with tempfile.TemporaryDirectory() as images:
    def image(name, width, height, pixel, gray=False):
        return pnm(images, name, width, height, pixel, gray)

    # Los que tienen .6b.raw.
    cjpeg('baseline-444-dri', image('detailed-16x24', 16, 24, detailed),
          '-sample', '1x1', '-restart', '1')
    cjpeg('baseline-420', image('detailed-13x11', 13, 11, detailed), '-sample', '2x2')
    cjpeg('baseline-440', image('detailed-16x16', 16, 16, detailed), '-sample', '1x2')
    cjpeg('progressive-444', image('detailed-16x16', 16, 16, detailed),
          '-sample', '1x1', '-progressive')
    cjpeg('gray', image('detailed-8x8', 8, 8, detailed, gray=True))
    whole = open(os.path.join(OUT, 'baseline-444-dri.jpg'), 'rb').read()
    open(os.path.join(OUT, 'truncated.jpg'), 'wb').write(whole[:data_middle(whole)])
    subprocess.run([CMYK6B, '3', '2', os.path.join(OUT, 'cmyk-3x2.jpg')], check=True)

    # Los que la 6b no lee.
    cjpeg('arithmetic', image('detailed-16x8', 16, 8, detailed),
          '-sample', '1x1', '-arithmetic')
    cjpeg('lossless', image('detailed-16x8', 16, 8, detailed), '-lossless', '1')
    cjpeg('precision-12', image('detailed-16x8', 16, 8, detailed),
          '-sample', '1x1', '-precision', '12')

    # Los que se usan por su tamaño.
    for name, width, height in [('flat-512x8', 512, 8), ('flat-513x8', 513, 8),
                                ('flat-8x513', 8, 513)]:
        cjpeg(name, image(name, width, height, flat), '-sample', '1x1')
    cjpeg('light-256', image('blocks-256x256', 256, 256, blocks), '-sample', '1x1')
    cjpeg('light-256x255', image('blocks-256x255', 256, 255, blocks), '-sample', '1x1')
    cjpeg('light-gray-256', image('blocks-256x256', 256, 256, blocks, gray=True))
    subprocess.run([CMYK6B, '4', '2', os.path.join(OUT, 'cmyk-4x2.jpg')], check=True)

# Los .6b.raw, y la línea de hash6b de cada uno; de los que no tienen, la línea sola.
for name in ['baseline-444-dri', 'baseline-420', 'baseline-440', 'progressive-444', 'gray',
             'truncated', 'cmyk-3x2']:
    print(name, *hash6b(os.path.join(OUT, name + '.jpg'), os.path.join(OUT, name + '.6b.raw')))
for name in ['arithmetic', 'lossless', 'precision-12', 'cmyk-4x2']:
    print(name, *hash6b(os.path.join(OUT, name + '.jpg')))
