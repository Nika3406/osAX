# ===== Variables =====
AS = nasm
CC = i386-elf-gcc
LD = i386-elf-ld
OBJCOPY = i386-elf-objcopy
QEMU = qemu-system-i386

# Compiler flags - Add all include subdirectories
CFLAGS = -m32 -ffreestanding -nostdinc -fno-builtin -O2 -Wall -Wextra \
         -Isrc/include \
         -Isrc/include/core \
         -Isrc/include/memory \
         -Isrc/include/fs \
         -Isrc/include/drivers \
         -Isrc/include/lib \
         -Isrc/include/system

LDFLAGS = -m elf_i386 -T src/bootloader/stage2.ld -nostdlib

# Bootloader assembly sources
BOOTLOADER_SRC = src/bootloader/boot.asm
ENTRY_ASM = src/bootloader/entry.asm
PAGING_ASM = src/bootloader/paging_asm.asm
IDT_ASM = src/bootloader/idt_asm.asm
IRQ_ASM = src/bootloader/irq_asm.asm

# C source files (organized by category)
CORE_SOURCES = \
    src/kernel/core/stage2.c \
    src/kernel/core/main.c \
    src/kernel/core/idt.c \
    src/kernel/core/process.c \
    src/kernel/core/executable.c

MEMORY_SOURCES = \
    src/kernel/memory/physical_mm.c \
    src/kernel/memory/paging.c \
    src/kernel/memory/heap.c \
    src/kernel/memory/memory.c \
    src/kernel/memory/dma.c

FS_SOURCES = \
    src/kernel/fs/exfat/exfat.c \
    src/kernel/fs/exfat/exfat_fileops.c \
    src/kernel/fs/metafs/metafs.c \
    src/kernel/fs/metafs/metafs_wrappers.c

DRIVER_SOURCES = \
    src/kernel/drivers/keyboard.c \
    src/kernel/drivers/serial.c \
    src/kernel/drivers/terminal.c

LIB_SOURCES = \
    src/kernel/lib/string.c \
    src/kernel/lib/kstring_util.c

SYSTEM_SOURCES = \
    src/kernel/system/system.c \
    src/kernel/system/logger.c \
    src/kernel/system/shell.c

LEGACY_SOURCES = \
    src/kernel/kernel.c

# Combine all sources
C_SOURCES = $(CORE_SOURCES) $(MEMORY_SOURCES) $(FS_SOURCES) \
            $(DRIVER_SOURCES) $(LIB_SOURCES) $(SYSTEM_SOURCES) \
            $(LEGACY_SOURCES)

# Object files
ASM_OBJS = \
    build/entry.o \
    build/paging_asm.o \
    build/idt_asm.o \
    build/irq_asm.o

# Flatten C object names (all go to build/)
C_OBJS = $(foreach src,$(C_SOURCES),build/$(notdir $(basename $(src))).o)

# Final outputs
BOOTLOADER_BIN = build/bootloader.bin
STAGE2_ELF = build/stage2.elf
STAGE2_BIN = build/stage2.bin
DISK_IMG = build/disk.img

# ===== Targets =====
.PHONY: all run debug serial clean distclean help

all: $(DISK_IMG)

build:
	@mkdir -p build

# --- Assembly files ---
build/%.o: src/bootloader/%.asm build
	@echo "Assembling $<..."
	$(AS) -f elf32 $< -o $@

# --- C files from various directories (match by basename) ---
build/stage2.o: src/kernel/core/stage2.c build
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) -c $< -o $@

build/main.o: src/kernel/core/main.c build
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) -c $< -o $@

build/idt.o: src/kernel/core/idt.c build
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) -c $< -o $@

build/process.o: src/kernel/core/process.c build
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) -c $< -o $@

build/executable.o: src/kernel/core/executable.c build
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) -c $< -o $@

build/physical_mm.o: src/kernel/memory/physical_mm.c build
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) -c $< -o $@

build/paging.o: src/kernel/memory/paging.c build
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) -c $< -o $@

build/heap.o: src/kernel/memory/heap.c build
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) -c $< -o $@

build/memory.o: src/kernel/memory/memory.c build
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) -c $< -o $@

build/dma.o: src/kernel/memory/dma.c build
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) -c $< -o $@

build/exfat.o: src/kernel/fs/exfat/exfat.c build
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) -c $< -o $@

build/exfat_fileops.o: src/kernel/fs/exfat/exfat_fileops.c build
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) -c $< -o $@

build/metafs.o: src/kernel/fs/metafs/metafs.c build
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) -c $< -o $@

build/metafs_wrappers.o: src/kernel/fs/metafs/metafs_wrappers.c build
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) -c $< -o $@

build/keyboard.o: src/kernel/drivers/keyboard.c build
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) -c $< -o $@

build/serial.o: src/kernel/drivers/serial.c build
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) -c $< -o $@

build/terminal.o: src/kernel/drivers/terminal.c build
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) -c $< -o $@

build/string.o: src/kernel/lib/string.c build
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) -c $< -o $@

build/kstring_util.o: src/kernel/lib/kstring_util.c build
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) -c $< -o $@

build/system.o: src/kernel/system/system.c build
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) -c $< -o $@

build/logger.o: src/kernel/system/logger.c build
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) -c $< -o $@

build/shell.o: src/kernel/system/shell.c build
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) -c $< -o $@

build/kernel.o: src/kernel/kernel.c build
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) -c $< -o $@

# --- Link Stage2 ---
$(STAGE2_ELF): $(ASM_OBJS) $(C_OBJS)
	@echo "Linking stage2..."
	$(LD) $(LDFLAGS) $^ -o $@

$(STAGE2_BIN): $(STAGE2_ELF)
	@echo "Creating stage2 binary..."
	$(OBJCOPY) -O binary $< $@
	@echo "Stage2 size: $$(wc -c < $@) bytes"

# --- Bootloader (MUST come AFTER stage2.bin) ---
$(BOOTLOADER_BIN): $(BOOTLOADER_SRC) $(STAGE2_BIN) build
	@echo "DEBUG: stage2.bin exists: $$(test -f $(STAGE2_BIN) && echo YES || echo NO)"
	@echo "DEBUG: stage2.bin size: $$(test -f $(STAGE2_BIN) && stat -c%s $(STAGE2_BIN) || echo 0)"
	$(eval STAGE2_SIZE := $(shell stat -c%s $(STAGE2_BIN) 2>/dev/null || echo 512))
	$(eval STAGE2_SECTORS := $(shell echo $$(( ($(STAGE2_SIZE) + 511) / 512 ))))
	@echo "Building bootloader: stage2 = $(STAGE2_SIZE) bytes ($(STAGE2_SECTORS) sectors)"
	$(AS) -f bin -DSTAGE2_SECTORS=$(STAGE2_SECTORS) $< -o $@

# --- Disk image ---
$(DISK_IMG): $(BOOTLOADER_BIN) $(STAGE2_BIN)
	$(eval STAGE2_SIZE := $(shell stat -c%s $(STAGE2_BIN) 2>/dev/null || echo 512))
	$(eval STAGE2_SECTORS := $(shell echo $$(( ($(STAGE2_SIZE) + 511) / 512 ))))
	$(eval DISK_SECTORS := $(shell echo $$(( $(STAGE2_SECTORS) + 200 ))))
	@echo "Creating disk image: $(DISK_SECTORS) sectors total"
	dd if=/dev/zero of=$(DISK_IMG) bs=512 count=$(DISK_SECTORS) 2>/dev/null
	dd if=$(BOOTLOADER_BIN) of=$(DISK_IMG) conv=notrunc 2>/dev/null
	dd if=$(STAGE2_BIN) of=$(DISK_IMG) bs=512 seek=1 conv=notrunc 2>/dev/null
	@echo "Build complete! Bootloader + $(STAGE2_SECTORS) kernel sectors"

# --- Run in QEMU ---
run: $(DISK_IMG)
	$(QEMU) -m 256 \
	        -drive file=$(DISK_IMG),format=raw,if=ide \
	        -serial stdio \
	        -no-reboot -no-shutdown \
	        -d int,cpu_reset,guest_errors -D qemu.log


debug: $(DISK_IMG)
	$(QEMU) -m 256 \
	        -drive file=$(DISK_IMG),format=raw,if=ide \
	        -serial stdio \
	        -no-reboot -no-shutdown \
	        -d int,cpu_reset,guest_errors -D qemu.log


serial: $(DISK_IMG)
	$(QEMU) -hda $(DISK_IMG) -serial stdio

# --- Clean ---
clean:
	@echo "Cleaning build files..."
	rm -f build/*.bin build/*.o build/*.elf build/*.img
	@echo "Clean complete!"

distclean: clean
	rm -rf build

# --- Help ---
help:
	@echo "Available targets:"
	@echo "  all       - Build everything (default)"
	@echo "  run       - Build and run in QEMU"
	@echo "  debug     - Build and run with debugging"
	@echo "  clean     - Remove build files"
	@echo "  distclean - Remove build directory"
