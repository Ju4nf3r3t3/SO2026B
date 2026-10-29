#!/bin/bash
# =============================================================================
# test_parcial3.sh - Pruebas de seguridad del módulo mmap_guard
#
# Corre 9 ataques/accesos sobre un archivo protegido, dos veces:
#   1) SIN el módulo  -> muestra qué tan vulnerable es el sistema por defecto.
#   2) CON el módulo  -> verifica que cada caso dé el resultado esperado (PASS/FAIL)
#                        y que el archivo en disco nunca cambie (md5sum).
#
# Uso:  sudo ./test_parcial3.sh
# =============================================================================

set -u
cd "$(dirname "$0")"

VERDE='\033[1;32m'; ROJO='\033[1;31m'; AMARILLO='\033[1;33m'; AZUL='\033[1;36m'; NC='\033[0m'

if [ "$(id -u)" -ne 0 ]; then
    echo "Este script carga un módulo de kernel: ejecútalo con  sudo ./test_parcial3.sh"
    exit 1
fi

DUENO=${SUDO_USER:-$(logname)}          # usuario autorizado (el que corre sudo)
UID_DUENO=$(id -u "$DUENO")
ATACANTE=atacante                        # usuario sin autorización
DIR=/srv/mmap_guard
ARCHIVO=$DIR/confidencial.txt
POC=$DIR/poc_mmap                        # copia accesible para ambos usuarios
CONTENIDO="DATOS CONFIDENCIALES: nomina 2026 - no modificar"

# ---------------------------------------------------------------- preparación
echo -e "${AZUL}=== Preparación ===${NC}"
sudo -u "$DUENO" make -s all || { echo -e "${ROJO}Falló la compilación${NC}"; exit 1; }
id "$ATACANTE" >/dev/null 2>&1 || useradd -M -s /usr/sbin/nologin "$ATACANTE"
mkdir -p "$DIR" && chmod 755 "$DIR"
install -m 755 poc_mmap "$POC"
lsmod | grep -q '^mmap_guard' && rmmod mmap_guard

restaurar() {      # mismo inodo: '>' trunca y reescribe, no crea otro archivo
    printf '%s\n' "$CONTENIDO" > "$ARCHIVO"
    chown "$DUENO:" "$ARCHIVO"
    chmod 644 "$ARCHIVO"                 # rw-r--r--: SIN bit de ejecución
}
restaurar
MD5_ORIGINAL=$(md5sum < "$ARCHIVO")
echo "Archivo protegido : $ARCHIVO  (permisos $(stat -c %A "$ARCHIVO"), dueño $DUENO uid=$UID_DUENO)"
echo "Usuario atacante  : $ATACANTE uid=$(id -u $ATACANTE)"
echo "Kernel            : $(uname -r) ($(uname -m))"

# Casos: "usuario|modo|resultado esperado con módulo (0=permitido 1=negado)|regla"
CASOS=(
    "$DUENO|leer|0|uso legítimo"
    "$ATACANTE|leer|1|R1 usuario no autorizado"
    "$DUENO|escribir_compartido|1|R3 escritura MAP_SHARED"
    "$DUENO|escribir_privado|0|COW privado (no toca el disco)"
    "$DUENO|ejecutar|1|R2 PROT_EXEC"
    "$DUENO|mprotect_escritura|1|R4 mprotect a escritura"
    "$DUENO|mprotect_ejecucion|1|R4 mprotect a ejecución"
    "$ATACANTE|carrera|1|R1 + ataque Dirty COW"
    "$DUENO|carrera|1|carrera sobre mapeo privado"
)

declare -a SIN CON VEREDICTO
PASAN=0

# Corre un caso: imprime la salida del PoC y deja en RES el código y en INTEGRO si
# el archivo quedó igual.
correr() {
    local usuario=$1 modo=$2
    sudo -u "$usuario" "$POC" "$modo" "$ARCHIVO" 2
    RES=$?
    if [ "$(md5sum < "$ARCHIVO")" = "$MD5_ORIGINAL" ]; then INTEGRO=si; else INTEGRO=NO; fi
}

texto() {   # texto del resultado según el modo
    if [ "$2" = carrera ]; then
        [ "$1" -eq 0 ] && echo "ataque exitoso" || echo "ataque fallido"
    else
        [ "$1" -eq 0 ] && echo "permitido" || echo "negado"
    fi
}

# ------------------------------------------------------------ fase 1: sin módulo
echo -e "\n${AZUL}=== FASE 1: SIN el módulo (comportamiento por defecto de Linux) ===${NC}"
for i in "${!CASOS[@]}"; do
    IFS='|' read -r usuario modo esperado regla <<< "${CASOS[$i]}"
    echo -e "${AMARILLO}[caso $((i+1))] $usuario -> $modo${NC}"
    correr "$usuario" "$modo"
    SIN[$i]="$(texto $RES $modo), archivo íntegro: $INTEGRO"
    [ "$INTEGRO" = NO ] && echo -e "  ${ROJO}!! el archivo en disco fue MODIFICADO: $(head -c 40 "$ARCHIVO")${NC}"
    restaurar
done

# ------------------------------------------------------------ fase 2: con módulo
echo -e "\n${AZUL}=== FASE 2: CON el módulo mmap_guard ===${NC}"
dmesg -C 2>/dev/null
insmod mmap_guard.ko archivo="$ARCHIVO" uid_autorizado="$UID_DUENO" || { echo -e "${ROJO}insmod falló${NC}"; exit 1; }
echo "insmod mmap_guard.ko archivo=$ARCHIVO uid_autorizado=$UID_DUENO"

for i in "${!CASOS[@]}"; do
    IFS='|' read -r usuario modo esperado regla <<< "${CASOS[$i]}"
    echo -e "${AMARILLO}[caso $((i+1))] $usuario -> $modo   (esperado: $(texto "$esperado" "$modo"), $regla)${NC}"
    correr "$usuario" "$modo"
    CON[$i]="$(texto $RES $modo), archivo íntegro: $INTEGRO"
    if [ "$RES" -eq "$esperado" ] && [ "$INTEGRO" = si ]; then
        VEREDICTO[$i]="${VERDE}PASS${NC}"; PASAN=$((PASAN+1))
    else
        VEREDICTO[$i]="${ROJO}FAIL${NC}"
        restaurar
    fi
done

echo -e "\n${AZUL}=== Auditoría del kernel: /proc/mmap_guard ===${NC}"
cat /proc/mmap_guard
echo -e "\n${AZUL}=== Auditoría del kernel: dmesg ===${NC}"
dmesg | grep mmap_guard | tail -n 15

rmmod mmap_guard
echo -e "\n(módulo descargado con rmmod)"

# ------------------------------------------------------------------- resumen
echo -e "\n${AZUL}=== RESUMEN ===${NC}"
printf "%-4s %-10s %-20s | %-36s | %-36s | %s\n" "#" "usuario" "modo" "SIN módulo" "CON módulo" "resultado"
for i in "${!CASOS[@]}"; do
    IFS='|' read -r usuario modo esperado regla <<< "${CASOS[$i]}"
    printf "%-4s %-10s %-20s | %-36s | %-36s | " "$((i+1))" "$usuario" "$modo" "${SIN[$i]}" "${CON[$i]}"
    echo -e "${VEREDICTO[$i]}"
done
echo -e "\nCasos correctos: ${PASAN}/${#CASOS[@]}"
[ "$PASAN" -eq "${#CASOS[@]}" ] && echo -e "${VERDE}=== TODAS LAS PRUEBAS PASARON ===${NC}" || echo -e "${ROJO}=== HAY PRUEBAS FALLIDAS ===${NC}"
