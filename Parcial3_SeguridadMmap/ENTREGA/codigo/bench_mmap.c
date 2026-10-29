/**
 * @file bench_mmap.c
 * @brief Mide el costo de mmap()/munmap() y de atender fallos de página.
 *
 * Uso:
 *   ./bench_mmap llamadas <archivo> <iteraciones>
 *        Repite mmap(PROT_READ, MAP_SHARED) + munmap() y reporta nanosegundos por par.
 *        Con mmap_guard cargado, cada mmap() pasa por el kretprobe.
 *   ./bench_mmap fallos <archivo>
 *        Proyecta el archivo completo, toca un byte de cada página y reporta los
 *        fallos de página (getrusage: menores + mayores) y nanosegundos por fallo.
 */

#define _GNU_SOURCE
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static long long ahora_ns(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return (long long)t.tv_sec * 1000000000LL + t.tv_nsec;
}

/** Fallos de página del proceso hasta ahora: menores (sin disco) + mayores (con disco). */
static long fallos(long *mayores)
{
	struct rusage r;
	getrusage(RUSAGE_SELF, &r);
	*mayores = r.ru_majflt;
	return r.ru_minflt + r.ru_majflt;
}

static int bench_llamadas(const char *ruta, long n)
{
	int fd = open(ruta, O_RDONLY);
	if (fd < 0) { perror("open"); return 1; }

	long long t0 = ahora_ns();
	for (long i = 0; i < n; i++) {
		void *p = mmap(NULL, 4096, PROT_READ, MAP_SHARED, fd, 0);
		if (p == MAP_FAILED) { perror("mmap"); close(fd); return 1; }
		munmap(p, 4096);
	}
	long long t1 = ahora_ns();
	close(fd);

	printf("mmap+munmap: %ld iteraciones, %.1f ns por par\n", n, (double)(t1 - t0) / n);
	return 0;
}

static int bench_fallos(const char *ruta)
{
	struct stat st;
	int fd = open(ruta, O_RDONLY);
	if (fd < 0 || fstat(fd, &st) < 0) { perror("open/fstat"); return 1; }

	long pag = sysconf(_SC_PAGESIZE);
	volatile char *p = mmap(NULL, st.st_size, PROT_READ, MAP_SHARED, fd, 0);
	close(fd);
	if (p == MAP_FAILED) { perror("mmap"); return 1; }

	long may0, may1;
	long f0 = fallos(&may0);
	long long t0 = ahora_ns();
	long suma = 0;
	for (off_t i = 0; i < st.st_size; i += pag)
		suma += p[i];                       /* primer acceso -> fallo de página */
	long long t1 = ahora_ns();
	long f1 = fallos(&may1);

	munmap((void *)p, st.st_size);
	long n = f1 - f0;
	/* Son menos fallos que páginas por el "fault-around": cada fallo mapea
	 * también las páginas vecinas que ya están en el Page Cache. */
	printf("%ld páginas tocadas, %ld fallos (%ld mayores), %.1f ns por fallo (suma=%ld)\n",
	       (long)(st.st_size / pag), n, may1 - may0, n ? (double)(t1 - t0) / n : 0.0, suma);
	return 0;
}

int main(int argc, char **argv)
{
	if (argc >= 4 && !strcmp(argv[1], "llamadas"))
		return bench_llamadas(argv[2], atol(argv[3]));
	if (argc >= 3 && !strcmp(argv[1], "fallos"))
		return bench_fallos(argv[2]);
	fprintf(stderr, "Uso: %s llamadas <archivo> <iteraciones> | %s fallos <archivo>\n", argv[0], argv[0]);
	return 2;
}
