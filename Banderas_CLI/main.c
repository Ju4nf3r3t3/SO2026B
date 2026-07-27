#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>

/* Códigos de Color ANSI para mejorar la estética en la terminal */
#define COLOR_RESET   "\033[0m"
#define COLOR_TITLE   "\033[1;97m"   /* Blanco brillante */
#define COLOR_PARAM   "\033[1;36m"   /* Cian: para parámetros leídos */
#define COLOR_VAL     "\033[1;32m"   /* Verde: para valores de las variables */
#define COLOR_ERROR   "\033[1;31m"   /* Rojo: para errores en formato */
#define COLOR_INFO    "\033[0;90m"   /* Gris oscuro: para trazas y depuración */
#define COLOR_BANNER  "\033[1;35m"   /* Magenta: cabeceras de modo */

/* Estructura para almacenar los datos parseados de la línea de comandos */
typedef struct {
    char *name;
    int port;
    int verbose;
} CliConfig;

/* Imprime la sintaxis general del programa */
void print_usage(const char *prog_name) {
    printf(COLOR_TITLE "Uso del programa:\n" COLOR_RESET);
    printf("  %s <modo> [banderas...]\n\n", prog_name);
    printf(COLOR_TITLE "Modos disponibles:\n" COLOR_RESET);
    printf("  " COLOR_PARAM "manual" COLOR_RESET "      - Parseo manual directo iterando sobre el arreglo argv\n");
    printf("  " COLOR_PARAM "getopt" COLOR_RESET "      - Parseo estándar POSIX usando getopt()\n");
    printf("  " COLOR_PARAM "getopt_long" COLOR_RESET " - Parseo estándar GNU usando getopt_long()\n\n");
    printf(COLOR_TITLE "Banderas soportadas:\n" COLOR_RESET);
    printf("  " COLOR_PARAM "-n, --name <texto>" COLOR_RESET "  Nombre de usuario (ej: -n Edison)\n");
    printf("  " COLOR_PARAM "-p, --port <entero>" COLOR_RESET " Puerto de conexión (ej: -p 8080)\n");
    printf("  " COLOR_PARAM "-v, --verbose" COLOR_RESET "       Activa logs de depuración adicionales\n");
    printf("  " COLOR_PARAM "-h, --help" COLOR_RESET "          Muestra esta ayuda de uso\n");
}

/* Imprime el estado del arreglo argv recibido del sistema operativo */
void print_argv_structure(int argc, char **argv) {
    printf(COLOR_INFO "[Trazado] Estructura recibida del stack (argv):\n" COLOR_RESET);
    printf(COLOR_INFO "  argc = %d\n" COLOR_RESET, argc);
    for (int i = 0; i < argc; i++) {
        printf(COLOR_INFO "  argv[%d] = \"%s\" (Dirección: %p)\n" COLOR_RESET, i, argv[i], (void*)argv[i]);
    }
    printf("\n");
}

/* Muestra el resultado final de la configuración después del parseo */
void print_config_result(const CliConfig *config) {
    printf(COLOR_TITLE "========================================\n" COLOR_RESET);
    printf(COLOR_TITLE "    Configuración Final Obtenida\n" COLOR_RESET);
    printf(COLOR_TITLE "========================================\n" COLOR_RESET);
    printf("  Nombre (Name):   " COLOR_VAL "%s" COLOR_RESET "\n", config->name ? config->name : "(no definido)");
    printf("  Puerto (Port):   " COLOR_VAL "%d" COLOR_RESET "\n", config->port);
    printf("  Modo Detallado:  " COLOR_VAL "%s" COLOR_RESET "\n", config->verbose ? "ACTIVO (True)" : "INACTIVO (False)");
    printf(COLOR_TITLE "========================================\n\n" COLOR_RESET);
}

/**
 * ====================================================================================
 * ENFOQUE 1: PARSEO MANUAL DE ARGUMENTOS
 * ====================================================================================
 * Se recorre argv paso a paso con un ciclo for o while. 
 * Compara cadenas usando strcmp().
 */
void parse_manual(int argc, char **argv, CliConfig *config) {
    printf(COLOR_BANNER "--- Iniciando Parseo Manual (Iteración Directa sobre argv) ---\n" COLOR_RESET);
    
    /* Empezamos en i = 2 porque argv[0] es el ejecutable y argv[1] es el modo "manual" */
    for (int i = 2; i < argc; i++) {
        printf(COLOR_INFO "[manual-step] Evaluando argv[%d] = \"%s\"\n" COLOR_RESET, i, argv[i]);

        if (strcmp(argv[i], "-n") == 0 || strcmp(argv[i], "--name") == 0) {
            /* Validar que el argumento de la bandera no esté vacío */
            if (i + 1 < argc) {
                config->name = argv[i + 1];
                printf(COLOR_INFO "  -> Detectado: Name = \"%s\"\n" COLOR_RESET, config->name);
                i++; /* Avanzar índice adicional ya que consumimos el valor */
            } else {
                fprintf(stderr, COLOR_ERROR "Error: La bandera '%s' requiere un argumento de texto.\n" COLOR_RESET, argv[i]);
                exit(1);
            }
        } 
        else if (strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--port") == 0) {
            if (i + 1 < argc) {
                config->port = atoi(argv[i + 1]);
                printf(COLOR_INFO "  -> Detectado: Port = %d\n" COLOR_RESET, config->port);
                i++;
            } else {
                fprintf(stderr, COLOR_ERROR "Error: La bandera '%s' requiere un argumento numérico.\n" COLOR_RESET, argv[i]);
                exit(1);
            }
        } 
        else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            config->verbose = 1;
            printf(COLOR_INFO "  -> Detectado: Modo Verbose activado\n" COLOR_RESET);
        } 
        else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            exit(0);
        } 
        else {
            fprintf(stderr, COLOR_ERROR "Advertencia: Bandera '%s' no reconocida. Ignorada.\n" COLOR_RESET, argv[i]);
        }
    }
}

/**
 * ====================================================================================
 * ENFOQUE 2: POSIX getopt()
 * ====================================================================================
 * getopt(3) lee consecutivamente las opciones y sus argumentos opcionales.
 * Utiliza variables globales:
 * - optarg: Puntero al argumento de la opción actual (si requiere uno).
 * - optind: Índice del siguiente argumento a procesar.
 * - optopt: Almacena el carácter no reconocido en caso de error.
 * - opterr: Si es 0, getopt no imprimirá mensajes de error por defecto a stderr.
 */
void parse_getopt(int argc, char **argv, CliConfig *config) {
    printf(COLOR_BANNER "--- Iniciando Parseo POSIX usando getopt() ---\n" COLOR_RESET);

    int opt;
    /* La cadena "n:p:vh" indica:
     * - 'n:' -> opción -n requiere argumento.
     * - 'p:' -> opción -p requiere argumento.
     * - 'v'  -> opción -v no requiere argumento.
     * - 'h'  -> opción -h no requiere argumento.
     */
    
    /* Reiniciamos el índice de getopt para saltar el modo (argv[1]) */
    optind = 2;

    while ((opt = getopt(argc, argv, "n:p:vh")) != -1) {
        printf(COLOR_INFO "[getopt-step] Encontrado carácter de opción '%c' (optind=%d)\n" COLOR_RESET, opt, optind);

        switch (opt) {
            case 'n':
                config->name = optarg;
                printf(COLOR_INFO "  -> optarg = \"%s\"\n" COLOR_RESET, optarg);
                break;
            case 'p':
                config->port = atoi(optarg);
                printf(COLOR_INFO "  -> optarg = \"%s\" (entero: %d)\n" COLOR_RESET, optarg, config->port);
                break;
            case 'v':
                config->verbose = 1;
                printf(COLOR_INFO "  -> Verbose activado\n" COLOR_RESET);
                break;
            case 'h':
                print_usage(argv[0]);
                exit(0);
            case '?':
                /* getopt ya imprime automáticamente el mensaje de error por defecto */
                fprintf(stderr, COLOR_ERROR "Error: Formato inválido o falta argumento para opción '-%c'\n" COLOR_RESET, optopt);
                exit(1);
            default:
                break;
        }
    }
}

/**
 * ====================================================================================
 * ENFOQUE 3: GNU getopt_long()
 * ====================================================================================
 * Permite parsear opciones largas como --name o --port, facilitando la legibilidad.
 * 
 * Requiere una estructura struct option con los siguientes campos:
 * - name: Nombre largo (ej. "name").
 * - has_arg: Indica si requiere valor (required_argument, no_argument, optional_argument).
 * - flag: Dirección de un entero que se modificará con el valor de 'val'. Si es NULL,
 *         getopt_long retorna el carácter en 'val'.
 * - val: Carácter equivalente de opción corta (ej. 'n').
 */
void parse_getopt_long(int argc, char **argv, CliConfig *config) {
    printf(COLOR_BANNER "--- Iniciando Parseo GNU usando getopt_long() ---\n" COLOR_RESET);

    int opt;
    int option_index = 0;

    /* Configuración de las opciones largas */
    static struct option long_options[] = {
        {"name",    required_argument, NULL, 'n'},
        {"port",    required_argument, NULL, 'p'},
        {"verbose", no_argument,       NULL, 'v'},
        {"help",    no_argument,       NULL, 'h'},
        {NULL,      0,                 NULL,  0 } /* Sentinela de cierre */
    };

    /* Reiniciamos el índice de getopt para saltar el modo (argv[1]) */
    optind = 2;

    while ((opt = getopt_long(argc, argv, "n:p:vh", long_options, &option_index)) != -1) {
        printf(COLOR_INFO "[getopt-long-step] Opción: '%c' (Larga: --%s, optind=%d)\n" COLOR_RESET, 
               opt, long_options[option_index].name, optind);

        switch (opt) {
            case 'n':
                config->name = optarg;
                printf(COLOR_INFO "  -> optarg = \"%s\"\n" COLOR_RESET, optarg);
                break;
            case 'p':
                config->port = atoi(optarg);
                printf(COLOR_INFO "  -> optarg = \"%s\" (entero: %d)\n" COLOR_RESET, optarg, config->port);
                break;
            case 'v':
                config->verbose = 1;
                printf(COLOR_INFO "  -> Verbose activado\n" COLOR_RESET);
                break;
            case 'h':
                print_usage(argv[0]);
                exit(0);
            case '?':
                exit(1);
            default:
                break;
        }
    }
}

int main(int argc, char **argv) {
    /* Valores iniciales de configuración por defecto */
    CliConfig config = {
        .name = NULL,
        .port = 80,
        .verbose = 0
    };

    /* Si no se pasan argumentos, mostramos la ayuda general */
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    /* Mostrar de forma pedagógica el stack argv completo */
    print_argv_structure(argc, argv);

    /* Enrutar la ejecución al parseador correspondiente según el primer argumento */
    const char *mode = argv[1];
    if (strcmp(mode, "manual") == 0) {
        parse_manual(argc, argv, &config);
    } 
    else if (strcmp(mode, "getopt") == 0) {
        parse_getopt(argc, argv, &config);
    } 
    else if (strcmp(mode, "getopt_long") == 0) {
        parse_getopt_long(argc, argv, &config);
    } 
    else if (strcmp(mode, "-h") == 0 || strcmp(mode, "--help") == 0 || strcmp(mode, "help") == 0) {
        print_usage(argv[0]);
        return 0;
    } 
    else {
        fprintf(stderr, COLOR_ERROR "Error: Modo '%s' no reconocido.\n\n" COLOR_RESET, mode);
        print_usage(argv[0]);
        return 1;
    }

    /* Imprimir el resultado de la configuración capturada */
    print_config_result(&config);

    /* Si existen argumentos posicionales adicionales sobrantes (no asociados a banderas) */
    if (optind < argc && strcmp(mode, "manual") != 0) {
        printf(COLOR_TITLE "Argumentos posicionales adicionales (sobrantes):\n" COLOR_RESET);
        for (int i = optind; i < argc; i++) {
            printf("  argv[%d] = \"%s\"\n", i, argv[i]);
        }
        printf("\n");
    }

    return 0;
}
