# Parcial 2 — Sistemas Operativos: Concurrencia y Sincronización

> Transcripción en Markdown de `ENUNCIADO_Parcial2.pdf` (el PDF es la fuente oficial).

**Curso:** Sistemas Operativos
**Tema central:** Concurrencia, Hilos (Threads), Exclusión Mutua, Sincronización y Condiciones de Carrera.
**Modalidad:** Se debe seleccionar **una (1)** de las dos alternativas.

## Instrucciones generales

- Implementado en **C o C++** bajo entorno **POSIX/Linux**.
- Uso explícito y correcto de primitivas de concurrencia y sincronización (hilos `pthread`, mutex, variables de condición y/o semáforos).
- **No** se permiten librerías de alto nivel que resuelvan o abstraigan automáticamente la sincronización.

### Condiciones de entrega y sustentación

- Equipos de hasta 5 personas.
- Entrega por EAFIT Interactiva; cada miembro publica individualmente las evidencias.
- Fecha límite: **semana 12** de clase.
- Consultas: 3 horas en clase (asistencia obligatoria antes de pedir asesoría por Teams).
- Sustentación: video grabado o presencial (oficina 19-424, con cita). Se deben presentar y defender rigurosamente los argumentos de ingeniería de la solución.

---

## Alternativa 1: Compresor de Archivos Huffman Concurrente Integrado al Editor  ← **ELEGIDA**

### 1. Descripción general

Diseñar e implementar un módulo de compresión y descompresión basado en **Codificación de Huffman Concurrente**, integrado como funcionalidad en segundo plano (asíncrona) dentro del editor de texto del Parcial 1.

### 2. Requerimientos técnicos y de concurrencia

- **División y procesamiento paralelo:** el archivo se fragmenta en bloques (*chunks*) procesados concurrentemente por un **pool de hilos trabajadores** (*worker threads*).
  - *Fase de conteo de frecuencias:* múltiples hilos leen bloques en paralelo y acumulan frecuencias usando sincronización adecuada o **reducción local**.
  - *Fase de compresión/codificación:* tras construir el árbol de Huffman global, la codificación de cada bloque se hace en paralelo.
- **Sincronización y ensamblado:** un hilo coordinador o cola de salida sincronizada garantiza que los bloques comprimidos se escriban en el archivo de salida **en orden secuencial correcto**, sin bloqueos activos ni inconsistencias.
- **Integración con el editor (Parcial 1):**
  - Compresión/descompresión **no deben bloquear** la interfaz ni la interacción del editor: tarea en segundo plano (*background worker*).
  - El editor debe mostrar el **estado o progreso en tiempo real** (barra de progreso o porcentaje).
  - Manejar **condiciones de carrera** si el usuario intenta editar o guardar el archivo mientras se comprime/descomprime.

### 3. Rúbrica — Alternativa 1 (100 pts)

| Criterio | Descripción | Puntos |
|---|---|---|
| Concurrencia en Huffman | Paralelización efectiva del conteo de frecuencias y compresión por bloques usando hilos. Manejo ordenado de la salida. | 25 |
| Mecanismos de sincronización | Uso riguroso de mutexes, semáforos y/o variables de condición. Ausencia total de condiciones de carrera, interbloqueos y espera activa (*busy waiting*). | 25 |
| Integración con el editor | Ejecución en segundo plano sin congelar la UI/editor; sincronización de acceso al archivo/buffer ante modificaciones concurrentes. | 20 |
| Corrección algorítmica | Los archivos comprimidos se descomprimen idénticos al original (integridad comprobada con checksum/md5/diff). | 15 |
| Manejo de errores y recursos | Liberación de memoria dinámica, cierre de descriptores, join/detach limpio de hilos, control de excepciones/señales. | 15 |

---

## Alternativa 2: Reproductor de Audio y Gestión Concurrente de Playlist  (no elegida)

### 1. Descripción general

Construir o extender un reproductor de audio CLI cuyo foco es el diseño concurrente del motor de reproducción desacoplado de la gestión dinámica y concurrente de la playlist.

### 2. Requerimientos técnicos

- **Productor-consumidor:**
  - Hilo lector/decodificador (productor) deposita datos en un **búfer circular** compartido.
  - Hilo de salida (consumidor) extrae muestras a la frecuencia de muestreo y las envía a ALSA, PulseAudio, libao o similar.
  - Sincronización estricta del búfer para evitar *underrun* u overflow sin espera activa.
- **Playlist concurrente:** estructura compartida tipo **lectores-escritores** o cola concurrente segura. Mientras se reproduce, el usuario puede agregar, eliminar, reordenar, pausar, saltar o limpiar. Usar `pthread_rwlock` o mutex + variables de condición para evitar accesos a memoria liberada.
- **Control de reproducción:** Play, Pause, Stop, Next, Previous asíncronos e inmediatos.

### 3. Rúbrica — Alternativa 2 (100 pts)

| Criterio | Puntos |
|---|---|
| Gestión concurrente de playlist | 25 |
| Búfer de audio (productor-consumidor) | 25 |
| Control asíncrono de eventos | 20 |
| Robustez y ausencia de deadlocks | 15 |
| Diseño modular y buenas prácticas | 15 |
