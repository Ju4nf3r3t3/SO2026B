#ifndef HUFFMAN_H
#define HUFFMAN_H

#include <stddef.h>
#include <stdint.h>

/**
 * ====================================================================================
 * Codificación de Huffman (sin hilos)
 * ====================================================================================
 * Lógica pura del algoritmo: construir el árbol a partir de una tabla de 256
 * frecuencias, generar el código de cada byte, codificar un bloque y decodificarlo.
 * No usa hilos ni variables globales, así que varios hilos pueden llamar a
 * huff_codificar() a la vez sobre el mismo árbol (solo lo leen).
 */

#define HUFF_MAX_NODOS 511   /* 256 hojas + 255 nodos internos como máximo */

typedef struct {
    uint64_t freq;   /* Frecuencia (suma de las hojas debajo de este nodo) */
    int simbolo;     /* Byte 0..255 si es hoja, -1 si es nodo interno */
    int izq, der;    /* Índices de los hijos en el arreglo (-1 si es hoja) */
} NodoHuff;

typedef struct {
    NodoHuff nodos[HUFF_MAX_NODOS];
    int raiz;               /* Índice de la raíz, -1 si no hay ningún símbolo (archivo vacío) */
    char codigos[256][256]; /* Código de cada byte como texto, p. ej. "0110" (máx. 255 bits) */
    int largo[256];         /* Largo en bits del código de cada byte (0 si no aparece) */
} ArbolHuff;

/* Construye el árbol y la tabla de códigos. Es determinista: con la misma tabla de
 * frecuencias siempre produce el mismo árbol (así el descompresor lo puede rehacer). */
void huff_construir(ArbolHuff *a, const uint64_t freq[256]);

/* Codifica n bytes. Retorna un buffer nuevo (malloc) con los bits empaquetados y deja
 * su tamaño en *n_salida. Retorna NULL si no hay memoria. */
unsigned char *huff_codificar(const ArbolHuff *a, const unsigned char *datos, size_t n,
                              size_t *n_salida);

/* Decodifica exactamente n_salida bytes desde los n_entrada bytes de entrada.
 * Retorna 0 si todo bien, -1 si los datos están corruptos. */
int huff_decodificar(const ArbolHuff *a, const unsigned char *entrada, size_t n_entrada,
                     unsigned char *salida, size_t n_salida);

#endif /* HUFFMAN_H */
