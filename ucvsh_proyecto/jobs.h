#ifndef JOBS_H
#define JOBS_H

#include <sys/types.h>

// Un enum (enumeración) para manejar los estados posibles de un proceso en segundo plano.
typedef enum {
    JOB_RUNNING,
    JOB_SUSPENDED,
    JOB_DONE
} JobState;

// Estructura de un nodo para la lista enlazada simple.
// Contiene la metadata del proceso y un puntero 'next' que apunta al siguiente trabajo en la lista.
typedef struct Job {
    int id;               // ID interno del trabajo en nuestra shell (ej: 1, 2, 3...)
    pid_t pid;            // ID real del proceso asignado por el kernel de Linux
    JobState state;       // Estado actual del trabajo
    char *command;        // String con el comando original ingresado por el usuario
    struct Job *next;     // Puntero al siguiente nodo
} Job;

// Inicializa la lista de trabajos
//  Asegura que el puntero cabeza (head) empiece en NULL al abrir la shell.
void init_jobs();

// Añade un trabajo a la lista de trabajos en segundo plano
void add_job(pid_t pid, const char *command);

// Elimina un trabajo por su PID
//  Busca en la lista enlazada y libera la memoria (free) del nodo cuando el proceso muere.
void remove_job(pid_t pid);

// Imprime todos los trabajos
void print_jobs();

// Revisa si hay trabajos en segundo plano terminados y los recolecta (reap)
//  Esta es la función clave para evitar procesos "zombies".
void check_background_jobs();

// Trae un trabajo al primer plano y espera por el
//  La implementación del comando interno 'fg'.
void fg_job(int job_id);

// Libera los recursos
//  Se llama al ejecutar 'exit' para vaciar toda la memoria de la lista enlazada.
void free_jobs();

#endif // JOBS_H