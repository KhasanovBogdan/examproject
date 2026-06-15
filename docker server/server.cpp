/*
 * server.cpp - TCP Chat Server (Linux POSIX only)
 * Project: TCP Server v1.0
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORT        12345
#define MAX_CLIENTS 5
#define BUF_SIZE    1024
#define LOG_FILE    "chat.log"

typedef struct {
    int  fd;
    int  id;
    char nick[64];
    int  active;
} Client;

static Client          g_clients[MAX_CLIENTS];
static pthread_mutex_t g_mutex   = PTHREAD_MUTEX_INITIALIZER;
static int             g_next_id = 1;
static volatile int    g_running = 1;
static int             g_server_fd = -1;
static FILE*           g_log = NULL;

static void log_write(const char* msg) {
    time_t t = time(NULL);
    struct tm tm_buf;
    localtime_r(&t, &tm_buf);
    char line[BUF_SIZE + 64];
    snprintf(line, sizeof(line),
        "[%04d-%02d-%02d %02d:%02d:%02d] %s\n",
        tm_buf.tm_year+1900, tm_buf.tm_mon+1, tm_buf.tm_mday,
        tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec, msg);
    printf("%s", line);
    fflush(stdout);
    if (g_log) { fprintf(g_log, "%s", line); fflush(g_log); }
}

static void send_msg(int fd, const char* msg) {
    char buf[BUF_SIZE];
    int len = snprintf(buf, sizeof(buf), "%s\n", msg);
    send(fd, buf, len, 0);
}

static void broadcast_count(void) {
    pthread_mutex_lock(&g_mutex);
    int cnt = 0;
    for (int i = 0; i < MAX_CLIENTS; i++)
        if (g_clients[i].active) cnt++;
    char m[128];
    snprintf(m, sizeof(m), ">>> Seychas v chate: %d klient(ov).", cnt);
    for (int i = 0; i < MAX_CLIENTS; i++)
        if (g_clients[i].active) send_msg(g_clients[i].fd, m);
    pthread_mutex_unlock(&g_mutex);
}

static int find_slot(int id) {
    for (int i = 0; i < MAX_CLIENTS; i++)
        if (g_clients[i].active && g_clients[i].id == id) return i;
    return -1;
}

typedef struct { int fd; int id; } ClientArg;

static void* client_thread(void* param) {
    ClientArg* arg = (ClientArg*)param;
    int fd = arg->fd;
    int id = arg->id;
    free(arg);

    char buf[BUF_SIZE];
    char nick[64] = "Anon";
    char tmp[BUF_SIZE];
    int  r, n;

    send_msg(fd, "Vvedite vash nik: ");
    r = recv(fd, buf, BUF_SIZE - 1, 0);
    if (r > 0) {
        buf[r] = '\0';
        n = (int)strlen(buf);
        while (n > 0 && (buf[n-1]=='\r'||buf[n-1]=='\n')) buf[--n]='\0';
        if (n > 0) { strncpy(nick, buf, 63); nick[63]='\0'; }
    } else { close(fd); return NULL; }

    pthread_mutex_lock(&g_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!g_clients[i].active) {
            g_clients[i].fd = fd;
            g_clients[i].id = id;
            g_clients[i].active = 1;
            strncpy(g_clients[i].nick, nick, 63);
            break;
        }
    }
    pthread_mutex_unlock(&g_mutex);

    snprintf(tmp, sizeof(tmp), "K chatu prisoedinilsya '%s' (ID: %d).", nick, id);
    log_write(tmp);
    pthread_mutex_lock(&g_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++)
        if (g_clients[i].active) send_msg(g_clients[i].fd, tmp);
    pthread_mutex_unlock(&g_mutex);
    broadcast_count();

    while (g_running) {
        r = recv(fd, buf, BUF_SIZE - 1, 0);
        if (r <= 0) break;
        buf[r] = '\0';
        n = (int)strlen(buf);
        while (n > 0 && (buf[n-1]=='\r'||buf[n-1]=='\n')) buf[--n]='\0';
        if (n == 0) continue;

        if (strncmp(buf, "/nick ", 6) == 0) {
            char* nn = buf + 6;
            int nl = (int)strlen(nn);
            while (nl > 0 && (nn[nl-1]=='\r'||nn[nl-1]=='\n')) nn[--nl]='\0';
            if (nl > 0) {
                snprintf(tmp, sizeof(tmp), "Klient %d smenil nik na '%s'.", id, nn);
                pthread_mutex_lock(&g_mutex);
                int sl = find_slot(id);
                if (sl >= 0) strncpy(g_clients[sl].nick, nn, 63);
                pthread_mutex_unlock(&g_mutex);
                strncpy(nick, nn, 63);
                log_write(tmp);
                pthread_mutex_lock(&g_mutex);
                for (int i = 0; i < MAX_CLIENTS; i++)
                    if (g_clients[i].active) send_msg(g_clients[i].fd, tmp);
                pthread_mutex_unlock(&g_mutex);
            }
            continue;
        }

        snprintf(tmp, sizeof(tmp), "[%s (ID:%d)]: %s", nick, id, buf);
        log_write(tmp);
        pthread_mutex_lock(&g_mutex);
        for (int i = 0; i < MAX_CLIENTS; i++)
            if (g_clients[i].active) send_msg(g_clients[i].fd, tmp);
        pthread_mutex_unlock(&g_mutex);
    }

    char leave_nick[64] = "";
    pthread_mutex_lock(&g_mutex);
    int sl = find_slot(id);
    if (sl >= 0) {
        strncpy(leave_nick, g_clients[sl].nick, 63);
        g_clients[sl].active = 0;
        g_clients[sl].fd = -1;
    }
    pthread_mutex_unlock(&g_mutex);
    close(fd);

    if (strlen(leave_nick) > 0) {
        snprintf(tmp, sizeof(tmp), "Otklyuchilsya '%s' (ID: %d).", leave_nick, id);
        log_write(tmp);
        pthread_mutex_lock(&g_mutex);
        for (int i = 0; i < MAX_CLIENTS; i++)
            if (g_clients[i].active) send_msg(g_clients[i].fd, tmp);
        pthread_mutex_unlock(&g_mutex);
        broadcast_count();
    }
    return NULL;
}

static void on_signal(int s) {
    (void)s;
    g_running = 0;
    if (g_server_fd >= 0) close(g_server_fd);
}

int main(void) {
    signal(SIGINT,  on_signal);
    signal(SIGTERM, on_signal);

    g_log = fopen(LOG_FILE, "w");
    memset(g_clients, 0, sizeof(g_clients));

    char tmp[256];
    snprintf(tmp, sizeof(tmp),
        "=== Server zapushhen na portu %d (maks. klientov: %d) ===",
        PORT, MAX_CLIENTS);
    log_write(tmp);

    g_server_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(g_server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(PORT);
    bind(g_server_fd, (struct sockaddr*)&addr, sizeof(addr));
    listen(g_server_fd, 16);
    log_write("Ozhidayu podklyucheniy...");

    while (g_running) {
        struct sockaddr_in ca;
        socklen_t cl = sizeof(ca);
        int cfd = accept(g_server_fd, (struct sockaddr*)&ca, &cl);
        if (cfd < 0) break;

        pthread_mutex_lock(&g_mutex);
        int cnt = 0;
        for (int i = 0; i < MAX_CLIENTS; i++)
            if (g_clients[i].active) cnt++;
        pthread_mutex_unlock(&g_mutex);

        if (cnt >= MAX_CLIENTS) {
            snprintf(tmp, sizeof(tmp),
                "Server zanyat. Vse %d mest zanyaty. Poprobuy pozzhe.", MAX_CLIENTS);
            send_msg(cfd, tmp);
            close(cfd);
            log_write("Otkloyon lishniy klient (limit dostignut).");
            continue;
        }

        pthread_mutex_lock(&g_mutex);
        int id = g_next_id++;
        pthread_mutex_unlock(&g_mutex);

        snprintf(tmp, sizeof(tmp), "Novoe podklyuchenie -> ID: %d", id);
        log_write(tmp);

        ClientArg* arg = (ClientArg*)malloc(sizeof(ClientArg));
        arg->fd = cfd;
        arg->id = id;
        pthread_t tid;
        pthread_create(&tid, NULL, client_thread, arg);
        pthread_detach(tid);
    }

    log_write("=== Server ostanovlen ===");
    if (g_log) fclose(g_log);
    return 0;
}
