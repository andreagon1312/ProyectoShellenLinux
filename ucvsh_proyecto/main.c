#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "parser.h"
#include "executor.h"
#include "jobs.h"
#include "history.h"

int main() {
    //  Inicializamos las estructuras de datos globales en memoria RAM
    // antes de entrar al ciclo infinito. Es vital prepararlas aqui para evitar 'segmentation faults'
    // cuando intentemos agregar el primer trabajo o buscar en el historial.
    init_jobs();
    init_history();

   //ciclo infinito
    while (1) {
        // Revisa si algun trabajo en segundo plano ha terminado
        // (CRITICO): Se coloca justo al inicio del ciclo para hacer la recoleccion
        // de procesos zombies lo mas pronto posible, antes de volver a bloquear la terminal pidiendo texto.
        check_background_jobs();

        // Imprime el prompt personalizado
        printf("ucvsh> ");
        //  fflush(stdout) fuerza a que el texto se imprima inmediatamente en la pantalla.
        // A veces printf guarda el texto en un buffer interno y no lo muestra hasta que encuentra 
        // un salto de linea (\n), lo cual arruinaria la experiencia del prompt.
        fflush(stdout);

        //  Aqui la ejecucion se "congela" esperando que el usuario interactue.
        char *line = read_input_with_history();
        if (!line) {
            // Si el usuario presiona Ctrl+D (EOF - End Of File), la funcion retorna NULL.
            // Rompemos el ciclo infinito para salir limpiamente.
            printf("\n");
            break; // EOF
        }

        // Si el usuario no presiono Enter en vacio 
        if (strlen(line) > 0) {
            // 1. Guardamos la linea en el archivo oculto en disco y en el arreglo RAM.
            add_to_history(line);
            
            // El flujo de datos (Pipeline de ejecucion).
            // 2. El parser convierte el texto crudo en la estructura CommandList (analisis lexico).
            CommandList cl = parse_line(line);
            
            // 3. El executor toma esas estructuras limpias y realiza las llamadas al sistema (fork, execv, pipe).
            execute_command_list(&cl);
            
            // 4. Limpiamos la memoria dinamica (los mallocs del parser) de esta iteracion 
            // para no saturar la RAM mientras la shell siga abierta.
            free_command_list(&cl);
        }

        // Liberamos la memoria del string crudo que nos devolvio read_input_with_history.
        free(line);
    }

    //  Si salimos del ciclo (por el comando 'exit' o Ctrl+D), 
    // destruimos toda la tabla de trabajos para devolverle la memoria limpia al SO.
    free_jobs();
    return 0;
}