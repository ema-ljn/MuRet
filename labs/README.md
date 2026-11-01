# labs

El código de las N.0: lo que se construye desde cero para aprender un concepto, antes de usar el
estándar en el servidor o en el cliente. Lo escribe Emanuel (la 0.0 la resolvió Claude a su pedido); cada carpeta es una N.0 y su guía está
en [`docs/learning/`](../docs/learning/README.md).

| Carpeta | N.0 | Guía |
| --- | --- | --- |
| [`0.0-enlace/`](0.0-enlace/) | del código al programa: unidades de traducción, símbolos, bibliotecas. Resuelta como ejemplo, con las respuestas en su [README](0.0-enlace/README.md) | [ola-0.0](../docs/learning/ola-0.0-del-codigo-al-programa.md) |

Hasta la 0.2 los tests son ejecutables que salen con 0 o con 1, registrados con `add_test()`;
desde la 0.2, de GoogleTest. Se corren con CTest:

```bash
cmake --preset linux-x64-debug && cmake --build --preset linux-x64-debug
ctest --test-dir build/linux-x64-debug --output-on-failure
```
