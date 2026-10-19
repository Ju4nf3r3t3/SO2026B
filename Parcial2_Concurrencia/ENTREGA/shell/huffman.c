#include "huffman.h"
#include <stdlib.h>
#include <string.h>

/**
 * Recorre el árbol desde `nodo` acumulando el camino en `camino` ('0' = izquierda,
 * '1' = derecha). Al llegar a una hoja, ese camino es el código del byte.
 */
static void asignar_codigos(ArbolHuff *a, int nodo, char *camino, int prof) {
    const NodoHuff *n = &a->nodos[nodo];
    if (n->simbolo != -1) {
        camino[prof] = '\0';
        memcpy(a->codigos[n->simbolo], camino, (size_t)prof + 1);
        a->largo[n->simbolo] = prof;
        return;
    }
    camino[prof] = '0';
    asignar_codigos(a, n->izq, camino, prof + 1);
    camino[prof] = '1';
    asignar_codigos(a, n->der, camino, prof + 1);
}

void huff_construir(ArbolHuff *a, const uint64_t freq[256]) {
    int vivo[HUFF_MAX_NODOS] = {0}; /* 1 = nodo aún sin padre (candidato a unir) */
    int n = 0;

    memset(a->codigos, 0, sizeof(a->codigos));
    memset(a->largo, 0, sizeof(a->largo));

    /* Una hoja por cada byte que aparece al menos una vez. */
    for (int s = 0; s < 256; s++) {
        if (freq[s] > 0) {
            a->nodos[n] = (NodoHuff){ freq[s], s, -1, -1 };
            vivo[n] = 1;
            n++;
        }
    }

    if (n == 0) {          /* Archivo vacío: no hay árbol. */
        a->raiz = -1;
        return;
    }

    /* Se unen repetidamente los dos nodos vivos de menor frecuencia. Con 256 símbolos
     * como máximo, una búsqueda lineal es suficiente y fácil de seguir. En empate gana
     * el índice menor, lo que hace el resultado determinista. */
    int restantes = n;
    while (restantes > 1) {
        int m1 = -1, m2 = -1;
        for (int i = 0; i < n; i++) {
            if (!vivo[i]) continue;
            if (m1 == -1 || a->nodos[i].freq < a->nodos[m1].freq) {
                m2 = m1;
                m1 = i;
            } else if (m2 == -1 || a->nodos[i].freq < a->nodos[m2].freq) {
                m2 = i;
            }
        }
        a->nodos[n] = (NodoHuff){ a->nodos[m1].freq + a->nodos[m2].freq, -1, m1, m2 };
        vivo[m1] = vivo[m2] = 0;
        vivo[n] = 1;
        n++;
        restantes--;
    }
    a->raiz = n - 1;

    if (a->nodos[a->raiz].simbolo != -1) {
        /* Un solo símbolo distinto (p. ej. "aaaa"): el árbol es solo una hoja y el
         * camino sería vacío, así que se le da el código de 1 bit "0". */
        int s = a->nodos[a->raiz].simbolo;
        strcpy(a->codigos[s], "0");
        a->largo[s] = 1;
    } else {
        char camino[256];
        asignar_codigos(a, a->raiz, camino, 0);
    }
}

unsigned char *huff_codificar(const ArbolHuff *a, const unsigned char *datos, size_t n,
                              size_t *n_salida) {
    /* Primero se calcula el tamaño exacto en bits para reservar justo lo necesario. */
    size_t bits = 0;
    for (size_t i = 0; i < n; i++) {
        bits += (size_t)a->largo[datos[i]];
    }
    size_t bytes = (bits + 7) / 8;

    unsigned char *salida = calloc(bytes > 0 ? bytes : 1, 1);
    if (salida == NULL) return NULL;

    /* Se escriben los bits de izquierda a derecha (el más significativo primero). */
    size_t pos = 0;
    for (size_t i = 0; i < n; i++) {
        const char *c = a->codigos[datos[i]];
        for (int j = 0; c[j] != '\0'; j++, pos++) {
            if (c[j] == '1') {
                salida[pos / 8] |= (unsigned char)(0x80 >> (pos % 8));
            }
        }
    }
    *n_salida = bytes;
    return salida;
}

int huff_decodificar(const ArbolHuff *a, const unsigned char *entrada, size_t n_entrada,
                     unsigned char *salida, size_t n_salida) {
    if (a->raiz == -1) {
        return n_salida == 0 ? 0 : -1;
    }

    const NodoHuff *raiz = &a->nodos[a->raiz];
    if (raiz->simbolo != -1) {
        /* Árbol de un solo símbolo: todos los bytes son ese símbolo. */
        memset(salida, raiz->simbolo, n_salida);
        return 0;
    }

    /* Por cada byte de salida se baja desde la raíz siguiendo los bits hasta una hoja. */
    size_t pos = 0;
    size_t total_bits = n_entrada * 8;
    for (size_t i = 0; i < n_salida; i++) {
        int nodo = a->raiz;
        while (a->nodos[nodo].simbolo == -1) {
            if (pos >= total_bits) return -1;  /* Se acabaron los bits: datos corruptos */
            int bit = (entrada[pos / 8] >> (7 - pos % 8)) & 1;
            pos++;
            nodo = bit ? a->nodos[nodo].der : a->nodos[nodo].izq;
        }
        salida[i] = (unsigned char)a->nodos[nodo].simbolo;
    }
    return 0;
}
