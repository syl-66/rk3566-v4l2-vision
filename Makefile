CC = /home/shiyangling/Desktop/linux-kernel-6.1/rk-linux6.1-2026060914/prebuilts/gcc/linux-x86/aarch64/gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu/bin/aarch64-none-linux-gnu-gcc

CFLAGS = -Wall -g
SRCS = $(wildcard src/*.c)
OBJS = $(SRCS:.c=.o)

TARGET = rk3566_app

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $@ $^
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)
