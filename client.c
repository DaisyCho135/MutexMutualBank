#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <signal.h>
#include <netinet/tcp.h>
//#include "bank_lib.h"
#include "models.h"
#include "protocol.h"
#include "security.h"
#include "bank_core.h"

//Number of concurrent threads simulating users
#define CLIENT_THREADS 100
//Number of transactions each thread will perform
#define TX_PER_THREAD 1

//Global flag for graceful shutdown on SIGINT
volatile sig_atomic_t stop_client = 0;
//Global statistics for performance measurement
long long total_tx_count = 0;
long long total_latency_ns = 0;
//Mutex to protect statistics updates
pthread_mutex_t stats_lock = PTHREAD_MUTEX_INITIALIZER;
struct timespec start_time;

//Local user structure for client simulation
typedef struct {
    char username[LOGIN_USERNAME_LEN];
    char password[LOGIN_PASSWORD_LEN];
} User;

User users[CLIENT_THREADS];

//Signal handler to stop client loop
void handle_sigint(int sig) {
    (void)sig;
    stop_client = 1;
}

//Thread function simulating a single client user
void* client_task(void* arg) {
    int my_id = *(int*)arg;
    free(arg);

    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);
    inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr);

    for (int i = 0; i < TX_PER_THREAD; i++) {
        if (stop_client) break;

        //Create TCP socket
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) continue;

        //Connect to the server
        if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
            close(sock);
            usleep(1000); //Retry backoff
            continue;
        }

        //TCP Socket Options for performance and reliability
        int keep = 1;
        setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &keep, sizeof(keep));
        int idle = 5, interval = 3, maxpkt = 3;
        setsockopt(sock, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(idle));
        setsockopt(sock, IPPROTO_TCP, TCP_KEEPINTVL, &interval, sizeof(interval));
        setsockopt(sock, IPPROTO_TCP, TCP_KEEPCNT, &maxpkt, sizeof(maxpkt));

        struct timeval tv = {3, 0}; //3-second timeout for send/recv
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        //Login Phase (High Security: AES)
        LoginRequest login_req = {0};
        strcpy(login_req.username, users[my_id].username);
        strcpy(login_req.password, users[my_id].password);

        //Encrypt credentials using AES-128-CBC
        unsigned char enc_login_req[sizeof(LoginRequest)] = {0};
        aes_encrypt(&login_req, enc_login_req, sizeof(LoginRequest));
        send_packet(sock, enc_login_req, sizeof(LoginRequest));

        //Receive and decrypt login response
        unsigned char enc_login_res[sizeof(LoginResponse)] = {0};
        int ret = recv_packet(sock, enc_login_res, sizeof(LoginResponse));

        LoginResponse login_res = {0};
        aes_decrypt(enc_login_res, &login_res, sizeof(LoginResponse));

        if (ret <= 0 || login_res.success != 1) {
            printf("[User %02d] Login Failed: %s\n", my_id, login_res.msg);
            close(sock);
            continue;
        }

        //Random Operation Phase (Efficiency: XOR)
        Request req = {0};
        //Randomly choose op: 0=Transfer, 1=Deposit, 2=Withdraw
        int op_type = rand() % 3;  

        if (op_type == 0) {
            req.op = OP_TRANSFER;
            do {
                req.dst_id = rand() % MAX_ACCOUNTS;
            } while (req.dst_id == my_id);
            req.amount = (rand() % 100) + 1;
        } else if (op_type == 1) {
            req.op = OP_DEPOSIT;
            req.amount = (rand() % 100) + 1;
        } else {
            req.op = OP_WITHDRAW;
            req.amount = (rand() % 100) + 1;
        }

        //Save plaintext data for printing before encryption
        int real_op   = req.op;
        int print_src = my_id;
        int print_dst = req.dst_id;
        int print_amt = req.amount;

        //Apply XOR obfuscation to the request payload
        xor_cipher(&req, sizeof(Request));

        struct timespec tx_start, tx_end;
        clock_gettime(CLOCK_MONOTONIC, &tx_start);

        if (send_packet(sock, &req, sizeof(Request)) != 0) {
            close(sock);
            continue;
        }

        Response res = {0};
        ret = recv_packet(sock, &res, sizeof(Response));

        if (ret > 0) {
            
            //Decrypt response using XOR
            xor_cipher(&res, sizeof(Response));
            clock_gettime(CLOCK_MONOTONIC, &tx_end);

            //Calculate latency in nanoseconds
            long latency_ns =
                (tx_end.tv_sec - tx_start.tv_sec) * 1000000000L +
                (tx_end.tv_nsec - tx_start.tv_nsec);

            //Update global statistics safely
            pthread_mutex_lock(&stats_lock);
            total_tx_count++;
            total_latency_ns += latency_ns;
            pthread_mutex_unlock(&stats_lock);

            if (res.status == RES_OK) {
                if (real_op == OP_TRANSFER)
                    printf("[User %02d] 轉帳成功！ Acc %02d -> Acc %02d ($%d)\n",
                           my_id, print_src, print_dst, print_amt);
                else if (real_op == OP_DEPOSIT)
                    printf("[User %02d] 存款成功！ Acc %02d ($%d)\n",
                           my_id, print_src, print_amt);
                else if (real_op == OP_WITHDRAW)
                    printf("[User %02d] 提款成功！ Acc %02d ($%d)\n",
                           my_id, print_src, print_amt);
            } else {
                printf("[User %02d] 操作失敗: %s\n", my_id, res.msg);
            }
        } else {
            printf("[User %02d] 收到錯誤或 Timeout\n", my_id);
        }

        close(sock);
    }
    return NULL;
}

void print_global_stats() {
    struct timespec end_time;
    clock_gettime(CLOCK_MONOTONIC, &end_time);

    double elapsed_sec =
        (end_time.tv_sec - start_time.tv_sec) +
        (end_time.tv_nsec - start_time.tv_nsec) / 1e9;

    pthread_mutex_lock(&stats_lock);
    double avg_latency_ms =
        total_tx_count > 0 ? (double)total_latency_ns / total_tx_count / 1e6 : 0;
    double tps = elapsed_sec > 0 ? total_tx_count / elapsed_sec : 0;

    printf("\n========== [Client 統計] ==========\n");
    printf("總交易筆數: %lld\n", total_tx_count);
    printf("平均延遲: %.3f ms\n", avg_latency_ms);
    printf("整體 Throughput (TPS): %.2f 交易/秒\n", tps);
    printf("總耗時: %.3f 秒\n", elapsed_sec);
    printf("===================================\n");

    pthread_mutex_unlock(&stats_lock);
}

int main() {
    setbuf(stdout, NULL);
    srand(time(NULL));
    signal(SIGINT, handle_sigint);
    clock_gettime(CLOCK_MONOTONIC, &start_time);

    pthread_t threads[CLIENT_THREADS];

    //Initialize mock user data
    for (int i = 0; i < CLIENT_THREADS; i++) {
        snprintf(users[i].username, LOGIN_USERNAME_LEN, "user%d", i);
        snprintf(users[i].password, LOGIN_PASSWORD_LEN, "pass%d", i);
    }

    printf("========================================\n");
    printf("[System] 交易中...\n");
    printf("========================================\n");

    //Spawn client threads
    for (int i = 0; i < CLIENT_THREADS; i++) {
        int *arg = malloc(sizeof(int));
        *arg = i;
        pthread_create(&threads[i], NULL, client_task, arg);
    }

    //Wait for all threads to complete
    for (int i = 0; i < CLIENT_THREADS; i++)
        pthread_join(threads[i], NULL);

    print_global_stats();

    printf("\n========================================\n");
    printf("[System] 交易結束。\n");
    return 0;
}
