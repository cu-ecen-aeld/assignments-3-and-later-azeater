CFLAGS := -g -Wall
SRC := writer.c
TARGET = writer
OBJS := $(SRC:.c=.o)
CROSS_COMPILE:=
CC := $(CROSS_COMPILE)gcc

all: $(TARGET)

$(TARGET) : $(OBJS)
	$(CC) $(CFLAGS) $(INCLUDES) $(OBJS) -o $(TARGET) $(LDFLAGS)

clean:
	-rm -f *.o $(TARGET) *.elf *.map

