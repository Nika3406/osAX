# =========================
# osAX Makefile (x86_64)
# build/ contains all artifacts
# stage1.5 sectors auto-computed (two-pass)
# =========================

AS      := nasm
CC      := x86_64-elf-gcc
LD      := x86_64-elf-ld
OBJCOPY := x86_64-elf-objcopy
QEMU    := qemu-system-x86_64

BUILD   := build

# -------------------------
# Includes
# -------------------------
INCLUDES := -Isrc/include -Isrc/include/core -Isrc/include/memory \
            -Isrc/include/fs -Isrc/include/drivers -Isrc/include/lib \
            -Isrc/include/system

# -------------------------
# Kernel C flags
# -------------------------
CFLAGS := -m64 -mcmodel=kernel -mno-red-zone \
          -mno-mmx -mno-sse -mno-sse2 \
          -ffreestanding -nostdinc -fno-builtin -O2 \
          -Wall -Wextra $(INCLUDES)

# -------------------------
# Sources
# -------------------------
BOOT_ASM    := src/bootloader/boot.asm
STAGE15_ASM := src/bootloader/stage15.asm

ENTRY_ASM   := src/bootloader/entry.asm
PAGING_ASM  := src/bootloader/paging_asm.asm
IDT_ASM     := src/bootloader/idt_asm.asm
IRQ_ASM     := src/bootloader/irq_asm.asm

STAGE2_LD   := src/bootloader/stage2.ld

CORE_SOURCES := \
    src/kernel/core/stage2.c \
    src/kernel/core/main.c \
    src/kernel/core/idt.c \
    src/kernel/core/process.c \
    src/kernel/core/executable.c

MEMORY_SOURCES := \
    src/kernel/memory/physical_mm.c \
    src/kernel/memory/paging.c \
    src/kernel/memory/heap.c \
    src/kernel/memory/memory.c \
    src/kernel/memory/dma.c

FS_SOURCES := \
    src/kernel/fs/exfat/exfat.c \
    src/kernel/fs/exfat/exfat_fileops.c \
    src/kernel/fs/metafs/metafs.c \
    src/kernel/fs/metafs/metafs_wrappers.c

DRIVER_SOURCES := \
    src/kernel/drivers/keyboard.c \
    src/kernel/drivers/serial.c \
    src/kernel/drivers/terminal.c

LIB_SOURCES := \
    src/kernel/lib/string.c \
    src/kernel/lib/kstring_util.c

SYSTEM_SOURCES := \
    src/kernel/system/system.c \
    src/kernel/system/logger.c \
    src/kernel/system/shell.c

KERNEL_SOURCES := \
    src/kernel/kernel.c

ALL_SOURCES := \
    $(CORE_SOURCES) \
    $(MEMORY_SOURCES) \
    $(FS_SOURCES) \
    $(DRIVER_SOURCES) \
    $(LIB_SOURCES) \
    $(SYSTEM_SOURCES) \
    $(KERNEL_SOURCES)

# -------------------------
# Outputs
# -------------------------
BOOT_BIN      := $(BUILD)/boot.bin
STAGE15_RAW   := $(BUILD)/stage15.raw
STAGE15_BIN   := $(BUILD)/stage15.bin
STAGE15_SECF  := $(BUILD)/stage15.sectors

ENTRY_OBJ     := $(BUILD)/entry.o
PAGING_OBJ    := $(BUILD)/paging_asm.o
IDT_OBJ       := $(BUILD)/idt_asm.o
IRQ_OBJ       := $(BUILD)/irq_asm.o

STAGE2_ELF    := $(BUILD)/stage2.elf
STAGE2_BIN    := $(BUILD)/stage2.bin

DISK_IMG      := $(BUILD)/os.img

# C object files: src/foo/bar.c -> build/foo/bar.o (no "src/" in the build path)
C_OBJECTS := $(patsubst src/%.c,$(BUILD)/%.o,$(ALL_SOURCES))

# -------------------------
# Default STAGE2 sectors (will be overridden when file exists)
# -------------------------
DEFAULT_STAGE2_SECTORS := 153

# -------------------------
# Targets
# -------------------------
.PHONY: all clean run run-simple run-debug run-serial verify

all: $(DISK_IMG)

# Ensure build dir exists
$(BUILD):
	@mkdir -p $(BUILD)

# -------------------------
# Compile C: create parent dirs automatically
# -------------------------
$(BUILD)/%.o: src/%.c | $(BUILD)
	@mkdir -p $(dir $@)
	@echo "Compiling $< (64-bit)..."
	$(CC) $(CFLAGS) -c $< -o $@

# -------------------------
# Assemble kernel-side asm objects (elf64)
# -------------------------
$(ENTRY_OBJ): $(ENTRY_ASM) | $(BUILD)
	@echo "Assembling $(ENTRY_ASM) (64-bit)..."
	$(AS) -f elf64 $< -o $@

$(PAGING_OBJ): $(PAGING_ASM) | $(BUILD)
	@echo "Assembling $(PAGING_ASM) (64-bit)..."
	$(AS) -f elf64 $< -o $@

$(IDT_OBJ): $(IDT_ASM) | $(BUILD)
	@echo "Assembling $(IDT_ASM) (64-bit)..."
	$(AS) -f elf64 $< -o $@

$(IRQ_OBJ): $(IRQ_ASM) | $(BUILD)
	@echo "Assembling $(IRQ_ASM) (64-bit)..."
	$(AS) -f elf64 $< -o $@

# -------------------------
# Link kernel
# -------------------------
$(STAGE2_ELF): $(ENTRY_OBJ) $(PAGING_OBJ) $(IDT_OBJ) $(IRQ_OBJ) $(C_OBJECTS)
	@echo "Linking 64-bit kernel..."
	$(LD) -m elf_x86_64 -T $(STAGE2_LD) -nostdlib \
		$(ENTRY_OBJ) $(PAGING_OBJ) $(IDT_OBJ) $(IRQ_OBJ) $(C_OBJECTS) \
		-o $@

# -------------------------
# Kernel binary
# -------------------------
$(STAGE2_BIN): $(STAGE2_ELF)
	@echo "Creating 64-bit kernel binary..."
	$(OBJCOPY) -O binary $< $@
	@echo "Kernel size: $$(wc -c < $@) bytes"

# -------------------------
# Stage1.5 (two-pass)
# Pass 1: build raw WITHOUT padding
# -------------------------
$(STAGE15_RAW): $(STAGE15_ASM) $(STAGE2_BIN) | $(BUILD)
	@if [ -f $(STAGE2_BIN) ]; then \
		S2SEC=$$(( ($$(stat -c%s $(STAGE2_BIN)) + 511) / 512 )); \
	else \
		S2SEC=$(DEFAULT_STAGE2_SECTORS); \
	fi; \
	echo "Assembling stage1.5 raw (no padding), stage2=$$S2SEC sectors"; \
	$(AS) -f bin -DNO_STAGE15_PAD=1 -DSTAGE2_SECTORS=$$S2SEC $(STAGE15_ASM) -o $(STAGE15_RAW); \
	echo "Stage1.5 raw size: $$(wc -c < $(STAGE15_RAW)) bytes"

# Compute stage1.5 sectors from raw
$(STAGE15_SECF): $(STAGE15_RAW) | $(BUILD)
	@SZ=$$(stat -c%s $(STAGE15_RAW)); \
	SECT=$$(( ($$SZ + 511) / 512 )); \
	echo $$SECT > $(STAGE15_SECF); \
	echo "Stage1.5 sectors (auto): $$SECT"

# Pass 2: build final WITH exact padding to STAGE15_SECTORS
$(STAGE15_BIN): $(STAGE15_ASM) $(STAGE2_BIN) $(STAGE15_SECF) | $(BUILD)
	@S15SEC=$$(cat $(STAGE15_SECF)); \
	if [ -f $(STAGE2_BIN) ]; then \
		S2SEC=$$(( ($$(stat -c%s $(STAGE2_BIN)) + 511) / 512 )); \
	else \
		S2SEC=$(DEFAULT_STAGE2_SECTORS); \
	fi; \
	echo "Building stage1.5: $$S15SEC sectors, stage2=$$S2SEC sectors"; \
	$(AS) -f bin -DSTAGE15_SECTORS=$$S15SEC -DSTAGE2_SECTORS=$$S2SEC $(STAGE15_ASM) -o $(STAGE15_BIN); \
	echo "Stage1.5 final size: $$(wc -c < $(STAGE15_BIN)) bytes"

# -------------------------
# Stage1 boot sector (MBR) - WITH VERIFICATION
# -------------------------
$(BOOT_BIN): $(BOOT_ASM) $(STAGE2_BIN) $(STAGE15_BIN) $(STAGE15_SECF) | $(BUILD)
	@S15SEC=$$(cat $(STAGE15_SECF)); \
	if [ -f $(STAGE2_BIN) ]; then \
		S2SEC=$$(( ($$(stat -c%s $(STAGE2_BIN)) + 511) / 512 )); \
	else \
		S2SEC=$(DEFAULT_STAGE2_SECTORS); \
	fi; \
	echo "Building boot sector: stage15=$$S15SEC sectors, stage2=$$S2SEC sectors"; \
	$(AS) -f bin -DSTAGE15_SECTORS=$$S15SEC -DSTAGE2_SECTORS=$$S2SEC $(BOOT_ASM) -o $(BOOT_BIN); \
	BOOT_SIZE=$$(stat -c%s $(BOOT_BIN) 2>/dev/null || stat -f%z $(BOOT_BIN)); \
	if [ "$$BOOT_SIZE" != "512" ]; then \
		echo "ERROR: Boot sector is $$BOOT_SIZE bytes, must be 512!"; \
		exit 1; \
	fi; \
	BOOT_SIG=$$(od -An -tx2 -j510 -N2 $(BOOT_BIN) | tr -d ' '); \
	if [ "$$BOOT_SIG" != "aa55" ]; then \
		echo "ERROR: Boot signature is 0x$$BOOT_SIG, must be 0xaa55!"; \
		exit 1; \
	fi; \
	echo "Boot sector verified: 512 bytes, signature 0xAA55"

# -------------------------
# Disk image layout:
#   LBA 0: boot.bin
#   LBA 1.. : stage15.bin (STAGE15_SECTORS)
#   Next    : stage2.bin  (STAGE2_SECTORS)
#   + some slack
# -------------------------
$(DISK_IMG): $(BOOT_BIN) $(STAGE15_BIN) $(STAGE2_BIN) $(STAGE15_SECF) | $(BUILD)
	@S15SEC=$$(cat $(STAGE15_SECF)); \
	if [ -f $(STAGE2_BIN) ]; then \
		S2SEC=$$(( ($$(stat -c%s $(STAGE2_BIN)) + 511) / 512 )); \
	else \
		S2SEC=$(DEFAULT_STAGE2_SECTORS); \
	fi; \
	TOTAL=$$(( 1 + $$S15SEC + $$S2SEC + 200 )); \
	echo "Creating disk image: $$TOTAL sectors total"; \
	dd if=/dev/zero of=$(DISK_IMG) bs=512 count=$$TOTAL 2>/dev/null; \
	dd if=$(BOOT_BIN)   of=$(DISK_IMG) conv=notrunc 2>/dev/null; \
	dd if=$(STAGE15_BIN) of=$(DISK_IMG) bs=512 seek=1 conv=notrunc 2>/dev/null; \
	dd if=$(STAGE2_BIN)  of=$(DISK_IMG) bs=512 seek=$$((1 + $$S15SEC)) conv=notrunc 2>/dev/null; \
	echo "Build complete!"

# -------------------------
# Run targets
# -------------------------
run: $(DISK_IMG)
	@echo "Running in QEMU..."
	$(QEMU) -m 256 \
	        -drive file=$(DISK_IMG),format=raw,if=ide,index=0 -boot c \
	        -serial mon:stdio \
	        -no-reboot -no-shutdown \
	        -d int,cpu_reset,guest_errors -D $(BUILD)/qemu.log

run-simple: $(DISK_IMG)
	@echo "Running in QEMU (simple mode)..."
	$(QEMU) -hda $(DISK_IMG)

run-debug: $(DISK_IMG)
	@echo "Running in QEMU with debug output..."
	$(QEMU) -drive file=$(DISK_IMG),format=raw,if=ide -boot c -d int,cpu_reset -no-reboot

run-serial: $(DISK_IMG)
	@echo "Running in QEMU with serial console..."
	$(QEMU) -drive file=$(DISK_IMG),format=raw,if=ide -boot c -serial stdio -no-reboot

# -------------------------
# Verify disk image
# -------------------------
verify: $(DISK_IMG)
	@echo "=== Verifying Boot Image ==="
	@echo
	@echo "1. Boot sector (boot.bin):"
	@BOOT_SIZE=$$(stat -c%s $(BOOT_BIN) 2>/dev/null || stat -f%z $(BOOT_BIN)); \
	echo "   Size: $$BOOT_SIZE bytes"; \
	if [ "$$BOOT_SIZE" = "512" ]; then echo "   Correct size"; else echo "   Wrong size!"; fi
	@BOOT_SIG=$$(od -An -tx2 -j510 -N2 $(BOOT_BIN) | tr -d ' '); \
	echo "   Signature: 0x$$BOOT_SIG"; \
	if [ "$$BOOT_SIG" = "aa55" ]; then echo "   Valid signature"; else echo "  Invalid signature!"; fi
	@echo
	@echo "2. Disk image (os.img):"
	@IMG_SIZE=$$(stat -c%s $(DISK_IMG) 2>/dev/null || stat -f%z $(DISK_IMG)); \
	echo "   Size: $$IMG_SIZE bytes"
	@IMG_SIG=$$(od -An -tx2 -j510 -N2 $(DISK_IMG) | tr -d ' '); \
	echo "   Signature: 0x$$IMG_SIG"; \
	if [ "$$IMG_SIG" = "aa55" ]; then echo "   Valid signature"; else echo "   Invalid signature!"; fi
	@echo
	@echo "3. First 32 bytes of disk:"
	@od -Ax -tx1z -N32 $(DISK_IMG)
	@echo

# -------------------------
# Clean
# -------------------------
clean:
	@echo "Cleaning build files..."
	rm -f $(BUILD)/*.bin $(BUILD)/*.o $(BUILD)/*.elf $(BUILD)/*.img $(BUILD)/*.raw $(BUILD)/*.sectors
	rm -rf $(BUILD)/kernel $(BUILD)/bootloader $(BUILD)/src $(BUILD)/include
	@echo "Clean complete!"
