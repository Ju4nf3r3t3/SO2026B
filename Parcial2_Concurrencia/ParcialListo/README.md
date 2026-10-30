# Parcial 2 — Compresor Huffman concurrente integrado al editor

**Autor:** Juan Fernando Romero · Sistemas Operativos SO2026B (EAFIT) · Alternativa 1 · individual

## Contenido

| Archivo | Qué es |
|---|---|
| `Sustentacion.pdf` | Documento de sustentación, organizado según los 5 criterios de la rúbrica (diseño de hilos, sincronización, integración con el editor, corrección y recursos) y con la evidencia. |
| `SUSTENTACION.md` | El mismo documento en Markdown. |
| `video_sustentacion.mp4` | Video de la sustentación (demo en vivo y explicación). |
| `shell/` | Código fuente completo: shell `eafitOS` + editor del Parcial 1 + compresor. |
| `shell/huffman.c/.h` | Algoritmo de Huffman (sin hilos). |
| `shell/compresor.c/.h` | Compresión concurrente: pool de 4 hilos, reducción local, escritura en orden. |
| `shell/editor.c` | Comandos `z`, `x`, `s`, `w`, progreso en el prompt, rwlock sobre el archivo, Ctrl+C. |
| `shell/test_parcial2.sh` | Pruebas: integridad con md5/cmp, demo de no bloqueo, valgrind y helgrind. |
| `shell/evidencias/` | Resultados guardados: `md5.txt`, `valgrind_memcheck.txt`, `valgrind_helgrind.txt`. |

## Compilar y probar (Linux)

```
cd shell
make                  # compila (gcc -Wall -Wextra -pthread)
./test_parcial2.sh    # pruebas de integridad, no bloqueo y valgrind
make run              # abre el shell; dentro: edit <archivo>, luego z / s / w / x <archivo.huf>
```
