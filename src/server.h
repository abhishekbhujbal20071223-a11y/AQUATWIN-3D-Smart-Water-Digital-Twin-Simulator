/**
 * ============================================================================
 * AQUATWIN: 3D Smart Water Monitoring and Control Simulator
 * Module: server.h (Embedded Winsock HTTP Server Header)
 * ============================================================================
 */

#ifndef AQUATWIN_SERVER_H
#define AQUATWIN_SERVER_H

#include "engine.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int port;
    const char *web_root;
    int is_running;
} ServerConfig;

/* Starts the embedded HTTP server and simulation engine loop */
int server_start(int port, const char *web_root);

/* Graceful stop */
void server_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* AQUATWIN_SERVER_H */
