//Estas tres líneas (#ifndef, #define y #endif al final) se conocen como "guardas de inclusión" (include guards).
// Sirven para decirle al compilador: "Si no has definido EXECUTOR_H, defínelo y lee el código. Si ya lo leíste antes, ignóralo". 
// Esto evita errores fatales de "redefinición" cuando varios archivos hacen #include "executor.h".
#ifndef EXECUTOR_H
#define EXECUTOR_H

// Incluimos parser.h porque necesitamos conocer la estructura CommandList que se usa en la función de abajo.
#include "parser.h"

// Ejecuta una linea completa de comandos ya procesados, manejando operadores logicos (;, &&, ||)
//Esta es la firma (declaración) de la función. Al estar aquí en el .h, se vuelve "pública",
// lo que permite que el main.c pueda invocarla y enviarle las estructuras que armó el parser.
void execute_command_list(CommandList *cl);

#endif // EXECUTOR_H