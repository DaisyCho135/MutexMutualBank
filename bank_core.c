#include "bank_core.h"

/*
Initializes the Bank structure in Shared Memory.
This function sets up the initial state of accounts and, crucially,
initializes the synchronization primitives (mutexes) to be process-shared.
 */
void bank_init(Bank *bank) {
    //Reset global performance statistics
    bank->total_tx_count = 0;
    bank->total_latency_ns = 0;

    //Initialize mutex attributes
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);

    /*
    [CRITICAL] Set the mutex attribute to PTHREAD_PROCESS_SHARED.
    This tells the OS that these mutexes will be accessed by multiple processes
    (not just threads within a single process). This is required because
    the 'bank' structure resides in shared memory (mmap) and is accessed by
    forked worker processes.
    */
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    //Initialize global locks using the shared attribute
    pthread_mutex_init(&bank->global_lock, &attr); //Protects global stats
    pthread_mutex_init(&bank->log_lock, &attr); //Protects file I/O for logging

    //Initialize all accounts
    for (int i = 0; i < MAX_ACCOUNTS; i++) {
        bank->accounts[i].id = i;
        bank->accounts[i].balance = 1000; //Set initial balance
        /*
        Initialize Row-Level Lock for each account.
        This allows high concurrency: locking Account A doesn't block Account B.
        */
        pthread_mutex_init(&bank->accounts[i].lock, &attr);
    }
}