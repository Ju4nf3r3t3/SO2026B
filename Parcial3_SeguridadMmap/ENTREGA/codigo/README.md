# mmap_guard — Seguridad de archivos mapeados con `mmap()` en la capa de kernel

Parcial 3 de Sistemas Operativos (EAFIT 2026-2). Juan Fernando Romero.

`mmap_guard` es un **módulo de kernel (LKM)** para Linux 5.x/6.x (x86_64 y arm64). Protege un archivo frente a abusos de `mmap()` y `mprotect()`. Engancha con **kretprobes** las funciones del framework LSM `security_mmap_file()` y `security_file_mprotect()`, y cuando una operación viola la política cambia su retorno a `-EACCES`. No hace falta recompilar el kernel.

## Política

| Regla | Se niega |
|---|---|
| R1 | Que un usuario distinto de `uid_autorizado` proyecte el archivo. |
| R2 | Proyectarlo como ejecutable (`PROT_EXEC`). |
| R3 | Proyectarlo con escritura compartida (`PROT_WRITE` + `MAP_SHARED`). La escritura privada (COW) sí se permite, porque no toca el disco. |
| R4 | Usar `mprotect()` para pasar un mapeo existente a ejecutable o a escritura compartida. |

Cada decisión queda en `/proc/mmap_guard` (contadores) y en `dmesg`.

## Requisitos

Ubuntu 22.04/24.04 (o similar) **con Secure Boot apagado**, porque el módulo no está firmado. Una sola vez:

```bash
sudo ./preparar_vm.sh      # build-essential, linux-headers, strace, perf, usuario 'atacante'
```

## Compilar

```bash
make            # genera mmap_guard.ko, poc_mmap y bench_mmap (sin advertencias)
make clean
```

## Probar

```bash
sudo ./test_parcial3.sh    # 9 ataques: primero SIN el módulo, luego CON el módulo -> PASS/FAIL
sudo ./medir_overhead.sh   # sobrecosto en mmap() y fallos de página; strace -c y perf stat
./verificar_ia.sh          # comprueba contra los headers las sugerencias erróneas de la IA
```

## Uso manual

```bash
sudo mkdir -p /srv/mmap_guard
echo "DATOS CONFIDENCIALES" | sudo tee /srv/mmap_guard/confidencial.txt
sudo chown $USER: /srv/mmap_guard/confidencial.txt

sudo insmod mmap_guard.ko archivo=/srv/mmap_guard/confidencial.txt uid_autorizado=$(id -u)
cat /proc/mmap_guard
./poc_mmap leer     /srv/mmap_guard/confidencial.txt     # permitido
./poc_mmap ejecutar /srv/mmap_guard/confidencial.txt     # NEGADO (R2)
sudo dmesg | grep mmap_guard
sudo rmmod mmap_guard
```

## Archivos

| Archivo | Qué es |
|---|---|
| `mmap_guard.c` | El módulo de kernel. |
| `poc_mmap.c` | Pruebas de concepto de ataque (lectura, escritura compartida y privada, ejecución, `mprotect`, Dirty COW). |
| `bench_mmap.c` | Medición de ns por `mmap()+munmap()` y por fallo de página. |
| `Makefile` | `all`, `test`, `bench`, `clean`. |
| `test_parcial3.sh`, `medir_overhead.sh`, `verificar_ia.sh`, `preparar_vm.sh` | Scripts de prueba, medición, verificación y preparación. |

El informe completo (10 preguntas, estrategia con IA, argumentación y mediciones) está en `../Informe_Parcial3.pdf`.
