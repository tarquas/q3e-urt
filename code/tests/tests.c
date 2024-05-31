#include "tests.h"
#include "../qcommon/q_platform.h"

int tests_run = 0;

void QDECL Com_DPrintf(const char *fmt, ...) {
    char buffer[8192];

    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char time_buffer[32];
    strftime(time_buffer, sizeof(time_buffer), "%Y-%m-%dT%H:%M:%S%z", tm_info);

    printf("[%s] ", time_buffer);

    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    printf("%s", buffer);
}

void QDECL Com_Printf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    Com_DPrintf(fmt, args);
    va_end(args);
}

void QDECL Sys_SetStatus(const char *format, ...) {
    (void) format;
}

void Sys_Sleep(int msec) {
    (void) msec;
}

void Sys_Print(const char *msg) {
    Com_DPrintf("%s", msg);
}

void Sys_Error(const char *format, ...) {
    va_list args;
    va_start(args, format);
    Com_DPrintf(format, args);
    va_end(args);

    exit(EXIT_FAILURE);
}

void Sys_Quit(void) {
    exit(EXIT_SUCCESS);
}

void Sys_SendKeyEvents(void) {
}

void Sys_Init(void) {
}

char *Sys_ConsoleInput(void) {
    return NULL;
}

int main(int argc, char **argv) {
    (void) argc;
    (void) argv;

    testPrint();

    Com_DPrintf("Tests run: %d\n", tests_run);
    return 0;
}
