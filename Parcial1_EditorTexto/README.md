# Parcial 1 — Editor de Texto CLI integrado al shell (ENTREGADO)

Estado: **entregado y cerrado**. No modificar esta carpeta; el Parcial 2 trabaja sobre una copia en `../Parcial2_Concurrencia/shell/`.

## Contenido

| Ruta | Qué es |
|---|---|
| `ENUNCIADO_Parcial1.md` | Enunciado del parcial en Markdown + notas de arquitectura. |
| `ENTREGA/shell/` | Código entregado: shell `eafitOS` + comando `edit` (`editor.c`). |
| `ENTREGA/Sustentacion.pdf` | Documento de sustentación. |
| `ENTREGA/VideoSustentacionParcial1.mp4` | Video de sustentación (**solo local**, pesa >100MB, está en `.gitignore`). |
| `ENTREGA.zip` | Zip subido a EAFIT Interactiva (**solo local**, en `.gitignore`). |
| `PARA_MI/` | Notas personales y guion del video (**solo local**, en `.gitignore`). |

## Compilar y probar (Linux)

```bash
cd ENTREGA/shell
make            # compila eafitOS y lo ejecuta
./demo_test.sh  # pruebas automáticas del editor
```

Comandos del editor (`edit [archivo]` desde el shell): `o <archivo>`, `p [n]`, `a <texto>`, `d <n>`, `q`.
