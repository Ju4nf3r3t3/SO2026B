# Evaluación de Sistemas Operativos — Proyecto: Editor de Texto CLI

**Tema:** Llamadas al Sistema (*System Calls*) e Interfaz POSIX (CLI) en ambiente Linux (sin concurrencia)
**Lenguaje de programación:** C
**Asignatura:** Sistemas Operativos — SO2026B

> Este documento es la versión estructurada en Markdown del enunciado original (`.docx`) del proyecto. Se ha reorganizado y ampliado para servir como **referencia de arquitectura** antes de empezar a codificar: cada sección incluye, además del requisito textual, las implicaciones técnicas y de diseño que se derivan de él.

---

## Tabla de contenidos

1. [Objetivos](#1-objetivos)
2. [Descripción del proyecto e integración arquitectónica](#2-descripción-del-proyecto-e-integración-arquitectónica)
3. [Restricción crítica de I/O](#3-restricción-crítica-de-io)
4. [Comandos base CLI (requisito mínimo)](#4-comandos-base-cli-requisito-mínimo)
5. [Escalamiento de dificultad por tamaño de equipo](#5-escalamiento-de-dificultad-por-tamaño-de-equipo)
6. [Rúbrica de evaluación](#6-rúbrica-de-evaluación)
7. [Instrucciones de entrega (entregables)](#7-instrucciones-de-entrega-entregables)
8. [Notas de arquitectura para la implementación](#8-notas-de-arquitectura-para-la-implementación)

---

## 1. Objetivos

- Comprender el funcionamiento interno de las herramientas CLI (*Command Line Interface*) para la edición de texto en sistemas Linux.
- Aplicar llamadas al sistema (*system calls*) de bajo nivel para la manipulación de archivos y control estricto de entrada/salida.
- Demostrar pensamiento crítico en la toma de decisiones arquitectónicas, análisis de sistemas y en la resolución de problemas de bajo nivel.
- Desarrollar habilidades avanzadas en la gestión de memoria dinámica, punteros y búferes en el lenguaje C *(preparación para el tema de concurrencia que sigue en el curso)*.
- Fomentar el trabajo colaborativo, adaptando la complejidad del software a la capacidad operativa del equipo de desarrollo.

---

## 2. Descripción del proyecto e integración arquitectónica

Se debe construir, en lenguaje C, un **editor de texto interactivo** operado estrictamente por CLI en un entorno Linux (**sin GUI**). El programa debe permitir **leer, modificar y guardar archivos planos** utilizando exclusivamente llamadas al sistema POSIX de bajo nivel.

La arquitectura del sistema debe estar **preparada para escalar** en funcionalidades según el tamaño del equipo de trabajo asignado (ver [sección 5](#5-escalamiento-de-dificultad-por-tamaño-de-equipo)) — es decir, el diseño inicial (aunque el equipo sea de 1 persona) no debería bloquear la incorporación posterior de undo/redo, múltiples buffers o locking si el alcance creciera.

### 2.1 Integración estratégica con el Shell

**Requisito fundamental para evaluar el pensamiento crítico:** el editor debe integrarse funcionalmente con el **Shell de clase** (repositorio `SO2026B` → carpeta `shell/`).

El equipo debe:

1. **Analizar rigurosamente la arquitectura del shell existente** antes de integrar nada.
2. **Tomar una decisión argumentada** sobre cómo se invoca el editor desde el shell.
3. **Determinar si el comando de invocación encaja en una categoría existente o si se justifica crear una categoría nueva.**

#### Contexto real del shell (repositorio `SO2026B/shell`)

El shell (`eafitOS`) ya define una tabla global de comandos en `main.c` mediante una estructura `Command` (declarada en `shell.h`):

```c
typedef struct {
    const char *name;        // nombre textual del comando
    const char *category;    // "datos", "memoria", "monitoreo", "utilidades"
    const char *usage;       // sintaxis de uso
    const char *description; // explicación en español
    const char *syscalls;    // syscalls involucradas (para el 'help')
    int (*handler)(int argc, char **argv); // función que implementa el comando
} Command;
```

Las **categorías actualmente registradas** son:

| Categoría | Contenido actual | Syscalls típicas |
|---|---|---|
| `datos` | `d_create`, `d_read`, `d_info`, `d_copy` | `open`, `read`, `write`, `close`, `stat` |
| `memoria` | `m_sbrk`, `m_mmap`, `m_info` | `sbrk`/`brk`, `mmap`, `munmap`, `/proc/self/status` |
| `monitoreo` | `p_fork`, `p_exec`, `p_kill`, `p_monitor` | `fork`, `execvp`, `waitpid`, `kill`, `getrusage` |
| `utilidades` | `saludar`, `despedir`, `hora`, `fecha` | `getuid`, `time` |

El shell resuelve comandos con búsqueda lineal (`strcmp` sobre `commands[i].name`) y expone ayuda contextual por categoría (`help <categoria>`) y por comando (`help <comando>`).

**Punto de decisión arquitectónica que el equipo debe justificar en el PDF de sustentación:**

- ¿El editor de texto encaja como una extensión de la categoría `datos` (porque manipula archivos con las mismas syscalls: `open`, `read`, `write`, `lseek`, `close`)?
- ¿O el editor merece una **categoría nueva** (p. ej. `editor` o `texto`) porque, a diferencia de los comandos `datos` actuales (que son operaciones atómicas de un solo disparo), el editor abre un **sub-modo interactivo** con su propio ciclo `open → prompt → comando → ... → close` — un patrón cualitativamente distinto al resto de comandos del shell, que son de ejecución única?
- Si se opta por invocar el editor como un solo comando (p. ej. `edit archivo.txt`) que internamente lanza su propio REPL, debe documentarse cómo se cede y se devuelve el control al bucle principal del shell (`main()` en `shell/main.c`), y si el editor se ejecuta *in-process* (mismo proceso, misma tabla de FDs) o vía `fork`/`execvp` (proceso independiente, como hace `p_exec`).

> Esta decisión (categoría existente vs. categoría nueva, e in-process vs. subproceso) es explícitamente parte de lo evaluado como "pensamiento crítico" en la rúbrica (sección 6) y debe quedar justificada por escrito en el documento de sustentación.

### 2.2 Restricción crítica de I/O

Queda **prohibido** el uso de las funciones de alto nivel de la biblioteca estándar de C (*buffered I/O* del `stdio.h`) para la manipulación del archivo de texto:

- ❌ `fopen`, `fread`, `fwrite`, `fclose`

Todo acceso a disco debe realizarse mediante **llamadas al sistema POSIX de bajo nivel** (*unbuffered I/O*, `unistd.h` / `fcntl.h`):

- ✅ `open()`, `read()`, `write()`, `lseek()`, `ftruncate()`, `close()`

Solo se permite I/O de alto nivel (`printf`, `scanf`, `fgets`) para:
- Leer los **comandos** que el usuario escribe por `STDIN`.
- Imprimir mensajes de la **consola/interfaz** por `STDOUT`.

> Es decir: la interacción con el humano puede usar `stdio`, pero la interacción con el **archivo que se está editando** debe usar exclusivamente syscalls crudas.

---

## 3. Restricción crítica de I/O

*(Detallado también arriba; se repite aquí como sección independiente por ser un requisito transversal a todo el proyecto, tal como aparece en el documento original.)*

| Permitido para el archivo editado | Prohibido para el archivo editado | Permitido para consola/comandos |
|---|---|---|
| `open()` con `O_RDWR \| O_CREAT`, `mode_t` | `fopen()` | `printf()` |
| `read()` | `fread()` | `scanf()` |
| `write()` | `fwrite()` | `fgets()` |
| `lseek()` | `fclose()` | — |
| `ftruncate()` | — | — |
| `close()` | — | — |

---

## 4. Comandos base CLI (requisito mínimo)

Independientemente del tamaño del equipo, **todos** deben implementar el ciclo interactivo con estos comandos base:

| Comando | Descripción | System calls recomendadas |
|---|---|---|
| `o [archivo]` | Abre un archivo en disco. Si no existe, lo crea con los permisos adecuados. | `open()` con `O_RDWR \| O_CREAT`, `mode_t` |
| `p [n]` | Imprime la línea `n`. Sin parámetros, imprime todo el archivo recorriendo bytes hasta `\n`. | `read()`, `lseek()`, `write()` (FD 1) |
| `a [texto]` | Añade el texto provisto como una nueva línea al final del archivo. | `lseek()` (`SEEK_END`), `write()` |
| `d [n]` | Borra la línea `n` del archivo (desplazando los bytes posteriores y truncando). | `read()`, `write()`, `lseek()`, `ftruncate()` |
| `q` | Cierra el *file descriptor* y sale de la aplicación sin dejar fugas de memoria/recursos. | `close()`, `exit()` |

### Notas de diseño para esta sección

- **`p [n]` y `d [n]` requieren un modelo de "líneas" sobre un archivo que, a nivel de syscalls, solo son bytes.** Hay que decidir explícitamente cómo se determinan los límites de línea: recorrer byte a byte con `read()` buscando `\n`, o cachear un índice de offsets de línea en memoria al abrir el archivo (más eficiente para archivos grandes, pero introduce estado que debe mantenerse sincronizado tras cada `a`/`d`/`i`).
- **`d [n]` es la operación más delicada del set base**: borrar una línea intermedia implica desplazar todos los bytes posteriores hacia atrás y luego truncar el archivo con `ftruncate()`. Esto debe hacerse con cuidado para no corromper el archivo si el proceso se interrumpe a mitad de la operación (no se pide tolerancia a fallos, pero sí una implementación correcta del desplazamiento).
- **Manejo de errores:** cada syscall retorna `-1` en caso de error y setea `errno`. La rúbrica (15%) exige verificar explícitamente estos retornos y usar `perror()` — este patrón debe aplicarse de forma consistente en *todas* las funciones que envuelven syscalls, no solo en las del set base.

---

## 5. Escalamiento de dificultad por tamaño de equipo

Para garantizar equidad en la evaluación, los equipos más grandes deben implementar funcionalidad adicional que exija un uso más avanzado de los recursos del sistema operativo.

> **⚠️ Los requisitos son estrictamente acumulativos.** Un equipo de tamaño *N* debe implementar los comandos base **más** todos los requisitos de los equipos de tamaño menor a *N*, sumados a sus propios requisitos.
>
> Como el tema de **concurrencia** aún no se ha visto en el curso, **todos** los niveles deben resolverse con **procesos de un solo hilo y gestión estrictamente secuencial** (nada de threads ni procesos concurrentes para el editor en sí — `fork`/`execvp` sí se usan en el shell base para otros fines, pero no como mecanismo de concurrencia dentro del editor).

| Integrantes | Requisitos acumulativos a implementar | Reto técnico de OS |
|---|---|---|
| **1** (Individual) | Comandos base completos (sección 4). Integración en el Shell de clase. | Dominio de `open`, `read`, `write`, `lseek`. Gestión básica de descriptores de archivo. |
| **2** (Parejas) | Todo lo anterior, más: <br>1. **Inserción arbitraria**: `i [n] [texto]` — inserta texto en la línea `n`, desplazando el resto. <br>2. **Búsqueda simple**: `s [palabra]`. | Manejo avanzado de `lseek` y manipulación de *buffers* dinámicos en memoria (`malloc`/`free`) para no perder datos al desplazar bytes. |
| **3** (Tres) | Todo lo anterior, más: <br>1. **Metadatos del archivo**: comando `m` que imprima tamaño, permisos, inodo y fecha de modificación. <br>2. **Copiar y pegar**: `y [n]` (yank) y `x [n]` (paste/cut). | Integración de la syscall `fstat()` para acceder a metadata en los inodos. Gestión de un portapapeles secuencial local. |
| **4** (Cuatro) | Todo lo anterior, más: <br>1. **Deshacer/Rehacer**: comandos `u` (undo) y `r` (redo). | Creación de archivos temporales (archivos de *swap*) en `/tmp`. Uso de `unlink()` al cerrar, para limpiar los temporales. |
| **5** (Cinco) | Todo lo anterior, más: <br>1. **Múltiples buffers**: comando `b [archivo]` para mantener varios archivos abiertos a la vez. <br>2. **Bloqueo de archivos** (*locks*): `fcntl()`. | Gestión manual de una tabla de descriptores de archivo (FDs). Implementación de *file locking* mediante `fcntl()` con `F_SETLK`. |

### Notas de diseño por nivel (para la arquitectura del código)

- **Nivel 2 — Inserción/Búsqueda:** al introducir `i [n] [texto]`, conviene reutilizar el mismo mecanismo de "desplazar bytes" que ya se necesita para `d [n]` (nivel 1). Es buen momento para extraer una función interna común, p. ej. `shift_bytes_from_offset(fd, offset, delta)`, en lugar de duplicar lógica de desplazamiento en cada comando.
- **Nivel 3 — Metadatos/Clipboard:** `fstat()` requiere el FD ya abierto (no la ruta), así que el diseño debe mantener el FD del archivo activo accesible globalmente o en una estructura de "sesión de edición". El portapapeles (`y`/`x`) es simplemente un buffer en memoria (`malloc`) con el contenido de la línea copiada — vive independientemente del archivo.
- **Nivel 4 — Undo/Redo:** al usar archivos de *swap* en `/tmp`, hay que definir una convención de nombres que evite colisiones entre instancias concurrentes del editor (p. ej. incluir el PID: `/tmp/edit_<pid>.swp`), y garantizar `unlink()` tanto en la salida normal (`q`) como en cualquier ruta de salida por error, para no dejar basura en `/tmp`.
- **Nivel 5 — Multi-buffer/Locking:** pasar de "un FD global" a "una tabla de FDs" es el cambio arquitectónico más grande del escalamiento — si el diseño de los niveles 1-4 ya usa una estructura tipo `EditorBuffer { int fd; char *filename; ... }` en lugar de variables sueltas, este nivel se reduce a mantener un **arreglo/lista de `EditorBuffer`** en vez de una sola instancia. Diseñar esto desde el nivel 1, aunque el equipo sea pequeño, evita una reescritura mayor si el alcance crece.

---

## 6. Rúbrica de evaluación

| Peso | Criterio | Detalle |
|---|---|---|
| **40%** | Implementación técnica y pensamiento crítico | Correcto uso de las llamadas al sistema, compilación limpia y robustez de la integración con el Shell, evidenciando decisiones de diseño sólidas. |
| **30%** | Completitud según equipo | Cumplimiento estricto de las funcionalidades acumulativas asignadas según el tamaño del equipo, validando casos borde. |
| **15%** | Manejo de errores | Verificación de valores de retorno (p. ej. si `open()` devuelve `-1`), usando `perror()` apropiadamente. |
| **15%** | Entregables (sustentación, demo y documentación) | Claridad en el video, calidad del PDF justificando la arquitectura, y exhaustividad del script de pruebas. |

---

## 7. Instrucciones de entrega (entregables)

La entrega oficial debe constar **estrictamente** de los siguientes elementos, evaluados en su totalidad:

1. **Código fuente**
   Archivos `.c` y `.h` debidamente documentados, acompañados de un `Makefile` configurado para compilar correctamente (*targets*: `all`, `clean`). Debe incluir el **código actualizado del Shell** con el editor ya integrado.

2. **Código de demostración**
   Un script de pruebas (o código adicional) que ejecute y valide los comandos requeridos, demostrando la robustez del editor ante distintos escenarios (incluyendo casos borde y de error).

3. **Documento de sustentación (PDF)**
   Reporte ejecutivo y técnico. Debe demostrar explícitamente el pensamiento crítico del equipo:
   - Justificación minuciosa de las **decisiones de diseño**.
   - Justificación de las **estructuras de datos** seleccionadas.
   - Justificación de la **estrategia de integración** con la arquitectura del Shell de clase (incluyendo la clasificación de categoría del comando, ver [sección 2.1](#21-integración-estratégica-con-el-shell)).

4. **Video explicativo**
   Corto (máximo 5–7 minutos), complementario al PDF. Los integrantes deben presentar de forma dinámica la estrategia diseñada, exponer el funcionamiento y sustentar cómo se abordaron los retos técnicos impuestos por el sistema operativo.

---

## 8. Notas de arquitectura para la implementación

Esta sección no proviene del documento original — es una guía de apoyo para cuando el equipo empiece a codificar, derivada de los requisitos anteriores.

### 8.1 Estructura de módulos sugerida

```
editor/
├── editor.h          # Definición de EditorBuffer, prototipos públicos
├── editor_core.c      # open/close de archivo, ciclo REPL del editor
├── editor_lines.c     # p, a, d, i — operaciones sobre líneas (usa read/write/lseek/ftruncate)
├── editor_clipboard.c # y, x (nivel 3+)
├── editor_undo.c      # u, r — manejo de swap files en /tmp (nivel 4+)
├── editor_buffers.c   # b [archivo] — tabla de múltiples EditorBuffer + fcntl locks (nivel 5+)
└── Makefile
```

Esto permite que un equipo pequeño solo compile `editor_core.c` + `editor_lines.c`, y que ir subiendo de nivel sea agregar archivos nuevos sin tocar los ya probados.

### 8.2 Estructura de datos central (ejemplo)

```c
typedef struct {
    int fd;                 // FD del archivo actualmente abierto
    char filename[PATH_MAX];
    off_t *line_offsets;    // índice opcional de offsets por línea (optimización)
    size_t line_count;
    // Nivel 4: char swap_path[PATH_MAX];
    // Nivel 5: reemplazar por un arreglo de estas structs (multi-buffer)
} EditorBuffer;
```

Diseñar esta struct desde el nivel 1 (aunque varios campos queden sin usar al principio) facilita el escalamiento acumulativo sin refactors grandes.

### 8.3 Patrón de manejo de errores

Dado que el 15% de la rúbrica es manejo de errores, conviene estandarizar un wrapper desde el inicio:

```c
static int checked_write(int fd, const void *buf, size_t count) {
    ssize_t n = write(fd, buf, count);
    if (n < 0) {
        perror("write");
        return -1;
    }
    return (int)n;
}
```

Aplicar el mismo patrón a `open`, `read`, `lseek`, `ftruncate`, `close`, `fstat` y `fcntl` evita olvidos y centraliza el formato de los mensajes de error, lo que también ayuda en la demo/video.

### 8.4 Puntos que deben quedar resueltos *antes* de escribir código

- [ ] ¿Categoría existente (`datos`) o categoría nueva (p. ej. `editor`) en el shell? — justificar en el PDF.
- [ ] ¿El editor corre in-process o vía `fork`/`execvp` como subproceso del shell?
- [ ] ¿Cómo se determinan los límites de línea: escaneo en cada operación o índice cacheado en memoria?
- [ ] Convención de nombres para archivos de swap en `/tmp` (nivel 4+) que evite colisiones entre instancias.
- [ ] Formato exacto de salida de `p` (sin argumentos) y de `m` (metadatos) — debe quedar definido y consistente para el script de pruebas.
