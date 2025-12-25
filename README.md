Mutex Mutual Bank

Introduction

This project is a simulation of a real-world financial system developed in C for the Linux environment.
The primary objective of this project is to address Race Conditions that occur when multiple users operate on shared data simultaneously. By simulating 100 concurrent users performing transfers, deposits, and withdrawals, this system demonstrates how to maintain data consistency and system security under high load.

Key Features & Technologies

1. System Architecture
Multi-process Concurrency:The Server uses fork() to create multiple Worker Processes, enabling it to handle multiple connection requests simultaneously.
Shared Memory:Utilizes mmap technology to allow different processes to access the same banking ledger data efficiently.

2. Data Consistency
Mutex Locks: Each bank account has its own independent lock (pthread_mutex). When one process is modifying a balance, others must wait, ensuring the transaction amount is always correct.
Deadlock Prevention: In the transfer logic, I enforced a rule to "always lock the account with the smaller ID first." This effectively prevents circular wait scenarios (Deadlocks).

3. Security Mechanisms
AES Encryption: Integrated the OpenSSL library to encrypt usernames and passwords using the AES-128-CBC algorithm during login, preventing plaintext credentials from being exposed.
XOR Packet Obfuscation: Implemented a lightweight XOR cipher for frequent transaction commands to balance performance and privacy.
Data Integrity: Implemented CRC32 checksums to ensure network packets are not corrupted during transmission.

How to use

1.Installation
Since the project uses OpenSSL for encryption, you must install the development library first:
sudo apt update 
sudo apt install libssl-dev

2.Compile
I have included a Makefile. You can compile everything automatically by running:
make

3. Run the System

3-1.Start the Server
./server
The server will initialize the shared memory and start listening on Port 8888.

3-2.Start the Stress Test Client Open a separate terminal window and run:
./client
The client will simulate 100 threads sending concurrent requests to the server.

4.Result
Client Side: After the test finishes, it will display the TPS (Transactions Per Second) and average latency.
Server Side: When you stop the server using Ctrl + C, the system will automatically print a Final Financial Report. You can use this to verify if the total assets are balanced and correct.

File Structure
1.server.c:The main server program (IPC, Socket connections, and transaction logic).
2.client.c: Stress testing tool (Generates massive concurrent requests).
3.bank_core.c: Defines bank account structures and initializes Mutex locks.
4.security.c: Encapsulates AES encryption and XOR cipher functions.
5.protocol.c: Handles network packet transmission and CRC32 verification.
6.models.h: Defines shared data structures and constants.

Division of Work:
許安妤(HSU AN YU): 
1.Use preforking to handle connections.
2.Implement:
(1).Row-Level Locking (mutex lock) to lock the two specific accounts that engaged in transferring money 
(2).Global Locking (log lock) to lock the writing right in the transaction.log and total_tx_count (total transaction variable)
(3).Prevention of deadlock: 
to specify that the order of user A with smaller ID would be locked to the account before the one with bigger ID.
3.Function to transferring money with the communication protocol of request and response struct ( | op | src | dst | amount | ) with XOR payload encryption and design the transaction logging.

   
4.Use multi-threaded architecture to simulate 100 concurrent connections sending requests to the server simultaneously.
5.Design the system architecture diagram.

卓郁茨(CHO YU TZU):  
1.Provide statistical data on Latency and Throughput(TPS) on the client side.
2.Add the function of deposit and withdrawal in the program.
3.Implement security mechanism:
(1).Put the request and response in the data(payload), keeping XOR encryption designed by 許安妤(HSU AN YU) on it.
(2).Complete OpenSSL(AES-128-CBC) Login Authentication.
4.Modify the communication protocol to be | Length | Checksum | Payload | (Application Layer Protocol), and the checksum in the protocol is to prevent data corruption using CRC32.
5.Add TCP keep-alive and timeout to prevent the connection being occupied by a specific user.