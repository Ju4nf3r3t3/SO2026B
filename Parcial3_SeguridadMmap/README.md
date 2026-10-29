# Parcial 3: Seguridad de archivos en la capa de kernel y `mmap()` (25 %)

Solución: **`mmap_guard`**, un módulo de kernel que engancha con kretprobes `security_mmap_file()` y `security_file_mprotect()`. Con eso bloquea los mapeos peligrosos de un archivo protegido y los audita. Está compilado y probado en Ubuntu 24.04, kernel 6.8: 9/9 pruebas, compilación sin advertencias y 0 errores del kernel en la prueba de estrés.

## Por dónde empezar

1. **`PARA_MI/explicacion.md`**: todo explicado en palabras simples, con las preguntas que te pueden hacer. Léelo primero.
2. **`PARA_MI/guion_video.md`**: el guion del video paso a paso (HACER / EN PANTALLA / DECIR). Tiene también una versión en PDF para tenerla abierta mientras grabas.
3. **`ENTREGA.zip`**: lo que se sube a EAFIT Interactiva. Después de grabar, agrégale el video (ver el final del guion).

## Contenido

| Ruta | Qué es |
|---|---|
| `ENUNCIADO_Parcial3.md`, `infograma_mmap.png` | Enunciado y rúbrica. |
| `ENTREGA/Informe_Parcial3.pdf` (y `.md`) | Informe: 10 preguntas, estrategia con IA, argumentación y mediciones. |
| `ENTREGA/codigo/` | Módulo `mmap_guard.c`, ataques `poc_mmap.c`, `bench_mmap.c`, `Makefile` y scripts. |
| `ENTREGA/evidencias/` | Salidas reales: compilación, pruebas, sobrecosto, estabilidad, verificación de IA y eBPF. |
| `ENTREGA.zip` | La carpeta `ENTREGA` comprimida (**solo local**, en `.gitignore`). |
| `PARA_MI/` | Guion y explicación (**solo local**, en `.gitignore`). |

## Probar en Linux (Ubuntu con Secure Boot apagado)

```bash
cd ENTREGA/codigo
sudo ./preparar_vm.sh       # una sola vez
make
sudo ./test_parcial3.sh     # 9 ataques sin y con el módulo -> 9/9 PASS
sudo ./medir_overhead.sh    # sobrecosto
./verificar_ia.sh           # alucinaciones de la IA verificadas contra los headers
```
