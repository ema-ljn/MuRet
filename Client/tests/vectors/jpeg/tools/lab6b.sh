#!/bin/sh
# Escribe lab-6b.txt: lo que da la 6b de cada OZJ de la carpeta golden, una línea por archivo,
# ordenadas por ruta (el README.md de los vectores). De los archivos del juego solo salen la ruta,
# el tamaño, los componentes y un hash.
# Uso: lab6b.sh <hash6b> <carpeta golden> > lab-6b.txt
set -eu

hash6b=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
cd "$2"
find . -type f -iname '*.ozj' | sed 's|^\./||' | LC_ALL=C sort | while IFS= read -r path; do
    "$hash6b" -skip 24 "$path"
done
