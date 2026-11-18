# CL9: contraste de Castle Siege, Crywolf y cierres compartidos en la VM

Pedido de Emanuel: el comportamiento se fija con ASM/pseudocódigo, con prioridad del ASM;
la VM aporta la evidencia dinámica que una prueba del puerto no puede producir.
Este documento prepara esa validación. **No registra pruebas ya ejecutadas en la VM.**

Leer `AGENTS.md`, `docs/CODING_RULES.md`, el
[plan de cierre](../../../../../../docs/superpowers/plans/2026-11-18-cl9-cierre-asm.md),
el [runbook H1/H9](RUNBOOK-cl9-vm.md) y el [uso de los lectores](README.md).
La implementación y los tests del candidato se revisan en el PR; no usar el verde de otra cabeza.

## Identidad y entorno

- Original: `main.exe` PE32 i386, ImageBase `0x00400000`, SHA-256
  `c32bbd5f16ca400c7cfc71fa5f4cd573c57f146dd19a6da52fdd1007abaca26a`.
  Verificar el archivo completo y la base cargada antes de aplicar direcciones.
- Anotar `git rev-parse HEAD` de MuRet, plataforma, compilador y SHA del ejecutable propio.
  El original corre en XP; MuRet SDL3/GL3.3 se prueba en el host moderno compatible.
- Preservar `Data` original y verificar su manifiesto. No reemplazar texturas/modelos faltantes
  por archivos ficticios para hacer pasar una comparación.
- Guardar capturas, volcados, credenciales y tráfico fuera de Git. Publicar sólo hashes,
  descripción de casos y resultados saneados. El tráfico puede contener datos de la cuenta.
- Registrar servidor/build, configuración del evento, clase, equipo, gremio/alianza/cargo,
  mapa y estado inicial. Usar cuentas de laboratorio.

Castle Siege necesita un servidor compatible con `CityWar` habilitado y cargos reales.
El servidor del laboratorio documentado no implementa Crywolf: en ese entorno sus recorridos
se marcan `BLOCKED: servidor incompatible`, no `PASS`. Para observarlos hace falta un entorno
que los produzca normalmente. Una fixture o un paquete sintético valida el puerto, pero no
se convierte en una observación del original.

## Prioridad 1: la posición indeterminada del polvo

El [research de los cuerpos del asedio](../../../2026-11-18-h13-siege-body-chain.md)
documenta las llamadas a partículas `5f3` desde los fragmentos. En las rutas
`529516..529520` y `525302..525310`, el original pasa `LEA [EBP-28]` sin haber
escrito los tres floats de esa posición. El puerto conserva el prefijo y comunica
esa frontera cuando queda un lugar en el pool; con las 3.000 partículas ocupadas
el original no lee el puntero y el puerto continúa. **Esto puede alcanzarse al atacar
o destruir estructuras; impide declarar H13 completamente jugable.**

Procedimiento para el agente con depurador de la VM:

1. En una copia de laboratorio, preparar la destrucción de una puerta y el ataque que
   genera fragmentos `131..133`/`10a..10b`. Empezar a capturar antes del ataque.
2. Usar puntos de ejecución de hardware en los sitios anteriores, sin sustituir instrucciones
   ni evitar protecciones. Si el depurador no puede observarlos, registrar ese bloqueo.
3. Al detenerse **antes de la llamada**, guardar EIP, EBP, ESP, pila y los 12 bytes
   desde `EBP-0x28`; anotar tipo/subtipo/posición del efecto padre y si hay lugar en partículas.
   Confirmar el marco con el ASM antes de interpretar cualquier offset.
4. Capturar la partícula realmente creada, el caller que había escrito esos bytes de pila,
   el estado x87/CW y el RNG antes/después del recorrido. Continuar hasta completar el paso.
5. Repetir al menos cinco veces por familia, con proceso recién abierto y con proceso usado;
   repetir con pool saturado y guardar también el caso sin creación.

Ejemplo de inspección manual en CDB/WinDbg x86, después de verificar la identidad:

```text
ba e 1 0x00529520
ba e 1 0x00525310
g
r
dd @ebp-0x28 L3
dd @esp L10
kb
```

Estos comandos sólo preparan una inspección; no automatizan ni certifican la captura.
Comprobar en el desensamblado que el segundo sitio es la instrucción de llamada
de esa build antes de continuar. Un valor observado no demuestra que siempre sea constante:
no normalizar a cero ni usar la posición del padre por conveniencia.
Devolver datos suficientes para reconstruir el origen de la pila o conservar la frontera explícita.

El mismo criterio se aplica a `52719e..5271d3` del efecto `147`: con vida menor que10,
la primera creación de `61e/1` lee `EBP-0x28` antes de que el loop lo escriba. Capturar
la primera iteración, además de las posteriores que ya tienen posición calculada. No extrapolar
el vector de una iteración posterior a la primera.

También queda el polvo de las acciones2/0xa en `431e86`, dentro de `431470`:
su posición es la del actor, pero algunas rutas pasan una **luz** desde un local
de pila sin inicializar. Comprobar en el ASM el marco y el argumento de luz,
guardar sus bytes antes de la llamada y contrastar capacidad libre/llena.
No reutilizar la captura de posición de los fragmentos como evidencia de esta luz.
En el frame normal del juego, `63710f` crea el polvo y `637128` lo pasa por
`5446e0`: `546b73/76/79` reemplaza RGB antes del dibujo `548550`. Capturar ambos
momentos. La incertidumbre del estado intermedio no demuestra por sí sola una
diferencia visual en ese frame; sigue sin habilitar fabricar el RGB del constructor.

## Matriz funcional

Para cada fila registrar `PASS`, `FAIL`, `BLOCKED` o `NOT_RUN`, con referencia a sus archivos
privados. Comparar el mismo estado y secuencia en ambos clientes; no sólo que ninguno se cierre.

| Caso | Acción y estados mínimos | Evidencia a comparar |
| --- | --- | --- |
| Registro H12 | Abrir desde NPC/respuesta; tabs 0/1/2; estados 1/3/5/6; cerrar y reabrir | Paquetes B2/B4/B5, una consulta al abrir, selección/scroll, cierre 31, fechas y textos con textura. VAs `64aca0`, `64d350`, `64dc00`. |
| Senior H12 | Entrada NPC 12 con byte4 0/1; puertas/estatuas, impuestos, retirar y fabricar | Modo de cierre antes de las tres consultas B3/B2; vida, costes y redondeos; permisos, caja ocupada, callbacks, orden de cierres. `661310`, `6691c0`, `6694c0`. |
| Zona de caza | Modos 1/2/3; permitido 0/1/2; dinero tarifa−1/igual/+1; editar y confirmar | Igualdad se pinta suficiente pero no habilita entrar; mensajes, B9 y 31, clamp/wrap, tres restauraciones Alpha/textura. `648a30`, `64a060`. |
| Marca del dueño | Recibir B9 02, repetir los mismos bytes y cambiar la marca | Cada recepción repinta/sube 64×64; marca central 32×32, borde/color/fondo y formato GL. `5f1890`. |
| Holders | Ocupación y salida del viewport; claves `13d`, otra y `813d` | Un mismo objeto para recepción/HUD; clave completa antes de máscara, limpieza de mitades y última terminación. `4e04e0`, `4ca780`, `5a6fc0`. |
| Corona/bandos | B2 14/15/16/18/19; éxito, rechazo y cancelación | Nombres, duración unsigned, callback, overwrite/OR/AND de flags y orden del aviso. `4dff10..4e04a5`. |
| Panel de batalla | Sin gremio, miembro, maestro solo y maestro en alianza | Clase 0/1/2, mapas/puntos, B6 reemplaza incluso count0; BB añade y count0 conserva; paginado y órdenes. `412140`, `411e80`, `411ed0`. |
| Puertas/armas | Abrir/cerrar puerta; dos modos B7; disparar e impactar; cambio de personaje con arma abierta | Wire y selección, cierre del arma anterior, acción/bone/frame, cantidad/orden de efectos y sonidos. `44dd10`, `44eea0`, `44efc0`, `42e3d0`. |
| Mapa 30 | Paz→batalla→paz y dueño distinto | `.att` 31/312, luces normal/2, agua/cáusticas, barreras, roles, deformación, sombras y polvo. `4294c0`, `42ab40`, `42ae60`, `5f9580`. |
| B1/servidor | Cruzar al servidor del mapa y volver; saludo posterior al login | Cierre→opciones/macros→20 ms→conexión; reset, 31 y auth en orden; tick, RNG y limpieza de caché sin borrar nombres/marcas. `412b30`, `412bb0`, `4c1e20`, `447ae0`. |
| Retorno | Cambiar personaje/servidor; entrenador vacío y ocupado; ventana/corona activa | 31 condicional del entrenador, aviso 1211, reset silencioso de diálogo, pets/tiendas/cámara; sin duplicar callbacks/paquetes. `4c2480`, `657670`, `4499b0`. |
| Battle Soccer | Partido con score activo, fin por F3 24 y aceptar A7 | Nombres y puntajes compartidos, telas/efectos, reloj y resultado aun con HUD oculto; A7 borra resultado. `50caaa`, `5a7640`, `5a7eb0`, `4deef0`. |
| Crywolf | Llegada, altares, contrato, ataque de cada familia, BD0C y final de la cámara | Estado persistente, orden de modelos/partículas/joints, RNG, sonido y dibujo incluso con BMD vacío; saturación omite el alta, no el caller. `4039f0`, `432bd0`, `434ecf`, `602596`. |
| NPC de evento | Hablar resultados 4/6 y usar cada botón, regresar/cerrar | Cierre previo, inventario y panel 1000/4000, diálogos y envíos, sonidos de aperturas anidadas. `4d2577`, `4d258e`, `65b265`, `65b29b`. |
| Inventario/equipo | Equipo sin mano por F3 13/lista/selección; doble clic joya; clic derecho almacén; tirar ticket | Mano wire `fff` produce modelo `1211` por guard `1fff`, sin geometría pero con sus sorteos; doble clic izquierdo canónico; solicitudes/esperas y restricciones nativas. |
| EDIT H9/H10 | Selección, cursor, clipboard, flechas de lista, Tab, Enter e IME | Perfil XP, Ctrl+A sin acción, foco y commit/preedit; autoscroll horizontal exacto todavía requiere oráculo original. Ver runbook H1/H9. |

La autoequipación de flechas conserva P-C151/Q-C773: `UpdateTheArrows` (`561b13`),
desde28 de una mano de elfo, lee manos después del cierre protegido. Falta observar
los bytes cifrados y el estado de protección, sin sustituirlos por una mano vacía o por ceros.
Registrar inventario, cursor vacío y orden de accesos; seguir los límites del runbook H1/H9.

La selección de objetos `59eb90` sigue siendo una dependencia de CL7.4 para rutas cuyo
productor es `selectedTargets[3]`. Probar un consumidor con un índice escrito por una fixture
no acredita el picking interactivo de esa ruta.

## Entrega de evidencia

Por caso guardar: identidades, precondiciones, pasos exactos, wire saneado con orden y tiempo,
capturas de imagen y/o traza GL, estado antes/después y RNG cuando interviene. Si hay floats,
preservar bits además del valor decimal. Para un fallo incluir el primer punto de divergencia
y no sólo el último mensaje del cliente.

Entregar un índice con nombre de caso, resultado, archivos y SHA-256. No renombrar una captura
histórica como si perteneciera a esta sesión. Si falta infraestructura, dejar el bloqueo explícito
y continuar los casos independientes. Los tests del puerto y el contraste estático se registran
separados de esta validación dinámica.
