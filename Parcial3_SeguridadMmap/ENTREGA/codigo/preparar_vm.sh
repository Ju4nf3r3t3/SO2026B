#!/bin/bash
# =============================================================================
# preparar_vm.sh - Deja una VM Ubuntu lista para compilar y probar mmap_guard.
# Se corre UNA vez:  sudo ./preparar_vm.sh
# =============================================================================

set -u
if [ "$(id -u)" -ne 0 ]; then echo "Ejecuta con  sudo ./preparar_vm.sh"; exit 1; fi

echo "== Instalando compilador, headers del kernel $(uname -r), strace y perf =="
export DEBIAN_FRONTEND=noninteractive
APT="apt-get -y -q -o DPkg::Lock::Timeout=600"   # espera si otro apt está corriendo
$APT update
$APT install build-essential "linux-headers-$(uname -r)" strace
# El módulo se compila con el MISMO gcc que compiló el kernel (ver Makefile).
KCC=$(grep -h '^CONFIG_CC_VERSION_TEXT' "/lib/modules/$(uname -r)/build/include/config/auto.conf" 2>/dev/null \
      | cut -d'"' -f2 | awk '{print $1}')
if [ -n "$KCC" ] && ! command -v "$KCC" >/dev/null; then
    $APT install "gcc-${KCC##*-}" || echo "(no se pudo instalar gcc-${KCC##*-})"
fi
$APT install linux-tools-common "linux-tools-$(uname -r)" \
    || echo "(perf no disponible para este kernel: las mediciones con perf se omitirán)"

echo "== Usuario de prueba 'atacante' =="
id atacante >/dev/null 2>&1 || useradd -M -s /usr/sbin/nologin atacante
id atacante

echo "== Secure Boot =="
if command -v mokutil >/dev/null && mokutil --sb-state 2>/dev/null | grep -q enabled; then
    echo "ATENCIÓN: Secure Boot está ACTIVO. El kernel no cargará módulos sin firmar."
    echo "Apágalo en la configuración de la VM (firmware/UEFI) y vuelve a intentar."
else
    echo "Secure Boot inactivo: se pueden cargar módulos propios."
fi

echo "== Verificación =="
if command -v make >/dev/null && command -v gcc >/dev/null && [ -d "/lib/modules/$(uname -r)/build" ]; then
    echo "OK: make, gcc y headers del kernel $(uname -r) instalados."
    echo "== Listo. Ahora: make && sudo ./test_parcial3.sh =="
else
    echo "ERROR: falta make, gcc o los headers. Vuelve a correr  sudo ./preparar_vm.sh"
    exit 1
fi
