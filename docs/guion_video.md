# Guion del video — Editor de Texto CLI integrado a eafitOS

Duración objetivo: 5 minutos (máximo 7). Grabar pantalla + voz. No hace falta cámara.

---

## 0. Antes de grabar

- Terminal grande, fuente legible, tema con buen contraste.
- Tener listo en otra pestaña/editor: `shell/editor.c` (para mostrar código puntual, no leerlo entero).
- Compilar una vez antes de grabar (`make clean && make`) para asegurarte de que corre sin errores.
- Tener un archivo de prueba ya borrado (`rm -f demo.txt`) para que la demo cree el archivo en vivo.

---

## 1. Intro y objetivo (0:00 – 0:45)

**Decir (aprox.):**
"Este proyecto es un editor de texto de línea de comandos, escrito en C, integrado al shell educativo de la clase. El objetivo era usar únicamente llamadas al sistema POSIX de bajo nivel — `open`, `read`, `write`, `lseek`, `ftruncate`, `close` — sin usar nunca `fopen` ni las demás funciones de la librería estándar de E/S con buffer, para manipular el archivo que se está editando."

**Mostrar en pantalla:** el enunciado / la tabla de la sección 3 (permitido vs. prohibido), o simplemente el título del proyecto.

## 2. Arquitectura y punto de integración con el shell (0:45 – 2:00)

**Decir:**
"El shell ya tenía cuatro categorías de comandos: datos, memoria, monitoreo y utilidades, cada una en su propio archivo `cat_*.c`, registradas en una tabla global `commands[]` en `main.c`."

**Mostrar:** `shell.h` (la struct `Command`) y la tabla `commands[]` en `main.c` (solo la parte relevante, no todo el archivo).

**Decir (la decisión de diseño clave):**
"Para el editor, decidí crear una categoría nueva llamada `editor`, en vez de meterlo dentro de `datos`. La razón: los comandos de `datos` son operaciones de un solo disparo — reciben argumentos, hacen su trabajo y regresan al prompt. El editor, en cambio, abre su propio sub-modo interactivo: uno entra con `edit`, y se queda ahí ejecutando comandos (`o`, `p`, `a`, `d`) hasta escribir `q`. Es un patrón distinto, así que merece su propia categoría."

"También decidí que el editor corre in-process, en el mismo proceso del shell, sin usar `fork` ni `execvp`. No es un programa externo como los que lanza `p_exec`, es lógica propia del shell, así que no hay razón para aislarlo en otro proceso — simplemente se registra como un handler más en la tabla de comandos, y cuando el usuario escribe `q`, la función hace `close()` y retorna, devolviendo el control al bucle principal del shell."

**Mostrar:** el comando `edit` registrado en `commands[]`, y el inicio de `cmd_edit()` en `editor.c`.

## 3. Demo en vivo (2:00 – 4:00)

**Decir:** "Vamos a verlo funcionando."

Ejecutar el shell (`./eafitOS`) y en vivo:

1. `edit demo.txt` → mostrar que abre/crea el archivo (se ve el `open()` trazado en pantalla).
2. `a Primera linea`, `a Segunda linea`, `a Tercera linea` → explicar brevemente que cada `a` hace `lseek` al final + `write`.
3. `p` → imprime todo el archivo.
4. `p 2` → imprime solo la línea 2 (explicar: se escanea el archivo contando `\n` hasta encontrar esa línea, sin cachear un índice — decisión de simplicidad).
5. `d 2` → borrar la línea 2. Explicar en una frase: "esto lee el resto del archivo después de la línea borrada, lo reescribe desde el offset donde empezaba la línea eliminada, y trunca el archivo con `ftruncate`".
6. `p` → confirmar que la línea desapareció y las demás se recorrieron bien.
7. `q` → salir del editor, volver al prompt `eafitOS>`.
8. (Opcional) `d_read demo.txt` → usar el comando ya existente del shell para confirmar que el archivo en disco quedó bien escrito.

## 4. Manejo de errores (4:00 – 4:45)

**Decir:** "El editor también valida errores en vez de fallar silenciosamente."

Mostrar en vivo:
- `p 99` sobre un archivo que no tiene esa línea → mensaje de error controlado, no un crash.
- (Opcional) `p` sin haber abierto ningún archivo (`edit` sin argumento seguido de `p`) → mensaje de error controlado.

**Decir:** "Todas las syscalls están envueltas en funciones que revisan si devuelven -1 y usan `perror()` para reportar el error del sistema, siguiendo el mismo patrón que ya usaba el resto del shell."

## 5. Cierre (4:45 – 5:00)

**Decir:** "En resumen: el editor cumple los 5 comandos base usando solo syscalls de bajo nivel, se integró como una categoría nueva del shell por su naturaleza interactiva, corre in-process porque es lógica propia del shell, y maneja errores de forma consistente. El detalle completo de estas decisiones está en el PDF de sustentación."

---

**Nota:** el guion cubre exactamente lo que pide la rúbrica de entregables (15%): claridad del video, y sirve de apoyo para no improvisar frente a cámara. No es necesario leerlo palabra por palabra — es un mapa de qué mostrar y qué decir en cada minuto.
