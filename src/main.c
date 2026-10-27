/**
 * ============================================================================
 * AQUATWIN: 3D Smart Water Monitoring and Control Simulator
 * Module: main.c (Application Entry Point)
 * ============================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include "server.h"

static void handle_sigint(int sig)
{
    (void)sig;
    printf("\n[SHUTDOWN] Stopping AquaTwin simulation server...\n");
    server_stop();
    exit(0);
}

int main(int argc, char *argv[])
{
    int port = 8080;
    const char *web_dir = "web";

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--web") == 0 && i + 1 < argc) {
            web_dir = argv[++i];
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("AquaTwin 3D Smart Water Digital Twin Server\n");
            printf("Usage: aquatwin.exe [--port <port>] [--web <web_dir>]\n");
            printf("Default: port 8080, web directory 'web'\n");
            return 0;
        }
    }

    signal(SIGINT, handle_sigint);

    int res = server_start(port, web_dir);
    return res;
}
