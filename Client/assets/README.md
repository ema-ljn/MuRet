# Assets

`Client/assets/Data/` es la `Data\` entera del cliente original, los 5726 archivos tal cual: el
original que MuRet reimagina y la base de los tests golden y de memoria (decidido por Emanuel el
2026-11-04, `AGENTS.md` y D-C7 del roadmap). Los nombres van como vienen, con mayúsculas mezcladas
(`player\`, `World7\`): MuRet los busca sin mirarlas, como Windows.

Nada de adentro se toca. El `.gitattributes` de la raíz la marca `-text`, para que git no le
cambie un fin de línea a `Macro.txt` ni a un `.bmd` sin un byte 0. `Data.sha256` tiene el hash
de cada archivo.

## Verificarla

```bash
cd Client/assets/Data && sha256sum -c --quiet ../Data.sha256
```

Sin salida es que están todos y son iguales. Un archivo que falta o que cambió sale con `FAILED`.
Los que sobran no se ven: son 5726 (`find . -type f | wc -l`). Sirve también para una `Data\` de
otra instalación, con los nombres con las mismas mayúsculas.

## Usarla

La carpeta no se toma sola: se le pasa a `Main` y a los tests como cualquier otra.

```bash
./build/linux-x64-debug/Client/Main --data Client/assets/Data
MURET_CLIENT_GOLDEN_DIR="$PWD/Client/assets/Data" ctest --preset linux-x64-debug
```

O en `muret-client.ini`, con `[Data] Path`. Una ruta relativa ahí se toma desde la carpeta del
ejecutable, no desde la actual (el [README del cliente](../README.md)): mejor la absoluta.

## Regenerar el manifiesto

Solo si cambia la `Data\` que usa el proyecto, y en el mismo PR que lo explique:

```bash
cd Client/assets/Data && LC_ALL=C find . -type f -print0 | LC_ALL=C sort -z | xargs -0 sha256sum > ../Data.sha256
```
