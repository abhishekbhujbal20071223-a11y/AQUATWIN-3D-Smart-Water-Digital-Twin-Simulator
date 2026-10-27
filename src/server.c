/**
 * ============================================================================
 * AQUATWIN: 3D Smart Water Monitoring and Control Simulator
 * Module: server.c (Embedded Winsock HTTP Server & Telemetry Dispatcher)
 * ============================================================================
 * Implements a lightweight, zero-dependency HTTP 1.1 REST and Static Asset
 * server for Windows using native Win32 sockets (ws2_32).
 * ============================================================================
 */

#define _WIN32_WINNT 0x0600
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "server.h"
#include "engine.h"

#ifdef _MSC_VER
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "winmm.lib")
#endif

static SimEngine g_engine;
static CRITICAL_SECTION g_engine_lock;
static volatile int g_server_running = 0;
static HANDLE g_sim_thread = NULL;
static char g_web_root[MAX_PATH] = "web";

/* Forward declarations */
static DWORD WINAPI simulation_worker_thread(LPVOID param);
static void handle_client_connection(SOCKET client_sock);
static void parse_and_apply_control(const char *query_or_body);

/* Helper to get content type from file extension */
static const char* get_mime_type(const char *path)
{
    const char *dot = strrchr(path, '.');
    if (!dot) return "application/octet-stream";
    if (_stricmp(dot, ".html") == 0 || _stricmp(dot, ".htm") == 0) return "text/html; charset=utf-8";
    if (_stricmp(dot, ".css") == 0) return "text/css; charset=utf-8";
    if (_stricmp(dot, ".js") == 0) return "application/javascript; charset=utf-8";
    if (_stricmp(dot, ".json") == 0) return "application/json; charset=utf-8";
    if (_stricmp(dot, ".png") == 0) return "image/png";
    if (_stricmp(dot, ".jpg") == 0 || _stricmp(dot, ".jpeg") == 0) return "image/jpeg";
    if (_stricmp(dot, ".svg") == 0) return "image/svg+xml";
    if (_stricmp(dot, ".ico") == 0) return "image/x-icon";
    return "text/plain; charset=utf-8";
}

/* Background thread for real-time continuous physics simulation with microsecond timing */
static DWORD WINAPI simulation_worker_thread(LPVOID param)
{
    (void)param;
    timeBeginPeriod(1);

    LARGE_INTEGER freq, last_tick, curr_tick;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&last_tick);

    while (g_server_running) {
        Sleep(50); /* Precise ~50ms cadence (20 Hz) with 1ms timer period */

        QueryPerformanceCounter(&curr_tick);
        double dt_sec = (double)(curr_tick.QuadPart - last_tick.QuadPart) / (double)freq.QuadPart;
        last_tick = curr_tick;

        /* Clamp dt to avoid huge jumps if system was suspended or delayed */
        if (dt_sec > 0.1) dt_sec = 0.1;
        if (dt_sec <= 0.0) dt_sec = 0.05;

        EnterCriticalSection(&g_engine_lock);
        engine_step(&g_engine, dt_sec);
        LeaveCriticalSection(&g_engine_lock);
    }

    timeEndPeriod(1);
    return 0;
}

int server_start(int port, const char *web_root)
{
    WSADATA wsa_data;
    SOCKET listen_sock = INVALID_SOCKET;
    struct sockaddr_in server_addr;

    if (web_root && strlen(web_root) > 0) {
        strncpy(g_web_root, web_root, sizeof(g_web_root) - 1);
    }

    InitializeCriticalSection(&g_engine_lock);

    /* Initialize core C simulation engine */
    EnterCriticalSection(&g_engine_lock);
    engine_init(&g_engine, NULL, NULL);
    LeaveCriticalSection(&g_engine_lock);

    /* Initialize Winsock */
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        fprintf(stderr, "[ERROR] WSAStartup failed: %d\n", WSAGetLastError());
        return -1;
    }

    listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_sock == INVALID_SOCKET) {
        fprintf(stderr, "[ERROR] Socket creation failed: %d\n", WSAGetLastError());
        WSACleanup();
        return -1;
    }

    int optval = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&optval, sizeof(optval));

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons((u_short)port);

    if (bind(listen_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        fprintf(stderr, "[ERROR] Socket bind to port %d failed: %d\n", port, WSAGetLastError());
        closesocket(listen_sock);
        WSACleanup();
        return -1;
    }

    if (listen(listen_sock, SOMAXCONN) == SOCKET_ERROR) {
        fprintf(stderr, "[ERROR] Listen failed: %d\n", WSAGetLastError());
        closesocket(listen_sock);
        WSACleanup();
        return -1;
    }

    g_server_running = 1;

    /* Start simulation physics background worker thread */
    g_sim_thread = CreateThread(NULL, 0, simulation_worker_thread, NULL, 0, NULL);
    if (!g_sim_thread) {
        fprintf(stderr, "[ERROR] Failed to spawn simulation worker thread.\n");
        closesocket(listen_sock);
        WSACleanup();
        return -1;
    }

    printf("====================================================================\n");
    printf("   AQUATWIN: 3D SMART WATER DIGITAL TWIN (C SIMULATION SERVER)      \n");
    printf("====================================================================\n");
    printf("  [STATUS] Physics Engine Active (20 Hz mass conservation loop)\n");
    printf("  [STATUS] HTTP Server listening at: http://localhost:%d/\n", port);
    printf("  [STATUS] Web root directory:       %s\n", g_web_root);
    printf("  [STATUS] Open your browser at:     http://localhost:%d\n", port);
    printf("====================================================================\n");
    printf("  Press Ctrl+C to stop the simulator server.\n\n");

    /* Main HTTP accept loop */
    while (g_server_running) {
        struct sockaddr_in client_addr;
        int client_len = sizeof(client_addr);
        SOCKET client_sock = accept(listen_sock, (struct sockaddr*)&client_addr, &client_len);

        if (client_sock == INVALID_SOCKET) {
            if (!g_server_running) break;
            continue;
        }

        handle_client_connection(client_sock);
        closesocket(client_sock);
    }

    if (listen_sock != INVALID_SOCKET) {
        closesocket(listen_sock);
    }
    WSACleanup();
    DeleteCriticalSection(&g_engine_lock);
    return 0;
}

void server_stop(void)
{
    g_server_running = 0;
    if (g_sim_thread) {
        WaitForSingleObject(g_sim_thread, 2000);
        CloseHandle(g_sim_thread);
        g_sim_thread = NULL;
    }
}

/* Parses HTTP parameters and dispatches actuator updates into C simulation */
static void parse_and_apply_control(const char *query_or_body)
{
    if (!query_or_body) return;

    EnterCriticalSection(&g_engine_lock);

    /* Control mode */
    if (strstr(query_or_body, "mode=auto") != NULL) {
        engine_set_mode(&g_engine, MODE_AUTO);
    } else if (strstr(query_or_body, "mode=manual") != NULL) {
        engine_set_mode(&g_engine, MODE_MANUAL);
    }

    /* Pump toggle */
    if (strstr(query_or_body, "pump=1") != NULL) {
        engine_set_pump(&g_engine, 1);
    } else if (strstr(query_or_body, "pump=0") != NULL) {
        engine_set_pump(&g_engine, 0);
    }

    /* Inflow valve percentage */
    const char *p_inflow = strstr(query_or_body, "inflow=");
    if (p_inflow) {
        double v = atof(p_inflow + 7);
        engine_set_inflow_valve(&g_engine, v);
    }

    /* Drain valve percentage */
    const char *p_drain = strstr(query_or_body, "drain=");
    if (p_drain) {
        double v = atof(p_drain + 6);
        engine_set_drain_valve(&g_engine, v);
    }

    /* Predefined Scenarios (0 to 5) */
    const char *p_scen = strstr(query_or_body, "scenario=");
    if (p_scen) {
        int s = atoi(p_scen + 9);
        if (s >= 0 && s <= 5) {
            engine_set_scenario(&g_engine, (ScenarioType)s);
        }
    }

    /* Pause toggle */
    const char *p_pause = strstr(query_or_body, "pause=");
    if (p_pause) {
        int pause_val = atoi(p_pause + 6);
        engine_set_paused(&g_engine, pause_val);
    }

    /* Reset simulation */
    if (strstr(query_or_body, "reset=1") != NULL) {
        engine_reset(&g_engine);
    }

    LeaveCriticalSection(&g_engine_lock);
}

/* HTTP Request Handler */
static void handle_client_connection(SOCKET client_sock)
{
    /* Optimize latency and prevent hung connections */
    int nodelay = 1;
    setsockopt(client_sock, IPPROTO_TCP, TCP_NODELAY, (const char*)&nodelay, sizeof(nodelay));
    DWORD timeout_ms = 1500;
    setsockopt(client_sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout_ms, sizeof(timeout_ms));
    setsockopt(client_sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout_ms, sizeof(timeout_ms));

    char recv_buf[8192];
    int bytes_read = recv(client_sock, recv_buf, sizeof(recv_buf) - 1, 0);
    if (bytes_read <= 0) return;

    recv_buf[bytes_read] = '\0';

    /* Parse HTTP Method and Path */
    char method[16] = {0};
    char path[256] = {0};
    sscanf(recv_buf, "%15s %255s", method, path);

    /* Handle CORS preflight */
    if (_stricmp(method, "OPTIONS") == 0) {
        const char *cors_hdr =
            "HTTP/1.1 204 No Content\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
            "Access-Control-Allow-Headers: Content-Type\r\n"
            "Connection: close\r\n\r\n";
        send(client_sock, cors_hdr, (int)strlen(cors_hdr), 0);
        return;
    }

    /* Extract query string if present */
    char *query = strchr(path, '?');
    if (query) {
        *query = '\0';
        query++;
    }

    /* ------------------------------------------------------------------------
     * Endpoint 1: GET /api/state (Authoritative C Simulation Telemetry)
     * ------------------------------------------------------------------------ */
    if (_stricmp(path, "/api/state") == 0) {
        char json_buf[4096];
        EnterCriticalSection(&g_engine_lock);
        engine_export_json(&g_engine, json_buf, sizeof(json_buf));
        LeaveCriticalSection(&g_engine_lock);

        char resp_hdr[512];
        int hdr_len = snprintf(resp_hdr, sizeof(resp_hdr),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Content-Length: %d\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Cache-Control: no-cache, no-store, must-revalidate\r\n"
            "Connection: close\r\n\r\n",
            (int)strlen(json_buf));

        send(client_sock, resp_hdr, hdr_len, 0);
        send(client_sock, json_buf, (int)strlen(json_buf), 0);
        return;
    }

    /* ------------------------------------------------------------------------
     * Endpoint 2: POST /api/control or GET /api/control (Actuator / Mode Updates)
     * ------------------------------------------------------------------------ */
    if (_stricmp(path, "/api/control") == 0) {
        if (_stricmp(method, "POST") == 0) {
            /* Check Content-Length to ensure body is fully read */
            int content_len = 0;
            const char *cl_ptr = strstr(recv_buf, "Content-Length:");
            if (!cl_ptr) cl_ptr = strstr(recv_buf, "content-length:");
            if (cl_ptr) {
                content_len = atoi(cl_ptr + 15);
            }

            char *body = strstr(recv_buf, "\r\n\r\n");
            if (body) {
                body += 4;
                int current_body_len = (int)(bytes_read - (body - recv_buf));
                /* If body is incomplete, read remaining bytes */
                while (current_body_len < content_len && bytes_read < (int)(sizeof(recv_buf) - 1)) {
                    int more = recv(client_sock, recv_buf + bytes_read,
                                    (int)(sizeof(recv_buf) - 1 - bytes_read), 0);
                    if (more <= 0) break;
                    bytes_read += more;
                    recv_buf[bytes_read] = '\0';
                    current_body_len += more;
                }
                parse_and_apply_control(body);
            }
        }
        if (query) {
            parse_and_apply_control(query);
        }

        /* Return fresh state after applying control */
        char json_buf[4096];
        EnterCriticalSection(&g_engine_lock);
        engine_export_json(&g_engine, json_buf, sizeof(json_buf));
        LeaveCriticalSection(&g_engine_lock);

        char resp_hdr[512];
        int hdr_len = snprintf(resp_hdr, sizeof(resp_hdr),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Content-Length: %d\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Connection: close\r\n\r\n",
            (int)strlen(json_buf));

        send(client_sock, resp_hdr, hdr_len, 0);
        send(client_sock, json_buf, (int)strlen(json_buf), 0);
        return;
    }

    /* ------------------------------------------------------------------------
     * Static File Serving (web/index.html, CSS, JS, etc.)
     * ------------------------------------------------------------------------ */
    char rel_path[1024];
    if (strcmp(path, "/") == 0) {
        snprintf(rel_path, sizeof(rel_path), "%s/index.html", g_web_root);
    } else {
        /* Prevent directory traversal */
        if (strstr(path, "..") != NULL) {
            const char *forbidden = "HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\n\r\n";
            send(client_sock, forbidden, (int)strlen(forbidden), 0);
            return;
        }
        snprintf(rel_path, sizeof(rel_path), "%s%s", g_web_root, path);
    }

    FILE *f = fopen(rel_path, "rb");
    if (!f) {
        const char *not_found =
            "HTTP/1.1 404 Not Found\r\n"
            "Content-Type: text/plain\r\n"
            "Content-Length: 13\r\n\r\n"
            "404 Not Found";
        send(client_sock, not_found, (int)strlen(not_found), 0);
        return;
    }

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    const char *mime = get_mime_type(rel_path);

    char resp_hdr[512];
    int hdr_len = snprintf(resp_hdr, sizeof(resp_hdr),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %ld\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Connection: close\r\n\r\n",
        mime, file_size);

    send(client_sock, resp_hdr, hdr_len, 0);

    /* Stream file contents */
    char file_chunk[4096];
    size_t bytes_to_read = 0;
    while ((bytes_to_read = fread(file_chunk, 1, sizeof(file_chunk), f)) > 0) {
        send(client_sock, file_chunk, (int)bytes_to_read, 0);
    }
    fclose(f);
}
