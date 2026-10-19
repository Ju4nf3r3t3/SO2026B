# Guion del video — Parcial 2 (≈ 9 min)

Preparación (antes de grabar): terminal grande, fuente legible, ubicado en `Parcial2_Concurrencia/ENTREGA/shell`.
Al terminar, guardar el video como `ENTREGA/video_sustentacion.mp4` (el `.gitignore` excluye los `.mp4`, así que no se sube a GitHub).

```bash
make clean && make
# ~1,5 GB: el compresor va a ~270 MB/s con 4 hilos, así que con 10 MB terminaría antes de
# alcanzar a teclear. Con 1,5 GB la compresión y la descompresión duran ~10 s cada una (en esta VM).
# Ocupa ~5 GB en disco entre original, .huf y .out: borrarlos al terminar.
seq 1 25000000 | sed 's/$/: El veloz murcielago hindu comia feliz cardillo y kiwi./' > grande.txt
```

---

## 0. Presentación (0:00 – 0:45)

- "Soy Juan Fernando Romero, Sistemas Operativos SO2026B. Parcial 2, individual, alternativa 1: compresor Huffman concurrente integrado al editor del Parcial 1."
- Qué se va a mostrar: diseño, demo en vivo, pruebas de integridad, valgrind/helgrind.

## 1. Diseño en una diapositiva (0:45 – 2:30)

Mostrar el diagrama de hilos de `SUSTENTACION.md` y explicar:

1. El editor lanza **un hilo de fondo** con `pthread_create` y regresa al prompt.
2. **Fase 1:** 4 trabajadores leen bloques de 64 KiB con `pread`, cuentan en un histograma **local** y lo suman al global bajo un mutex (reducción local).
3. `pthread_join` → se construye el árbol global.
4. **Fase 2:** 4 trabajadores codifican en paralelo y dejan el resultado en `resultados[i]` + `cond_broadcast`. El coordinador escribe en orden esperando con `cond_wait`: **no hay busy waiting**.
5. El `rwlock` protege el archivo: compresión y `p` leen, `a`/`d` usan `trywrlock`.

## 2. Recorrido rápido por el código (2:30 – 4:00)

Abrir y señalar (sin leer todo):

- `compresor.c` → `tomar_bloque()` (contador bajo mutex) y `hilo_frecuencias()` (histograma local).
- `compresor.c` → bucle del escritor en `hilo_comprimir()`: `while (!resultados[i].listo && error == 0) pthread_cond_wait(...)`.
- `compresor.c` → `compresor_iniciar()`: `pthread_sigmask` bloquea SIGINT antes de crear el hilo.
- `editor.c` → `a`/`d` con `pthread_rwlock_trywrlock` y el prompt `edit [z 45%]>`.
- `huffman.c` → caso de un solo símbolo: código `"0"`.

## 3. Demo en vivo: el editor no se congela (4:00 – 6:00)

```
./eafitOS
eafitOS> edit grande.txt
edit> z
edit [z 12%]> s
edit [z 30%]> p 1
edit [z 41%]> a hola
edit [z 55%]> d 1
edit [z 70%]> z
edit [z 85%]> w
edit> x grande.txt.huf
edit [x 20%]> s
edit [x 60%]> w
edit> q
eafitOS> exit
```

Puntos a decir mientras se teclea:
- `z` regresa enseguida y el prompt muestra el porcentaje.
- `s` muestra la barra de progreso y `p 1` funciona durante la compresión (también es lector).
- `a hola` y `d 1` responden **"Archivo ocupado: compresión en curso (xx%)"** y el editor no se bloquea.
- El segundo `z` se rechaza porque solo puede haber un trabajo a la vez.
- `w` espera con `pthread_join`. Sin trabajo activo, `a`/`d` vuelven a funcionar (aquí no se usan, para que el `cmp` final compare contra el mismo archivo que se comprimió).
- Teclear rápido después de `z`. Si no da tiempo, pegar de una vez el bloque `s`, `p 1`, `a hola`, `d 1`, `z` (cada línea se procesa mientras la compresión sigue corriendo).
- La descompresión es secuencial y más lenta, así que ahí hay más tiempo para mostrar `s`.

Mostrar también Ctrl+C con un trabajo en curso: imprime el aviso y el programa no muere. Luego `q` espera a que termine el trabajo.

Verificar el resultado:

```bash
cmp grande.txt grande.txt.out && echo IDENTICOS
md5sum grande.txt grande.txt.out
ls -l grande.txt grande.txt.huf
rm -f grande.txt grande.txt.huf grande.txt.out
```

## 4. Pruebas automáticas (6:00 – 7:30)

```bash
./test_parcial2.sh
```

- Señalar las líneas **PASS**: vacío, 1 byte, un solo carácter, binario aleatorio, tamaño no múltiplo, mediano y grande.
- Señalar la demo de no bloqueo ("ocupado") y el resumen de valgrind/helgrind.
- `./demo_test.sh` (Parcial 1) sigue funcionando.

## 5. Evidencia de recursos y sincronización (7:30 – 8:30)

```bash
grep -E "All heap blocks|ERROR SUMMARY|FILE DESCRIPTORS" evidencias/valgrind_memcheck.txt
grep "ERROR SUMMARY" evidencias/valgrind_helgrind.txt
```

- Memcheck: todos los bloques liberados, 0 errores, sin descriptores abiertos de más.
- Helgrind: 0 errores, es decir, ninguna condición de carrera ni violación de orden de locks.

## 6. Cierre (8:30 – 9:00)

Resumir los argumentos:
- **Sin deadlock:** nunca se tienen dos mutex a la vez, y el editor solo usa `trywrlock`.
- **Sin carreras:** tabla recurso → lock, confirmada por helgrind.
- **Sin busy waiting:** solo `cond_wait` y `pthread_join`.
- **Recursos:** todo `malloc` tiene su `free`, todo `open` su `close`, todo hilo su join. SIGINT solo lo atiende el hilo principal.
