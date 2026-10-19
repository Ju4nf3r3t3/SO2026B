# Instrucciones para Claude — Parcial 2 (Concurrencia y Sincronización)

Estás en una **VM Ubuntu Linux**. Tu tarea es implementar completo el Parcial 2, **Alternativa 1: Compresor Huffman concurrente integrado al editor**, compilarlo, probarlo y dejar lista la carpeta de entrega. Escribe código, comentarios y documentación **en español**, con el mismo estilo del código existente (comentarios en bloque `/** ... */`, trazas `LOG_SYSCALL`, colores `COLOR_*`).

- Enunciado completo: `ENUNCIADO_Parcial2.md` (fuente oficial: `ENUNCIADO_Parcial2.pdf`). Léelo primero, sobre todo la rúbrica.
- Código base: `shell/` es una **copia** del shell `eafitOS` + editor entregado en el Parcial 1. Trabaja solo aquí.
- **No modifiques** `../Parcial1_EditorTexto/` (ya entregado) ni las carpetas de clase en la raíz (`shell/`, `Banderas_CLI/`, `callsys/`).

## 0. Preparar el entorno

```bash
sudo apt-get update
sudo apt-get install -y build-essential valgrind
cd Parcial2_Concurrencia/shell && make clean
```

Nota: el `Makefile` actual ejecuta el binario al final de `all`. Cámbialo para que `all` solo compile, agrega un target `run`, y agrega `-pthread` a `CFLAGS` y `LDFLAGS`. Ajusta `demo_test.sh` en consecuencia.

## 1. Lo que hay hoy (Parcial 1)

- `main.c`: tabla `commands[]` de `Command` (ver `shell.h`), parser y REPL del shell (`eafitOS> `).
- `editor.c`: comando `edit [archivo]`, sub-REPL `edit> ` con `o <archivo>`, `p [n]`, `a <texto>`, `d <n>`, `q`. Un solo archivo abierto en `g_fd`. Todo el I/O del archivo usa syscalls (`open/read/write/lseek/ftruncate/close`), **nunca** `fopen/fread`. Mantén esa regla también en el compresor.

## 2. Lo que hay que construir

### 2.1 Nuevos comandos del editor (dentro de `edit>`)

| Comando | Qué hace |
|---|---|
| `z [salida]` | Comprime el archivo abierto en segundo plano. Salida por defecto: `<archivo>.huf`. Regresa al prompt **de inmediato**. |
| `x <archivo.huf> [salida]` | Descomprime en segundo plano. Salida por defecto: el nombre sin `.huf` + `.out` (no pisar el original sin querer). |
| `s` | Estado del trabajo en curso: barra de progreso `[#####.....] 50% (12/24 bloques) fase: codificando`. |
| `w` | Espera mostrando la barra actualizándose en vivo hasta que termine (con `pthread_cond_timedwait`, **no** con un loop de `sleep`/polling). |
| `c` | Cancela el trabajo en curso (bandera de cancelación + limpieza; borra el archivo de salida parcial). |

Además:
- El **prompt** muestra el estado cuando hay un trabajo activo, p. ej. `edit [z 45%]> `, para que el progreso se vea "en tiempo real" en cada interacción.
- Al terminar un trabajo, el siguiente prompt imprime un aviso una vez: `[huffman] compresión terminada: 1.2MB -> 640KB (53.3%) en 0.21s`.
- `q` con un trabajo activo: preguntar o esperar/cancelar limpio (join del hilo); nunca salir dejando hilos vivos.
- Actualiza `help`/la tabla de comandos (descripción y syscalls de `edit`) y el mensaje de "comando no reconocido".

### 2.2 Arquitectura sugerida (archivos nuevos)

- `huffman.h / huffman.c` — núcleo **sin hilos**: histograma de un bloque, construcción del árbol (min-heap), tabla de códigos (longitud ≤ 64 bits; usa códigos canónicos o un árbol con límite), codificar un bloque a un buffer de bits, decodificar un bloque. Testeable aislado.
- `threadpool.h / threadpool.c` — pool fijo de N hilos (N = `sysconf(_SC_NPROCESSORS_ONLN)`, mínimo 2) con **cola de tareas acotada protegida por mutex + 2 variables de condición** (`not_empty`, `not_full`). Apagado limpio con bandera `shutdown` + `pthread_cond_broadcast` + `pthread_join`.
- `compressor.h / compressor.c` — orquesta las fases de compresión/descompresión usando el pool y expone el estado de progreso.
- `bgjob.h / bgjob.c` — el "background worker" del editor: un hilo que corre el trabajo, estructura de estado compartida (`fase`, `bloques_hechos`, `bloques_totales`, `bytes_in/out`, `error`, `cancelado`, `terminado`) protegida por un mutex + `cond` para `w`.
- `editor.c` — integra los comandos nuevos y el bloqueo del archivo.

### 2.3 Algoritmo de compresión (por bloques)

1. Tamaño de bloque fijo (p. ej. 64 KiB; constante configurable). `fstat` para el tamaño → número de bloques.
2. **Fase 1 — conteo de frecuencias en paralelo con reducción local:** cada tarea lee su bloque con **`pread`** (no `lseek`+`read`: `pread` no comparte offset entre hilos, así no hay carrera sobre el offset del fd) y llena un histograma **local** `uint64_t[256]`. Al terminar, lo suma al histograma global bajo un mutex (una sola sección crítica corta por bloque). Explica esta decisión en la doc (reducción local vs. mutex por byte vs. atómicos).
3. Barrera: el coordinador espera a que terminen todas las tareas de la fase 1 (contador + condvar, o `pthread_barrier`), construye el árbol global y la tabla de códigos (secuencial, es O(256)).
4. **Fase 2 — codificación en paralelo:** cada tarea vuelve a leer su bloque con `pread`, lo codifica a un buffer propio y lo entrega a la cola de salida.
5. **Ensamblado ordenado:** un **hilo escritor** mantiene `next_to_write`. Los workers depositan el resultado del bloque `i` en un arreglo de slots y hacen `pthread_cond_broadcast`; el escritor espera con `pthread_cond_wait` mientras `slot[next_to_write]` no esté listo, lo escribe con `write`, lo libera y avanza. Limitar bloques en vuelo (semáforo `sem_t` contado o condvar) para que la memoria no crezca sin control con archivos grandes.
6. Formato de archivo `.huf` (documentarlo en el README):
   ```
   magic "HUF1" | uint64 tamaño_original | uint32 tam_bloque | uint32 num_bloques
   | uint64 frecuencias[256]
   | por bloque: uint32 bytes_originales | uint32 bytes_comprimidos | datos
   ```
   Guardar el tamaño de cada bloque permite **descomprimir también en paralelo**: el lector del índice calcula offsets, los workers decodifican con `pread` y el escritor ensambla en orden con el mismo mecanismo.
7. Casos borde obligatorios: archivo vacío, archivo de 1 byte, archivo con **un solo símbolo** repetido (árbol de un nodo → asignar código de 1 bit), archivo binario aleatorio (compresión >100%, debe funcionar igual), tamaño no múltiplo del bloque, archivo `.huf` corrupto/truncado (error controlado, no segfault).

### 2.4 Condiciones de carrera con el editor (20 pts de la rúbrica)

- Un `pthread_rwlock_t` asociado al archivo abierto en el editor:
  - El trabajo de compresión toma **lock de lectura** sobre el archivo fuente durante toda la lectura.
  - `p` (leer) toma lock de lectura → puede convivir con la compresión.
  - `a` y `d` (escribir) usan `pthread_rwlock_trywrlock`; si falla, **no bloquean la UI**: imprimen `Archivo ocupado: compresión en curso (45%). Usa 's', 'w' o 'c'.`
  - `o` (abrir otro archivo) mientras hay un trabajo sobre el actual: rechazar o esperar, decidir y justificar.
- La descompresión cuyo destino sea el archivo abierto debe tomar el lock de **escritura**.
- Solo un trabajo en segundo plano a la vez (o una cola; justificar). Rechazar `z`/`x` si ya hay uno activo.
- El `g_fd` del editor no se comparte con los workers: el trabajo abre **su propio fd** (`open` O_RDONLY) para no compartir el offset del archivo.

### 2.5 Recursos, errores y señales (15 pts)

- Todo `malloc` con su `free`, todo `open` con su `close`, todos los hilos con `pthread_join` (o `detach` justificado).
- En los hilos del pool y en el background bloquear `SIGINT` con `pthread_sigmask`, para que la señal le llegue al hilo principal. Ctrl+C en el editor con un trabajo activo → cancela el trabajo de forma limpia (handler que solo pone una bandera `volatile sig_atomic_t`), no mata el shell.
- Reportar errores de syscalls con las macros `LOG_SYSCALL_ERROR`/`perror` del proyecto. Desde los workers **no imprimir directo** a stdout mientras el usuario escribe: guardar el error en el estado compartido y mostrarlo en el prompt.
- **Cero espera activa**: ningún `while(!listo);` ni `while(!listo) usleep(...)`. Solo `pthread_cond_wait/timedwait`, `sem_wait`, `pthread_join`.

## 3. Pruebas obligatorias (y dejarlas como scripts)

Crea `shell/tests/` con:

1. `test_huffman.sh`: genera archivos de prueba (vacío, 1 byte, `aaaa…` 1MB, texto grande —p. ej. repetir `/usr/share/common-licenses/GPL-3` hasta ~20MB—, binario aleatorio con `head -c 20M /dev/urandom`, tamaño no múltiplo de 64KiB), comprime y descomprime cada uno vía el shell (stdin con `w` para esperar) y verifica con `cmp` **y** `md5sum`. Debe imprimir PASS/FAIL por caso y salir con código ≠ 0 si algo falla.
2. Prueba de no-bloqueo: lanzar `z` sobre el archivo de 20MB y en las líneas siguientes mandar `s`, `p 1`, `a texto` (debe ser rechazado con el mensaje de ocupado), y luego `w`. Guardar la salida como evidencia.
3. Prueba de cancelación (`c`) y de `q` con trabajo activo.
4. Ejecutar con `valgrind --leak-check=full --error-exitcode=1` (sin leaks) y con `valgrind --tool=helgrind` y/o compilar con `-fsanitize=thread` (target `make tsan`) — sin reportes de data race. Guarda los resúmenes de salida en `shell/tests/evidencias/`.
5. Medir tiempos con 1, 2, 4 y N hilos sobre el archivo grande (variable de entorno o argumento para fijar los hilos, p. ej. `HUF_THREADS`) y dejar una tabla de speedup en la documentación.

Actualiza `demo_test.sh` para que siga pasando las pruebas del Parcial 1 y agregue una demo corta del compresor.

## 4. Documentación y entregables

Crea `Parcial2_Concurrencia/ENTREGA/` con:
- `shell/` — copia final del código (sin binarios ni `.o`).
- `README.md` dentro de `shell/`: cómo compilar, comandos nuevos, formato `.huf`.
- `SUSTENTACION.md` (y si `pandoc` está disponible, también `Sustentacion.pdf`): argumentos de ingeniería que hay que defender, organizados según la rúbrica:
  - Diagrama (ASCII o Mermaid) de hilos: UI/editor ↔ background job ↔ pool de workers ↔ hilo escritor.
  - **Protocolo de concurrencia**: tabla de cada recurso compartido → primitiva que lo protege → quién lo lee/escribe.
  - Por qué no hay deadlock (orden de adquisición de locks, nunca se sostiene un lock mientras se hace I/O bloqueante de usuario, etc.), por qué no hay busy waiting, por qué no hay carreras (evidencia helgrind/TSan).
  - Reducción local vs. alternativas; `pread` vs. `lseek`+`read`; tamaño de bloque elegido; límite de bloques en vuelo.
  - Resultados: integridad (md5) y tabla de speedup.
- `GUION_VIDEO.md`: guion paso a paso (~10 min) para grabar la sustentación con los comandos exactos a correr en la demo.

El video lo graba el usuario; no intentes generarlo.

## 5. Flujo de trabajo y git

- Trabaja por fases y compila con `-Wall -Wextra` **sin warnings** en cada paso: (1) Makefile + `huffman.c` probado aislado, (2) pool + compresión paralela con verificación md5, (3) descompresión paralela, (4) background job + integración con el editor + locks, (5) señales/cancelación, (6) tests + valgrind/TSan, (7) documentación y `ENTREGA/`.
- Haz commits pequeños y descriptivos en español al terminar cada fase, y `git push` al final (rama `main`, remoto `origin`).
- No subas binarios (`eafitOS`, `*.o`), archivos de prueba grandes ni `.huf` generados: agrégalos al `.gitignore`.
- Si algo del enunciado es ambiguo, toma la decisión más defendible, implémentala y **documenta la justificación** en `SUSTENTACION.md` (la sustentación evalúa justamente esos argumentos).
