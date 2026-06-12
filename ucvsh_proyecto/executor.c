#include "executor.h"
#include "jobs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>

// Reconstruye la cadena del comando para la lista de trabajos a partir de la estructura Command
// Esta funcion simplemente vuelve a unir los argumentos
// del comando original para poder mostrarlos enteros cuando el usuario
// escriba el comando interno 'jobs'. 
// Es necesaria ya que El 'parser' fragmenta la linea original eliminando espacios,
// operadores y redirecciones para que 'execv' pueda trabajar. Esta funcion hace el
// proceso inverso


//El modificador 'static' limita el alcance de esta funcion UNICAMENTE a este
// archivo (executor.c). Evita conflictos de nombres con otros archivos del proyecto.
static char *reconstruct_cmd_str(Command *cmd) {
    //Se declara un buffer temporal en la pila de 1024 bytes.
    char buf[1024] = {0};

    //se hace un ciclo para iterar sobre los argumentos del comando.
    //No usamos un contador como 'num_args'. Aprovechamos que el estandar de
    // sistemas Unix exige que el arreglo de argumentos para 'execv' termine obligatoriamente en NULL.
    // haciendo que el ciclo se detenga de forma segura cuando 'cmd->args[i]' sea igual a NULL.
    for (int i = 0; cmd->args[i] != NULL; i++) {
        //'strcat' busca el final actual del string en 'buf' y concatena
        //secuencialmente el argumento actual
        strcat(buf, cmd->args[i]);
        if (cmd->args[i+1] != NULL) strcat(buf, " ");
    }
    // RECONSTRUCCION DE LA REDIRECCION DE SALIDA:
    // El parser extrajo el nombre del archivo de redireccion y lo guardo en el puntero 'redirect_out'.
    // Si este puntero no es NULL, significa que el usuario uso un operador '>'.
    if (cmd->redirect_out) {
        strcat(buf, " > ");
        strcat(buf, cmd->redirect_out);
    }

    // RECONSTRUCCION DEL BACKGROUND:
    // Si la bandera 'background' esta activa, significa que la linea original finalizaba con '&'.
    if (cmd->background) {
        strcat(buf, " &");
    }

    // GESTION DE MEMORIA CRITICA:
    // 'buf' es una variable local automatica; al terminar la funcion, su memoria en el stack se destruye.
    // 'strdup(buf)' realiza un 'malloc' con el tamaño del string en 'buf'
    // y copia los datos hacia la memoria dinamica.
    // Quien reciba el puntero retornado por esta funcion (el struct Job)
    // queda estrictamente OBLIGADO a invocar 'free()' sobre el mas adelante para evitar fugas de memoria.
    return strdup(buf);
}

// Verifica si un archivo es ejecutable
// Se utiliza 'stat' (llamada al sistema) para obtener la metadata del archivo.
// Verifica que devuelva 0 (el archivo existe) y que tenga los permisos de ejecucion para el usuario (S_IXUSR).
static int is_executable(const char *path) {
    struct stat sb;
    return (stat(path, &sb) == 0 && sb.st_mode & S_IXUSR);
}

// Resuelve un comando usando la variable de entorno PATH
// Esta funcion cumple al buscar binarios dinamicamente.
//El modificador 'static' encapsula la funcion dentro de 'executor.c'.
// Previene choque entre nombres si otro componente del proyecto tuviese una funcion llamada igual.
static char *resolve_path(const char *cmd) {
    // 'strchr' escanea la cadena 'cmd' buscando la primera aparicion del caracter '/'.
    // Si contiene una barra oblicua, significa que el usuario NO escribio un comando simple,
    // sino una ruta explicita
    // Si existe y es ejecutable, la usamos tal cual.
    if (strchr(cmd, '/') != NULL) {
        if (is_executable(cmd)) return strdup(cmd);
            // Retornamos una copia duplicada en el Heap usando 'strdup'.
            // Esto mantiene la consistencia de que la funcion SIEMPRE devuelve memoria dinamica
            // que el receptor tendra la obligacion de liberar con 'free()'.
        return NULL;
    }

    // INTERACCION CON EL ENTORNO DEL PROCESO:
    // 'getenv' accede al bloque de memoria especial del proceso actual (el arreglo 'envp[]')
    // y busca la variable de entorno denominada "PATH". Esta variable contiene strings de rutas
    // concatenadas y separadas por caracteres de dos puntos
    char *path_env = getenv("PATH");
    if (!path_env) path_env = "/bin:/usr/bin:/sbin"; 
    // MECANISMO DE TOLERANCIA A FALLOS (FALLBACK):
    // Si por alguna razon la variable PATH fue borrada o esta vacia (NULL), asignamos rutas
    // esenciales por defecto de sistemas Linux.
    // Se copia la ruta porque strtok destruye/modifica la cadena original al procesarla.
    char *path_copy = strdup(path_env);
    // PROTECCION DE MEMORIA DE ENTORNO CRITICA:
    // 'getenv' devuelve un puntero DIRECTO a la memoria original del entorno del sistema operativo.
    // NUNCA debemos modificar ese string directamente. Por ello, usamos 'strdup' para clonar todo el
    // contenido del PATH en una nueva zona ('path_copy')
    // Extraemos la primera ruta delimitada por dos puntos (:)
    char *dir = strtok(path_copy, ":");
    //'strtok' mantiene un estado estatico interno para recordar la posicion del string.
    char full_path[1024];

    // Iteramos por cada una de las rutas extraidas del PATH
    while (dir != NULL) {
        // PREVENCION DE VULNERABILIDADES:
        //El segundo argumento 'sizeof(full_path)' limita estrictamente la escritura
        // a 1024 bytes, garantizando que el software nunca corrompa la pila (stack) y asegurando
        // la inclusion automatica del caracter terminador '\0' al final del string.
        // Formato: combina "directorio" + "/" + "nombre_del_comando"
        snprintf(full_path, sizeof(full_path), "%s/%s", dir, cmd);
        // Si el archivo concatenado existe en esta ruta y es ejecutable, lo retornamos.
        if (is_executable(full_path)) {
            free(path_copy); // Liberamos la copia para evitar fugas de memoria (memory leaks)
            return strdup(full_path);
        }
        // Pasamos al siguiente directorio del PATH
        dir = strtok(NULL, ":");
    }

    free(path_copy);
    return NULL; // El comando no se encontro en ninguna ruta.
}
// Un comando 'builtin' es una instruccion que se ejecuta DIRECTAMENTE dentro del espacio
// de direccionamiento de nuestra propia Shell (el proceso padre), sin hacer un 'fork()'.
// Maneja comandos internos (built-ins): exit, cd, jobs, fg
//Para que un comando afecte el estado interno de la Shell 
// DEBE ser un builtin ejecutado por el propio padre.
static int handle_builtin(Command *cmd) {
    // Retorna: 0 o 1 si el comando fue un builtin procesado con exito/error, 
    // o -1 si no coincide con ningun builtin.
    // 'strcmp' compara el primer argumento (el nombre del comando) con el literal "exit".
    // Si da 0, significa que son exactamente iguales.
    if (strcmp(cmd->args[0], "exit") == 0) {
        // Simplemente salimos directamente
        exit(0);
    } else if (strcmp(cmd->args[0], "cd") == 0) {
        // Validamos la sintaxis del comando. El arreglo args[] esta terminado en NULL.
        // Si 'args[1]' es NULL, significa que el usuario escribio 'cd' a secas sin especificar ruta.
        if (cmd->args[1] == NULL) {
            fprintf(stderr, "cd: falta el argumento\n");
        } else {
            // LLAMADA AL SISTEMA CRITICA: chdir()
            // 'chdir' le solicita al Kernel de Linux modificar la propiedad "Current Working Directory"
            // del proceso actual de nuestra Shell al string apuntado por 'cmd->args[1]'.
            // Si la llamada falla (ej: la carpeta no existe o no hay permisos), chdir() retorna -1.
  
            if (chdir(cmd->args[1]) != 0) {
                perror("cd");
                return 1;
            }
        }
        return 0;
    } else if (strcmp(cmd->args[0], "jobs") == 0) {
        // Invocamos a 'print_jobs()', una funcion implementada en 'jobs.c'.
        // Como todos los hilos/funciones de la shell comparten el mismo espacio de direccionamiento,
        // esta funcion puede leer la cabeza de la lista enlazada global ('job_head') e imprimir en 
        // pantalla la lista de procesos que dejamos corriendo en segundo plano con el operador '&'.
        print_jobs();
        return 0;
    } else if (strcmp(cmd->args[0], "fg") == 0) {
        // Validacion de sintaxis: Verificamos si el usuario nos dio el identificador del trabajo
        if (cmd->args[1] == NULL) {
            fprintf(stderr, "fg: falta el id del trabajo\n");
        } else {
            // CONVERSION DE TIPOS DE DATOS
            // Luego, le pasamos ese ID entero a 'fg_job' (de jobs.c) para que busque el proceso en la lista,
            // aplique un 'waitpid()' bloqueante sobre su PID real, y congele temporalmente la shell hasta que 
            // dicho trabajo en segundo plano termine, trayendolo efectivamente al primer plano.
            fg_job(atoi(cmd->args[1]));
        }
        return 0;
    }
    return -1; // No es un comando interno
}
// "Fork-and-Exec". Divide el hilo de ejecucion de la shell en dos entidades virtuales
// aisladas (Padre e Hijo), reconfigura sus canales de comunicacion de datos (E/S) , evoluciona al hijo en el programa destino y decide
// estrategicamente si la shell debe esperar (bloquearse) o continuar inmediatamente.
static int execute_single_command(Command *cmd, int in_fd, int out_fd) {
    // CONTROL DE ENTRADA VACÍA:
    if (cmd->args[0] == NULL) return 0;
    // INTERCEPTOR DE COMANDOS INTERNOS:
    // Evaluamos si el comando corresponde a una instruccion del sistema de nuestra shell (exit, cd, etc.).
    // Si 'handle_builtin' devuelve algo distinto a -1, significa que intercepto y proceso el comando
    // dentro del espacio de memoria de este mismo proceso padre, por lo que retornamos su resultado directamente.

    int builtin_res = handle_builtin(cmd);
    if (builtin_res != -1) return builtin_res;

    // Buscamos la ruta real del binario antes de clonar el proceso.
    char *exec_path = resolve_path(cmd->args[0]);
    if (!exec_path) {
        // CONVENCION ESTANDAR POSIX:
        // Si no se encuentra el binario, imprimimos un mensaje en el canal de errores ('stderr').
        // Retornamos estrictamente el codigo entero 127. Este numero es el estandar internacional
        // en sistemas Unix (Linux/macOS) que notifica de manera univoca: "Command not found".
        fprintf(stderr, "ucvsh: %s: comando no encontrado\n", cmd->args[0]);
        return 127;
    }

    //  Iniciamos la concurrencia. fork() crea un clon exacto de nuestra shell.
    pid_t pid = fork();
    if (pid < 0) {
        // MANEJO DE ERROR DEL SISTEMA OPERATIVO:
        perror("fork");
        free(exec_path);
        return 1;
        // El Kernel le asigna el valor 0 a la variable 'pid' solo dentro del proceso clonado.
    } else if (pid == 0) {
        // Proceso hijo -> Estamos dentro del clon.
        
        // REDIRECCION POR TUBERIAS (PIPES):
        // Si 'in_fd' no apunta a la entrada estandar (0), significa que este comando recibe datos
        // de un pipe previo. Reemplazamos la entrada usando 'dup2'.
        if (in_fd != STDIN_FILENO) {
            // 'dup2' copia el descriptor 'in_fd' sobre (STDIN). Cualquier lectura
            // despues del nuevo programa extraera datos de la tuberia en lugar del teclado.
            dup2(in_fd, STDIN_FILENO);
            // Cerramos el descriptor duplicado original para que no queden copias residuales libres.
            close(in_fd);
        }
        // De igual forma, si 'out_fd' no es la pantalla (1), el comando pondra sus datos en un pipe.
        if (out_fd != STDOUT_FILENO) {
            // Duplicamos 'out_fd' en  (STDOUT).
            dup2(out_fd, STDOUT_FILENO);
            close(out_fd);
        }

        // Maneja la redireccion si fue especificada
        if (cmd->redirect_out) {
            // PERMISOS OCTALES (0644): Establece los permisos POSIX por defecto del archivo si este es creado:
            // El dueño puede Leer/Escribir (6), el grupo puede Leer (4), otros pueden Leer (4).
            // Abrimos el archivo: O_WRONLY , O_CREAT , O_TRUNC.
            int fd = open(cmd->redirect_out, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            // Si falla la apertura
            if (fd < 0) {
                perror("open");
                exit(1);
            }
            // dup2 "fuerza" a que la salida estandar de este hijo (STDOUT_FILENO, que suele ser la pantalla)
            // apunte ahora al archivo fisico que acabamos de abrir.
            dup2(fd, STDOUT_FILENO);
            close(fd);
        }

        //  El hijo se destruye y es reemplazado por el nuevo programa (exec_path)
        execv(exec_path, cmd->args);
        perror("execv"); // No deberia llegar aqui si execv tiene exito
        exit(1);
    } else {
        // Proceso padre -> Seguimos en la shell original.
        free(exec_path);
        
        // Manejo sincrono vs asincrono.
        if (cmd->background) {
            // ASINCRONO: Si el comando termino en &, agregamos el PID a nuestra lista enlazada interna
            // y retornamos inmediatamente sin esperar, dejando que el hijo corra en segundo plano.
            // Retorna exito inmediatamente para trabajos en segundo plano
            char *cmd_str = reconstruct_cmd_str(cmd);
            // REGISTRO EN LA LISTA GLOBAL DE TAREAS:
            // Registramos el PID real del hijo en la estructura de datos enlazada de 'jobs.c'.
            // Esto permite monitorearlo
            add_job(pid, cmd_str);
            free(cmd_str);
            return 0; 
        } else {
            // SINCRONO: Por defecto, bloqueamos la shell usando waitpid() hasta que el hijo termine su tarea.
            int status;
            waitpid(pid, &status, 0);
            // Si el hijo salio limpiamente, extraemos su codigo de salida (0 suele significar exito).
            if (WIFEXITED(status)) {
                return WEXITSTATUS(status);
            }
            return 1;
        }
    }
}

// Esta funcion orquesta la linea completa de comandos ya procesada por el parser.
// Coordinar, iterar y sincronizar una secuencia de comandos interconectados es su funcion.
// el "Scheduler" logico de nuestra shell. Implementa dos
// caracteristicas :
// 1. Evaluacion de Cortocircuito : Ejecucion condicional basada
//    en operadores booleanos (&& y ||) emulando tablas de verdad.
// 2. Pipeline: Enlaza la salida de un proceso infinito de manera 
//    directa con la entrada del siguiente, sincronizando el cierre de descriptores del Padre 
//    y del Hijo en el Kernel de Linux para evitar bloqueos por Deadlocks.
void execute_command_list(CommandList *cl) {
    // GUARDIA DE ENTRADA: Si la estructura no contiene comandos cargados por el parser,
    // abortamos la funcion inmediatamente para no procesar basura.
    if (cl->num_cmds == 0) return;
    //'last_status' almacena el codigo de retorno del ultimo comando ejecutado
    int last_status = 0;
    int i = 0;

    while (i < cl->num_cmds) {
        // Verifica si hay tuberias (pipeline)
        int pipe_count = 0;
        int j = i;
        //Revisamos hacia adelante cuantos comandos estan unidos por operadores de pipe (|)
        while (j < cl->num_cmds && cl->ops[j] == OP_PIPE) {
            pipe_count++;
            j++;
        }

        if (pipe_count > 0) {
            // Ejecuta las tuberias (pipeline)
            //  Inicia el enlazado dinamico de tuberias en memoria.
            int num_pipe_cmds = pipe_count + 1;
            int pipes[2]; // Un arreglo que guarda dos descriptores: [0] para lectura, [1] para escritura.
            int in_fd = STDIN_FILENO;
            pid_t pids[MAX_COMMANDS];

            // Iteramos sobre todos los comandos que pertenecen a la cadena de tuberias
            for (int k = 0; k < num_pipe_cmds; k++) {
                // Obtenemos la direccion de memoria del comando actual en el arreglo.
                Command *cmd = &cl->cmds[i + k];
                // Por defecto, cada comando escribe hacia la pantalla (STDOUT_FILENO / 1).
                int out_fd = STDOUT_FILENO;

                // Si NO es el ultimo comando de la cadena, creamos una nueva tuberia (pipe).
                if (k < num_pipe_cmds - 1) {
                    if (pipe(pipes) < 0) {
                        perror("pipe");
                        break;
                    }
                    // La salida estandar del proceso actual se debe ir al extremo de escritura del pipe.
                    out_fd = pipes[1];
                }

                // Si es un comando interno dentro de un pipe, lo ejecutamos en el hijo
                // pero por simplicidad asumimos que los comandos internos son raros en pipes excepto quizas echo
                char *exec_path = resolve_path(cmd->args[0]);
                if (!exec_path && handle_builtin(cmd) == -1) {
                    fprintf(stderr, "ucvsh: %s: comando no encontrado\n", cmd->args[0]);
                    last_status = 127;
                } else {
                    pid_t pid = fork(); // Clonamos un nuevo hijo para cada eslabon de la tuberia
                    if (pid == 0) {
                        //  Conectando los tubos.
                        // Conectamos la entrada (in_fd) a la tuberia anterior (si no es el primer comando).
                        if (in_fd != STDIN_FILENO) {
                            dup2(in_fd, STDIN_FILENO);
                            close(in_fd);
                        }
                        // Conectamos la salida (out_fd) a la tuberia actual (si no es el ultimo comando).
                        if (out_fd != STDOUT_FILENO) {
                            dup2(out_fd, STDOUT_FILENO);
                            close(out_fd);
                        }
                        
                        // Cierra el extremo de lectura del pipe en el hijo si existe
                        // Si el proceso hijo no cierra el extremo de lectura (pipes[0])
                        // del ducto actual, los programas (como grep o cat) se quedarian esperando datos
                        // infinitamente porque el ducto jamas emitiria un EOF (End Of File).
                        if (k < num_pipe_cmds - 1) {
                            close(pipes[0]);
                        }

                        // Resolvemos redirecciones (>) al final de un pipe si las hay.
                        if (cmd->redirect_out) {
                            int fd = open(cmd->redirect_out, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                            if (fd >= 0) {
                                dup2(fd, STDOUT_FILENO);
                                close(fd);
                            }
                        }
                        // Si el comando es un builtin , lo ejecutamos dentro de este proceso hijo aislado 
                        // y hacemos exit(0) para que no rompa el flujo de la Shell padre.
                        if (handle_builtin(cmd) != -1) {
                            exit(0);
                        }

                        execv(exec_path, cmd->args);
                        perror("execv");
                        exit(1);
                    } else if (pid > 0) {
                        pids[k] = pid; // El padre guarda todos los PIDs generados
                    }
                }
                
                if (exec_path) free(exec_path);
                // LIMPIEZA DE MEMORIA DINAMICA: 
                //  Limpieza en el padre para no agotar los descriptores del sistema.
                if (in_fd != STDIN_FILENO) close(in_fd);
                if (out_fd != STDOUT_FILENO) close(out_fd);
                // GESTION DE DESCRIPTORES EN EL PADRE:
                // El padre clono los procesos, pero sigue teniendo abiertos los descriptores en su propia tabla.
                // Si no los cierra, la shell sufrira una fuga de descriptores (*File Descriptor Leak*) y colapsará 
                // tras ejecutar varios comandos al alcanzar el limite del sistema.

                // Preparamos in_fd para la siguiente iteracion: el extremo de lectura del pipe recien creado
                // sera la entrada para el siguiente comando en el bucle.
                if (k < num_pipe_cmds - 1) {
                    in_fd = pipes[0];
                }
            }
            // SINCRONIZACION DE LA TUBERIA
            // Espera a que terminen todos los comandos en la tuberia
            //  Al usar pipes, debemos esperar a que todos los comandos terminen.
            for (int k = 0; k < num_pipe_cmds; k++) {
                int status;
                waitpid(pids[k], &status, 0);// Espera bloqueante.
                // Guardamos el estatus de salida del *ultimo* comando de la tuberia
                if (k == num_pipe_cmds - 1 && WIFEXITED(status)) {
                    last_status = WEXITSTATUS(status);
                }
            }
            // AVANCE DE INDICE: Incrementamos el contador principal saltandonos de golpe todos los comandos 
            // que acabamos de procesar y vaciar dentro del pipeline concurrente.
            i += num_pipe_cmds; 
        } else {
            // Se invoca la funcion para ejecutar un unico comando aislado en primer o segundo plano.
            // Recibe los descriptores estandar: el teclado (0) y la pantalla (1).
            // Ejecuta un comando simple
            last_status = execute_single_command(&cl->cmds[i], STDIN_FILENO, STDOUT_FILENO);
            // Obtenemos el tipo de operador logico que el parser coloco despues de este comando.
            OperatorType op = cl->ops[i];
            i++;

            // Logica de encadenamiento condicional (&& y ||).
            // Si es un AND y el anterior fallo (!= 0), o si es un OR y el anterior fue exitoso (== 0),
            // nos saltamos el comando que le sigue inmediatamente, tal como un shell real haria.
            if (op == OP_AND && last_status != 0) {
                // Salta el siguiente comando
                i++;
            } else if (op == OP_OR && last_status == 0) {
                // Salta el siguiente comando
                i++;
            }
        }
    }
}