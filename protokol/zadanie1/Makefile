CC     = g++
CFLAGS = -Wall -Wextra -O2 -std=gnu17
LFLAGS =

.PHONY: all clean

TARGET1 = ppcbc
TARGET2 = ppcbs

all: $(TARGET1) $(TARGET2)

$(TARGET1): $(TARGET1).o err.o common.o
$(TARGET2): $(TARGET2).o err.o common.o

# To są zależności wygenerowane automatycznie za pomocą polecenia `gcc -MM *.c`.

err.o: err.cc err.h
common.o: common.cc err.h common.h

ppcbc.o: ppcbc.cc err.h common.h protconst.h
ppcbs.o: ppcbs.cc err.h common.h protconst.h

clean:
	rm -f $(TARGET1) $(TARGET2) *.o *~
