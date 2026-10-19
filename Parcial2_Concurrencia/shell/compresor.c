#include "shell.h"
#include "huffman.h"
#include "compresor.h"
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <limits.h>
#include <sys/stat.h>

/**
 * ====================================================================================
 * Compresor Huffman concurrente
 * ====================================================================================
 * Hilos que intervienen:
 *
 *   hilo principal (editor) --pthread_create--> hilo de fondo (coordinador)
 *                                                 |
 *       Fase 1: N trabajadores cuentan frecuencias (histograma local + suma bajo mutex)
 *       join -> se construye el árbol global (secuencial)
 *       Fase 2: N trabajadores codifican bloques -> resultados[i] + cond_broadcast
 *               el coordinador es el escritor: espera resultados[i] en orden y lo escribe
 *       join -> fin
 *
 * Formato .huf:
 *   cabecera: "HUF1" | tamaño original (uint64) | número de bloques (uint32) | 256 frecuencias (uint64)
 *   por bloque: bytes originales (uint32) | bytes comprimidos (uint32) | datos comprimidos
 */

#define TAM_BLOQUE (64 * 1024)  /* Tamaño de cada bloque del archivo */
#define NUM_HILOS  4            /* Hilos trabajadores por fase */

pthread_rwlock_t g_archivo_lock = PTHREAD_RWLOCK_INITIALIZER;

/* ---- Estado del trabajo en segundo plano (compartido editor <-> hilo de fondo) ---- */

static struct {
    pthread_mutex_t mutex;          /* Protege todos los campos de abajo */
    pthread_cond_t cond_arranque;   /* El hilo de fondo avisa que ya tomó el rwlock */
    int arranco;                    /* 1 cuando el hilo de fondo ya arrancó */
    int activo;                     /* 1 mientras el trabajo corre */
    char tipo;                      /* 'z' (comprimir) o 'x' (descomprimir) */
    int hechos, total;              /* Progreso en bloques */
    int fallo;                      /* 1 si el trabajo terminó con error */
    char mensaje[2 * PATH_MAX + 256]; /* Resultado final o error, lo imprime el editor */
} g_estado = { PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, 0, 0, ' ', 0, 0, 0, "" };

/* Solo los usa el hilo principal, por eso no necesitan mutex. */
static pthread_t g_hilo;
static int g_hay_hilo = 0;          /* 1 si hay un hilo de fondo creado y aún sin join */
static char g_entrada[PATH_MAX];    /* Se escriben antes de pthread_create (que las publica al hilo) */
static char g_salida[PATH_MAX];

static void fijar_total(int total) {
    pthread_mutex_lock(&g_estado.mutex);
    g_estado.total = total;
    pthread_mutex_unlock(&g_estado.mutex);
}

static void avanzar_progreso(void) {
    pthread_mutex_lock(&g_estado.mutex);
    g_estado.hechos++;
    pthread_mutex_unlock(&g_estado.mutex);
}

/* Marca el trabajo como terminado y guarda el mensaje que mostrará el editor. */
static void terminar(int fallo, const char *mensaje) {
    pthread_mutex_lock(&g_estado.mutex);
    g_estado.fallo = fallo;
    strncpy(g_estado.mensaje, mensaje, sizeof(g_estado.mensaje) - 1);
    g_estado.mensaje[sizeof(g_estado.mensaje) - 1] = '\0';
    g_estado.activo = 0;
    pthread_mutex_unlock(&g_estado.mutex);
}

/* ---- Lectura/escritura completas (read/write pueden transferir menos de lo pedido) ---- */

static ssize_t leer_todo(int fd, void *buf, size_t n) {
    size_t hecho = 0;
    while (hecho < n) {
        ssize_t r = read(fd, (char *)buf + hecho, n - hecho);
        if (r == -1) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (r == 0) break;  /* Fin de archivo */
        hecho += (size_t)r;
    }
    return (ssize_t)hecho;
}

static int escribir_todo(int fd, const void *buf, size_t n) {
    size_t hecho = 0;
    while (hecho < n) {
        ssize_t r = write(fd, (const char *)buf + hecho, n - hecho);
        if (r == -1) {
            if (errno == EINTR) continue;
            return -1;
        }
        hecho += (size_t)r;
    }
    return 0;
}

/* ====================================================================================
 * COMPRESIÓN PARALELA
 * ==================================================================================== */

typedef struct {
    unsigned char *datos;   /* Bloque comprimido (malloc), NULL cuando ya se escribió */
    size_t n_comp;          /* Bytes comprimidos */
    uint32_t n_orig;        /* Bytes originales del bloque */
    int listo;              /* 1 cuando un trabajador terminó de codificarlo */
} BloqueSalida;

typedef struct {
    int fd;                     /* Archivo de entrada (solo se lee con pread) */
    off_t tam;
    int num_bloques;

    pthread_mutex_t mutex;      /* Protege: siguiente, freq, resultados, error */
    pthread_cond_t cond_listo;  /* Se hace broadcast cuando un bloque queda listo o hay error */
    int siguiente;              /* Próximo bloque libre para los trabajadores */
    uint64_t freq[256];         /* Histograma global */
    BloqueSalida *resultados;   /* resultados[i] = bloque i comprimido */
    int error;                  /* errno del primer error (0 = sin error) */

    ArbolHuff arbol;            /* Se construye entre las fases; en la fase 2 solo se lee */
} Compresion;

/* Entrega el índice del siguiente bloque sin procesar, o -1 si no quedan o hubo error. */
static int tomar_bloque(Compresion *c) {
    pthread_mutex_lock(&c->mutex);
    int i = -1;
    if (c->error == 0 && c->siguiente < c->num_bloques) {
        i = c->siguiente++;
    }
    pthread_mutex_unlock(&c->mutex);
    return i;
}

/* Guarda el primer error y despierta al escritor para que no espere para siempre. */
static void marcar_error(Compresion *c, int err) {
    pthread_mutex_lock(&c->mutex);
    if (c->error == 0) c->error = err;
    pthread_cond_broadcast(&c->cond_listo);
    pthread_mutex_unlock(&c->mutex);
}

/* Lee el bloque i con pread: cada hilo usa su propio offset, así no compiten por
 * el offset compartido del descriptor (que sí cambiaría con lseek + read). */
static int leer_bloque(Compresion *c, int i, unsigned char *buf, size_t *n) {
    off_t inicio = (off_t)i * TAM_BLOQUE;
    size_t falta = (size_t)(c->tam - inicio < TAM_BLOQUE ? c->tam - inicio : TAM_BLOQUE);
    size_t hecho = 0;
    while (hecho < falta) {
        ssize_t r = pread(c->fd, buf + hecho, falta - hecho, inicio + (off_t)hecho);
        if (r == -1) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (r == 0) {          /* El archivo se acortó: no debería pasar (tenemos el rwlock) */
            errno = EIO;
            return -1;
        }
        hecho += (size_t)r;
    }
    *n = hecho;
    return 0;
}

/* Fase 1: cada trabajador cuenta en un histograma local y lo suma al global bajo mutex
 * (reducción local: se toma el mutex una vez por bloque, no una vez por byte). */
static void *hilo_frecuencias(void *arg) {
    Compresion *c = arg;
    unsigned char *buf = malloc(TAM_BLOQUE);
    if (buf == NULL) {
        marcar_error(c, ENOMEM);
        return NULL;
    }

    int i;
    while ((i = tomar_bloque(c)) != -1) {
        size_t n;
        if (leer_bloque(c, i, buf, &n) == -1) {
            marcar_error(c, errno);
            break;
        }
        uint64_t local[256] = {0};
        for (size_t j = 0; j < n; j++) {
            local[buf[j]]++;
        }
        pthread_mutex_lock(&c->mutex);
        for (int s = 0; s < 256; s++) {
            c->freq[s] += local[s];
        }
        pthread_mutex_unlock(&c->mutex);
    }
    free(buf);
    return NULL;
}

/* Fase 2: cada trabajador codifica bloques y deja el resultado en resultados[i]. */
static void *hilo_codificar(void *arg) {
    Compresion *c = arg;
    unsigned char *buf = malloc(TAM_BLOQUE);
    if (buf == NULL) {
        marcar_error(c, ENOMEM);
        return NULL;
    }

    int i;
    while ((i = tomar_bloque(c)) != -1) {
        size_t n, n_comp;
        if (leer_bloque(c, i, buf, &n) == -1) {
            marcar_error(c, errno);
            break;
        }
        unsigned char *comp = huff_codificar(&c->arbol, buf, n, &n_comp);
        if (comp == NULL) {
            marcar_error(c, ENOMEM);
            break;
        }
        pthread_mutex_lock(&c->mutex);
        c->resultados[i].datos = comp;
        c->resultados[i].n_comp = n_comp;
        c->resultados[i].n_orig = (uint32_t)n;
        c->resultados[i].listo = 1;
        pthread_cond_broadcast(&c->cond_listo);   /* Despierta al escritor */
        pthread_mutex_unlock(&c->mutex);
    }
    free(buf);
    return NULL;
}

/* Crea los trabajadores. Si no se pudo crear ninguno marca error (si no, el escritor
 * esperaría un bloque que nadie va a producir). Retorna cuántos se crearon. */
static int crear_trabajadores(pthread_t *hilos, void *(*funcion)(void *), Compresion *c) {
    int creados = 0;
    for (int k = 0; k < NUM_HILOS; k++) {
        int err = pthread_create(&hilos[creados], NULL, funcion, c);
        if (err != 0) break;
        creados++;
    }
    if (creados == 0) marcar_error(c, EAGAIN);
    return creados;
}

static void unir_trabajadores(pthread_t *hilos, int n) {
    for (int k = 0; k < n; k++) {
        pthread_join(hilos[k], NULL);
    }
}

static int escribir_cabecera(int fd, const Compresion *c) {
    uint64_t tam = (uint64_t)c->tam;
    uint32_t nb = (uint32_t)c->num_bloques;
    if (escribir_todo(fd, "HUF1", 4) == -1 ||
        escribir_todo(fd, &tam, sizeof(tam)) == -1 ||
        escribir_todo(fd, &nb, sizeof(nb)) == -1 ||
        escribir_todo(fd, c->freq, sizeof(c->freq)) == -1) {
        return -1;
    }
    return 4 + sizeof(tam) + sizeof(nb) + sizeof(c->freq);
}

/* Hilo de fondo para 'z': coordina las dos fases y es el escritor en orden. */
static void *hilo_comprimir(void *arg) {
    (void)arg;
    char mensaje[sizeof(g_estado.mensaje)];
    int fallo = 1;   /* Se pone en 0 solo si todo sale bien */
    Compresion *c = calloc(1, sizeof(Compresion));  /* En el heap: el árbol ocupa ~80 KB */
    int fd_out = -1;
    pthread_t hilos[NUM_HILOS];
    int n_hilos;
    size_t bytes_salida = 0;

    /* Lock de lectura sobre el archivo durante toda la compresión: 'p' puede seguir
     * leyendo, pero 'a' y 'd' no pueden cambiarlo mientras lo leemos. */
    pthread_rwlock_rdlock(&g_archivo_lock);
    pthread_mutex_lock(&g_estado.mutex);
    g_estado.arranco = 1;
    pthread_cond_signal(&g_estado.cond_arranque);
    pthread_mutex_unlock(&g_estado.mutex);

    if (c == NULL) {
        snprintf(mensaje, sizeof(mensaje), "Error en calloc: %s", strerror(ENOMEM));
        goto fin;
    }
    c->fd = -1;
    pthread_mutex_init(&c->mutex, NULL);
    pthread_cond_init(&c->cond_listo, NULL);

    c->fd = open(g_entrada, O_RDONLY);
    if (c->fd == -1) {
        snprintf(mensaje, sizeof(mensaje), "Error en open('%s'): %s", g_entrada, strerror(errno));
        goto fin;
    }
    struct stat st;
    if (fstat(c->fd, &st) == -1) {
        snprintf(mensaje, sizeof(mensaje), "Error en fstat: %s", strerror(errno));
        goto fin;
    }
    c->tam = st.st_size;
    c->num_bloques = (int)((c->tam + TAM_BLOQUE - 1) / TAM_BLOQUE);
    fijar_total(c->num_bloques);

    c->resultados = calloc((size_t)(c->num_bloques > 0 ? c->num_bloques : 1), sizeof(BloqueSalida));
    if (c->resultados == NULL) {
        snprintf(mensaje, sizeof(mensaje), "Error en calloc: %s", strerror(ENOMEM));
        goto fin;
    }

    /* ---- Fase 1: frecuencias en paralelo ---- */
    c->siguiente = 0;
    n_hilos = crear_trabajadores(hilos, hilo_frecuencias, c);
    unir_trabajadores(hilos, n_hilos);
    if (c->error != 0) {   /* Ya no hay trabajadores vivos: se puede leer sin mutex */
        snprintf(mensaje, sizeof(mensaje), "Error contando frecuencias: %s", strerror(c->error));
        goto fin;
    }

    /* Árbol global (secuencial, entre las dos fases). */
    huff_construir(&c->arbol, c->freq);

    fd_out = open(g_salida, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd_out == -1) {
        snprintf(mensaje, sizeof(mensaje), "Error en open('%s'): %s", g_salida, strerror(errno));
        goto fin;
    }
    int n_cab = escribir_cabecera(fd_out, c);
    if (n_cab == -1) {
        snprintf(mensaje, sizeof(mensaje), "Error en write: %s", strerror(errno));
        goto fin;
    }
    bytes_salida = (size_t)n_cab;

    /* ---- Fase 2: codificación en paralelo + escritura en orden ---- */
    c->siguiente = 0;
    n_hilos = crear_trabajadores(hilos, hilo_codificar, c);

    for (int i = 0; i < c->num_bloques; i++) {
        /* Esperar (dormido, sin busy waiting) a que el bloque i esté listo. */
        pthread_mutex_lock(&c->mutex);
        while (!c->resultados[i].listo && c->error == 0) {
            pthread_cond_wait(&c->cond_listo, &c->mutex);
        }
        int err = c->error;
        pthread_mutex_unlock(&c->mutex);
        if (err != 0) break;

        /* resultados[i] ya no lo toca ningún trabajador: se escribe fuera del mutex. */
        BloqueSalida *b = &c->resultados[i];
        uint32_t n_comp = (uint32_t)b->n_comp;
        if (escribir_todo(fd_out, &b->n_orig, sizeof(b->n_orig)) == -1 ||
            escribir_todo(fd_out, &n_comp, sizeof(n_comp)) == -1 ||
            escribir_todo(fd_out, b->datos, b->n_comp) == -1) {
            marcar_error(c, errno);   /* Detiene a los trabajadores */
            break;
        }
        bytes_salida += sizeof(b->n_orig) + sizeof(n_comp) + b->n_comp;
        free(b->datos);
        b->datos = NULL;
        avanzar_progreso();
    }
    unir_trabajadores(hilos, n_hilos);

    if (c->error != 0) {
        snprintf(mensaje, sizeof(mensaje), "Error comprimiendo: %s", strerror(c->error));
    } else {
        fallo = 0;
        snprintf(mensaje, sizeof(mensaje), "Comprimido '%s' -> '%s' (%ld -> %zu bytes, %d hilos)",
                 g_entrada, g_salida, (long)c->tam, bytes_salida, NUM_HILOS);
    }

fin:
    if (c != NULL) {
        if (c->resultados != NULL) {
            for (int i = 0; i < c->num_bloques; i++) free(c->resultados[i].datos);
            free(c->resultados);
        }
        if (c->fd != -1) close(c->fd);
        pthread_mutex_destroy(&c->mutex);
        pthread_cond_destroy(&c->cond_listo);
        free(c);
    }
    if (fd_out != -1 && close(fd_out) == -1) {
        fallo = 1;
        snprintf(mensaje, sizeof(mensaje), "Error en close: %s", strerror(errno));
    }
    pthread_rwlock_unlock(&g_archivo_lock);
    terminar(fallo, mensaje);
    return NULL;
}

/* ====================================================================================
 * DESCOMPRESIÓN (secuencial, pero en el hilo de fondo para no bloquear el editor)
 * ==================================================================================== */

static void *hilo_descomprimir(void *arg) {
    (void)arg;
    char mensaje[sizeof(g_estado.mensaje)];
    int fallo = 1;   /* Se pone en 0 solo si todo sale bien */
    int fd_in = -1, fd_out = -1;
    unsigned char *comp = NULL, *orig = NULL;
    ArbolHuff *arbol = malloc(sizeof(ArbolHuff));
    char magia[4];
    uint64_t tam, freq[256], escritos = 0;
    uint32_t nb;

    pthread_mutex_lock(&g_estado.mutex);
    g_estado.arranco = 1;
    pthread_cond_signal(&g_estado.cond_arranque);
    pthread_mutex_unlock(&g_estado.mutex);

    if (arbol == NULL) {
        snprintf(mensaje, sizeof(mensaje), "Error en malloc: %s", strerror(ENOMEM));
        goto fin;
    }
    fd_in = open(g_entrada, O_RDONLY);
    if (fd_in == -1) {
        snprintf(mensaje, sizeof(mensaje), "Error en open('%s'): %s", g_entrada, strerror(errno));
        goto fin;
    }
    if (leer_todo(fd_in, magia, 4) != 4 || memcmp(magia, "HUF1", 4) != 0 ||
        leer_todo(fd_in, &tam, sizeof(tam)) != sizeof(tam) ||
        leer_todo(fd_in, &nb, sizeof(nb)) != sizeof(nb) ||
        leer_todo(fd_in, freq, sizeof(freq)) != sizeof(freq)) {
        snprintf(mensaje, sizeof(mensaje), "'%s' no es un archivo .huf válido", g_entrada);
        goto fin;
    }
    huff_construir(arbol, freq);
    fijar_total((int)nb);

    fd_out = open(g_salida, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd_out == -1) {
        snprintf(mensaje, sizeof(mensaje), "Error en open('%s'): %s", g_salida, strerror(errno));
        goto fin;
    }

    for (uint32_t i = 0; i < nb; i++) {
        uint32_t n_orig, n_comp;
        if (leer_todo(fd_in, &n_orig, sizeof(n_orig)) != sizeof(n_orig) ||
            leer_todo(fd_in, &n_comp, sizeof(n_comp)) != sizeof(n_comp) ||
            n_orig > TAM_BLOQUE || n_comp > (size_t)n_orig * 32 + 1) {
            snprintf(mensaje, sizeof(mensaje), "'%s' está corrupto (bloque %u)", g_entrada, i);
            goto fin;
        }
        comp = malloc(n_comp > 0 ? n_comp : 1);
        orig = malloc(n_orig > 0 ? n_orig : 1);
        if (comp == NULL || orig == NULL) {
            snprintf(mensaje, sizeof(mensaje), "Error en malloc: %s", strerror(ENOMEM));
            goto fin;
        }
        if (leer_todo(fd_in, comp, n_comp) != (ssize_t)n_comp ||
            huff_decodificar(arbol, comp, n_comp, orig, n_orig) == -1) {
            snprintf(mensaje, sizeof(mensaje), "'%s' está corrupto (bloque %u)", g_entrada, i);
            goto fin;
        }
        if (escribir_todo(fd_out, orig, n_orig) == -1) {
            snprintf(mensaje, sizeof(mensaje), "Error en write: %s", strerror(errno));
            goto fin;
        }
        escritos += n_orig;
        free(comp);
        free(orig);
        comp = orig = NULL;
        avanzar_progreso();
    }

    if (escritos != tam) {
        snprintf(mensaje, sizeof(mensaje), "'%s' está corrupto (tamaño no coincide)", g_entrada);
    } else {
        fallo = 0;
        snprintf(mensaje, sizeof(mensaje), "Descomprimido '%s' -> '%s' (%lu bytes)",
                 g_entrada, g_salida, (unsigned long)escritos);
    }

fin:
    free(comp);
    free(orig);
    free(arbol);
    if (fd_in != -1) close(fd_in);
    if (fd_out != -1 && close(fd_out) == -1) {
        fallo = 1;
        snprintf(mensaje, sizeof(mensaje), "Error en close: %s", strerror(errno));
    }
    terminar(fallo, mensaje);
    return NULL;
}

/* ====================================================================================
 * INTERFAZ CON EL EDITOR (solo la llama el hilo principal)
 * ==================================================================================== */

/* Hace join del hilo de fondo e imprime su resultado. */
static void recoger_hilo(void) {
    pthread_join(g_hilo, NULL);
    g_hay_hilo = 0;
    pthread_mutex_lock(&g_estado.mutex);
    printf("%s[%c terminado] %s\n" COLOR_RESET, g_estado.fallo ? COLOR_ERROR : COLOR_RESULT,
           g_estado.tipo, g_estado.mensaje);
    pthread_mutex_unlock(&g_estado.mutex);
}

int compresor_iniciar(char tipo, const char *entrada) {
    if (compresor_activo(NULL, NULL)) {
        printf(COLOR_ERROR "Ya hay un trabajo en curso. Usa 's' para ver el progreso o 'w' para esperar.\n" COLOR_RESET);
        return -1;
    }
    if (g_hay_hilo) recoger_hilo();   /* Terminó pero aún no se había hecho join */

    /* Nombre de salida: 'z' agrega ".huf"; 'x' cambia ".huf" por ".out". */
    size_t len = strlen(entrada);
    int largo;
    if (tipo == 'z') {
        largo = snprintf(g_salida, sizeof(g_salida), "%s.huf", entrada);
    } else if (len > 4 && strcmp(entrada + len - 4, ".huf") == 0) {
        largo = snprintf(g_salida, sizeof(g_salida), "%.*s.out", (int)(len - 4), entrada);
    } else {
        largo = snprintf(g_salida, sizeof(g_salida), "%s.out", entrada);
    }
    if (largo < 0 || (size_t)largo >= sizeof(g_salida) || len >= sizeof(g_entrada)) {
        printf(COLOR_ERROR "Nombre de archivo demasiado largo.\n" COLOR_RESET);
        return -1;
    }
    strcpy(g_entrada, entrada);

    pthread_mutex_lock(&g_estado.mutex);
    g_estado.activo = 1;
    g_estado.arranco = 0;
    g_estado.tipo = tipo;
    g_estado.hechos = 0;
    g_estado.total = 0;
    g_estado.mensaje[0] = '\0';
    pthread_mutex_unlock(&g_estado.mutex);

    /* Se bloquea SIGINT antes de crear el hilo: el hilo de fondo (y los trabajadores
     * que él crea) heredan la máscara, así Ctrl+C solo lo atiende el hilo principal. */
    sigset_t bloquear, anterior;
    sigemptyset(&bloquear);
    sigaddset(&bloquear, SIGINT);
    pthread_sigmask(SIG_BLOCK, &bloquear, &anterior);
    int err = pthread_create(&g_hilo, NULL, tipo == 'z' ? hilo_comprimir : hilo_descomprimir, NULL);
    pthread_sigmask(SIG_SETMASK, &anterior, NULL);

    if (err != 0) {
        printf(COLOR_ERROR "pthread_create: %s\n" COLOR_RESET, strerror(err));
        pthread_mutex_lock(&g_estado.mutex);
        g_estado.activo = 0;
        pthread_mutex_unlock(&g_estado.mutex);
        return -1;
    }
    g_hay_hilo = 1;

    /* Esperar (pocos microsegundos, dormido en la variable de condición) a que el hilo
     * tome el lock de lectura: así ninguna edición se cuela antes de que empiece a leer. */
    pthread_mutex_lock(&g_estado.mutex);
    while (!g_estado.arranco) {
        pthread_cond_wait(&g_estado.cond_arranque, &g_estado.mutex);
    }
    pthread_mutex_unlock(&g_estado.mutex);

    printf(COLOR_RESULT "%s '%s' -> '%s' en segundo plano. Usa 's' (progreso) o 'w' (esperar).\n" COLOR_RESET,
           tipo == 'z' ? "Comprimiendo" : "Descomprimiendo", g_entrada, g_salida);
    return 0;
}

int compresor_activo(char *tipo, int *porcentaje) {
    pthread_mutex_lock(&g_estado.mutex);
    int activo = g_estado.activo;
    if (tipo != NULL) *tipo = g_estado.tipo;
    if (porcentaje != NULL) {
        *porcentaje = g_estado.total > 0 ? g_estado.hechos * 100 / g_estado.total : 0;
    }
    pthread_mutex_unlock(&g_estado.mutex);
    return activo;
}

void compresor_mostrar_progreso(void) {
    /* Se copia el estado bajo el mutex y se imprime ya sin él. */
    pthread_mutex_lock(&g_estado.mutex);
    int activo = g_estado.activo, hechos = g_estado.hechos, total = g_estado.total;
    char tipo = g_estado.tipo;
    pthread_mutex_unlock(&g_estado.mutex);

    if (!activo && !g_hay_hilo) {
        printf(COLOR_INFO "No hay ningún trabajo en curso.\n" COLOR_RESET);
        return;
    }
    int pct = total > 0 ? hechos * 100 / total : (activo ? 0 : 100);
    char barra[11];
    for (int k = 0; k < 10; k++) barra[k] = k < pct / 10 ? '#' : '.';
    barra[10] = '\0';
    printf(COLOR_PARAM "[%c] [%s] %d%% (%d/%d bloques)\n" COLOR_RESET, tipo, barra, pct, hechos, total);
}

void compresor_revisar_fin(void) {
    if (g_hay_hilo && !compresor_activo(NULL, NULL)) {
        recoger_hilo();
    }
}

void compresor_esperar(void) {
    if (!g_hay_hilo) {
        printf(COLOR_INFO "No hay ningún trabajo en curso.\n" COLOR_RESET);
        return;
    }
    printf(COLOR_INFO "Esperando a que termine el trabajo (pthread_join)...\n" COLOR_RESET);
    recoger_hilo();
}
