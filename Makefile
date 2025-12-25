CC = gcc
CFLAGS = -Wall -O2
LIBS = -lpthread -lrt -lcrypto

# 模組化的物件檔案
OBJS = bank_core.o protocol.o security.o

all: libbank.a server client

# 編譯各個模組
%.o: %.c %.h models.h
	$(CC) $(CFLAGS) -c $< -o $@

# 製作靜態庫
libbank.a: $(OBJS)
	ar rcs libbank.a $(OBJS)

# 編譯 Server 與 Client (連結 libbank.a)
server: server.c libbank.a
	$(CC) $(CFLAGS) server.c -o server -L. -lbank $(LIBS)

client: client.c libbank.a
	$(CC) $(CFLAGS) client.c -o client -L. -lbank $(LIBS)

clean:
	rm -f server client *.o *.a
	rm -f /dev/shm/mutex_bank_shm
	rm -f transaction.log