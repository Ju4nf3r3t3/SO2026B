# Documento de Sustentación — Editor de Texto CLI integrado a eafitOS

**Asignatura:** Sistemas Operativos — SO2026B
**Proyecto:** Editor de texto CLI (System Calls / POSIX)
**Tamaño de equipo:** 1 (individual) → alcance: comandos base (sección 4 del enunciado)

> Este documento está en Markdown para que sea fácil de editar. Se convierte a PDF exportándolo (por ejemplo desde VS Code, Typora, o `pandoc sustentacion.md -o sustentacion.pdf`).

---

## 1. Qué se construyó

Un editor de texto interactivo (`edit`), integrado como un nuevo comando del shell educativo `eafitOS`. Al ejecutarlo, se entra a un sub-modo con su propio prompt (`edit>`) que soporta 5 comandos:

| Comando | Función | Syscalls usadas |
|---|---|---|
| `o <archivo>` | Abre/crea el archivo | `open()` |
| `p [n]` | Imprime todo el archivo, o la línea `n` | `lseek()`, `read()`, `write()` |
| `a <texto>` | Añade una línea al final | `lseek()`, `write()` |
| `d <n>` | Borra la línea `n` | `lseek()`, `read()`, `write()`, `ftruncate()` |
| `q` | Cierra el archivo y vuelve al shell | `close()` |

Toda la manipulación del archivo editado usa exclusivamente syscalls POSIX de bajo nivel (`open/read/write/lseek/ftruncate/close`), nunca `fopen/fread/fwrite/fclose`, cumpliendo la restricción de I/O del enunciado (sección 2.2 / 3). La interacción con el usuario (prompts, mensajes) sí usa `printf`/`fgets`, como permite el enunciado.

## 2. Decisión de integración con el shell (sección 2.1)

**Categoría nueva `editor`, no extensión de `datos`.**

Los comandos de `datos` (`d_create`, `d_read`, `d_info`, `d_copy`) son operaciones de un solo disparo: reciben argumentos, ejecutan una acción y retornan de inmediato al prompt `eafitOS>`. El editor, en cambio, abre un **sub-modo interactivo propio** con su propio ciclo `open → prompt → comando → ... → close`, cualitativamente distinto: el usuario permanece "dentro" del editor ejecutando múltiples comandos (`o`, `p`, `a`, `d`) hasta escribir `q`. Forzarlo dentro de `datos` habría escondido esta diferencia de comportamiento (comando atómico vs. sesión interactiva) y habría sido confuso de documentar en el `help`.

**Ejecución in-process, sin `fork`/`execvp`.**

`cmd_edit()` se registra igual que cualquier otro handler de la tabla `commands[]` y se ejecuta en el mismo proceso del shell, compartiendo la misma tabla de descriptores de archivo. Cuando el usuario escribe `q`, la función simplemente hace `close()` y retorna (`return 0`), devolviendo el control al bucle `main()` del shell — igual que retorna cualquier otro comando (`d_create`, `saludar`, etc.). No se usó `fork`/`execvp` (como sí hace `p_exec` para lanzar binarios externos de Linux) porque el editor **no es un programa externo**: es lógica del mismo shell, no necesita aislarse en otro proceso, y evitar el `fork` simplifica el código (no hay que sincronizar con `waitpid`, ni preocuparse por procesos huérfanos si el editor falla).

## 3. Estructuras de datos

Se usan dos variables `static` a nivel de archivo en `editor.c`:

```c
static int g_fd = -1;
static char g_filename[PATH_MAX];
```

**Por qué no un `struct EditorBuffer`:** para el equipo de 1 persona el alcance es fijo (solo comandos base, sin multi-buffer ni undo/redo), así que agregar una estructura con campos que nunca se usarían (índice de líneas cacheado, lista de buffers, etc.) habría sido complejidad innecesaria. Si el proyecto creciera a más integrantes, el primer paso de refactor sería envolver `g_fd`/`g_filename` en un `struct EditorBuffer` — el cambio es mecánico porque toda la lógica ya depende de "el archivo actualmente abierto" como unidad, no de variables dispersas sin relación.

## 4. Cómo se determinan los límites de línea

Se eligió **escaneo bajo demanda** (recorrer el archivo byte a byte contando `\n` cada vez que se necesita, función `find_line()`), en vez de cachear un índice de offsets en memoria.

Justificación: para el tamaño de archivos que maneja un editor educativo esto es más que suficiente en rendimiento, y evita el problema de mantener un índice sincronizado tras cada `a`/`d` (que sería una fuente extra de bugs). Es la opción más simple de explicar y de verificar como correcta.

## 5. Manejo de errores (15% de la rúbrica)

Todas las syscalls (`open`, `read`, `write`, `lseek`, `ftruncate`, `close`) se invocan a través de wrappers `checked_*()` en `editor.c` que verifican el valor de retorno `-1` y llaman a `perror()`, siguiendo el patrón sugerido en la sección 8.3 del enunciado. Además, el editor valida en el propio REPL casos de uso inválido antes de llegar a la syscall (por ejemplo, pedir `p 99` en un archivo de 3 líneas, o ejecutar `p`/`a`/`d` sin haber abierto ningún archivo), mostrando un mensaje claro en vez de intentar la operación y fallar.

## 6. Cómo se probó

Ver `shell/demo_test.sh`: compila el proyecto y envía por `stdin` una secuencia de comandos que cubre el flujo normal (crear archivo, agregar 3 líneas, imprimir, borrar una línea, verificar con `d_read`) y los casos borde (archivo nuevo vía `o`, comandos sin archivo abierto, línea fuera de rango en `p`/`d`).
