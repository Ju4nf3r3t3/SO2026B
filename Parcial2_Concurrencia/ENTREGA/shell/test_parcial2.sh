#!/bin/bash
# Pruebas del Parcial 2: compresor Huffman concurrente integrado al editor.
# Pensado para correr en Linux. Compila el shell, genera archivos de prueba y los
# comprime/descomprime enviando comandos al editor por stdin (z, w, x, w).
# Verifica la integridad con md5sum y cmp, muestra la demo de no bloqueo y corre
# valgrind (memcheck y helgrind) guardando la evidencia en evidencias/.

cd "$(dirname "$0")"

echo "=== Compilando (make all) ==="
make clean > /dev/null
make all || exit 1

FALLOS=0

# Comprime y descomprime un archivo a través del editor y compara con el original.
probar() {
    local archivo="$1"
    rm -f "$archivo.huf" "$archivo.out"
    {
        echo "edit $archivo"
        echo "z"
        echo "w"
        echo "x $archivo.huf"
        echo "w"
        echo "q"
        echo "exit"
    } | ./eafitOS > /dev/null 2>&1

    local md5_orig md5_out
    md5_orig=$(md5sum < "$archivo" | cut -d' ' -f1)
    md5_out=$(md5sum < "$archivo.out" 2>/dev/null | cut -d' ' -f1)
    if [ -f "$archivo.out" ] && cmp -s "$archivo" "$archivo.out"; then
        printf "PASS  %-26s %10s bytes -> %10s bytes  md5 %s\n" "$archivo" \
            "$(stat -c %s "$archivo")" "$(stat -c %s "$archivo.huf")" "$md5_orig"
    else
        printf "FAIL  %-26s md5 original %s / descomprimido %s\n" "$archivo" "$md5_orig" "$md5_out"
        FALLOS=$((FALLOS + 1))
    fi
}

echo
echo "=== Generando archivos de prueba ==="
: > prueba_vacio.txt                                       # 0 bytes
printf 'A' > prueba_1byte.txt                              # 1 byte
head -c 200000 /dev/zero | tr '\0' 'a' > prueba_repetido.txt  # un solo carácter (árbol de un nodo)
head -c 1000000 /dev/urandom > prueba_binario.bin          # binario aleatorio
head -c 100003 /dev/urandom | base64 > prueba_no_multiplo.txt # tamaño que no es múltiplo de 64 KiB
seq 1 20000 | sed 's/$/: El veloz murcielago hindu comia feliz cardillo y kiwi./' > prueba_mediano.txt
seq 1 170000 | sed 's/$/: El veloz murcielago hindu comia feliz cardillo y kiwi./' > prueba_grande.txt  # ~10 MB
ls -l prueba_*

echo
echo "=== Caso 1: integridad (comprimir -> descomprimir -> md5sum/cmp) ==="
for f in prueba_vacio.txt prueba_1byte.txt prueba_repetido.txt prueba_binario.bin \
         prueba_no_multiplo.txt prueba_mediano.txt prueba_grande.txt; do
    probar "$f"
done

echo
echo "=== Caso 2: el editor no se bloquea mientras comprime ==="
echo "(z sobre el archivo grande, luego s, p 1 y 'a hola' -> debe decir 'ocupado'; luego w)"
cp prueba_grande.txt prueba_demo.txt
md5_antes=$(md5sum < prueba_demo.txt)
{
    echo "edit prueba_demo.txt"
    echo "z"
    echo "s"
    echo "p 1"
    echo "a hola"
    echo "w"
    echo "s"
    echo "q"
    echo "exit"
} | ./eafitOS 2>&1 | grep -E "Comprimiendo|bloques|El veloz|ocupado|Esperando|terminado|añadida"
if [ "$md5_antes" == "$(md5sum < prueba_demo.txt)" ]; then
    echo "PASS  el archivo no se modificó durante la compresión"
else
    echo "(nota: 'a hola' alcanzó a ejecutarse después de terminar la compresión)"
fi

echo
echo "=== Caso 3: valgrind (memcheck y helgrind) sobre el archivo mediano ==="
mkdir -p evidencias
if command -v valgrind > /dev/null; then
    ENTRADA="edit prueba_mediano.txt\nz\nw\nx prueba_mediano.txt.huf\nw\nq\nexit\n"
    printf "$ENTRADA" | valgrind --leak-check=full --show-leak-kinds=all --track-fds=yes \
        ./eafitOS > /dev/null 2> evidencias/valgrind_memcheck.txt
    printf "$ENTRADA" | valgrind --tool=helgrind \
        ./eafitOS > /dev/null 2> evidencias/valgrind_helgrind.txt
    echo "--- memcheck (evidencias/valgrind_memcheck.txt) ---"
    grep -E "in use at exit|All heap blocks|definitely lost|ERROR SUMMARY|FILE DESCRIPTORS" evidencias/valgrind_memcheck.txt
    echo "--- helgrind (evidencias/valgrind_helgrind.txt) ---"
    grep -E "ERROR SUMMARY" evidencias/valgrind_helgrind.txt
else
    echo "valgrind no está instalado: sudo apt-get install -y valgrind"
fi

echo
echo "=== Evidencia de integridad (evidencias/md5.txt) ==="
md5sum prueba_*.txt prueba_*.bin prueba_*.out 2>/dev/null | grep -v prueba_demo | sort -k2 | tee evidencias/md5.txt

echo
if [ "$FALLOS" -eq 0 ]; then
    echo "=== Todas las pruebas de integridad pasaron ==="
else
    echo "=== $FALLOS prueba(s) fallaron ==="
    exit 1
fi
