#include "history.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>

// history es un arreglo bidimensional que actua como nuestra memoria RAM para los comandos.
static char history[MAX_HISTORY][MAX_LINE];
static int history_count = 0;
static char history_file_path[1024];

void init_history() {
    char *home = getenv("HOME");
    if (!home) return;
    snprintf(history_file_path, sizeof(history_file_path), "%s/.ucvsh_history", home);
    
    FILE *f = fopen(history_file_path, "r");
    if (!f) return;
    
    char line[MAX_LINE];
    // Leemos linea por linea del archivo hasta llenar nuestro arreglo o llegar a MAX_HISTORY
    while (fgets(line, sizeof(line), f) && history_count < MAX_HISTORY) {
        line[strcspn(line, "\n")] = 0; // Eliminamos el salto de linea al final
        strncpy(history[history_count++], line, MAX_LINE);
    }
    fclose(f);
}

void add_to_history(const char *line) {
    if (strlen(line) == 0) return; // No guarda lineas vacias
    
    // No agrega un duplicado si es exactamente igual al ultimo comando
    if (history_count > 0 && strcmp(history[history_count - 1], line) == 0) return;
    
    if (history_count < MAX_HISTORY) {
        strncpy(history[history_count++], line, MAX_LINE);
    } else {
        // perdiendo el mas viejo (el indice 0) para hacerle espacio al nuevo al final del arreglo.
        for (int i = 1; i < MAX_HISTORY; i++) {
            strncpy(history[i - 1], history[i], MAX_LINE);
        }
        strncpy(history[MAX_HISTORY - 1], line, MAX_LINE);
    }
    
    // Anexamos (modo "a" - append) el comando al archivo fisico en disco
    FILE *f = fopen(history_file_path, "a");
    if (f) {
        fprintf(f, "%s\n", line);
        fclose(f);
    }
}

// Implementacion muy basica de lectura de entrada con flechas del teclado
char *read_input_with_history() {
    static char buf[MAX_LINE];
    memset(buf, 0, MAX_LINE); // Limpiamos el buffer
    int pos = 0;
    int history_pos = history_count;
    
    struct termios oldt, newt;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    
    // ICANON hace que la terminal espere un 'Enter' para enviar datos. Al negarlo (~), leemos byte a byte.
    // ECHO hace que la terminal imprima automaticamente lo que tecleas. Lo apagamos para nosotros dibujarlo manualmente.
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt); // Aplicamos la nueva configuracion al instante (TCSANOW)

    while (1) {
        char c;
        if (read(STDIN_FILENO, &c, 1) <= 0) break;
        
        if (c == '\n') {
            write(STDOUT_FILENO, "\n", 1); // Dibujamos el salto de linea nosotros mismos
            break;
        } else if (c == 127 || c == '\b') { // (Borrar)
            if (pos > 0) {
                pos--;
                buf[pos] = '\0';
                // para borrar el caracter en pantalla, y otro retroceso para dejar el cursor apuntando bien.
                write(STDOUT_FILENO, "\b \b", 3);
            }
        } else if (c == '\033') { // Secuencia de escape (teclas especiales)
            // Ej: Flecha Arriba envia '\033' (Escape), luego '[', y luego 'A'.
            char seq[3];
            if (read(STDIN_FILENO, &seq[0], 1) == 0) break;
            if (read(STDIN_FILENO, &seq[1], 1) == 0) break;
            
            if (seq[0] == '[') {
                if (seq[1] == 'A') { // Flecha arriba
                    if (history_pos > 0) {
                        history_pos--;
                        // Limpia la linea actual
                        for (int i = 0; i < pos; i++) write(STDOUT_FILENO, "\b \b", 3);
                        strncpy(buf, history[history_pos], MAX_LINE);
                        pos = strlen(buf);
                        write(STDOUT_FILENO, buf, pos); // Dibuja el comando viejo rescatado del historial
                    }
                } else if (seq[1] == 'B') { // Flecha abajo
                    if (history_pos < history_count - 1) {
                        history_pos++;
                        // Limpia la linea actual
                        for (int i = 0; i < pos; i++) write(STDOUT_FILENO, "\b \b", 3);
                        strncpy(buf, history[history_pos], MAX_LINE);
                        pos = strlen(buf);
                        write(STDOUT_FILENO, buf, pos);
                    } else if (history_pos == history_count - 1) { // Llegamos al presente
                        history_pos++;
                        for (int i = 0; i < pos; i++) write(STDOUT_FILENO, "\b \b", 3);
                        buf[0] = '\0'; // Dejamos el string en blanco
                        pos = 0;
                    }
                }
            }
        } else if (c >= 32 && c <= 126) {
            if (pos < MAX_LINE - 1) {
                buf[pos++] = c;
                write(STDOUT_FILENO, &c, 1); // Lo dibujamos en pantalla porque desactivamos el ECHO
            }
        }
    }

    // Restauramos la configuracion original de la terminal antes de regresar. Si el proceso muriera sin hacer esto, la terminal de Linux quedaria "rota" y no mostraria lo que el usuario escribe.
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    return strdup(buf);
}