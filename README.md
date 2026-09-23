# ucvsh — Shell Personalizada en C para Linux

> **Proyecto #1 — Sistemas Operativos**  
> Diseño e implementación de un intérprete de comandos (shell) interactivo para entornos UNIX/Linux, construido completamente en C sobre llamadas directas al sistema.

---

## 📖 Descripción General

**`ucvsh`** es una shell interactiva desarrollada como proyecto académico para la asignatura de **Sistemas Operativos**. Su objetivo es comprender de forma práctica los mecanismos fundamentales del núcleo de un sistema operativo, tales como:

- Gestión del **ciclo de vida de procesos** (`fork`, `exec`, `wait`, `exit`).
- **Comunicación inter-procesos** mediante *pipes* y redirecciones.
- Manipulación de **descriptores de archivo** (`dup2`, `open`, `close`).
- **Sincronización** entre procesos padres e hijos.
- Control de trabajos (*job control*) y ejecución asíncrona.

La shell emula el comportamiento de intérpretes clásicos como `bash` o `sh`, ofreciendo un prompt personalizado (`ucvsh>`), manejo de operadores lógicos, tuberías, redirecciones, historial persistente y comandos internos (*built-ins*).

---

## ✨ Características Principales

### 🔹 Parsing de línea de comandos
- División de la entrada por espacios en blanco (ignorando espacios múltiples).
- Identificación de operadores especiales antes de la ejecución.
- Soporte para **operadores lógicos**:
  - `;` → Ejecución secuencial incondicional.
  - `&&` → Ejecuta el comando derecho solo si el izquierdo tuvo éxito (`exit code == 0`).
  - `||` → Ejecuta el comando derecho solo si el izquierdo falló (`exit code != 0`).
- Detección de **redirección de salida** (`>`) y **background** (`&`) dentro del parser.

### 🔹 Búsqueda de binarios en `$PATH`
- Lectura dinámica de la variable de entorno `$PATH`.
- Segmentación en rutas de directorio válidas.
- Concatenación iterativa del nombre del comando hasta encontrar un binario ejecutable.
- Función `resolve_path` que comprueba existencia y permisos de ejecución mediante `stat`.

### 🔹 Ejecución de procesos
- **Foreground (por defecto):** El shell espera pasivamente la finalización del hijo.
- **Background (`&`):** Crea el proceso y devuelve el prompt inmediatamente.
- Uso de `fork`, `execv` y `waitpid` para el control de procesos.

### 🔹 Control de trabajos (*Job Control*)
- Estructura de datos interna: **lista enlazada** de nodos `Job`.
- Cada nodo almacena:
  - `id` secuencial, `pid` real, `state` (ejecución/suspendido/finalizado) y `command` (texto original).
- Built-ins asociados:
  - `jobs` → Lista el estado de todos los trabajos en background.
  - `fg` → Trae un trabajo al foreground.
  - `exit` → Termina la shell liberando recursos (sin hijos huérfanos o zombis).
- Recolección automática de procesos terminados con `check_background_jobs()` en cada iteración del bucle principal.

### 🔹 Tuberías (*Pipes*) y Redirecciones
- Detección del operador `|` en el parser.
- Creación del *pipe* mediante `pipe()` y bifurcación de procesos.
- Enlace de `stdout` → `stdin` con `dup2()`.
- Redirecciones de entrada/salida:
  - `>` → Redirección de salida (truncado).
  - `<` → Redirección de entrada (soportada a nivel de parser).
- Cierre correcto de extremos no utilizados para evitar **interbloqueos**.
- Reconstrucción del comando original para mostrarlo en `jobs` (`reconstruct_cmd_str`).

### 🔹 Historial persistente
- Registro de cada línea introducida en `~/.ucvsh_history` (archivo oculto).
- Carga del historial en memoria (arreglo bidimensional) al iniciar la shell.
- Navegación con **flechas direccionales** (↑ / ↓) en modo *raw*.
- Uso de `termios.h` para deshabilitar `ICANON` y `ECHO`, capturando secuencias de escape (`\033`).

---

## 🛠️ Tecnologías y Conceptos Aplicados

| Área | Herramientas / Conceptos |
|---|---|
| Lenguaje | C (C99 o superior) |
| Sistema | GNU/Linux (familia UNIX) |
| Build System | `Makefile` con `gcc -Wall -Wextra -std=c99` |
| Syscalls | `fork`, `execv`, `waitpid`, `pipe`, `dup2`, `open`, `close`, `kill` |
| Terminal | `termios.h`, modo *raw*, secuencias de escape ANSI |
| IPC | Pipes anónimos, descriptores de archivo |
| Job Control | Lista enlazada, señales, estados de trabajo |
| Memoria | `malloc`/`free`, `strdup`, prevención de fugas |

---


## 📂 Estructura del Proyecto
ucvsh_proyecto/
├── main.c # Bucle principal REPL (read-eval-print-loop)
├── parser.c # Tokenización y detección de operadores (;, &&, ||, |, >, &)
├── parser.h
├── executor.c # fork/exec, pipes, redirecciones, resolución de $PATH
├── executor.h
├── jobs.c # Tabla de trabajos (lista enlazada) y control de fondo
├── jobs.h
├── history.c # Historial persistente y navegación con flechas
├── history.h
├── Makefile # Compilación del binario ucvsh
└── README.md

---

## 🚀 Compilación y Ejecución

### Requisitos
- GCC
- GNU Make
- Sistema GNU/Linux

### Pasos

```bash
# Clonar el repositorio
git clone https://github.com/andreagon1312/ProyectoShellenLinux.git
cd ProyectoShellenLinux/ucvsh_proyecto

# Compilar con Makefile
make

# Ejecutar la shell
./ucvsh

# limpieza
make clean

### Ejemplo
ucvsh> ls -la
ucvsh> cat main.c | grep "include" > cabeceras.txt
ucvsh> sleep 15 &
[1] 12345
ucvsh> vim cabeceras.txt
ucvsh> jobs
[1]  Running    sleep 15 &
ucvsh> fg 1
ucvsh> mkdir test_dir && cd test_dir || echo "Fallo al crear directorio"
ucvsh> exit

### Comportamiento esperado
ls -la → Ejecución en foreground, bloquea el prompt hasta finalizar.
cat | grep > archivo → Orquestación concurrente con pipe y redirección.
sleep 15 & → Ejecución en background con retorno inmediato del prompt.
jobs → Muestra los trabajos activos.
fg 1 → Sincroniza con el proceso en background.
&& / || → Encadenamiento condicional según código de salida.

### Decisiones Arquitectónicas
1. Bucle REPL separado del ejecutor: facilita el testeo unitario de cada componente.
2. Tabla de trabajos basada en lista enlazada: permite inserción/eliminación eficiente y soporta un número ilimitado de jobs.
3. Modo raw habilitado solo durante la lectura de línea: se restaura el modo canónico al terminar para no romper el comportamiento de programas hijos como vim.
4. Reaping automático de procesos: se usa waitpid con WNOHANG (a través de check_background_jobs) para evitar zombis en background.
5. Cierre correcto de descriptores: cada hijo cierra los extremos del pipe que no utiliza para evitar bloqueos.
6. Historial en disco y RAM: doble persistencia (arreglo en memoria + archivo ~/.ucvsh_history) para navegación rápida y conservación entre sesiones.
7. Reconstrucción de comandos para jobs: el parser fragmenta la línea, por lo que executor.c reconstruye el string original para mostrarlo en la tabla de trabajos.


### Licencia
Proyecto de carácter académico. Su uso, copia o distribución con fines comerciales no está permitido sin autorización expresa de los autores.
