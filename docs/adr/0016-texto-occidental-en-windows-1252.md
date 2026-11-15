# ADR 0016: entrada y dibujo occidental con el perfil Windows-1252 de XP

Fecha: 2026-11-15. Estado: propuesto junto con los cambios de entrada y rasterizador del perfil occidental
del laboratorio. Responde al pedido de Emanuel de reproducir el original y analizar su codificación
histórica. Actualiza la parte de
decodificación para dibujo de D-C12 y fija el encoder del perfil CP1252 observado en XP.
No define una conversión universal para cualquier Windows o IME.

## Contexto

MuRet pedía a FreeType el punto de código con el mismo número que cada byte, como Latin-1.
Q-C67 justificaba la elección con Text.bmd. Ese archivo no contiene bytes `80..9f`, por lo que
su corpus no distingue Latin-1 de CP1252. La lectura pasiva del original en XP encontró ACP y
CRT multibyte 1252. El EDIT nativo convirtió `€` a `80` al pegar.

Una prueba propia de GDI en ese mismo XP, con los argumentos de fuente del original, resolvió
la petición GB2312 a Arial occidental. Su byte ANSI `80` seleccionó el glifo U+20AC, mientras
U+0080 no tenía glifo. La procedencia, hashes y límites están en
[12-codificacion](../../Client/docs/research/12-codificacion.md).

## Decisión

El rasterizador del perfil occidental decodifica los bytes como Windows-1252 al elegir los
glifos Unicode de FreeType. ASCII y `a0..ff` conservan su correspondencia. `80..9f` usa la tabla
comprobada en XP, incluidos los valores de control sin asignación gráfica.

El texto almacenado, los activos y los paquetes mantienen sus bytes y largos. No se normalizan
acentos ni se recodifica el wire. La tabla de 32 posiciones es pequeña y no incorpora una
dependencia nueva. La regresión usa una fuente sintética con un glifo U+20AC diferente de
`.notdef`, para que falle con la interpretación Latin-1 anterior sin depender de fuentes del host.

## Alcance y consecuencias

Se corrige la interpretación occidental del render; no se afirma soporte general DBCS ni
igualdad de píxeles GDI/FreeType. La selección efectiva de fuente en el proceso necesita su
captura propia. Text.bmd conserva su apariencia porque sus bytes presentes coinciden entre
ambas páginas.

La frontera UTF-8 del EDIT occidental se convierte con el perfil XP: correspondencias directas,
441 aproximaciones best fit y sustitución predeterminada, sin normalización. Un escalar
suplementario pasa por sus dos unidades UTF-16 y produce dos sustituciones. El pegado corta
antes de CR/NUL, conserva LF y respeta el límite en bytes. Un fallo de conversión no modifica
la selección; un pegado válido vacío sí la reemplaza, como el EDIT nativo.

Se reutiliza el lector UTF-8 ya existente de PathText. Se consultaron SDL, utf8cpp, utf8proc y
simdutf: SDL pertenece al adaptador de plataforma; las otras opciones agregaban una dependencia
para un lector corto ya disponible y no garantizaban el best fit histórico. Las tablas XP se
contrastan con el oráculo BMP y vectores de secuencias, separados del decoder del render.
El perfil se aplica al EDIT viejo de Agregar, al mensaje (50 bytes), al destinatario del susurro
(10 bytes) y al nombre de personaje (10 bytes). `4691e0` no activa un filtro que descarte Euro en
los dos campos de chat; el research del nombre también fija entrada sin filtro. Sus validaciones
posteriores de creación se conservan, incluido el chequeo histórico de bytes `80..af` en posiciones
pares. Login y códigos que usan Ascii conservan ese filtro; el enum Latin1 mantiene su contrato
para otros lectores, sin presentarlo como el perfil de esos cuatro campos.

La conversión compartida no completa el editor del chat común: su enrutamiento todavía omite
navegación, clipboard y composición. H10 (chats de amigos/cartas), modos IMM y perfiles de otras
páginas requieren su implementación y evidencia propias. El servidor actual no permite validar
un alta de amigo exitosa y no se modifica para esta reparación.
