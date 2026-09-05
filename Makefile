CC = gcc
AS = gcc
AR = ar
LD = ld
VERSION := $(shell cat VERSION)

CFLAGS = -m32 -ffreestanding -fno-pie -fno-stack-protector -nostdlib \
         -Iinclude -Wall -Wextra -DCACTLIB_VERSION=\"$(VERSION)\"
CFLAGS_PIC = -m32 -ffreestanding -fPIC -fno-stack-protector -nostdlib \
             -Iinclude -Wall -Wextra -DCACTLIB_VERSION=\"$(VERSION)\"
ASFLAGS = -m32 -ffreestanding -fno-pie -nostdlib

SRC_DIR = src
BUILD_DIR = build
PIC_DIR   = $(BUILD_DIR)/pic
INCLUDE_DIR = include

C_SRCS = $(wildcard $(SRC_DIR)/*.c)
S_SRCS = $(wildcard $(SRC_DIR)/*.S)
C_OBJS = $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(C_SRCS))
S_OBJS = $(patsubst $(SRC_DIR)/%.S, $(BUILD_DIR)/%.o, $(S_SRCS))
OBJS = $(C_OBJS) $(S_OBJS)

C_PIC_OBJS = $(patsubst $(SRC_DIR)/%.c, $(PIC_DIR)/%.o, $(C_SRCS))
START_O_PIC = $(PIC_DIR)/start.o

TARGET_A  = libc.a
TARGET_SO = libc.so
LD_SO     = ld.so

all: $(TARGET_A) $(TARGET_SO) $(START_O_PIC) $(LD_SO)

$(TARGET_A): $(OBJS)
	$(AR) rcs $@ $^

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.S
	@mkdir -p $(BUILD_DIR)
	$(AS) $(ASFLAGS) -c $< -o $@

$(PIC_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(PIC_DIR)
	$(CC) $(CFLAGS_PIC) -c $< -o $@

$(PIC_DIR)/%.o: $(SRC_DIR)/%.S
	@mkdir -p $(PIC_DIR)
	$(CC) $(CFLAGS_PIC) -c $< -o $@

ALL_PIC_OBJS = $(C_PIC_OBJS) $(filter-out $(START_O_PIC),$(patsubst $(SRC_DIR)/%.S,$(PIC_DIR)/%.o,$(S_SRCS)))

$(TARGET_SO): $(ALL_PIC_OBJS) libc.ld
	$(LD) -m elf_i386 -shared -nostdlib \
	      --hash-style=both -soname=libc.so \
	      -T libc.ld -o $@ $(ALL_PIC_OBJS)

# Userspace dynamic linker (PT_INTERP interpreter). Self-contained, no libc.
ldso/start.o: ldso/start.S
	$(CC) $(CFLAGS) -c $< -o $@

ldso/ldso.o: ldso/ldso.c ldso/ldso.h
	$(CC) $(CFLAGS) -O2 -c $< -o $@

$(LD_SO): ldso/start.o ldso/ldso.o ldso/ldso.ld
	$(LD) -m elf_i386 -nostdlib -T ldso/ldso.ld -o $@ ldso/start.o ldso/ldso.o

clean:
	rm -rf $(BUILD_DIR) $(TARGET_A) $(TARGET_SO) $(LD_SO)
	rm -f ldso/start.o ldso/ldso.o

version:
	@echo "CactLib version: $(VERSION)"

.PHONY: all clean version
