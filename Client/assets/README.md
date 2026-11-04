# Assets

La carpeta `Data\` del cliente original que usa MuRet va acá, en `Client/assets/Data/`. Son
assets del juego: no se versionan (`AGENTS.md`), así que la carpeta está en `.gitignore` y cada
uno pone la suya. Lo que entra al repo es `Data.sha256`, el hash de cada archivo, para saber que
la `Data\` de cada uno es la misma.

## Ponerla

Copiar el contenido de `Data\` de la instalación (`Enc1.dat`, `Local\`, `player\`, `World1\`…) en
`Client/assets/Data/`, con los nombres como vienen: MuRet los busca sin mirar mayúsculas, como
Windows.

## Verificarla

```bash
cd Client/assets/Data && sha256sum -c --quiet ../Data.sha256
```

Sin salida es que están todos y son iguales. Un archivo que falta o que cambió sale con `FAILED`.
Los que sobran no se ven: el manifiesto lista 5726 archivos (`find . -type f | wc -l`).

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
