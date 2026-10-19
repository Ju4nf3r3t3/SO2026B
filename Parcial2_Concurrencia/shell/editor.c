#include "shell.h"
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include "compresor.h"

/**
 * ====================================================================================
 * COMANDO: edit [archivo]
 * ====================================================================================
 * Editor de texto interactivo (sub-modo REPL propio dentro del shell). Toda la
 * manipulación del archivo editado usa syscalls POSIX de bajo nivel (open, read,
 * write, lseek, ftruncate, close) — nunca fopen/fread/fwrite/fclose.
 *
 * Comandos internos del editor (nivel individual, sección 4 del enunciado):
 *   o <archivo>   Abre/crea el archivo.
 *   p [n]         Imprime todo el archivo, o solo la línea n.
 *   a <texto>     Añade una línea al final.
 *   d <n>         Borra la línea n.
 *   q             Cierra el archivo y sale del editor (vuelve al shell).
 *
 * Comandos del Parcial 2 (compresión Huffman concurrente en segundo plano):
 *   z             Comprime el archivo abierto a <archivo>.huf sin bloquear el editor.
 *   x <arch.huf>  Descomprime en segundo plano a <nombre>.out.
 *   s             Muestra el progreso del trabajo.
 *   w             Espera a que termine el trabajo (pthread_join) y muestra el resultado.
 *
 * Estado: un único archivo abierto a la vez (nivel 1, sin multi-buffer).
 * Mientras se comprime, 'p' puede leer (lock de lectura), pero 'a' y 'd' necesitan el
 * lock de escritura y, si está ocupado, avisan en vez de bloquear el editor.
 */

static int g_fd = -1;
static char g_filename[PATH_MAX];

/* ---- Wrappers de syscalls con manejo de error (patrón sección 8.3) ---- */

static int checked_open(const char *path) {
    LOG_SYSCALL("open", "\"%s\", O_RDWR|O_CREAT, 0644", path);
    int fd = open(path, O_RDWR | O_CREAT, 0644);
    if (fd == -1) {
        LOG_SYSCALL_ERROR(strerror(errno));
        perror("open");
        return -1;
    }
    LOG_SYSCALL_RESULT(fd);
    return fd;
}

static off_t checked_lseek(int fd, off_t offset, int whence) {
    off_t pos = lseek(fd, offset, whence);
    if (pos == (off_t)-1) {
        perror("lseek");
    }
    return pos;
}

static ssize_t checked_read(int fd, void *buf, size_t count) {
    ssize_t n = read(fd, buf, count);
    if (n == -1) {
        perror("read");
    }
    return n;
}

static ssize_t checked_write(int fd, const void *buf, size_t count) {
    ssize_t n = write(fd, buf, count);
    if (n == -1) {
        perror("write");
    }
    return n;
}

static int checked_ftruncate(int fd, off_t length) {
    int res = ftruncate(fd, length);
    if (res == -1) {
        perror("ftruncate");
    }
    return res;
}

static int checked_close(int fd) {
    int res = close(fd);
    if (res == -1) {
        perror("close");
    }
    return res;
}

/**
 * Escanea el archivo abierto buscando el inicio y el fin (offset justo después
 * del '\n') de la línea número `n` (1-indexada). No se cachea ningún índice:
 * se recorre byte a byte desde el inicio en cada operación (decisión de diseño
 * elegida por simplicidad, ver sección 8.4 del enunciado).
 *
 * Retorna 0 si la linea existe y llena start/end. Retorna -1 si no existe.
 */
static int find_line(int fd, int n, off_t *start, off_t *end) {
    if (n < 1) return -1;

    if (checked_lseek(fd, 0, SEEK_SET) == -1) return -1;

    char c;
    int current_line = 1;
    off_t pos = 0;
    off_t line_start = 0;
    int saw_any_byte = 0;

    while (1) {
        ssize_t r = checked_read(fd, &c, 1);
        if (r <= 0) {
            /* Fin de archivo. Si la última línea no terminó en '\n', igual cuenta. */
            if (current_line == n && saw_any_byte) {
                *start = line_start;
                *end = pos;
                return 0;
            }
            return -1;
        }
        saw_any_byte = 1;
        pos++;
        if (c == '\n') {
            if (current_line == n) {
                *start = line_start;
                *end = pos;
                return 0;
            }
            current_line++;
            line_start = pos;
            saw_any_byte = 0;
        }
    }
}

/* ---- Comandos internos del editor ---- */

static void editor_open(const char *filename) {
    if (g_fd != -1) {
        checked_close(g_fd);
        g_fd = -1;
    }
    int fd = checked_open(filename);
    if (fd == -1) return;
    g_fd = fd;
    strncpy(g_filename, filename, sizeof(g_filename) - 1);
    g_filename[sizeof(g_filename) - 1] = '\0';
    printf(COLOR_RESULT "Archivo '%s' abierto (fd=%d).\n" COLOR_RESET, g_filename, g_fd);
}

static void editor_print_all(void) {
    if (checked_lseek(g_fd, 0, SEEK_SET) == -1) return;
    char buffer[512];
    ssize_t n;
    while ((n = checked_read(g_fd, buffer, sizeof(buffer))) > 0) {
        checked_write(STDOUT_FILENO, buffer, (size_t)n);
    }
}

static void editor_print_line(int n) {
    off_t start, end;
    if (find_line(g_fd, n, &start, &end) == -1) {
        printf(COLOR_ERROR "La línea %d no existe.\n" COLOR_RESET, n);
        return;
    }
    if (checked_lseek(g_fd, start, SEEK_SET) == -1) return;
    off_t len = end - start;
    char *buf = malloc((size_t)len);
    if (buf == NULL) {
        perror("malloc");
        return;
    }
    ssize_t r = checked_read(g_fd, buf, (size_t)len);
    if (r > 0) {
        checked_write(STDOUT_FILENO, buf, (size_t)r);
    }
    free(buf);
}

static void editor_append(const char *text) {
    off_t size = checked_lseek(g_fd, 0, SEEK_END);
    if (size == -1) return;

    /* Si el archivo no termina en '\n' (p. ej. creado con d_create), se agrega
     * uno antes para que el texto quede como una línea nueva y no pegado a la última. */
    if (size > 0) {
        char last;
        if (checked_lseek(g_fd, -1, SEEK_END) == -1) return;
        if (checked_read(g_fd, &last, 1) != 1) return;
        if (last != '\n' && checked_write(g_fd, "\n", 1) != 1) return;
    }

    if (checked_write(g_fd, text, strlen(text)) == -1) return;
    if (checked_write(g_fd, "\n", 1) == -1) return;
    printf(COLOR_RESULT "Línea añadida.\n" COLOR_RESET);
}

static void editor_delete(int n) {
    off_t start, end;
    if (find_line(g_fd, n, &start, &end) == -1) {
        printf(COLOR_ERROR "La línea %d no existe.\n" COLOR_RESET, n);
        return;
    }

    off_t file_size = checked_lseek(g_fd, 0, SEEK_END);
    if (file_size == -1) return;
    off_t rest_len = file_size - end;

    /* Si alguna syscall falla se aborta antes de truncar, para no dañar el archivo. */
    if (rest_len > 0) {
        char *buf = malloc((size_t)rest_len);
        if (buf == NULL) {
            perror("malloc");
            return;
        }
        if (checked_lseek(g_fd, end, SEEK_SET) == -1 ||
            checked_read(g_fd, buf, (size_t)rest_len) != rest_len ||
            checked_lseek(g_fd, start, SEEK_SET) == -1 ||
            checked_write(g_fd, buf, (size_t)rest_len) != rest_len) {
            free(buf);
            printf(COLOR_ERROR "No se pudo borrar la línea %d.\n" COLOR_RESET, n);
            return;
        }
        free(buf);
    }

    if (checked_ftruncate(g_fd, start + rest_len) == -1) return;
    printf(COLOR_RESULT "Línea %d eliminada.\n" COLOR_RESET, n);
}

/* ---- Integración con el compresor (Parcial 2) ---- */

/* Mensaje cuando 'a' o 'd' no consiguen el lock de escritura. */
static void archivo_ocupado(void) {
    char tipo;
    int pct;
    compresor_activo(&tipo, &pct);
    printf(COLOR_ERROR "Archivo ocupado: compresión en curso (%d%%)\n" COLOR_RESET, pct);
}

/* Manejador de Ctrl+C dentro del editor. Solo lo ejecuta el hilo principal, porque los
 * hilos del compresor tienen SIGINT bloqueado. No termina el proceso para no dejar
 * hilos a medio escribir un .huf; se sale con 'q'. Solo usa write (async-signal-safe). */
static void manejador_sigint(int sig) {
    (void)sig;
    const char msg[] = "\n[Ctrl+C] Usa 'q' para salir del editor (espera el trabajo en curso).\n";
    write(STDOUT_FILENO, msg, sizeof(msg) - 1);
}

/* ---- REPL del editor ---- */

int cmd_edit(int argc, char **argv) {
    if (argc == 2) {
        editor_open(argv[1]);
    }

    /* SA_RESTART: fgets no se interrumpe con EINTR al llegar la señal. */
    struct sigaction sa, sa_anterior;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = manejador_sigint;
    sa.sa_flags = SA_RESTART;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, &sa_anterior);

    char line[2048];
    while (1) {
        /* Si el trabajo de fondo terminó, se avisa una vez (y se hace join). */
        compresor_revisar_fin();

        char tipo;
        int pct;
        if (compresor_activo(&tipo, &pct)) {
            printf(COLOR_PROMPT "edit [%c %d%%]> " COLOR_RESET, tipo, pct);
        } else {
            printf(COLOR_PROMPT "edit> " COLOR_RESET);
        }
        fflush(stdout);

        if (fgets(line, sizeof(line), stdin) == NULL) {
            printf("\n");
            break;
        }
        line[strcspn(line, "\n")] = '\0';

        char *cmd = strtok(line, " ");
        char *rest = strtok(NULL, "");

        if (cmd == NULL) {
            continue;
        }

        if (strcmp(cmd, "o") == 0) {
            if (rest == NULL) {
                printf(COLOR_ERROR "Uso: o <archivo>\n" COLOR_RESET);
            } else if (compresor_activo(NULL, NULL)) {
                /* Un solo archivo y un solo trabajo: no se cambia de archivo a mitad. */
                printf(COLOR_ERROR "Hay un trabajo en curso; espera con 'w' antes de abrir otro archivo.\n" COLOR_RESET);
            } else {
                editor_open(rest);
            }
        } else if (strcmp(cmd, "p") == 0) {
            if (g_fd == -1) {
                printf(COLOR_ERROR "No hay ningún archivo abierto. Usa: o <archivo>\n" COLOR_RESET);
            } else {
                /* Lectura: puede ir en paralelo con la compresión (también es lectora). */
                pthread_rwlock_rdlock(&g_archivo_lock);
                if (rest == NULL) {
                    editor_print_all();
                } else {
                    editor_print_line(atoi(rest));
                }
                pthread_rwlock_unlock(&g_archivo_lock);
            }
        } else if (strcmp(cmd, "a") == 0) {
            if (g_fd == -1) {
                printf(COLOR_ERROR "No hay ningún archivo abierto. Usa: o <archivo>\n" COLOR_RESET);
            } else if (rest == NULL) {
                printf(COLOR_ERROR "Uso: a <texto>\n" COLOR_RESET);
            } else if (pthread_rwlock_trywrlock(&g_archivo_lock) != 0) {
                archivo_ocupado();   /* trywrlock: no se bloquea el editor */
            } else {
                editor_append(rest);
                pthread_rwlock_unlock(&g_archivo_lock);
            }
        } else if (strcmp(cmd, "d") == 0) {
            if (g_fd == -1) {
                printf(COLOR_ERROR "No hay ningún archivo abierto. Usa: o <archivo>\n" COLOR_RESET);
            } else if (rest == NULL) {
                printf(COLOR_ERROR "Uso: d <n>\n" COLOR_RESET);
            } else if (pthread_rwlock_trywrlock(&g_archivo_lock) != 0) {
                archivo_ocupado();
            } else {
                editor_delete(atoi(rest));
                pthread_rwlock_unlock(&g_archivo_lock);
            }
        } else if (strcmp(cmd, "z") == 0) {
            if (g_fd == -1) {
                printf(COLOR_ERROR "No hay ningún archivo abierto. Usa: o <archivo>\n" COLOR_RESET);
            } else {
                compresor_iniciar('z', g_filename);
            }
        } else if (strcmp(cmd, "x") == 0) {
            if (rest == NULL) {
                printf(COLOR_ERROR "Uso: x <archivo.huf>\n" COLOR_RESET);
            } else {
                compresor_iniciar('x', rest);
            }
        } else if (strcmp(cmd, "s") == 0) {
            compresor_mostrar_progreso();
        } else if (strcmp(cmd, "w") == 0) {
            compresor_esperar();
        } else if (strcmp(cmd, "q") == 0) {
            printf(COLOR_INFO "Saliendo del editor.\n" COLOR_RESET);
            break;
        } else {
            printf(COLOR_ERROR "Comando de editor no reconocido: '%s'. Usa o/p/a/d/z/x/s/w/q.\n" COLOR_RESET, cmd);
        }
    }

    /* No se sale dejando hilos vivos: si hay un trabajo, se espera con pthread_join. */
    compresor_revisar_fin();
    if (compresor_activo(NULL, NULL)) {
        compresor_esperar();
    }
    sigaction(SIGINT, &sa_anterior, NULL);

    /* Se cierra el archivo tanto al salir con 'q' como con Ctrl+D (EOF). */
    if (g_fd != -1) {
        checked_close(g_fd);
        g_fd = -1;
    }
    return 0;
}
