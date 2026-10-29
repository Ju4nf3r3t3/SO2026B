#!/bin/bash
# =============================================================================
# medir_overhead.sh - Sobrecosto (overhead) del módulo mmap_guard
#
# Mide, SIN y CON el módulo cargado:
#   A) el costo de mmap()+munmap() sobre un archivo cualquiera y sobre el protegido;
#   B) el costo de atender fallos de página al recorrer un archivo de 64 MB.
# Además muestra perf stat (eventos de fallos de página, sin y con módulo) y
# strace -c (cuántas syscalls hace el programa y cuánto tardan).
#
# Uso:  sudo ./medir_overhead.sh
# =============================================================================

set -u
cd "$(dirname "$0")"
AZUL='\033[1;36m'; NC='\033[0m'

if [ "$(id -u)" -ne 0 ]; then
    echo "Ejecuta con  sudo ./medir_overhead.sh"; exit 1
fi

DUENO=${SUDO_USER:-$(logname)}
UID_DUENO=$(id -u "$DUENO")
DIR=/srv/mmap_guard
ARCHIVO=$DIR/confidencial.txt
OTRO=$DIR/otro.txt
GRANDE=$DIR/grande.bin
N=200000                       # iteraciones de mmap+munmap
BENCH=$DIR/bench_mmap

sudo -u "$DUENO" make -s all || exit 1
mkdir -p "$DIR" && chmod 755 "$DIR"
install -m 755 bench_mmap "$BENCH"
[ -f "$ARCHIVO" ] || { echo "DATOS CONFIDENCIALES" > "$ARCHIVO"; chown "$DUENO:" "$ARCHIVO"; chmod 644 "$ARCHIVO"; }
echo "archivo no protegido" > "$OTRO"; chmod 644 "$OTRO"
[ -f "$GRANDE" ] || dd if=/dev/urandom of="$GRANDE" bs=1M count=64 status=none
chmod 644 "$GRANDE"
lsmod | grep -q '^mmap_guard' && rmmod mmap_guard

# Corre las mediciones (3 repeticiones de cada una).
medir() {
    for r in 1 2 3; do
        echo -n "  [mmap otro archivo   ] "; sudo -u "$DUENO" "$BENCH" llamadas "$OTRO" $N
    done
    for r in 1 2 3; do
        echo -n "  [mmap arch. protegido] "; sudo -u "$DUENO" "$BENCH" llamadas "$ARCHIVO" $N
    done
    for r in 1 2 3; do
        sync; echo 3 > /proc/sys/vm/drop_caches          # que los fallos vayan a disco igual en ambas fases
        echo -n "  [fallos 64 MB, frío  ] "; sudo -u "$DUENO" "$BENCH" fallos "$GRANDE"
    done
    for r in 1 2 3; do
        echo -n "  [fallos 64 MB, caché ] "; sudo -u "$DUENO" "$BENCH" fallos "$GRANDE"
    done
    if perf --version >/dev/null 2>&1; then
        echo "  perf stat (64 MB, en caché):"
        perf stat -e page-faults,major-faults,task-clock "$BENCH" fallos "$GRANDE" 2>&1 \
            | grep -E 'page-faults|major-faults|task-clock|elapsed' | sed 's/^ */    /'
    fi
}

echo -e "${AZUL}=== Kernel $(uname -r) ($(uname -m)), $(nproc) CPUs ===${NC}"
echo -e "\n${AZUL}=== SIN módulo ===${NC}"
medir

insmod mmap_guard.ko archivo="$ARCHIVO" uid_autorizado="$UID_DUENO"
echo -e "\n${AZUL}=== CON módulo (insmod mmap_guard.ko) ===${NC}"
medir

echo -e "\n${AZUL}=== kprobes registrados (/sys/kernel/debug/kprobes/list) ===${NC}"
grep -E 'security_(mmap_file|file_mprotect)' /sys/kernel/debug/kprobes/list 2>/dev/null || echo "(debugfs no disponible)"

echo -e "\n${AZUL}=== strace -c: syscalls de 10000 mmap+munmap sobre el archivo protegido ===${NC}"
if command -v strace >/dev/null; then
    sudo -u "$DUENO" strace -c -e trace=mmap,munmap,openat,close "$BENCH" llamadas "$ARCHIVO" 10000 2>&1 | tail -n 12
else
    echo "(strace no instalado: sudo apt install strace)"
fi

perf --version >/dev/null 2>&1 || echo "(perf no instalado: sudo apt install linux-tools-\$(uname -r))"

echo -e "\n${AZUL}=== /proc/mmap_guard ===${NC}"
cat /proc/mmap_guard
rmmod mmap_guard
