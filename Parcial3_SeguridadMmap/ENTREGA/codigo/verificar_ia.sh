#!/bin/bash
# =============================================================================
# verificar_ia.sh - Comprueba contra los headers del kernel instalado las
# sugerencias incorrectas ("alucinaciones") que dio la IA durante el diseño.
#
# Cada chequeo compila o busca algo REAL en /lib/modules/$(uname -r)/build;
# no depende de lo que diga el modelo. Uso:  ./verificar_ia.sh
# =============================================================================

set -u
cd "$(dirname "$0")"
KDIR=/lib/modules/$(uname -r)/build
SYMS=$KDIR/Module.symvers
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

echo "Kernel: $(uname -r) ($(uname -m))   headers: $KDIR"

exportado() {   # ¿un símbolo se puede usar desde un módulo?
    if grep -qw "$1" "$SYMS"; then
        echo "  $1: EXPORTADO ($(grep -w "$1" "$SYMS" | awk '{print $4}'))"
    else
        echo "  $1: NO exportado -> un módulo no puede usarlo"
    fi
}

# Compila un módulo mínimo con el cuerpo dado y muestra el primer error.
compila() {
    local nombre=$1 cuerpo=$2
    mkdir -p "$TMP/$nombre"
    cat > "$TMP/$nombre/prueba.c" <<EOF
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/kprobes.h>
MODULE_LICENSE("GPL");
$cuerpo
EOF
    echo "obj-m += prueba.o" > "$TMP/$nombre/Makefile"
    if make -s -C "$KDIR" M="$TMP/$nombre" CONFIG_DEBUG_INFO_BTF_MODULES= modules > "$TMP/$nombre/log" 2>&1; then
        echo "  compila: SÍ"
    else
        echo "  compila: NO -> $(grep -m1 -E 'error' "$TMP/$nombre/log" | sed 's/.*prueba.c://')"
    fi
}

echo -e "\n[1] 'Usa kallsyms_lookup_name() para encontrar sys_call_table y engancharla'"
exportado kallsyms_lookup_name
exportado sys_call_table

echo -e "\n[2] 'Registra un LSM desde el módulo con security_add_hooks()'"
exportado security_add_hooks

echo -e "\n[3] 'Usa el hook LSM file_mmap' (nombre de kernels 2.6/3.x)"
echo -n "  file_mmap en lsm_hook_defs.h: "; grep -c 'LSM_HOOK(.*, file_mmap,' "$KDIR/include/linux/lsm_hook_defs.h"
echo -n "  mmap_file en lsm_hook_defs.h: "; grep -c 'LSM_HOOK(.*, mmap_file,' "$KDIR/include/linux/lsm_hook_defs.h"
grep 'LSM_HOOK(.*, mmap_file,' -A1 "$KDIR/include/linux/lsm_hook_defs.h" | sed 's/^/  /'

echo -e "\n[4] 'Quita VM_MAYWRITE con vma->vm_flags &= ~VM_MAYWRITE' (no compila desde 6.3)"
compila vmflags 'void f(struct vm_area_struct *v) { v->vm_flags &= ~VM_MAYWRITE; }'
echo "  alternativa correcta en 6.3+: vm_flags_clear(vma, VM_MAYWRITE)"
compila vmflags_ok 'void f(struct vm_area_struct *v) { vm_flags_clear(v, VM_MAYWRITE); }'

echo -e "\n[5] 'Lee los argumentos del kprobe con regs->di, regs->si'"
compila regs_di 'unsigned long f(struct pt_regs *r) { return r->di; }'
echo "  (regs->di solo existe en x86_64: en arm64 no compila. No es portable.)"
echo "  alternativa portable: regs_get_kernel_argument(regs, n)"
compila regs_ok 'unsigned long f(struct pt_regs *r) { return regs_get_kernel_argument(r, 0); }'

echo -e "\n[6] 'Desactiva la protección de escritura con write_cr0(read_cr0() & ~0x10000)'"
compila cr0 'void f(void) { write_cr0(read_cr0() & ~0x10000UL); }'
echo "  (en arm64 no existe CR0. En x86_64 compila, pero desde Linux 5.3 el kernel"
echo "   'fija' el bit WP y lo vuelve a poner: 'CR0 WP bit went missing!?'. Y sin"
echo "   bloquear interrupciones ni a las otras CPUs deja todo el kernel escribible.)"

echo -e "\n[7] Símbolos que SÍ usa mmap_guard.c (todos deben estar exportados)"
for s in register_kretprobe unregister_kretprobe kern_path path_put proc_create_single_data remove_proc_entry from_kuid; do
    exportado "$s"
done
echo -n "  security_mmap_file y security_file_mprotect en kallsyms (sondeables): "
sudo grep -cwE 'security_mmap_file|security_file_mprotect' /proc/kallsyms
