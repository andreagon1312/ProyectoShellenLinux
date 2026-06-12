#include "parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// Funcion auxiliar para limpiar los espacios en blanco
// Aritmetica de punteros pura. Avanza el puntero inicial para saltar
// los espacios al principio, y retrocede un puntero al final para cortar los espacios extra.
static char *trim_whitespace(char *str) {
    char *end;
    while(isspace((unsigned char)*str)) str++;
    if(*str == 0) return str;
    end = str + strlen(str) - 1;
    while(end > str && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    return str;
}

CommandList parse_line(char *line) {
    CommandList cl;
    cl.num_cmds = 0;
    
    char *p = line;
    char *cmd_start = p;
    
    // Haremos una division basica por operadores: ;, &&, ||, |
    // Por simplicidad en este proyecto, asumimos un analisis simple de izquierda a derecha.
    // En una shell real, || y && tienen precedencia, pero los procesaremos linealmente.

    while (*p) {
        OperatorType op = OP_NONE;
        char *op_ptr = NULL;
        int op_len = 0;

        // Buscamos operadores clave. strncmp compara los primeros 'n' caracteres.
        if (strncmp(p, "&&", 2) == 0) { op = OP_AND; op_ptr = p; op_len = 2; }
        else if (strncmp(p, "||", 2) == 0) { op = OP_OR; op_ptr = p; op_len = 2; }
        else if (*p == ';') { op = OP_SEQ; op_ptr = p; op_len = 1; }
        else if (*p == '|') { op = OP_PIPE; op_ptr = p; op_len = 1; }
        
        // Si encontramos un operador, o si llegamos al final del string de texto ('\0')
        if (op != OP_NONE || *(p+1) == '\0') {
            char *cmd_str;
            if (op != OP_NONE) {
                *op_ptr = '\0'; // Dividimos el string insertando un caracter nulo justo donde inicia el operador
                cmd_str = strdup(cmd_start); // Copiamos el pedazo de comando aislado (ej: "ls -la ")
                p = op_ptr + op_len; // Movemos el puntero general saltando el operador
                cmd_start = p; // Marcamos el inicio del proximo comando
            } else {
                cmd_str = strdup(cmd_start); // Es el ultimo comando de la linea
                p++;
            }
            
            char *trimmed_cmd = trim_whitespace(cmd_str);
            if (strlen(trimmed_cmd) > 0) {
                Command *cmd = &cl.cmds[cl.num_cmds];
                cmd->redirect_out = NULL;
                cmd->background = 0;
                
                // Verifica procesos en segundo plano (background)
                //  Revisa si el ultimo caracter del comando aislado es un(&).
                int len = strlen(trimmed_cmd);
                if (trimmed_cmd[len-1] == '&') {
                    cmd->background = 1;
                    trimmed_cmd[len-1] = '\0'; // Borramos el '&' para que no ensucie el comando a ejecutar
                    trimmed_cmd = trim_whitespace(trimmed_cmd); // Limpiamos por si habia un espacio antes del '&'
                }
                
                // Analiza argumentos y redirecciones
                int arg_idx = 0;
                char *saveptr;
                
                
                // strtok_r es la version "reentrante" (thread-safe). Guarda su estado interno en 'saveptr'.
                
                char *token = strtok_r(trimmed_cmd, " \t", &saveptr);
                while (token != NULL) {
                    if (strcmp(token, ">") == 0) {
                        // Si encontramos un '>', el PROXIMO token es obligatoriamente el archivo.
                        token = strtok_r(NULL, " \t", &saveptr);
                        if (token) {
                            cmd->redirect_out = strdup(token);
                        }
                    } else {
                        if (arg_idx < MAX_ARGS - 1) {
                            int tlen = strlen(token);
                            //  Limpieza de comillas. Si un argumento vino entre comillas dobles,
                            // las removemos para que execv reciba el string limpio (ej: "hola" -> hola).
                            if (tlen >= 2 && token[0] == '"' && token[tlen-1] == '"') {
                                token[tlen-1] = '\0';
                                token++;
                            }
                            cmd->args[arg_idx++] = strdup(token);
                        }
                    }
                    token = strtok_r(NULL, " \t", &saveptr);
                }
                cmd->args[arg_idx] = NULL; // Obligatorio: execv SIEMPRE necesita que el arreglo termine en NULL.
                
                if (arg_idx > 0) {
                    cl.ops[cl.num_cmds] = op;
                    cl.num_cmds++;
                }
            }
            free(cmd_str);
        } else {
            p++; // Avanza al siguiente caracter si no es un operador
        }
    }
    
    return cl;
}

void free_command_list(CommandList *cl) {
    //  Limpieza de memoria.
    // Como usamos strdup() multiples veces arriba (el cual hace malloc internamente), 
    // estamos obligados a liberar (free) cada argumento y cada string de redireccion 
    // para no ahogar la RAM.
    for (int i = 0; i < cl->num_cmds; i++) {
        for (int j = 0; cl->cmds[i].args[j] != NULL; j++) {
            free(cl->cmds[i].args[j]);
        }
        if (cl->cmds[i].redirect_out) {
            free(cl->cmds[i].redirect_out);
        }
    }
    cl->num_cmds = 0;
}