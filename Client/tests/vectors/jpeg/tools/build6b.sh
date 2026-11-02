#!/bin/sh
# Arma la IJG libjpeg 6b como la del main.exe, fuera de CI, para sacar lo que da la 6b de un JPEG:
# baja el tarball, mira su SHA-256 y compila la biblioteca con INT32 de 32 bits, más hash6b y
# cmyk6b (el README.md de los vectores). Uso: build6b.sh <carpeta de trabajo>
set -eu

work=$1
tools=$(cd "$(dirname "$0")" && pwd)

# La 6b del archivo de Ubuntu: la biblioteca idéntica a la 6b de la IJG (27-Mar-1998), con otro
# build (su README.6b1).
url=http://archive.ubuntu.com/ubuntu/pool/main/libj/libjpeg6b/libjpeg6b_6b1.orig.tar.gz
sha256=dbe673129e69eaaed47e3cfccf86b1582fa19862a3ce8189ea8f8cfc1800d9aa

mkdir -p "$work"
cd "$work"
[ -f libjpeg6b_6b1.orig.tar.gz ] || curl -fsSLO "$url"
echo "$sha256  libjpeg6b_6b1.orig.tar.gz" | sha256sum -c -
rm -rf jpeg-6b1 lib
tar xzf libjpeg6b_6b1.orig.tar.gz
mkdir lib

# jconfig.h, el de jconfig.doc para un compilador de C actual. El main.exe es de 32 bits: INT32 es
# de 32 bits, como el long de MSVC 6. jmorecfg.h solo deja cambiarlo si XMD_H está definido, y
# entonces también hay que dar INT16.
cat > lib/jconfig.h <<'EOF'
#define HAVE_PROTOTYPES
#define HAVE_UNSIGNED_CHAR
#define HAVE_UNSIGNED_SHORT
#define HAVE_STDDEF_H
#define HAVE_STDLIB_H
#undef NEED_BSD_STRINGS
#undef NEED_SYS_TYPES_H
#undef NEED_FAR_POINTERS
#undef NEED_SHORT_EXTERNAL_NAMES
#undef INCOMPLETE_TYPES_BROKEN
#undef RIGHT_SHIFT_IS_UNSIGNED
#define XMD_H
typedef int INT32;
typedef short INT16;
EOF

# Las fuentes de la biblioteca (LIBSOURCES y SYSDEPSOURCES de makefile.cfg, con jmemnobs.c). Con
# -fwrapv, una suma o un producto de enteros con signo que desborda da la vuelta, como en x86.
sources="jcapimin jcapistd jccoefct jccolor jcdctmgr jchuff jcinit jcmainct jcmarker jcmaster
jcomapi jcparam jcphuff jcprepct jcsample jctrans jdapimin jdapistd jdatadst jdatasrc jdcoefct
jdcolor jddctmgr jdhuff jdinput jdmainct jdmarker jdmaster jdmerge jdphuff jdpostct jdsample
jdtrans jerror jfdctflt jfdctfst jfdctint jidctflt jidctfst jidctint jidctred jquant1 jquant2
jutils jmemmgr jmemnobs"
cflags="-O2 -fwrapv -w -Ilib -Ijpeg-6b1"
for s in $sources; do
    cc $cflags -c "jpeg-6b1/$s.c" -o "lib/$s.o"
done
ar rcs lib/libjpeg6b.a lib/*.o

cc $cflags "$tools/hash6b.c" lib/libjpeg6b.a -o hash6b
cc $cflags "$tools/cmyk6b.c" lib/libjpeg6b.a -o cmyk6b
echo "listo: $work/hash6b y $work/cmyk6b"
