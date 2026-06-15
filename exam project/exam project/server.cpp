/*
 * server.cpp - Mnogo pol'zovatel'skiy chat-server
 * Tol'ko WinAPI + Winsock2, bez STL
 * Kompilyaciya: cl server.cpp /Fe:server.exe ws2_32.lib /nologo
 */

#pragma comment(lib, "ws2_32.lib")
#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <string.h>

#define PORT         12345
#define MAX_CLIENTS  5
#define BUF_SIZE     1024
#define LOG_FILE     "chat.log"

typedef struct {
    SOCKET  fd;
    int     id;
    char    nick[64];
    int     active;
} Client;

static Client           g_clients[MAX_CLIENTS];
static CRITICAL_SECTION g_cs;
static volatile LONG    g_next_id = 1;
static volatile LONG    g_running = 1;
static SOCKET           g_server_fd = INVALID_SOCKET;
static HANDLE           g_log_file = INVALID_HANDLE_VALUE;

/* ==================== LOG ==================== */
static void log_write(const char* msg) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    char line[BUF_SIZE + 64];
    int len = wsprintfA(line,
        "[%04d-%02d-%02d %02d:%02d:%02d] %s\r\n",
        st.wYear, st.wMonth, st.wDay,
        st.wHour, st.wMinute, st.wSecond, msg);
    DWORD wr;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), line, (DWORD)len, &wr, NULL);
    if (g_log_file != INVALID_HANDLE_VALUE)
        WriteFile(g_log_file, line, (DWORD)len, &wr, NULL);
}

/* ==================== SEND ==================== */
static void send_msg(SOCKET fd, const char* msg) {
    char buf[BUF_SIZE];
    int len = wsprintfA(buf, "%s\n", msg);
    send(fd, buf, len, 0);
}

/* ==================== BROADCAST COUNT ==================== */
static void broadcast_count(void) {
    EnterCriticalSection(&g_cs);
    int cnt = 0;
    for (int i = 0; i < MAX_CLIENTS; i++)
        if (g_clients[i].active) cnt++;
    char m[128];
    wsprintfA(m, ">>> Seychas v chate: %d klient(ov).", cnt);
    for (int i = 0; i < MAX_CLIENTS; i++)
        if (g_clients[i].active) send_msg(g_clients[i].fd, m);
    LeaveCriticalSection(&g_cs);
}

/* ==================== FIND SLOT ==================== */
static int find_slot(int id) {
    for (int i = 0; i < MAX_CLIENTS; i++)
        if (g_clients[i].active && g_clients[i].id == id) return i;
    return -1;
}

/* ==================== CLIENT THREAD ==================== */
typedef struct { SOCKET fd; int id; } ClientArg;

static DWORD WINAPI client_thread(LPVOID param) {
    ClientArg* arg = (ClientArg*)param;
    SOCKET fd = arg->fd;
    int    id = arg->id;
    HeapFree(GetProcessHeap(), 0, arg);

    char buf[BUF_SIZE];
    char nick[64];
    char tmp[BUF_SIZE];
    int  r, n;

    /* Zapros nika */
    send_msg(fd, "Vvedite vash nik: ");
    r = recv(fd, buf, BUF_SIZE - 1, 0);
    if (r <= 0) { closesocket(fd); return 0; }
    buf[r] = '\0';
    n = (int)strlen(buf);
    while (n > 0 && (buf[n - 1] == '\r' || buf[n - 1] == '\n')) buf[--n] = '\0';
    if (n == 0) lstrcpyA(buf, "Anon");
    lstrcpynA(nick, buf, 63);

    /* Registraciya */
    EnterCriticalSection(&g_cs);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!g_clients[i].active) {
            g_clients[i].fd = fd; g_clients[i].id = id;
            g_clients[i].active = 1;
            lstrcpynA(g_clients[i].nick, nick, 63);
            break;
        }
    }
    LeaveCriticalSection(&g_cs);

    wsprintfA(tmp, "K chatu prisoedinilsya '%s' (ID: %d).", nick, id);
    log_write(tmp);
    EnterCriticalSection(&g_cs);
    for (int i = 0; i < MAX_CLIENTS; i++)
        if (g_clients[i].active) send_msg(g_clients[i].fd, tmp);
    LeaveCriticalSection(&g_cs);
    broadcast_count();

    /* Osnovnoy tsikl */
    while (g_running) {
        r = recv(fd, buf, BUF_SIZE - 1, 0);
        if (r <= 0) break;
        buf[r] = '\0';
        n = (int)strlen(buf);
        while (n > 0 && (buf[n - 1] == '\r' || buf[n - 1] == '\n')) buf[--n] = '\0';
        if (n == 0) continue;

        /* Smena nika: /nick NovoeImya */
        if (strncmp(buf, "/nick ", 6) == 0) {
            char* nn = buf + 6;
            int nl = (int)strlen(nn);
            while (nl > 0 && (nn[nl - 1] == '\r' || nn[nl - 1] == '\n')) nn[--nl] = '\0';
            if (nl > 0) {
                wsprintfA(tmp, "Klient %d smenil nik na '%s'.", id, nn);
                EnterCriticalSection(&g_cs);
                int sl = find_slot(id);
                if (sl >= 0) lstrcpynA(g_clients[sl].nick, nn, 63);
                LeaveCriticalSection(&g_cs);
                lstrcpynA(nick, nn, 63);
                log_write(tmp);
                EnterCriticalSection(&g_cs);
                for (int i = 0; i < MAX_CLIENTS; i++)
                    if (g_clients[i].active) send_msg(g_clients[i].fd, tmp);
                LeaveCriticalSection(&g_cs);
            }
            continue;
        }

        /* Obychnoe soobshenie */
        wsprintfA(tmp, "[%s (ID:%d)]: %s", nick, id, buf);
        log_write(tmp);
        EnterCriticalSection(&g_cs);
        for (int i = 0; i < MAX_CLIENTS; i++)
            if (g_clients[i].active) send_msg(g_clients[i].fd, tmp);
        LeaveCriticalSection(&g_cs);
    }

    /* Otklyuchenie */
    char leave_nick[64] = "";
    EnterCriticalSection(&g_cs);
    int sl = find_slot(id);
    if (sl >= 0) {
        lstrcpynA(leave_nick, g_clients[sl].nick, 63);
        g_clients[sl].active = 0;
        g_clients[sl].fd = INVALID_SOCKET;
    }
    LeaveCriticalSection(&g_cs);
    closesocket(fd);

    if (lstrlenA(leave_nick) > 0) {
        wsprintfA(tmp, "Otklyuchilsya '%s' (ID: %d).", leave_nick, id);
        log_write(tmp);
        EnterCriticalSection(&g_cs);
        for (int i = 0; i < MAX_CLIENTS; i++)
            if (g_clients[i].active) send_msg(g_clients[i].fd, tmp);
        LeaveCriticalSection(&g_cs);
        broadcast_count();
    }
    return 0;
}

/* ==================== CTRL+C ==================== */
static BOOL WINAPI ctrl_handler(DWORD t) {
    (void)t;
    InterlockedExchange(&g_running, 0);
    if (g_server_fd != INVALID_SOCKET) closesocket(g_server_fd);
    return TRUE;
}

/* ==================== MAIN ==================== */
int main(void) {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    SetConsoleCtrlHandler(ctrl_handler, TRUE);
    InitializeCriticalSection(&g_cs);
    memset(g_clients, 0, sizeof(g_clients));

    g_log_file = CreateFileA(LOG_FILE, GENERIC_WRITE, FILE_SHARE_READ,
        NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

    char tmp[256];
    wsprintfA(tmp, "=== Server zapushhen na portu %d (maks. klientov: %d) ===",
        PORT, MAX_CLIENTS);
    log_write(tmp);

    g_server_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(g_server_fd, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(PORT);
    bind(g_server_fd, (struct sockaddr*)&addr, sizeof(addr));
    listen(g_server_fd, 16);
    log_write("Ozhidayu podklyucheniy...");

    while (g_running) {
        struct sockaddr_in ca; int cl = sizeof(ca);
        SOCKET cfd = accept(g_server_fd, (struct sockaddr*)&ca, &cl);
        if (cfd == INVALID_SOCKET) break;

        EnterCriticalSection(&g_cs);
        int cnt = 0;
        for (int i = 0; i < MAX_CLIENTS; i++)
            if (g_clients[i].active) cnt++;
        LeaveCriticalSection(&g_cs);

        if (cnt >= MAX_CLIENTS) {
            wsprintfA(tmp,
                "Server zanyat. Vse %d mest zanyaty. Poprobuy podklyuchit'sya pozzhe.",
                MAX_CLIENTS);
            send_msg(cfd, tmp);
            closesocket(cfd);
            log_write("Otkloyon lishniy klient (limit dostignut).");
            continue;
        }

        int id = (int)InterlockedIncrement(&g_next_id);
        wsprintfA(tmp, "Novoe podklyuchenie -> ID: %d", id);
        log_write(tmp);

        ClientArg* arg = (ClientArg*)HeapAlloc(GetProcessHeap(), 0, sizeof(ClientArg));
        arg->fd = cfd; arg->id = id;
        CreateThread(NULL, 0, client_thread, arg, 0, NULL);
    }

    log_write("=== Server ostanovlen ===");
    if (g_log_file != INVALID_HANDLE_VALUE) CloseHandle(g_log_file);
    DeleteCriticalSection(&g_cs);
    WSACleanup();
    return 0;
}
