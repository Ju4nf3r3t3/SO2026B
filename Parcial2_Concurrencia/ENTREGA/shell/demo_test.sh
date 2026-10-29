#!/bin/bash
# Script de pruebas del editor de texto CLI integrado al shell eafitOS.
# Pensado para correr en Linux (requisito del enunciado). Compila el shell
# y le envía por stdin una secuencia de comandos, cubriendo el flujo normal
# y los casos borde del editor (o, p, a, d, q).

set -e
cd "$(dirname "$0")"

echo "=== Compilando (make all) ==="
make clean
make all

echo
echo "=== Caso 1: flujo normal ==="
echo "(crear archivo con 'edit', agregar lineas, imprimir todo/una linea, borrar una linea, verificar en disco)"
rm -f demo1.txt
{
  echo "edit demo1.txt"
  echo "a Primera linea"
  echo "a Segunda linea"
  echo "a Tercera linea"
  echo "p"
  echo "p 2"
  echo "d 2"
  echo "p"
  echo "q"
  echo "d_read demo1.txt"
  echo "exit"
} | ./eafitOS

echo
echo "=== Caso 2: casos borde ==="
echo "(comandos sin archivo abierto, abrir archivo nuevo con 'o', pedir/borrar una linea que no existe)"
rm -f demo2.txt
{
  echo "edit"
  echo "p"
  echo "a intento sin archivo abierto"
  echo "o demo2.txt"
  echo "p"
  echo "p 5"
  echo "d 5"
  echo "q"
  echo "exit"
} | ./eafitOS

echo
echo "=== Pruebas finalizadas ==="
echo "Revisar arriba: cada open/read/write/lseek/ftruncate/close debe verse trazado,"
echo "los casos borde deben mostrar mensajes de error controlados (nunca un crash/segfault)."
