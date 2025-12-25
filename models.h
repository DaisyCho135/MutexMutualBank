#ifndef MODELS_H
#define MODELS_H

#include <pthread.h>
#include <stdint.h>

#define LOGIN_USERNAME_LEN 16
#define LOGIN_PASSWORD_LEN 16
#define MAX_ACCOUNTS 100
#define PORT 8888
#define SHM_NAME "/mutex_bank_shm"

typedef enum { OP_TRANSFER = 1, OP_DEPOSIT = 2, OP_WITHDRAW = 3 } OpCode;
typedef enum { RES_OK = 0, RES_ERROR, RES_NO_FUNDS } ResCode;

typedef struct { int src_id; int dst_id; int amount; OpCode op; } Request;
typedef struct { ResCode status; int balance; char msg[64]; } Response;

typedef struct { int id; long long balance; pthread_mutex_t lock; } Account;

typedef struct { char username[LOGIN_USERNAME_LEN]; char password[LOGIN_PASSWORD_LEN]; } LoginRequest;
typedef struct { int success; char msg[64]; } LoginResponse;

typedef struct {
    Account accounts[MAX_ACCOUNTS];
    pthread_mutex_t global_lock;
    pthread_mutex_t log_lock;
    long long total_tx_count;
    long long total_latency_ns;
} Bank;

#endif