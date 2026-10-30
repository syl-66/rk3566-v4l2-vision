CC = /home/shiyangling/Desktop/linux-kernel-6.1/rk-linux6.1-2026060914/prebuilts/gcc/linux-x86/aarch64/gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu/bin/aarch64-none-linux-gnu-gcc
# 1. 编译选项：告诉编译器去哪里找 .h
CFLAGS = -Wall -g -I./inc -I/home/shiyangling/Desktop/linux-kernel-6.1/rk-linux6.1-2026060914/external/rknpu2/runtime/Linux/librknn_api/include

# 2. 链接选项：告诉链接器要链哪些库
LDFLAGS = -lpthread -L/home/shiyangling/Desktop/linux-kernel-6.1/rk-linux6.1-2026060914/external/rknpu2/runtime/Linux/librknn_api/aarch64 -lrknnrt

SRCS = $(wildcard src/*.c)
OBJS = $(SRCS:.c=.o)

TARGET = rk3566_app

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

.PHONY: clean
clean:
	rm -f $(OBJS) $(TARGET)
