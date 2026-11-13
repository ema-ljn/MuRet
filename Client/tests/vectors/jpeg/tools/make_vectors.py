# Arma los vectores JPEG de CI (el README.md de los vectores): imágenes sintéticas, codificadas con
# el cjpeg de libjpeg-turbo 3.2.0, salvo las CMYK, que escribe cmyk6b, y decodificadas con hash6b,
# que decodifica con la 6b como el original. hash6b, cmyk6b y shot6b salen de build6b.sh; hashturbo
# es hash6b compilado contra libjpeg-turbo, para el único vector que trae lo que da turbo. Las
# capturas (screenshot-*.6b.jpg) las escribe shot6b con el compresor de la 6b, como 0x63b150.
#
# Uso: python3 make_vectors.py <cjpeg de turbo> <hash6b> <cmyk6b> <hashturbo> <shot6b> <carpeta de salida>
import os
import struct
import subprocess
import sys
import tempfile

CJPEG, HASH6B, CMYK6B, HASHTURBO, SHOT6B, OUT = sys.argv[1:7]


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


def hash6b(path, raw=None, tool=HASH6B):
    # La línea de hash6b: "archivo ancho alto componentes fnv" o "archivo ERR código".
    command = [tool] + (['-raw', raw] if raw else []) + [path]
    return subprocess.run(command, check=True, capture_output=True, text=True).stdout.split()[1:]


def without_dht(jpeg, first_scan=1):
    # `jpeg` sin los segmentos DHT (FF C4), los que definen las tablas de Huffman, que van antes
    # del scan `first_scan` (desde 1) o de uno que le sigue. Recorre los segmentos uno por uno; en
    # los datos de un scan, un FF seguido de 00 (el FF de los datos), de otro FF (relleno) o de
    # D0 a D7 (un reinicio) no es un segmento.
    out, i, scans = bytearray(jpeg[:2]), 2, 0
    while i < len(jpeg):
        following = jpeg[i + 1] if i + 1 < len(jpeg) else 0
        if jpeg[i] != 0xFF or following in (0x00, 0xFF) or 0xD0 <= following <= 0xD7:
            out.append(jpeg[i])
            i += 1
            continue
        if following == 0xD9:
            out += jpeg[i:]
            break
        length = struct.unpack('>H', jpeg[i + 2:i + 4])[0]
        if following != 0xC4 or scans + 1 < first_scan:
            out += jpeg[i:i + 2 + length]
        if following == 0xDA:
            scans += 1
        i += 2 + length
    return bytes(out)


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
    cjpeg('mixed-sampling', image('detailed-16x16', 16, 16, detailed), '-sample', '2x2,2x1,1x1')
    cjpeg('mixed-sampling-narrow', image('detailed-4x16', 4, 16, detailed),
          '-sample', '2x2,2x1,1x1')

    # Los que la 6b no lee.
    cjpeg('arithmetic', image('detailed-16x8', 16, 8, detailed),
          '-sample', '1x1', '-arithmetic')
    cjpeg('lossless', image('detailed-16x8', 16, 8, detailed), '-lossless', '1')
    cjpeg('precision-12', image('detailed-16x8', 16, 8, detailed),
          '-sample', '1x1', '-precision', '12')
    open(os.path.join(OUT, 'no-huffman-tables.jpg'), 'wb').write(without_dht(whole))
    # Dos scans: el Y, con las tablas 0, y el Cb con el Cr, con las 1, que van antes del segundo.
    scans = os.path.join(images, 'two-scans.txt')
    open(scans, 'w').write('0: 0-63, 0, 0;\n1 2: 0-63, 0, 0;\n')
    two = cjpeg('two-scans', image('detailed-16x16', 16, 16, detailed),
                '-sample', '1x1', '-scans', scans)
    open(os.path.join(OUT, 'no-huffman-tables-scan-2.jpg'), 'wb').write(
        without_dht(open(two, 'rb').read(), first_scan=2))
    os.remove(two)

    # Los que se usan por su tamaño.
    for name, width, height in [('flat-512x8', 512, 8), ('flat-513x8', 513, 8),
                                ('flat-8x513', 8, 513)]:
        cjpeg(name, image(name, width, height, flat), '-sample', '1x1')
    cjpeg('light-256', image('blocks-256x256', 256, 256, blocks), '-sample', '1x1')
    cjpeg('light-256x255', image('blocks-256x255', 256, 255, blocks), '-sample', '1x1')
    cjpeg('light-255x256', image('blocks-255x256', 255, 256, blocks), '-sample', '1x1')
    cjpeg('light-gray-256', image('blocks-256x256', 256, 256, blocks, gray=True))
    subprocess.run([CMYK6B, '4', '2', os.path.join(OUT, 'cmyk-4x2.jpg')], check=True)

# Las capturas de Impr Pant, con la calidad 100 de 0x6202e0: la imagen `detailed`, de un ancho y un
# alto que no son múltiplos de 16 (el borde del 4:2:0 de jpeg_set_defaults) y de unos que sí, en
# alto (40 × 24).
for width, height in [(13, 11), (40, 24)]:
    name = f'screenshot-{width}x{height}.6b.jpg'
    subprocess.run([SHOT6B, str(width), str(height), '100', os.path.join(OUT, name)], check=True)

# Los .6b.raw, y la línea de hash6b de cada uno; de los que no tienen, la línea sola.
for name in ['baseline-444-dri', 'baseline-420', 'baseline-440', 'progressive-444', 'gray',
             'truncated', 'cmyk-3x2', 'mixed-sampling', 'mixed-sampling-narrow']:
    print(name, *hash6b(os.path.join(OUT, name + '.jpg'), os.path.join(OUT, name + '.6b.raw')))
for name in ['arithmetic', 'lossless', 'precision-12', 'no-huffman-tables',
             'no-huffman-tables-scan-2', 'cmyk-4x2']:
    print(name, *hash6b(os.path.join(OUT, name + '.jpg')))
# Lo que da turbo, en el vector en que no es la 6b (Q-C33).
path, raw = (os.path.join(OUT, 'mixed-sampling' + end) for end in ('.jpg', '.turbo.raw'))
print('mixed-sampling turbo', *hash6b(path, raw, HASHTURBO))
