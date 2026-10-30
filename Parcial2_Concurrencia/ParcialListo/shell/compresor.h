#ifndef COMPRESOR_H
#define COMPRESOR_H

#include <pthread.h>

/**
 * ====================================================================================
 * Compresor Huffman concurrente (trabajo en segundo plano del editor)
 * ====================================================================================
 * El editor lanza la compresión ('z') o descompresión ('x') en un hilo en segundo
 * plano y sigue atendiendo al usuario. Solo puede haber un trabajo a la vez.
 */

/* Lock lectores-escritores sobre el archivo abierto en el editor.
 *   - Lectores: el hilo de compresión (mientras lee el archivo) y el comando 'p'.
 *   - Escritores: los comandos 'a' y 'd' (con trywrlock, para no bloquear el editor). */
extern pthread_rwlock_t g_archivo_lock;

/* Lanza el trabajo: tipo 'z' comprime `entrada` a `entrada.huf`; tipo 'x' descomprime
 * `entrada` (un .huf) a `<nombre>.out`. Retorna 0 si se lanzó, -1 si no (ya imprime el motivo). */
int compresor_iniciar(char tipo, const char *entrada);

/* Retorna 1 si hay un trabajo corriendo y llena tipo y porcentaje; 0 si no. */
int compresor_activo(char *tipo, int *porcentaje);

/* Comando 's': imprime la barra de progreso. */
void compresor_mostrar_progreso(void);

/* Se llama antes de cada prompt: si el trabajo ya terminó, hace join y avisa (una vez). */
void compresor_revisar_fin(void);

/* Comandos 'w' y 'q': espera (pthread_join) a que termine el trabajo y muestra el resultado. */
void compresor_esperar(void);

#endif /* COMPRESOR_H */
