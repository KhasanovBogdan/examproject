/*
 * client.cpp - Konsol'nyy chat-klient
 * Tol'ko WinAPI + Winsock2, bez STL
 * Kompilyaciya: cl client.cpp /Fe:client.exe ws2_32.lib /nologo
 */

#define _WINSOCK_DEPRECATED_NO_WARNINGS
#pragma comment(lib, "ws2_32.lib")
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>
#include <string.h>

#define PORT     12345
#define HOST     "127.0.0.1"
#define BUF_SIZE 1024

static volatile LONG g_running = 1;
static SOCKET        g_fd = INVALID_SOCKET;

/* ==================== POTOK CHTENIYA ==================== */
static DWORD WINAPI recv_thread(LPVOID param) {
    SOCKET fd = (SOCKET)param;
    char   buf[BUF_SIZE];
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD  wr;

    while (g_running) {
        int r = recv(fd, buf, BUF_SIZE - 1, 0);
        if (r <= 0) {
            const char* m = "\n[Soedinenie s serverom razorvan]\n";
            WriteFile(hOut, m, (DWORD)lstrlenA(m), &wr, NULL);
            InterlockedExchange(&g_running, 0);
            break;
        }
        buf[r] = '\0';
        WriteFile(hOut, buf, (DWORD)r, &wr, NULL);
    }
    return 0;
}

/* ==================== CTRL+C ==================== */
static BOOL WINAPI ctrl_handler(DWORD t) {
    (void)t;
    InterlockedExchange(&g_running, 0);
    if (g_fd != INVALID_SOCKET) closesocket(g_fd);
    return TRUE;
}

/* ==================== MAIN ==================== */
int main(void) {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    SetConsoleCtrlHandler(ctrl_handler, TRUE);

    g_fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in srv;
    memset(&srv, 0, sizeof(srv));
    srv.sin_family = AF_INET;
    srv.sin_port = htons(PORT);
    inet_pton(AF_INET, HOST, &srv.sin_addr);

    if (connect(g_fd, (struct sockaddr*)&srv, sizeof(srv)) == SOCKET_ERROR) {
        printf("Ne udalos' podklyuchit'sya k %s:%d\n", HOST, PORT);
        return 1;
    }

    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD  wr;
    const char* hello =
        "[Podklyucheno k serveru]\n"
        "Komandy: /nick NovoeImya - smenit' nik | Ctrl+C - vyhod\n\n";
    WriteFile(hOut, hello, (DWORD)lstrlenA(hello), &wr, NULL);

    CreateThread(NULL, 0, recv_thread, (LPVOID)g_fd, 0, NULL);

    char line[BUF_SIZE];
    while (g_running) {
        if (!fgets(line, BUF_SIZE, stdin)) break;
        int len = (int)strlen(line);
        if (len == 0) continue;
        if (line[len - 1] != '\n') { line[len] = '\n'; len++; }
        if (send(g_fd, line, len, 0) == SOCKET_ERROR) break;
    }

    InterlockedExchange(&g_running, 0);
    closesocket(g_fd);
    WSACleanup();
    return 0;
}
