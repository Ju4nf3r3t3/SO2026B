/**
 * @file mmap_guard.c
 * @brief Módulo de kernel (LKM) que protege un archivo frente a mmap() y mprotect().
 * @course Sistemas Operativos - EAFIT 2026-2 - Parcial 3
 *
 * ==============================================================================
 * IDEA GENERAL
 * ==============================================================================
 *
 * Una vez que un archivo queda proyectado con mmap(), los accesos posteriores
 * los resuelve la MMU directamente (sin syscalls). Por eso el control no puede
 * hacerse en espacio de usuario: hay que hacerlo en el kernel, en el momento
 * en que se CREA el mapeo (o se cambian sus permisos), que es el último punto
 * donde el kernel todavía tiene la palabra.
 *
 * El kernel ya llama a dos funciones del framework LSM en esos momentos:
 *
 *   security_mmap_file(file, prot, flags)        <- desde vm_mmap_pgoff()
 *   security_file_mprotect(vma, reqprot, prot)   <- desde do_mprotect_pkey()
 *
 * Desde Linux 4.x un LSM no se puede registrar desde un módulo cargable (las
 * tablas de hooks son __ro_after_init). Por eso usamos KRETPROBES: el módulo
 * "envuelve" esas dos funciones sin recompilar el kernel:
 *
 *   1. entry_handler  -> lee los argumentos y decide (permitir o denegar).
 *   2. ret_handler    -> si se decidió denegar, cambia el valor de retorno a
 *                        -EACCES. El llamador aborta el mmap()/mprotect() y el
 *                        proceso recibe "Permission denied".
 *
 * ==============================================================================
 * POLÍTICA SOBRE EL ARCHIVO PROTEGIDO (parámetro "archivo")
 * ==============================================================================
 *
 *   R1. Solo el usuario "uid_autorizado" puede proyectarlo en memoria.
 *       (Otros usuarios no comparten sus marcos físicos -> sin canales laterales
 *        tipo Flush+Reload y sin ver los cambios en vivo.)
 *   R2. Nadie puede proyectarlo como ejecutable (PROT_EXEC).
 *       (POSIX lo permite aunque el archivo no tenga bit x -> inyección de código.)
 *   R3. Nadie puede proyectarlo con escritura COMPARTIDA (PROT_WRITE + MAP_SHARED).
 *       (Esas escrituras llegan al disco sin pasar por ningún punto de inspección.)
 *       La escritura PRIVADA (MAP_PRIVATE) sí se permite: es Copy-On-Write y
 *       nunca modifica el archivo.
 *   R4. mprotect() no puede "subir" un mapeo existente a ejecutable ni a
 *       escritura compartida (si no, R2 y R3 se saltarían en dos pasos).
 *
 * Toda decisión queda auditada: contadores exactos en /proc/mmap_guard y
 * mensajes en dmesg (con límite de frecuencia para no inundar el log).
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kprobes.h>
#include <linux/ptrace.h>
#include <linux/fs.h>
#include <linux/namei.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/cred.h>
#include <linux/sched.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/atomic.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Juan Fernando Romero - SO 2026-2");
MODULE_DESCRIPTION("Guardia de mmap()/mprotect() sobre un archivo protegido (kretprobes)");
MODULE_VERSION("1.0");

#define TAG "[mmap_guard] "

/* ------------------------------------------------------------------------- */
/* Parámetros del módulo (se pasan en insmod)                                */
/* ------------------------------------------------------------------------- */

static char *archivo = "/srv/mmap_guard/confidencial.txt";
module_param(archivo, charp, 0444);
MODULE_PARM_DESC(archivo, "Ruta del archivo protegido");

static uint uid_autorizado = 1000;
module_param(uid_autorizado, uint, 0444);
MODULE_PARM_DESC(uid_autorizado, "Único UID que puede proyectar el archivo con mmap()");

/* Identidad del archivo protegido: (dispositivo, inodo). Comparar dos enteros
 * en cada mmap() es mucho más barato que comparar rutas de texto. */
static dev_t dev_protegido;
static unsigned long ino_protegido;

/* ------------------------------------------------------------------------- */
/* Contadores de auditoría (atómicos: los probes corren en varias CPUs)      */
/* ------------------------------------------------------------------------- */

static atomic_long_t n_mmap_revisados;
static atomic_long_t n_mmap_permitidos;
static atomic_long_t n_den_usuario;      /* R1 */
static atomic_long_t n_den_exec;         /* R2 */
static atomic_long_t n_den_escritura;    /* R3 */
static atomic_long_t n_mprotect_revisados;
static atomic_long_t n_den_mprotect;     /* R4 */

/* Datos que el entry_handler le pasa al ret_handler (uno por llamada en curso). */
struct decision {
	int denegar;
};

/** Devuelve true si el struct file corresponde al archivo protegido. */
static bool es_protegido(struct file *f)
{
	struct inode *inode;

	if (!f)                       /* mmap anónimo: no hay archivo */
		return false;
	inode = file_inode(f);
	return inode->i_ino == ino_protegido && inode->i_sb->s_dev == dev_protegido;
}

/** UID efectivo del proceso actual, visto desde el espacio de nombres inicial. */
static uid_t uid_actual(void)
{
	return from_kuid(&init_user_ns, current_euid());
}

/* ========================================================================= */
/* Gancho 1: security_mmap_file(struct file *file, ulong prot, ulong flags)   */
/* ========================================================================= */

static int mmap_entrada(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	struct decision *d = (struct decision *)ri->data;
	/* regs_get_kernel_argument() es portable (x86_64 y arm64): evita usar
	 * registros concretos como regs->di, que solo existen en x86. */
	struct file *f       = (struct file *)regs_get_kernel_argument(regs, 0);
	unsigned long prot   = regs_get_kernel_argument(regs, 1);
	unsigned long flags  = regs_get_kernel_argument(regs, 2);
	bool compartido      = (flags & MAP_TYPE) != MAP_PRIVATE;
	uid_t uid;

	if (!es_protegido(f))
		return 1;   /* != 0: no nos interesa, el kernel ni llama a mmap_retorno */

	atomic_long_inc(&n_mmap_revisados);
	uid = uid_actual();
	d->denegar = 1;

	if (uid != uid_autorizado) {
		atomic_long_inc(&n_den_usuario);
		pr_warn_ratelimited(TAG "DENEGADO mmap (R1 usuario no autorizado): pid=%d comm=%s uid=%u archivo=%pD\n",
				    current->pid, current->comm, uid, f);
	} else if (prot & PROT_EXEC) {
		atomic_long_inc(&n_den_exec);
		pr_warn_ratelimited(TAG "DENEGADO mmap (R2 PROT_EXEC): pid=%d comm=%s uid=%u archivo=%pD\n",
				    current->pid, current->comm, uid, f);
	} else if ((prot & PROT_WRITE) && compartido) {
		atomic_long_inc(&n_den_escritura);
		pr_warn_ratelimited(TAG "DENEGADO mmap (R3 escritura MAP_SHARED): pid=%d comm=%s uid=%u archivo=%pD\n",
				    current->pid, current->comm, uid, f);
	} else {
		d->denegar = 0;
		atomic_long_inc(&n_mmap_permitidos);
		pr_info_ratelimited(TAG "permitido mmap: pid=%d comm=%s uid=%u prot=%s%s%s %s\n",
				    current->pid, current->comm, uid,
				    (prot & PROT_READ) ? "R" : "-",
				    (prot & PROT_WRITE) ? "W" : "-",
				    (prot & PROT_EXEC) ? "X" : "-",
				    compartido ? "MAP_SHARED" : "MAP_PRIVATE(COW)");
	}
	return 0;       /* 0: sí queremos que se ejecute mmap_retorno */
}

static int mmap_retorno(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	struct decision *d = (struct decision *)ri->data;

	/* Solo se cambia un retorno exitoso; si el kernel ya negó, se respeta. */
	if (d->denegar && regs_return_value(regs) == 0)
		regs_set_return_value(regs, (unsigned long)-EACCES);
	return 0;
}

static struct kretprobe kp_mmap = {
	.kp.symbol_name = "security_mmap_file",
	.entry_handler  = mmap_entrada,
	.handler        = mmap_retorno,
	.data_size      = sizeof(struct decision),
	/* Llamadas simultáneas que se pueden vigilar. Si se agotan, el kernel
	 * cuenta un "nmissed" y esa llamada NO se revisa (ver /proc/mmap_guard). */
	.maxactive      = 256,
};

/* ========================================================================= */
/* Gancho 2: security_file_mprotect(vma, ulong reqprot, ulong prot)           */
/* ========================================================================= */

static int mprotect_entrada(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	struct decision *d = (struct decision *)ri->data;
	struct vm_area_struct *vma = (struct vm_area_struct *)regs_get_kernel_argument(regs, 0);
	unsigned long prot = regs_get_kernel_argument(regs, 2);
	/* VM_MAYSHARE marca un mapeo MAP_SHARED. (VM_SHARED se borra si el fd
	 * era de solo lectura, por eso no sirve para esta pregunta.) */
	bool compartido = vma->vm_flags & VM_MAYSHARE;

	if (!es_protegido(vma->vm_file))
		return 1;

	atomic_long_inc(&n_mprotect_revisados);
	d->denegar = (prot & PROT_EXEC) || ((prot & PROT_WRITE) && compartido);

	if (d->denegar) {
		atomic_long_inc(&n_den_mprotect);
		pr_warn_ratelimited(TAG "DENEGADO mprotect (R4 -> %s): pid=%d comm=%s uid=%u archivo=%pD\n",
				    (prot & PROT_EXEC) ? "PROT_EXEC" : "PROT_WRITE en MAP_SHARED",
				    current->pid, current->comm, uid_actual(), vma->vm_file);
	}
	return 0;
}

static struct kretprobe kp_mprotect = {
	.kp.symbol_name = "security_file_mprotect",
	.entry_handler  = mprotect_entrada,
	.handler        = mmap_retorno,          /* misma lógica de retorno */
	.data_size      = sizeof(struct decision),
	.maxactive      = 256,
};

/* ========================================================================= */
/* /proc/mmap_guard: estado y auditoría                                      */
/* ========================================================================= */

static int proc_mostrar(struct seq_file *m, void *v)
{
	seq_printf(m, "archivo protegido        : %s (inodo %lu)\n", archivo, ino_protegido);
	seq_printf(m, "uid autorizado           : %u\n", uid_autorizado);
	seq_printf(m, "mmap revisados           : %ld\n", atomic_long_read(&n_mmap_revisados));
	seq_printf(m, "  permitidos             : %ld\n", atomic_long_read(&n_mmap_permitidos));
	seq_printf(m, "  denegados R1 usuario   : %ld\n", atomic_long_read(&n_den_usuario));
	seq_printf(m, "  denegados R2 exec      : %ld\n", atomic_long_read(&n_den_exec));
	seq_printf(m, "  denegados R3 escritura : %ld\n", atomic_long_read(&n_den_escritura));
	seq_printf(m, "mprotect revisados       : %ld\n", atomic_long_read(&n_mprotect_revisados));
	seq_printf(m, "  denegados R4           : %ld\n", atomic_long_read(&n_den_mprotect));
	seq_printf(m, "llamadas no vigiladas    : %d (nmissed de los kretprobes)\n",
		   kp_mmap.nmissed + kp_mprotect.nmissed);
	return 0;
}

/* ========================================================================= */
/* Carga y descarga                                                          */
/* ========================================================================= */

static int __init mmap_guard_init(void)
{
	struct path ruta;
	struct inode *inode;
	int ret;

	/* 1. Resolver la ruta a (dispositivo, inodo) una sola vez. */
	ret = kern_path(archivo, LOOKUP_FOLLOW, &ruta);
	if (ret) {
		pr_err(TAG "no se encontró el archivo '%s' (error %d)\n", archivo, ret);
		/* -EINVAL y no -ENOENT: insmod traduce -ENOENT como "Unknown symbol". */
		return -EINVAL;
	}
	inode = d_inode(ruta.dentry);
	ino_protegido = inode->i_ino;
	dev_protegido = inode->i_sb->s_dev;
	path_put(&ruta);

	/* 2. Instalar los dos ganchos. */
	ret = register_kretprobe(&kp_mmap);
	if (ret) {
		pr_err(TAG "register_kretprobe(security_mmap_file) falló: %d\n", ret);
		return ret;
	}
	ret = register_kretprobe(&kp_mprotect);
	if (ret) {
		pr_err(TAG "register_kretprobe(security_file_mprotect) falló: %d\n", ret);
		unregister_kretprobe(&kp_mmap);
		return ret;
	}

	/* 3. Archivo de estado en /proc. */
	if (!proc_create_single("mmap_guard", 0444, NULL, proc_mostrar)) {
		unregister_kretprobe(&kp_mprotect);
		unregister_kretprobe(&kp_mmap);
		return -ENOMEM;
	}

	pr_info(TAG "cargado: protegiendo %s (inodo %lu), uid autorizado %u\n",
		archivo, ino_protegido, uid_autorizado);
	return 0;
}

static void __exit mmap_guard_exit(void)
{
	remove_proc_entry("mmap_guard", NULL);
	/* unregister_kretprobe() espera a que terminen los handlers en curso,
	 * así que después de esto es seguro liberar el código del módulo. */
	unregister_kretprobe(&kp_mprotect);
	unregister_kretprobe(&kp_mmap);
	pr_info(TAG "descargado. mmap revisados=%ld, denegados=%ld\n",
		atomic_long_read(&n_mmap_revisados),
		atomic_long_read(&n_den_usuario) + atomic_long_read(&n_den_exec) +
		atomic_long_read(&n_den_escritura) + atomic_long_read(&n_den_mprotect));
}

module_init(mmap_guard_init);
module_exit(mmap_guard_exit);
