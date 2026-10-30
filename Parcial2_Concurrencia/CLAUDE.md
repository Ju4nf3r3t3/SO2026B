# Instrucciones para Claude — Parcial 2 (Concurrencia y Sincronización)

> **Estado: TERMINADO.** El código, la sustentación y el video ya están listos. La carpeta de entrega ahora se llama `ParcialListo/` (antes `ENTREGA/`). No modificar nada; lo de abajo queda como registro de cómo se hizo.

Estás en una **VM Ubuntu Linux**. Tu tarea es implementar el Parcial 2, **Alternativa 1: Compresor Huffman concurrente integrado al editor**, compilarlo, probarlo y dejar lista la carpeta de entrega.

## Regla principal: sencillo, como el Parcial 1

- La solución debe ser **sencilla y fácil de explicar en la sustentación**, al estilo del Parcial 1: pocos archivos, código directo, comentarios claros en español.
- **Cumplir cada punto de la rúbrica, pero sin excederse**: nada de módulos genéricos, abstracciones "por si acaso", optimizaciones ni features extra que el enunciado no pida.
- Usar solo lo necesario: `pthread` (hilos, mutex, variables de condición, rwlock) y las syscalls ya usadas (`open/read/pread/write/close/fstat`). Nada de librerías externas.
- Ante la duda entre dos diseños, elegir el más simple que cumpla la rúbrica y justificarlo en una o dos frases en la sustentación.

## Contexto

- Enunciado y rúbrica: `ENUNCIADO_Parcial2.md` (fuente oficial: `ENUNCIADO_Parcial2.pdf`). Léelo primero.
- Código base: `shell/` es una **copia** del shell `eafitOS` + editor del Parcial 1. Trabaja solo aquí.
- **No modifiques** `../Parcial1_EditorTexto/` (ya entregado) ni las carpetas de clase de la raíz (`shell/`, `Banderas_CLI/`, `callsys/`).
- Hoy existe: `main.c` (tabla `commands[]`, REPL del shell), `editor.c` (comando `edit`, sub-REPL `edit>` con `o`, `p`, `a`, `d`, `q`; todo el I/O con syscalls, nunca `fopen`). Mantén ese estilo (`LOG_SYSCALL`, `COLOR_*`, comentarios `/** ... */`).

## Preparación

```bash
sudo apt-get update && sudo apt-get install -y build-essential valgrind
```

En el `Makefile`: agregar `-pthread`, agregar los archivos nuevos y hacer que `all` solo compile (hoy además ejecuta el binario; poner eso en un target `run`). Ajustar `demo_test.sh`.

## Qué construir (mínimo que cubre la rúbrica)

### Archivos nuevos (solo dos)

- `huffman.c / huffman.h` — lógica de Huffman sin hilos: árbol a partir de frecuencias, tabla de códigos, codificar un bloque, decodificar.
- `compresor.c / compresor.h` — los hilos: conteo paralelo, codificación paralela, escritura en orden y el trabajo en segundo plano con su progreso.

### Compresión concurrente (rúbrica: "Concurrencia en Huffman", 25 pts)

1. Dividir el archivo en bloques de tamaño fijo (p. ej. 64 KiB).
2. Lanzar **N hilos trabajadores** (N fijo, p. ej. 4). Cada hilo toma el siguiente bloque libre de un contador compartido protegido por mutex.
3. **Fase 1 — frecuencias:** cada hilo lee su bloque con `pread` (cada hilo con su propio offset, sin carreras sobre el offset) y cuenta en un **histograma local**; al terminar el bloque lo suma al global bajo un mutex. Esto es la "reducción local" que pide el enunciado.
4. `pthread_join` de los hilos → construir el árbol global (secuencial).
5. **Fase 2 — codificación:** de nuevo N hilos, cada uno codifica bloques en paralelo.
6. **Salida en orden:** cada hilo deja el bloque codificado en `resultados[i]` y hace `pthread_cond_broadcast`. Un hilo escritor espera con `pthread_cond_wait` hasta que `resultados[siguiente]` esté listo, lo escribe con `write` y avanza. **Sin espera activa.**
7. Formato `.huf` simple: cabecera (`"HUF1"`, tamaño original, número de bloques, tabla de 256 frecuencias) y por cada bloque `[bytes originales][bytes comprimidos][datos]`.
8. La **descompresión** puede ser secuencial (el enunciado solo pide que no bloquee el editor), pero corre en segundo plano igual que la compresión.

### Integración con el editor (20 pts)

Comandos nuevos dentro de `edit>`:

| Comando | Qué hace |
|---|---|
| `z` | Comprime el archivo abierto a `<archivo>.huf` **en segundo plano** y vuelve al prompt de inmediato. |
| `x <archivo.huf>` | Descomprime en segundo plano a `<nombre>.out`. |
| `s` | Muestra el progreso: `[#####.....] 50% (12/24 bloques)`. |
| `w` | Espera a que termine el trabajo (con `pthread_join` o una variable de condición) y muestra el resultado. Sirve también para los scripts de prueba. |

- El trabajo corre en **un hilo en segundo plano**, lanzado desde el editor. El progreso (bloques hechos / totales) va en una estructura protegida por mutex.
- El prompt muestra el progreso mientras hay trabajo, p. ej. `edit [z 45%]> `, y avisa una vez cuando termina.
- Solo un trabajo a la vez: `z`/`x` se rechazan si ya hay uno activo.
- **Condición de carrera al editar mientras se comprime:** un `pthread_rwlock_t` sobre el archivo. El hilo de compresión toma el lock de lectura mientras lee; `p` también toma lectura (puede leer a la vez); `a` y `d` usan `pthread_rwlock_trywrlock` y, si está ocupado, muestran `Archivo ocupado: compresión en curso (45%)` **sin bloquear** el editor.
- `q` con un trabajo activo: esperar a que termine (join) antes de salir. No dejar hilos vivos.

### Sincronización (25 pts) y recursos (15 pts)

- Solo mutex, variables de condición, rwlock y `pthread_join`. **Cero busy waiting** (ningún `while(!listo);` ni `while(!listo) usleep()`).
- Todo `malloc` con `free`, todo `open` con `close`, todos los hilos con `join`.
- Errores de syscalls con el mismo patrón del Parcial 1 (`perror` / `LOG_SYSCALL_ERROR`). Los hilos no imprimen mientras el usuario escribe: guardan el error en el estado y el editor lo muestra en el siguiente prompt.
- Señales, lo mínimo: los hilos trabajadores bloquean `SIGINT` con `pthread_sigmask` para que la señal la maneje solo el hilo principal. Explicarlo en la sustentación.

### Corrección (15 pts)

Casos que deben funcionar: archivo vacío, 1 byte, un solo carácter repetido (árbol de un nodo: darle código de 1 bit), texto grande (~10 MB), binario aleatorio, tamaño que no sea múltiplo del bloque.

## Pruebas

Un solo script `shell/test_parcial2.sh`, en el estilo de `demo_test.sh`:
1. Genera los archivos de prueba de arriba, los comprime y descomprime a través del shell (mandando `z`, `w`, `x`, `w` por stdin) y verifica con `md5sum` y `cmp`. Imprime PASS/FAIL por caso.
2. Demo de no bloqueo: `z` sobre el archivo grande seguido de `s`, `p 1` y `a hola` (debe salir "ocupado"), y luego `w`.
3. Una corrida con `valgrind --leak-check=full` y otra con `valgrind --tool=helgrind` sobre el archivo mediano, para mostrar que no hay leaks ni carreras. Guarda el resumen como evidencia en `shell/evidencias/`.

`demo_test.sh` del Parcial 1 debe seguir pasando.

## Entregables

Crear `Parcial2_Concurrencia/ENTREGA/` con:
- `shell/` — código final (sin binarios, `.o` ni archivos de prueba).
- `SUSTENTACION.md` (y `Sustentacion.pdf` si `pandoc` está disponible). Corto y claro, organizado según los 5 criterios de la rúbrica: diagrama simple de hilos, tabla "recurso compartido → qué lo protege", por qué no hay deadlock, carreras ni busy waiting, y evidencia (md5, valgrind/helgrind).
- `GUION_VIDEO.md` — guion paso a paso (~8–10 min) con los comandos exactos de la demo. El video lo graba el usuario.

## Git

- Compilar con `-Wall -Wextra` sin warnings.
- Commits pequeños en español por cada paso (Makefile + huffman → compresión paralela → editor en segundo plano → pruebas → documentación), y `git push` a `origin main` al final.
- No subir binarios (`eafitOS`, `*.o`), archivos de prueba ni `.huf`/`.out` generados: agregarlos al `.gitignore`.
