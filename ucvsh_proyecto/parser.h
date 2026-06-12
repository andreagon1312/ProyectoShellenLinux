#ifndef PARSER_H
#define PARSER_H

// Limitamos el tamaño de los arreglos estáticos para evitar desbordamientos
// y hacer más sencillo el manejo de memoria en C.
#define MAX_ARGS 128
#define MAX_COMMANDS 32

//  Una enumeración para identificar qué operador une a dos comandos.
// Esto facilita usar comparaciones numéricas (ej: op == OP_AND) en lugar de estar
// comparando strings ("&&") en el executor.c.
typedef enum {
    OP_NONE,
    OP_SEQ,    // ;
    OP_AND,    // &&
    OP_OR,     // ||
    OP_PIPE    // |
} OperatorType;

//  Estructura que abstrae un comando individual.
typedef struct {
    char *args[MAX_ARGS]; // El arreglo que se le pasará a execv() (ej: ["ls", "-la", NULL])
    char *redirect_out;   // Archivo destino si se usó '>'
    int background;       // Bandera (flag) que vale 1 si el comando termina en '&'
} Command;

//  Estructura que agrupa toda la línea escrita por el usuario.
typedef struct {
    Command cmds[MAX_COMMANDS];     // Arreglo de los comandos aislados
    OperatorType ops[MAX_COMMANDS]; // ops[i] es el operador que va DESPUES de cmds[i]
    int num_cmds;                   // Contador de cuántos comandos hay en la línea
} CommandList;

// Funcion para analizar una linea de entrada en una lista de comandos
CommandList parse_line(char *line);

// Libera cualquier memoria asignada dinamicamente en CommandList
void free_command_list(CommandList *cl);

#endif // PARSER_H