//Guardas de inclusión (include guards) para evitar errores de compilación.
// Si varios archivos hacen #include "history.h", esto garantiza que el código de aquí adentro
// solo sea leído e integrado una única vez por el compilador (gcc).
#ifndef HISTORY_H
#define HISTORY_H

//Constantes globales para controlar estrictamente el uso de memoria RAM.
// MAX_HISTORY: Límite de comandos guardados. Si llegamos a 1000, empezamos a sobreescribir el más viejo.
// MAX_LINE: Para evitar desbordamientos de búfer (buffer overflow), limitamos cada comando a 1024 caracteres.
#define MAX_HISTORY 1000
#define MAX_LINE 1024

// Inicializa el historial (lo carga desde el archivo)
// Esta función lee el archivo oculto en el disco duro y lo transfiere 
// al arreglo en la memoria RAM al momento de arrancar la shell ucvsh.
void init_history();

// Añade una línea al historial (en memoria y en el archivo)
//Recibe la cadena del comando recién ejecutado, la inserta en el arreglo 
// en memoria para su acceso rápido (flechas), y le hace "append" al archivo físico para no perderla si cerramos la shell.
void add_to_history(const char *line);

// Lee la entrada de texto soportando el historial (flechas direccionales)
// Esta es la firma de la función que reemplaza herramientas básicas como scanf() o fgets(). 
// Es pública para que el main.c pueda llamarla en su ciclo infinito.
char *read_input_with_history();

#endif // HISTORY_H