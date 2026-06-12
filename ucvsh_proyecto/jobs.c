#include "jobs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
//Implementar un subsistema de rastreo de procesos asincronos en segundo plano.
//se hace ya que cuando el usuario ejecuta un comando con '&', la shell no espera
// al hijo con waitpid(). Al perder el control directo, la shell necesita una estructura
// dinamica en memoria para registrar el PID asignado por el Kernel. De lo contrario, se 
// perderia el rastro del hijo, imposibilitando su recoleccion y provocando procesos "Zombies".
//  Variables globales estaticas.
// job_head es el puntero inicial de nuestra lista enlazada.
// ENCAPSULAMIENTO DE DATOS A NIVEL DE ENLACE:
// El modificador 'static' restringe el alcance de estas variables globales estrictamente
// al archivo 'jobs.c'.
static Job *job_head = NULL;
static int next_job_id = 1;

void init_jobs() {
    // INICIALIZADOR DE SUBSISTEMA:
    // Restablece los valores base del modulo. Se invoca una sola vez en el main.c al arrancar la shell
    job_head = NULL;
    next_job_id = 1;
}
// Esta funcion es llamada exclusivamente por el proceso Padre justo despues de realizar un fork() 
// exitoso de un comando que tiene la bandera 'background' activada.
void add_job(pid_t pid, const char *command) {
    // Solicitamos memoria dinámica al sistema para el nuevo nodo.
    
    Job *new_job = malloc(sizeof(Job));
    // Inicializamos cada campo de la metadata del nuevo proceso en el nodo asignado:
    new_job->id = next_job_id++; // Asigna el identificador secuencial e incrementa el contador global.
    new_job->pid = pid; // Guarda el ID real del proceso hijo proporcionado por el Kernel de Linux.
    new_job->state = JOB_RUNNING; // hace que pase del estado inicial a RUNNING
    // strdup hace un malloc interno para copiar la cadena, por lo que luego hay que liberarla.
    new_job->command = strdup(command);
    // PROTECCION DE MEMORIA TEXTUAL:

    // Como este nuevo nodo sera el ultimo de la lista enlazada, su puntero 'next' debe apuntar
    // obligatoriamente a NULL para indicar el final de la estructura de datos.
    new_job->next = NULL;

    //  Lógica clásica de inserción al final de una lista enlazada.
    if (!job_head) {
        // Si la lista está vacía, este es el primer nodo.
        job_head = new_job;
    } else {
        // Si ya hay nodos, recorremos la lista hasta llegar al último y lo enlazamos ahí.
        Job *curr = job_head;
        while (curr->next) {
            curr = curr->next;
        }
        curr->next = new_job;
    }
    // Imprime el mensaje clásico de shell cuando mandas algo al fondo: [ID] PID
    printf("[%d] %d\n", new_job->id, new_job->pid);
}

void remove_job(pid_t pid) {
    Job *curr = job_head;
    Job *prev = NULL;

    //  Recorremos la lista buscando el nodo que coincida con el PID.
    while (curr) {
        if (curr->pid == pid) {
            // Si lo encontramos, reconectamos los punteros para "puentear" este nodo.
            if (prev) {
                prev->next = curr->next;
            } else {
                job_head = curr->next; // Si era el primero, la cabeza ahora es el segundo.
            }
            //  (CRÍTICO): Liberamos primero el string interno, y luego el nodo completo.
            // Omitir esto genera fugas de memoria gigantes.
            free(curr->command);
            free(curr);
            return;
        }
        prev = curr;
        curr = curr->next;
    }
}

void print_jobs() {
    Job *curr = job_head;
    //  Recorre toda la lista imprimiendo el estado actual de cada nodo.
    // Esto es lo que se ejecuta cuando el usuario escribe 'jobs'.
    while (curr) {
        const char *state_str = "Running";
        if (curr->state == JOB_SUSPENDED) state_str = "Suspended";
        else if (curr->state == JOB_DONE) state_str = "Done";
        
        printf("[%d] %s \t %s\n", curr->id, state_str, curr->command);
        curr = curr->next;
    }
}

void check_background_jobs() {
    int status;
    pid_t pid;
    
    // WNOHANG hace que waitpid retorne inmediatamente si ningun hijo ha terminado
    //  Al usar el PID -1, le decimos al sistema: 
    // "Revisa el estado de CUALQUIER proceso hijo". El flag WNOHANG es mágico porque hace la llamada 
    // "no bloqueante". Si ningún hijo ha muerto, waitpid retorna 0 al instante y el ciclo del main.c puede continuar.
    // Si algún hijo murió, retorna su PID (> 0), entramos al ciclo, imprimimos que terminó y lo borramos de la lista.
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        Job *curr = job_head;
        while (curr) {
            if (curr->pid == pid) {
                printf("[%d] Done \t %s\n", curr->id, curr->command);
                remove_job(pid);
                break;
            }
            curr = curr->next;
        }
    }
}

void fg_job(int job_id) {
    Job *curr = job_head;
    //  Busca el trabajo en la lista por su ID interno.
    while (curr) {
        if (curr->id == job_id) {
            printf("%s\n", curr->command);
            int status;
            // Al encontrarlo, clavamos un waitpid bloqueante (sin WNOHANG) sobre ese PID específico.
            // Esto "congela" nuestra shell hasta que el proceso termine, simulando traerlo al primer plano (foreground).
            waitpid(curr->pid, &status, 0);
            remove_job(curr->pid);
            return;
        }
        curr = curr->next;
    }
    printf("fg: trabajo no encontrado: %d\n", job_id);
}

void free_jobs() {
    Job *curr = job_head;
    // Función de limpieza absoluta.
    // Se ejecuta justo antes de que la shell termine (comando exit) para devolverle 
    // limpia toda la memoria al sistema operativo.
    while (curr) {
        Job *next = curr->next;
        free(curr->command);
        free(curr);
        curr = next;
    }
    job_head = NULL;
}