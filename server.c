#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include <string.h>
#include <signal.h>
#include <sys/wait.h>
#include <time.h>
#include <netinet/tcp.h>
#include <stdarg.h>
//#include "bank_lib.h"
#include "models.h"
#include "protocol.h"
#include "security.h"
#include "bank_core.h"

Bank *bank; //Pointer to the Shared Memory region accessible by all processes
static int server_fd; //Flag to control the server shutdown loop
volatile sig_atomic_t stop_server = 0;

#define MAX_USERS 100

//Structure to hold mock user database in memory
typedef struct {
    char username[LOGIN_USERNAME_LEN];
    char password[LOGIN_PASSWORD_LEN];
    int account_id;
} UserInfo;

UserInfo users[MAX_USERS];

/* ================= Console Mutex ================= */
//Mutex to ensure thread-safe printing to stdout
pthread_mutex_t console_lock = PTHREAD_MUTEX_INITIALIZER; 
//Helper function to print timestamped logs safely
void print_server_console_log(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    pthread_mutex_lock(&console_lock);
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    printf("[%02d:%02d:%02d] ", t->tm_hour, t->tm_min, t->tm_sec);
    vprintf(fmt, args);
    printf("\n");
    fflush(stdout);
    pthread_mutex_unlock(&console_lock);
    va_end(args);
}

/* ================= Signal Handler ================= */
//Handles SIGINT (Ctrl+C) to gracefully stop the server
void handle_sigint(int sig) { (void)sig; stop_server = 1; }

/* ================= Init Bank ================= */
//Initializes Shared Memory, Mutexes, and Mock User Data
void init_bank() {
    //Create or open the shared memory object
    int fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (fd < 0) { perror("shm_open"); exit(1); }
    //Set the size of the shared memory object
    if (ftruncate(fd, sizeof(Bank)) == -1) { perror("ftruncate"); exit(1); }

    //Map the shared memory into this process's address space
    bank = mmap(NULL, sizeof(Bank), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (bank == MAP_FAILED) { perror("mmap"); exit(1); }

    //Initialize bank data (accounts, balances, mutex attributes)
    bank_init(bank);

    //Initialize mock users (user0 -> pass0 -> account 0)
    for (int i = 0; i < MAX_USERS; i++) {
        snprintf(users[i].username, LOGIN_USERNAME_LEN, "user%d", i);
        snprintf(users[i].password, LOGIN_PASSWORD_LEN, "pass%d", i);
        users[i].account_id = i;
    }

    //Initialize the transaction log file
    FILE *fp = fopen("transaction.log", "w");
    if (fp) { fprintf(fp, "=== Mutex Bank Transaction Log Started ===\n"); fclose(fp); }

    print_server_console_log("Mutex Bank Server Starting...");
    print_server_console_log("Shared memory initialized");
    print_server_console_log("Listening on port %d", PORT);
    print_server_console_log("Waiting for clients...");
}

/* ================= Transfer Handler ================= */
//Handles money transfer between two accounts with Row-Level Locking
void handle_transfer(int client_sock, Request *req, FILE *log_fp) {
    Response res = {0};
    struct timespec start, end;
    //Start timing for latency metric
    clock_gettime(CLOCK_MONOTONIC, &start);

    int u1 = req->src_id;
    int u2 = req->dst_id;
    int amt = req->amount;

    if (u1 < 0 || u1 >= MAX_ACCOUNTS || u2 < 0 || u2 >= MAX_ACCOUNTS) {
        res.status = RES_ERROR;
        strcpy(res.msg, "Invalid ID");
    } else {
        //Deadlock Prevention: Always lock the smaller ID first
        int first = (u1 < u2) ? u1 : u2;
        int second = (u1 < u2) ? u2 : u1;

        pthread_mutex_lock(&bank->accounts[first].lock);
        pthread_mutex_lock(&bank->accounts[second].lock);

        //Critical Section: Check balance and update
        if (bank->accounts[u1].balance >= amt) {
            bank->accounts[u1].balance -= amt;
            bank->accounts[u2].balance += amt;
            res.status = RES_OK;
            res.balance = bank->accounts[u1].balance;
            strcpy(res.msg, "Transfer OK");

            //Lock global log mutex to update statistics and write to file
            pthread_mutex_lock(&bank->log_lock);
            bank->total_tx_count++;
            if (log_fp) {
                time_t now = time(NULL);
                char *time_str = ctime(&now);
                time_str[strlen(time_str)-1] = '\0';
                fprintf(log_fp, "[%s] #%lld Transfer: Acc %02d -> Acc %02d ($%d)\n",
                        time_str, bank->total_tx_count, u1, u2, amt);
                fflush(log_fp);
            }
            pthread_mutex_unlock(&bank->log_lock);
        } else {
            res.status = RES_NO_FUNDS;
            strcpy(res.msg, "No Funds");
        }

        pthread_mutex_unlock(&bank->accounts[second].lock);
        pthread_mutex_unlock(&bank->accounts[first].lock);
    }

    //Encrypt response with XOR and send
    xor_cipher(&res, sizeof(Response));
    send_packet(client_sock, &res, sizeof(Response));

    //Calculate latency and atomically add to total
    clock_gettime(CLOCK_MONOTONIC, &end);
    long latency_ns = (end.tv_sec - start.tv_sec) * 1000000000L + (end.tv_nsec - start.tv_nsec);
    __sync_fetch_and_add(&bank->total_latency_ns, latency_ns);
}

/* ================= Deposit Handler ================= */
void handle_deposit(int client_sock, Request *req, FILE *log_fp) {
    Response res = {0};
    int acc = req->src_id;
    int amt = req->amount;

    //Lock specific account
    pthread_mutex_lock(&bank->accounts[acc].lock);
    bank->accounts[acc].balance += amt;
    res.status = RES_OK;
    res.balance = bank->accounts[acc].balance;
    strcpy(res.msg, "Deposit OK");
    pthread_mutex_unlock(&bank->accounts[acc].lock);

    //Update global stats
    pthread_mutex_lock(&bank->log_lock);
    bank->total_tx_count++;
    if (log_fp) {
        time_t now = time(NULL);
        char *time_str = ctime(&now);
        time_str[strlen(time_str)-1] = '\0';
        fprintf(log_fp, "[%s] #%lld Deposit: Acc %02d ($%d)\n",
                time_str, bank->total_tx_count, acc, amt);
        fflush(log_fp);
    }
    pthread_mutex_unlock(&bank->log_lock);

    xor_cipher(&res, sizeof(Response));
    send_packet(client_sock, &res, sizeof(Response));
}

/* ================= Withdraw Handler ================= */
void handle_withdraw(int client_sock, Request *req, FILE *log_fp) {
    Response res = {0};
    int acc = req->src_id;
    int amt = req->amount;

    pthread_mutex_lock(&bank->accounts[acc].lock);
    if (bank->accounts[acc].balance >= amt) {
        bank->accounts[acc].balance -= amt;
        res.status = RES_OK;
        res.balance = bank->accounts[acc].balance;
        strcpy(res.msg, "Withdraw OK");
    } else {
        res.status = RES_NO_FUNDS;
        strcpy(res.msg, "Insufficient Funds");
    }
    pthread_mutex_unlock(&bank->accounts[acc].lock);

    pthread_mutex_lock(&bank->log_lock);
    bank->total_tx_count++;
    if (log_fp) {
        time_t now = time(NULL);
        char *time_str = ctime(&now);
        time_str[strlen(time_str)-1] = '\0';
        fprintf(log_fp, "[%s] #%lld Withdraw: Acc %02d ($%d)\n",
                time_str, bank->total_tx_count, acc, amt);
        fflush(log_fp);
    }
    pthread_mutex_unlock(&bank->log_lock);

    xor_cipher(&res, sizeof(Response));
    send_packet(client_sock, &res, sizeof(Response));
}

/* ================= Worker Loop ================= */
//Main loop for child processes
void worker_loop(int server_fd) {
    signal(SIGINT, SIG_DFL);
    FILE *log_fp = fopen("transaction.log", "a");

    while (1) {
        struct sockaddr_in addr;
        socklen_t len = sizeof(addr);
        //Accept new connection (Preforking: OS handles load balancing)
        int client_sock = accept(server_fd, (struct sockaddr*)&addr, &len);
        if (client_sock < 0) continue;

        //Configure TCP Keep-alive and Timeouts
        int keep = 1;
        setsockopt(client_sock, SOL_SOCKET, SO_KEEPALIVE, &keep, sizeof(keep));
        int idle = 5, interval = 3, maxpkt = 3;
        setsockopt(client_sock, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(idle));
        setsockopt(client_sock, IPPROTO_TCP, TCP_KEEPINTVL, &interval, sizeof(interval));
        setsockopt(client_sock, IPPROTO_TCP, TCP_KEEPCNT, &maxpkt, sizeof(maxpkt));

        struct timeval tv = {3, 0};
        setsockopt(client_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(client_sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        //Authentication Phase (AES Encryption)
        LoginRequest login_req;
        unsigned char encrypted_req[sizeof(LoginRequest)] = {0};
        int ret = recv_packet(client_sock, encrypted_req, sizeof(LoginRequest));
        if (ret <= 0) { close(client_sock); continue; }

        //Decrypt login credentials using AES
        aes_decrypt(encrypted_req, &login_req, sizeof(LoginRequest));

        int valid = 0, account_id = -1;
        //Verify credentials against memory DB
        for (int i = 0; i < MAX_USERS; i++) {
            if (strcmp(users[i].username, login_req.username) == 0 &&
                strcmp(users[i].password, login_req.password) == 0) {
                valid = 1;
                account_id = users[i].account_id;
                break;
            }
        }

        LoginResponse login_res = {0};
        if (!valid) {
            login_res.success = 0;
            strcpy(login_res.msg, "Login Failed");
            print_server_console_log("[AUTH] Failed login attempt: User %s", login_req.username);
        } else {
            login_res.success = 1;
            strcpy(login_res.msg, "Login OK");
            print_server_console_log("[AUTH] User %s connected, account=%d",
                                     login_req.username, account_id);
        }

        //Send encrypted response back
        unsigned char encrypted_res[sizeof(LoginResponse)] = {0};
        aes_encrypt(&login_res, encrypted_res, sizeof(LoginResponse));
        send_packet(client_sock, encrypted_res, sizeof(LoginResponse));

        if (!valid) { close(client_sock); continue; }

        //Transaction Phase (XOR Encryption)
        Request req;
        ret = recv_packet(client_sock, &req, sizeof(req));
        if (ret > 0) {
            xor_cipher(&req, sizeof(Request));
            req.src_id = account_id;
            switch (req.op) {
                case OP_TRANSFER: handle_transfer(client_sock, &req, log_fp); break;
                case OP_DEPOSIT:  handle_deposit(client_sock, &req, log_fp); break;
                case OP_WITHDRAW: handle_withdraw(client_sock, &req, log_fp); break;
                default:
                    { Response res = {.status=RES_ERROR}; strcpy(res.msg,"Invalid Operation");
                      xor_cipher(&res,sizeof(Response)); send_packet(client_sock,&res,sizeof(Response)); }
                    break;
            }
        } else {
            Response res = { .status = RES_ERROR };
            strcpy(res.msg, "Packet Error / Timeout");
            xor_cipher(&res, sizeof(Response));
            send_packet(client_sock, &res, sizeof(Response));
        }

        close(client_sock);
    }

    if (log_fp) fclose(log_fp);
}

/* ================= Print Final Bank ================= */
void print_final_report() {
    printf("\n========== [Mutex Bank 帳戶餘額一覽] ==========\n");
    long long total_assets = 0;
    for (int i = 0; i < MAX_ACCOUNTS; i++) {
        if (i % 4 == 0) printf("\n");
        printf("[Acc %02d: $%4lld]  ", bank->accounts[i].id, bank->accounts[i].balance);
        total_assets += bank->accounts[i].balance;
    }
    printf("\n-----------------------------------------------\n");
    printf("📊 統計數據:\n");
    printf(" 1. 銀行總資產: $%lld\n", total_assets);
    printf(" 2. 總交易筆數: %lld 筆\n", bank->total_tx_count);
    double avg_latency_ms = bank->total_tx_count > 0 ? (double)bank->total_latency_ns / bank->total_tx_count / 1e6 : 0;
    printf(" 3. 平均延遲: %.3f ms\n", avg_latency_ms);
    printf("===============================================\n");
}

/* ================= Main ================= */
int main() {
    signal(SIGINT, handle_sigint);

    init_bank();

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in serv_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(PORT),
        .sin_addr.s_addr = INADDR_ANY
    };
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    if (bind(server_fd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) { perror("Bind"); exit(1); }
    listen(server_fd, 100);

    //Preforking: Create 10 child processes to handle connections
    for (int i = 0; i < 10; i++) {
        if (fork() == 0) { worker_loop(server_fd); exit(0); }
    }

    //Parent process waits for signal to stop
    while (!stop_server) pause();

    print_final_report();
    shm_unlink(SHM_NAME);
    close(server_fd);
    return 0;
}
