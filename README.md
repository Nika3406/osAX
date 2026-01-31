# OSAX Documentation

### Objects are truths, Paths are views

OSAX is an operating system that rejects the traditional norms of paths and relies on every files (Objects) metadata to run, build, and transport. Directories are considered views which is part of the metadata of each Object. 

# 1. Bootloader

## Overview

This is a multi-stage x86_64 bootloader that transitions from BIOS real mode through 32-bit protected mode to 64-bit long mode, ultimately loading and executing a higher-level kernel. The bootloader is designed to be loaded from a disk image and consists of three main stages.

## Architecture Summary

```
┌─────────────────────────────────────────────────────────────────┐
│                      Boot Process Flow                          │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  BIOS  →  Stage 1 (MBR)  →  Stage 1.5  →  Stage 2 (Kernel)      │
│           512 bytes         ~32KB          Variable size        │
│           @ 0x7C00          @ 0x7E00       @ 0x100000           │
│                                                                 │
│  Real Mode  →  Real Mode  →  PM32 → LM64  →  Long Mode          │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

## Disk Layout

The bootloader expects a specific on-disk layout using LBA (Logical Block Addressing):

```
LBA 0:      boot.bin (Stage 1 - MBR, 512 bytes)
LBA 1-N:    stage15.bin (Stage 1.5, typically 64 sectors / 32KB)
LBA N+1-M:  stage2.bin (Kernel, typically 168 sectors / 86KB)
```

The sector counts are automatically calculated during build based on actual file sizes, ensuring efficient use of disk space.

---

## Stage 1: MBR (Master Boot Record)

**File:** `boot.asm`  
**Size:** Exactly 512 bytes  
**Load Address:** `0x7C00` (loaded by BIOS)  
**Mode:** 16-bit Real Mode

### Purpose
The MBR is the first code executed when the system boots. Its sole responsibility is to load Stage 1.5 from disk into memory and transfer control to it.

### Process

1. **Initialization**
   - Clear interrupts (`cli`)
   - Set up segment registers (DS, ES, SS) to zero
   - Initialize stack pointer to `0x7C00` (grows downward)
   - Save boot drive number from DL register (provided by BIOS)

2. **Load Stage 1.5**
   - Uses BIOS INT 13h Extensions (function 0x42) for LBA disk access
   - Creates a Disk Address Packet (DAP) structure containing:
     - Number of sectors to read (64 sectors / 32KB)
     - Starting LBA (1, immediately after the MBR)
     - Destination address (`0x7E00`)
   - Reads Stage 1.5 from disk into memory at `0x7E00`

3. **Transfer Control**
   - If read succeeds: jump to `0x0000:0x7E00` (Stage 1.5 entry point)
   - If read fails: display error message and halt

### Technical Details

- **Boot Signature:** The last two bytes are `0xAA55`, required by BIOS to recognize a valid boot sector
- **DAP Structure:** Uses the BIOS extended read function which supports LBA addressing beyond CHS limitations
- **Error Handling:** Simple error display using BIOS INT 10h (video services)

---

## Stage 1.5: Intermediate Bootloader

**File:** `stage15.asm`  
**Size:** ~32KB (64 sectors, auto-calculated)  
**Load Address:** `0x7E00`  
**Mode:** Starts in 16-bit Real Mode, transitions to 32-bit Protected Mode, then 64-bit Long Mode

### Purpose
Stage 1.5 is the workhorse of the bootloader. It handles memory detection, enables the A20 line, loads the kernel from disk, sets up paging, and transitions to 64-bit long mode.

### Process Overview

#### 1. Real Mode Setup (16-bit)

**Initialization:**
- Clear interrupts
- Reset segment registers (DS, ES, SS)
- Set up stack at `0x7E00`
- Save boot drive number

**Enable A20 Line:**
- Uses the fast A20 gate method (port 0x92)
- A20 line must be enabled to access memory above 1MB
- Without this, address line 20 is masked, limiting accessible memory

**Memory Detection (E820):**
- Uses BIOS INT 15h, function E820h to detect available memory regions
- Iterates through all memory map entries
- Finds the highest usable memory address
- Calculates total system memory in MB
- Stores result at `0x9000` (BOOTINFO_MEM_MB)
- Default minimum: 16MB if detection fails

#### 2. Load Kernel from Disk

**Bounce Buffer Strategy:**
The Stage 2 kernel must reside at `0x100000` (1MB), but BIOS INT 13h cannot reliably DMA to addresses above 1MB using real mode segment:offset addressing.

**Solution:**
- Load kernel in chunks to a "bounce buffer" at `0x10000` (64KB, below 1MB)
- Later copy from bounce buffer to final location at `0x100000` in protected mode

**Loading Process:**
- Calculate starting LBA: `1 + STAGE15_SECTORS` (immediately after Stage 1.5)
- Read kernel in conservative chunks (127 sectors max per read)
- Uses INT 13h Extensions with DAP structure
- Converts linear addresses to segment:offset for each chunk
- Advances through all required sectors until kernel is fully loaded

#### 3. Transition to 32-bit Protected Mode

**Setup GDT (Global Descriptor Table):**
- Defines memory segments for protected mode:
  - Null descriptor (required by x86)
  - 32-bit code segment (base=0, limit=4GB, readable/executable)
  - 32-bit data segment (base=0, limit=4GB, readable/writable)
  - 64-bit code segment (for long mode)
  - 64-bit data segment (for long mode)

**Enter Protected Mode:**
- Load GDT using `lgdt` instruction
- Set bit 0 of CR0 register (PE - Protection Enable)
- Far jump to 32-bit code segment to flush prefetch queue
- Update all segment registers to data segment selector

#### 4. Copy Kernel to Final Location (32-bit)

**Memory Copy:**
- Source: `0x10000` (bounce buffer)
- Destination: `0x100000` (1MB - kernel's expected location)
- Size: STAGE2_SECTORS × 512 bytes
- Uses `rep movsd` for efficient 32-bit copy

#### 5. Setup Identity Paging for 64-bit Mode

**Paging Structure:**
Creates a four-level page table hierarchy:
- **PML4** (Page Map Level 4): Top level, 512 entries
- **PDP** (Page Directory Pointer): Third level, 512 entries
- **PD** (Page Directory): Second level, 512 entries using 2MB pages
- No PT (Page Tables) needed - using 2MB huge pages

**Identity Mapping:**
- Maps first 1GB of physical memory 1:1 (virtual = physical)
- Uses 2MB pages (PSE - Page Size Extension)
- Each PD entry covers 2MB: 512 entries × 2MB = 1GB
- Page flags: Present (0x1), Writable (0x2), Page Size (0x80)
- Combined flags: `0x83` per entry

**Why Identity Mapping?**
After enabling paging, the CPU requires virtual addresses. Identity mapping ensures that:
- Code can continue executing at its current physical address
- No complex virtual memory management needed during boot
- Kernel can set up its own page tables later

#### 6. Enter 64-bit Long Mode

**Requirements for Long Mode:**
1. Identity paging established
2. Enable PAE (Physical Address Extension) in CR4
3. Set LME (Long Mode Enable) in EFER MSR
4. Load CR3 with PML4 address
5. Enable paging by setting PG bit in CR0

**Process:**
- Set CR4.PAE (bit 5) - required for 64-bit paging
- Read EFER MSR (0xC0000080), set bit 8 (LME), write back
- Load PML4 address into CR3
- Set CR0.PG (bit 31) - enable paging
- Far jump to 64-bit code segment

#### 7. Jump to Kernel (64-bit)

**Final Steps:**
- Load detected memory size (MB) into EAX register
- Load kernel entry point address (`0x100000`) into RBX
- Jump to kernel entry point

---

## Stage 2: Kernel Entry Point

**File:** `entry.asm`  
**Load Address:** `0x100000` (1MB)  
**Mode:** 64-bit Long Mode

### Purpose
The kernel entry point sets up a clean 64-bit environment and transfers control to the C kernel code.

### Process

1. **Disable Interrupts**
   - Ensures no interrupts occur during setup
   - IDT will be configured later by kernel

2. **Setup Stack**
   - Allocate 64KB stack in BSS section
   - Align stack pointer to 16-byte boundary (ABI requirement)
   - Stack grows downward from `stack_top`

3. **Store Boot Information**
   - Save detected memory size (from EAX) to global variable
   - Initialize framebuffer variables to zero (VGA text mode used instead)
   - These globals are accessible from C code

4. **Call C Main**
   - Jump to `c_main` function (implemented in C)
   - From here, the kernel takes over completely

5. **Halt Loop**
   - If `c_main` ever returns, enter infinite halt loop
   - Prevents execution of invalid code

### Global Variables Exposed to C

```c
uint32_t detected_memory_size;   // System memory in MB
uint64_t framebuffer_address;    // Graphics framebuffer (0 = text mode)
uint64_t framebuffer_width;      // Screen width in pixels
uint64_t framebuffer_height;     // Screen height in pixels
uint64_t framebuffer_pitch;      // Bytes per scanline
```

---

## Interrupt Handling

**File:** `idt_asm.asm`  
**Purpose:** Low-level interrupt service routine stubs

### Design

The bootloader includes infrastructure for handling CPU exceptions and interrupts in 64-bit mode.

**ISR Stubs (0-31):**
- 32 exception handlers for CPU exceptions
- Some exceptions push error codes (8, 10-14, 17), others don't
- Each stub pushes interrupt number and optional error code
- All converge on common handler routine

**Common Handler:**
- Saves all general-purpose registers (15 registers)
- Calls C function `isr_handler(int_no, err_code)`
- Restores all registers
- Adjusts stack (removes int_no and err_code)
- Returns from interrupt with `iretq`

**Exception Types:**
- Division Error (0)
- Debug (1)
- Page Fault (14)
- General Protection Fault (13)
- Double Fault (8)
- And 27 others

---

## Build System

**File:** `Makefile`  
**Build Tool:** GNU Make

### Two-Pass Assembly

The bootloader uses a sophisticated two-pass build process to automatically calculate sector sizes:

**Pass 1: Size Determination**
1. Compile all C sources to object files
2. Link kernel ELF binary
3. Convert to flat binary (stage2.bin)
4. Assemble stage15.asm WITHOUT padding
5. Calculate required sectors for stage1.5
6. Store sector count in `stage15.sectors`

**Pass 2: Final Assembly**
1. Read calculated sector counts
2. Assemble stage15.asm WITH exact padding
3. Assemble boot.asm with correct STAGE15_SECTORS define
4. Verify boot sector is exactly 512 bytes with 0xAA55 signature

### Disk Image Creation

```bash
# Layout:
dd if=/dev/zero of=os.img bs=512 count=TOTAL
dd if=boot.bin of=os.img conv=notrunc                    # LBA 0
dd if=stage15.bin of=os.img bs=512 seek=1 conv=notrunc   # LBA 1+
dd if=stage2.bin of=os.img bs=512 seek=N conv=notrunc    # After stage1.5
```

### Verification

The build system includes automatic verification:
- Boot sector must be exactly 512 bytes
- Boot signature must be 0xAA55
- Sector alignment checked
- Size calculations verified

---

## Memory Map

### After Stage 1.5 Completes

```
0x00000000 - 0x000003FF : Real Mode IVT (Interrupt Vector Table)
0x00000400 - 0x000004FF : BIOS Data Area
0x00000500 - 0x00007BFF : Free (used by bootloader temporarily)
0x00007C00 - 0x00007DFF : Stage 1 (MBR) loaded here by BIOS
0x00007E00 - 0x0000FFFF : Stage 1.5 code
0x00009000 - 0x00009003 : Boot info (detected memory size)
0x00010000 - 0x0002FFFF : Bounce buffer (kernel loaded here in RM)
0x00100000 - 0x001FFFFF : Stage 2 (kernel) final location
0x00200000+            : Heap and free memory (managed by kernel)

Identity-mapped to same virtual addresses in 64-bit mode
```

### Paging Tables (Stage 1.5)

```
PML4:  0x????? (12KB allocated for all page tables)
PDP:   PML4 + 4096
PD:    PML4 + 8192

Identity mapped: 0x00000000 - 0x3FFFFFFF (1GB)
Using 2MB pages for efficiency
```

---

## Technical Specifications

### CPU Mode Transitions

1. **Real Mode (16-bit)**
   - Segmented memory model
   - 1MB addressable (with A20 hack)
   - BIOS services available
   - No memory protection

2. **Protected Mode (32-bit)**
   - Flat memory model (with GDT)
   - 4GB addressable
   - Memory protection
   - No BIOS services

3. **Long Mode (64-bit)**
   - 64-bit addressing
   - Paging required
   - Extended registers (R8-R15)
   - No real mode or BIOS

### Key Registers

- **CR0:** Control Register 0 (PE=Protected Enable, PG=Paging)
- **CR3:** Page Directory Base Register
- **CR4:** Extended features (PAE=Physical Address Extension)
- **EFER:** Extended Feature Enable Register (LME=Long Mode Enable)
- **GDTR:** Global Descriptor Table Register

### Compiler Flags

```
-m64                : 64-bit code generation
-mcmodel=kernel     : Kernel code model (high 2GB)
-mno-red-zone       : Disable red zone (for interrupt safety)
-mno-mmx/sse        : Disable floating point (not initialized)
-ffreestanding      : Freestanding environment
-nostdinc           : No standard includes
-fno-builtin        : No built-in functions
```

---

## Debugging

### QEMU Run Targets

```bash
make run         # Full debugging with interrupt logging
make run-simple  # Minimal output
make run-debug   # CPU reset and interrupt debugging
make run-serial  # Serial console output
```

### Verification

```bash
make verify      # Check boot sector signature and disk layout
```

### Debug Output

QEMU logs are written to `build/qemu.log` and include:
- Interrupt events
- CPU state on reset
- Guest errors

### Common Issues

**Boot failure:**
- Check boot signature: `xxd -l 512 build/boot.bin | tail -2`
- Should end with: `01f0: ... aa55`

**Stage 1.5 not loading:**
- Verify LBA layout matches in boot.asm and stage15.asm
- Check sector count calculations

**Kernel not executing:**
- Ensure stage2.bin is at correct LBA
- Verify identity paging covers kernel location (0x100000)
- Check kernel entry point in linker script

---

## Linker Script

**File:** `stage2.ld`  
**Purpose:** Controls kernel memory layout

```ld
ENTRY(_start)           /* Entry point is _start in entry.asm */

SECTIONS {
    . = 0x100000;       /* Kernel loads at 1MB */
    
    .text : {           /* Code section */
        *(.text)
    }
    
    .rodata : {         /* Read-only data */
        *(.rodata*)
    }
    
    .data : {           /* Initialized data */
        *(.data)
    }
    
    .bss : {            /* Uninitialized data */
        *(.bss)
        *(COMMON)
    }
}
```

---

## Dependencies

### Build Tools

- **NASM:** Netwide Assembler for x86 assembly
- **GCC:** Cross-compiler (x86_64-elf-gcc)
- **GNU LD:** Linker for ELF binaries
- **objcopy:** Binary extraction from ELF
- **QEMU:** Emulator for testing

### Installing (Ubuntu/Debian)

```bash
sudo apt install nasm qemu-system-x86 build-essential

# Cross-compiler (may need to build from source)
# Or use existing x86_64 gcc if on x86_64 Linux
```

---

## Future Enhancements

Possible improvements to this bootloader:

1. **Graphics Support:** Add VESA VBE mode setting in stage1.5
2. **Filesystem Support:** Load kernel from filesystem instead of fixed LBA
3. **Multiboot2:** Make compliant with Multiboot2 specification
4. **EFI Support:** Add UEFI boot path alongside BIOS
5. **Compression:** Compress stage2 and decompress in stage1.5
6. **Disk Drivers:** Add AHCI support for modern SATA
7. **Error Recovery:** More robust error handling and retry logic

---

## References

- Intel 64 and IA-32 Architectures Software Developer's Manual
- OSDev Wiki: https://wiki.osdev.org
- BIOS Interrupt Call: https://en.wikipedia.org/wiki/BIOS_interrupt_call

---

## License

See project LICENSE file for licensing information.

## Contributing

When modifying the bootloader, always:
1. Update sector count calculations if changing stage sizes
2. Run `make verify` after building
3. Test in QEMU before real hardware
4. Document any changes to memory layout or calling conventions
5. Maintain compatibility with existing kernel entry point

---

# 2. Kernel

## Overview

OSAX (Objects as Truth, Paths as Views) is a modern 64-bit operating system kernel written in C and assembly for x86_64 architecture. The kernel implements a unique object-based filesystem philosophy where files are identified by immutable ObjectIDs rather than traditional hierarchical paths.

```
┌─────────────────────────────────────────────────────────────┐
│                    OSAX Kernel Architecture                 │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐       │
│  │   Shell &    │  │   Drivers    │  │   MetaFS     │       │
│  │   System     │  │   Terminal   │  │   Layer      │       │
│  │   Services   │  │   Keyboard   │  │              │       │
│  └──────────────┘  └──────────────┘  └──────────────┘       │
│                                                             │
│  ┌──────────────────────────────────────────────────────┐   │
│  │              Filesystem Layer (exFAT)                │   │
│  └──────────────────────────────────────────────────────┘   │
│                                                             │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐       │
│  │   Process    │  │   Virtual    │  │   Physical   │       │
│  │   Manager    │  │   Memory     │  │   Memory     │       │
│  └──────────────┘  └──────────────┘  └──────────────┘       │
│                                                             │
│  ┌──────────────────────────────────────────────────────┐   │
│  │              Core (IDT, IRQ, Exceptions)             │   │
│  └──────────────────────────────────────────────────────┘   │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

## Philosophy: Objects vs Paths

Traditional filesystems use hierarchical paths (`/home/user/document.txt`) which are:
- Fragile (renaming breaks references)
- Ambiguous (multiple paths can point to same file via symlinks)
- Coupled to storage structure

OSAX uses **ObjectIDs** (128-bit UUIDs) as the source of truth:
- Immutable identity
- Location-independent
- Can be accessed through multiple "views" (filtered directories)

**Example:**
```
ObjectID: 1a2b3c4d5e6f7890abcdef0123456789
Type: DOCUMENT
Views:
  - /documents/  (all documents)
  - /recent/     (recently modified)
  - /work/       (tagged as work-related)
```

---

## Directory Structure

```
src/
├── bootloader/          # Boot process (see BOOTLOADER_README.md)
│   ├── boot.asm         # Stage 1 MBR
│   ├── stage15.asm      # Stage 1.5 loader
│   ├── entry.asm        # 64-bit kernel entry
│   └── *.asm            # Interrupt/paging stubs
│
└── kernel/
    ├── core/            # Core kernel functionality
    │   ├── stage2.c     # Early kernel init
    │   ├── main.c       # Main kernel entry (os_main)
    │   ├── idt.c        # Interrupt Descriptor Table
    │   ├── process.c    # Process management
    │   └── executable.c # ELF loader
    │
    ├── memory/          # Memory management subsystem
    │   ├── physical_mm.c   # Physical page allocator
    │   ├── paging.c        # Virtual memory (4-level paging)
    │   ├── heap.c          # Kernel heap allocator
    │   ├── memory.c        # Memory detection
    │   └── dma.c           # DMA buffer pool (<1MB)
    │
    ├── drivers/         # Hardware drivers
    │   ├── terminal.c   # VGA text mode driver
    │   ├── keyboard.c   # PS/2 keyboard driver
    │   └── serial.c     # Serial port (COM1)
    │
    ├── fs/              # Filesystem implementations
    │   ├── exfat/       # exFAT implementation
    │   │   ├── exfat.c
    │   │   └── exfat_fileops.c
    │   └── metafs/      # Object-based metadata layer
    │       ├── metafs.c
    │       └── metafs_wrappers.c
    │
    ├── system/          # System services
    │   ├── system.c     # Boot/shutdown management
    │   ├── logger.c     # File-based logging
    │   └── shell.c      # Interactive shell
    │
    └── lib/             # Utility libraries
        ├── string.c     # String manipulation
        └── kstring_util.c  # Printf/scanf helpers
```

---

## Boot Process

### Stage 2 Initialization (`core/stage2.c`)

After the bootloader transfers control, the kernel performs early initialization:

1. **Serial Port** - Initialize COM1 for debugging output
2. **IDT** - Set up Interrupt Descriptor Table for exceptions and IRQs
3. **Memory Detection** - Read memory size passed from bootloader (via EAX)
4. **Physical Memory Manager** - Initialize page frame allocator
5. **Heap** - Create initial kernel heap (16MB, physical mode)
6. **DMA** - Reserve 0x10000-0x9FFFF for DMA buffers (576KB)
7. **Paging** - Enable 64-bit virtual memory (4-level page tables)
8. **Virtual Heap** - Switch heap to virtual memory mode
9. **exFAT Disk** - Allocate 10MB in-memory disk buffer
10. **Transfer to Main** - Call `os_main()` to start high-level kernel

```c
void c_main(void) {
    serial_init();
    idt_init();
    
    uint32_t mem_mb = detect_memory();
    physical_mm_init(mem_mb);
    heap_init();
    dma_init();          // Reserve low memory for DMA
    
    paging_init();       // Enable virtual memory
    heap_init_virtual();
    kernel_heap_init();
    exfat_set_paging_mode();
    exfat_init_disk(10); // 10MB disk
    
    os_main();           // High-level kernel
}
```

### Main Kernel Entry (`core/main.c`)

The main kernel performs high-level system initialization:

1. **Terminal** - VGA text mode output
2. **Boot Splash** - Display OSAX banner
3. **PIC** - Initialize Programmable Interrupt Controller
4. **Keyboard** - PS/2 keyboard driver with IRQ1
5. **System Boot** - Mount filesystem or perform first-boot setup
6. **Shell** - Start interactive command shell
7. **Main Loop** - Process user commands until shutdown

```c
void os_main(void) {
    terminal_init();
    boot_splash();
    
    pic_init();
    keyboard_init();
    
    exfat_volume_t* volume = kmalloc(sizeof(exfat_volume_t));
    metafs_context_t* metafs = system_boot(volume);
    
    shell_init(metafs);
    
    // Main loop
    while (1) {
        shell_prompt();
        keyboard_readline(line, sizeof(line));
        
        if (strcmp(line, "shutdown") == 0) {
            system_shutdown();
            break;
        }
        
        shell_execute(line);
    }
}
```

---

## Memory Management

### Physical Memory Manager (`memory/physical_mm.c`)

Manages physical RAM using a bitmap allocator.

**Features:**
- Page-level allocation (4KB pages)
- Bitmap-based free page tracking
- Reserves kernel code/data and DMA region
- Thread-safe allocation

**API:**
```c
void physical_mm_init(uint32_t mem_mb);
void* alloc_page(void);              // Allocate single page
void* alloc_pages(uint32_t count);   // Allocate contiguous pages
void free_page(void* page);
```

**Memory Layout:**
```
0x00000000 - 0x0000FFFF : Reserved (Real Mode IVT, BIOS)
0x00010000 - 0x0009FFFF : DMA Buffer Pool (576 KB)
0x000A0000 - 0x000FFFFF : VGA/BIOS ROM
0x00100000 - 0x001FFFFF : Kernel Code/Data
0x00200000+             : Heap, User Memory
```

### Virtual Memory (`memory/paging.c`)

Implements x86_64 4-level paging with identity mapping for kernel.

**Page Table Hierarchy:**
```
PML4 (Page Map Level 4)
  └─> PDP (Page Directory Pointer)
      └─> PD (Page Directory)
          └─> PT (Page Table)
              └─> 4KB Physical Page
```

**Features:**
- Identity mapping for first 32MB (kernel space)
- 2MB huge pages for efficiency
- Virtual address space: 0xFFFF800000000000 - 0xFFFFFFFFFFFFFFFF
- Kernel heap: 0xFFFF800000000000+
- Page fault handler with detailed error reporting

**API:**
```c
void paging_init(void);
void map_page(page_directory_t* pml4, uint64_t virt, uint64_t phys, uint64_t flags);
void unmap_page(page_directory_t* pml4, uint64_t virt);
void switch_page_directory(page_directory_t* pml4);
```

**Page Flags:**
```c
#define PAGE_PRESENT   0x1
#define PAGE_WRITABLE  0x2
#define PAGE_USER      0x4
```

### Kernel Heap (`memory/heap.c`)

Dynamic memory allocator with support for both physical and virtual memory.

**Two-Phase Operation:**

**Phase 1: Physical Mode (Before Paging)**
- Direct physical memory allocation
- Used for early kernel structures
- Limited by identity mapping (32MB)

**Phase 2: Virtual Mode (After Paging)**
- Virtual memory allocation
- Can allocate beyond identity-mapped region
- Uses demand paging

**Features:**
- First-fit allocation strategy
- Block splitting and coalescing
- Automatic heap expansion
- Alignment support

**API:**
```c
void* kmalloc(size_t size);
void* kmalloc_aligned(size_t size, size_t alignment);
void* krealloc(void* ptr, size_t new_size);
void  kfree(void* ptr);
```

**Virtual Memory Extensions:**
```c
void* kmalloc_virtual(size_t size);   // Allocate in virtual address space
void  kfree_virtual(void* ptr, size_t size);
```

### DMA Buffer Pool (`memory/dma.c`)

Manages DMA-capable memory below 1MB for legacy ISA devices.

**Why DMA Buffers?**
- ISA devices can only access first 16MB (24-bit addressing)
- Modern devices often require buffers below 1MB for compatibility
- Used for disk I/O, audio, USB, networking

**Reserved Region:**
```
0x00010000 - 0x0009FFFF (576 KB)
```

**Features:**
- 4KB-aligned allocations
- Block splitting and coalescing
- Usage tracking

**API:**
```c
void  dma_init(void);
void* dma_alloc(uint32_t size);
void  dma_free(void* ptr);
void  dma_get_stats(uint32_t* total, uint32_t* used, uint32_t* free);
```

---

## Interrupt Handling

### Interrupt Descriptor Table (`core/idt.c`)

Manages CPU exceptions and hardware interrupts.

**IDT Structure (64-bit):**
```c
typedef struct {
    uint16_t offset_low;    // Handler offset bits 0-15
    uint16_t selector;      // Code segment selector
    uint8_t  ist;           // Interrupt Stack Table
    uint8_t  type_attr;     // Type and attributes
    uint16_t offset_mid;    // Handler offset bits 16-31
    uint32_t offset_high;   // Handler offset bits 32-63
    uint32_t reserved;
} __attribute__((packed)) idt_entry_t;
```

**Exception Handlers (0-31):**
- Division by Zero (0)
- Debug (1)
- Page Fault (14) - Special handling with CR2 register
- General Protection Fault (13)
- Double Fault (8)
- etc.

**IRQ Handlers (32+):**
- IRQ0 (32): Timer
- IRQ1 (33): Keyboard
- IRQ14 (46): Primary IDE
- IRQ15 (47): Secondary IDE

**Assembly Stubs (`idt_asm.asm`):**
```nasm
ISR_NOERR 0    ; Division by Zero (no error code)
ISR_ERR   8    ; Double Fault (CPU pushes error code)
ISR_ERR   14   ; Page Fault (CPU pushes error code)
```

**Common Handler:**
```nasm
isr_common:
    PUSH_REGS              ; Save all registers
    mov rdi, [rsp+120]     ; int_no
    mov rsi, [rsp+120+8]   ; err_code
    call isr_handler       ; Call C handler
    POP_REGS
    add rsp, 16            ; Clean stack
    iretq
```

### Page Fault Handler

The page fault handler provides detailed diagnostics:

```c
void page_fault_handler(uint64_t error_code) {
    uint64_t faulting_address = read_cr2();
    
    kprintf("!!! PAGE FAULT !!!\n");
    kprintf("Faulting address: 0x%llx\n", faulting_address);
    kprintf("Error code: 0x%llx\n", error_code);
    kprintf("  Present: %d\n", error_code & 0x1);
    kprintf("  Write: %d\n", (error_code & 0x2) >> 1);
    kprintf("  User: %d\n", (error_code & 0x4) >> 2);
    kprintf("  Reserved: %d\n", (error_code & 0x8) >> 3);
    kprintf("  Instruction fetch: %d\n", (error_code & 0x10) >> 4);
    
    for(;;) __asm__ volatile("cli; hlt");
}
```

---

## Drivers

### Terminal Driver (`drivers/terminal.c`)

VGA text mode (80x25) terminal with color support.

**Features:**
- 16 foreground / 8 background colors
- Hardware scrolling
- Printf-style formatted output
- Cursor positioning

**API:**
```c
void terminal_init(void);
void terminal_putchar(char c);
void terminal_write(const char* str);
void terminal_writeln(const char* str);
void terminal_printf(const char* format, ...);
void terminal_setcolor(uint8_t fg, uint8_t bg);
void terminal_clear(void);
```

**VGA Colors:**
```c
typedef enum {
    VGA_COLOR_BLACK = 0,
    VGA_COLOR_BLUE = 1,
    VGA_COLOR_GREEN = 2,
    VGA_COLOR_CYAN = 3,
    VGA_COLOR_RED = 4,
    VGA_COLOR_MAGENTA = 5,
    VGA_COLOR_BROWN = 6,
    VGA_COLOR_LIGHT_GREY = 7,
    VGA_COLOR_DARK_GREY = 8,
    VGA_COLOR_LIGHT_BLUE = 9,
    VGA_COLOR_LIGHT_GREEN = 10,
    VGA_COLOR_LIGHT_CYAN = 11,
    VGA_COLOR_LIGHT_RED = 12,
    VGA_COLOR_LIGHT_MAGENTA = 13,
    VGA_COLOR_YELLOW = 14,
    VGA_COLOR_WHITE = 15
} vga_color;
```

### Keyboard Driver (`drivers/keyboard.c`)

PS/2 keyboard driver with interrupt-driven input.

**Features:**
- IRQ1 interrupt handling
- Scancode to ASCII conversion
- Modifier key support (Shift, Ctrl, Alt, Caps Lock)
- Line editing (backspace, enter)
- Non-blocking and blocking reads

**API:**
```c
void keyboard_init(void);
int keyboard_available(void);
uint8_t keyboard_getkey(void);        // Get scancode
char keyboard_getchar(void);          // Get ASCII character
int keyboard_readline(char* buffer, int max_len);
```

**Scancode Translation:**
```c
// US QWERTY layout
static const char scancode_to_ascii[57] = {
    0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' '
};
```

### Serial Port Driver (`drivers/serial.c`)

COM1 serial port for debugging output.

**Features:**
- 38400 baud, 8N1
- Printf-style output
- Hex/decimal number formatting
- Non-blocking transmit

**API:**
```c
void serial_init(void);
void serial_putc(char c);
void serial_puts(const char* str);
void serial_put_hex(uint32_t val);
void serial_put_dec(uint32_t val);
void kprintf(const char* format, ...);  // Main debug output
```

**Usage:**
```c
kprintf("Memory: %d MB detected\n", mem_mb);
kprintf("Page directory at: %x\n", page_dir);
```

---

## Filesystem

### exFAT Implementation (`fs/exfat/`)

In-memory exFAT filesystem for persistent storage.

**Why exFAT?**
- Simple, modern filesystem
- No journaling overhead
- Good for embedded systems
- Widely compatible

**Features:**
- Boot sector with filesystem metadata
- File Allocation Table (FAT)
- Cluster-based storage (4KB clusters)
- Directory entries with metadata
- Support for large files (64-bit sizes)

**Key Structures:**

**Boot Sector:**
```c
typedef struct {
    uint8_t  jump_boot[3];
    char     fs_name[8];              // "EXFAT   "
    uint64_t partition_offset;
    uint64_t volume_length;
    uint32_t fat_offset;              // Sectors to FAT
    uint32_t fat_length;
    uint32_t cluster_heap_offset;
    uint32_t cluster_count;
    uint32_t root_dir_cluster;
    uint32_t volume_serial;
    uint16_t fs_revision;
    uint16_t volume_flags;
    uint8_t  bytes_per_sector_shift;  // Log2(512) = 9
    uint8_t  sectors_per_cluster_shift;
    uint8_t  number_of_fats;
    // ...
    uint16_t boot_signature;          // 0xAA55
} __attribute__((packed)) exfat_boot_sector_t;
```

**Directory Entry:**
```c
typedef struct {
    uint8_t entry_type;
    uint8_t data[31];
} __attribute__((packed)) exfat_dir_entry_t;

// Entry types
#define EXFAT_TYPE_EOD          0x00  // End of directory
#define EXFAT_TYPE_VOLUME_LABEL 0x83
#define EXFAT_TYPE_ALLOCATION   0x81  // Allocation bitmap
#define EXFAT_TYPE_FILE         0x85
#define EXFAT_TYPE_STREAM       0xC0  // File stream extension
#define EXFAT_TYPE_FILE_NAME    0xC1  // File name extension
```

**API:**
```c
// Volume operations
int exfat_format(uint32_t total_sectors);
int exfat_mount(exfat_volume_t* volume);

// Cluster operations
int exfat_read_cluster(exfat_volume_t* vol, uint32_t cluster, void* buf);
int exfat_write_cluster(exfat_volume_t* vol, uint32_t cluster, const void* buf);
uint32_t exfat_get_next_cluster(exfat_volume_t* vol, uint32_t cluster);

// File operations (in exfat_fileops.c)
int exfat_create(exfat_volume_t* vol, const char* path);
int exfat_open(exfat_volume_t* vol, const char* path, exfat_file_t* file);
int exfat_read(exfat_volume_t* vol, exfat_file_t* file, void* buf, uint32_t size);
int exfat_write(exfat_volume_t* vol, exfat_file_t* file, const void* buf, uint32_t size);
void exfat_close(exfat_file_t* file);
```

**Disk Backend:**

Currently uses in-memory buffer (10MB) for testing:
```c
static uint8_t* disk_buffer;

int disk_read_sector(uint32_t sector, void* buffer) {
    memcpy(buffer, disk_buffer + sector * 512, 512);
    return 0;
}

int disk_write_sector(uint32_t sector, const void* buffer) {
    memcpy(disk_buffer + sector * 512, buffer, 512);
    return 0;
}
```

Future: Replace with actual disk driver (IDE, AHCI, NVMe).

### MetaFS Layer (`fs/metafs/`)

Object-based metadata layer on top of exFAT.

**Core Concept:**

Every file is an **Object** with:
- **ObjectID**: 128-bit UUID (immutable)
- **Type**: Document, Image, Audio, Video, Code, etc.
- **Metadata**: Creation time, size, hash, custom fields
- **Data**: Actual file content (stored in exFAT)

Objects are accessed through **Views**:
- Views are virtual directories that filter objects by type/tags
- Multiple views can show the same object
- Views are just organizational—they don't move data

**Example:**

```
# Create an object
$ create my_essay.txt

# Object created:
ObjectID: a1b2c3d4e5f6789012345678abcdef01
Type: DOCUMENT
exFAT path: /.metafs/objects/a1b2c3d4e5f6789012345678abcdef01

# Access through views:
/documents/my_essay.txt   → links to ObjectID
/recent/my_essay.txt      → links to same ObjectID
/work/my_essay.txt        → links to same ObjectID
```

**Key Structures:**

**ObjectID:**
```c
typedef struct {
    uint32_t high;
    uint32_t low;
} object_id_t;

#define OBJECT_ID_NULL ((object_id_t){0, 0})
```

**Object Types:**
```c
typedef enum {
    OBJ_TYPE_UNKNOWN = 0,
    OBJ_TYPE_DOCUMENT,
    OBJ_TYPE_IMAGE,
    OBJ_TYPE_AUDIO,
    OBJ_TYPE_VIDEO,
    OBJ_TYPE_ARCHIVE,
    OBJ_TYPE_CODE,
    OBJ_TYPE_EXECUTABLE,
    OBJ_TYPE_DIRECTORY
} object_type_t;
```

**Metadata:**
```c
typedef struct {
    object_id_t id;
    object_type_t type;
    uint64_t size;
    uint64_t created;
    uint64_t modified;
    uint8_t hash[32];      // SHA-256
} metafs_core_meta_t;

typedef struct {
    metafs_core_meta_t core;
    // Custom metadata fields
    char custom_data[512];
} object_metadata_t;
```

**Views:**
```c
typedef enum {
    VIEW_STATIC_DOCUMENTS,
    VIEW_STATIC_IMAGES,
    VIEW_STATIC_AUDIO,
    VIEW_DYNAMIC_RECENT,
    VIEW_DYNAMIC_LARGE_FILES,
    VIEW_DYNAMIC_QUERY
} view_type_t;

typedef struct {
    char name[64];              // "documents", "images", etc.
    view_type_t type;
    object_type_t filter_type;  // Show only objects of this type
} view_definition_t;
```

**API:**
```c
// Initialization
int metafs_init(metafs_context_t* ctx, exfat_volume_t* volume);
int metafs_format(metafs_context_t* ctx);
int metafs_mount(metafs_context_t* ctx);
void metafs_sync(metafs_context_t* ctx);

// Object operations
object_id_t metafs_create(metafs_context_t* ctx, object_type_t type, 
                          const void* data, size_t size);
int metafs_read(metafs_context_t* ctx, object_id_t id, void* buf, size_t size);
int metafs_write(metafs_context_t* ctx, object_id_t id, 
                 const void* buf, size_t size);
int metafs_delete(metafs_context_t* ctx, object_id_t id);

// Metadata operations
int metafs_metadata_get(metafs_context_t* ctx, object_id_t id, 
                        object_metadata_t* meta);
int metafs_metadata_set(metafs_context_t* ctx, object_id_t id, 
                        const object_metadata_t* meta);

// View operations
int metafs_view_create(metafs_context_t* ctx, const char* name, 
                       object_type_t filter_type);
int metafs_view_list(metafs_context_t* ctx, const char* view_path, 
                     metafs_view_entry_t** entries);

// Path resolution (for shell compatibility)
object_id_t metafs_path_resolve(metafs_context_t* ctx, const char* path);
```

**Storage Layout:**

```
exFAT Volume:
  /
  ├── .kernel/                  # System files
  │   └── system.log
  │
  ├── .metafs/                  # MetaFS internal storage
  │   ├── index.dat             # Object index
  │   └── objects/              # Object storage
  │       ├── a1b2c3d4...       # Object data files
  │       └── ...
  │
  └── views/                    # View directories
      ├── documents/            # Links to document objects
      ├── images/               # Links to image objects
      └── recent/               # Links to recently modified objects
```

---

## Process Management (`core/process.c`)

Basic process management with context switching support.

**Process Control Block:**
```c
typedef struct {
    uint64_t rax, rbx, rcx, rdx;
    uint64_t rsi, rdi, rbp, rsp;
    uint64_t r8, r9, r10, r11;
    uint64_t r12, r13, r14, r15;
    uint64_t rip;
    uint64_t rflags;
    uint64_t cr3;  // Page directory
} cpu_context_t;

typedef enum {
    PROCESS_READY,
    PROCESS_RUNNING,
    PROCESS_BLOCKED,
    PROCESS_TERMINATED
} process_state_t;

typedef struct process {
    uint32_t pid;
    char name[32];
    process_state_t state;
    
    cpu_context_t context;
    page_directory_t* page_dir;
    
    uint64_t kernel_stack;
    uint64_t user_stack;
    
    uint8_t priority;
    uint32_t time_slice;
    
    struct process* next;
} process_t;
```

**API:**
```c
void process_init(void);
process_t* process_create(const char* name, void (*entry)(void), uint32_t is_kernel);
void process_destroy(process_t* proc);
void process_switch(process_t* next);
process_t* process_get_current(void);

// Scheduler
void scheduler_init(void);
void schedule(void);
void yield(void);
```

**Context Switch:**
```nasm
context_switch:
    ; Save current context
    mov [rdi + 0],  rax
    mov [rdi + 8],  rbx
    ; ... (save all registers)
    
    ; Restore new context
    mov rax, [rsi + 0]
    mov rbx, [rsi + 8]
    ; ... (restore all registers)
    
    ret
```

---

## System Services

### System Manager (`system/system.c`)

Manages boot, shutdown, and system state.

**System State:**
```c
typedef struct {
    uint32_t magic;            // 0x4F534158 ("OSAX")
    uint32_t version;
    uint32_t boot_count;
    uint32_t clean_shutdown;   // 1 if last shutdown was clean
    uint32_t last_boot_timestamp;
} system_state_t;
```

**First Boot vs Normal Boot:**

**First Boot:**
1. Detect no filesystem present
2. Format exFAT volume
3. Initialize MetaFS
4. Create default views
5. Import system files as objects
6. Initialize logger
7. Save system state

**Normal Boot:**
1. Mount exFAT volume
2. Load system state
3. Check for dirty shutdown
4. Mount MetaFS
5. Initialize logger
6. Increment boot counter

**API:**
```c
metafs_context_t* system_boot(exfat_volume_t* volume);
void system_shutdown(void);
void system_get_stats(uint32_t* boot_count, uint32_t* clean_shutdown);
int system_logger_ready(void);  // Check if logging available
```

### Logger (`system/logger.c`)

File-based logging system for debugging and auditing.

**Features:**
- Writes to `.kernel.system.log` in exFAT
- Multiple log levels
- Printf-style formatting
- Optional serial output for critical errors

**Log Levels:**
```c
typedef enum {
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR
} log_level_t;
```

**API:**
```c
int logger_init(exfat_volume_t* volume);
void logger_set_level(log_level_t level);
void log_write(log_level_t level, const char* subsystem, const char* message);
void log_printf(log_level_t level, const char* subsystem, 
                const char* format, ...);
void logger_flush(void);
void logger_close(void);
```

**Usage:**
```c
log_write(LOG_INFO, "BOOT", "System starting");
log_printf(LOG_WARN, "MEMORY", "Low memory: %d KB free", free_kb);
log_write(LOG_ERROR, "DISK", "Failed to read sector 42");
```

**Log Format:**
```
[INFO ] BOOT: System starting
[WARN ] MEMORY: Low memory: 1024 KB free
[ERROR] DISK: Failed to read sector 42
```

### Shell (`system/shell.c`)

Interactive command-line interface.

**Commands:**
- `help` - Show available commands
- `ls [path]` - List directory contents
- `cd <path>` - Change directory
- `pwd` - Print working directory
- `cat <file>` - Display file contents
- `create <name>` - Create new object
- `delete <name>` - Delete object
- `copy <src> <dst>` - Copy object
- `move <src> <dst>` - Move object
- `view <name>` - Create new view
- `info <name>` - Show object metadata
- `tree [path]` - Display directory tree
- `mem` - Show memory statistics
- `clear` - Clear screen
- `shutdown` / `halt` - Shut down system

**API:**
```c
void shell_init(metafs_context_t* metafs);
void shell_prompt(void);
void shell_execute(const char* line);
```

**Path Resolution:**

The shell supports both absolute and relative paths:
```bash
# Absolute paths
$ ls /documents
$ cat /documents/readme.txt

# Relative paths
$ cd documents
$ cat readme.txt
$ cat ./readme.txt

# Parent directory
$ cd ..
$ ls ../images
```

---

## Library Functions

### String Library (`lib/string.c`)

Basic string manipulation functions.

**API:**
```c
void* memset(void* ptr, int value, size_t num);
void* memcpy(void* dest, const void* src, size_t num);
size_t strlen(const char* str);
int strcmp(const char* s1, const char* s2);
int strncmp(const char* s1, const char* s2, size_t n);
char* strcpy(char* dest, const char* src);
char* strncpy(char* dest, const char* src, size_t n);
char* strstr(const char* haystack, const char* needle);
void itoa(uint32_t val, char* out);
```

### String Utilities (`lib/kstring_util.c`)

Printf/scanf-style formatting.

**API:**
```c
int ksprintf(char* str, const char* format, ...);
int ksscanf_hex(const char* str, uint32_t* high, uint32_t* low);
char* strchr(const char* str, int ch);
char* strrchr(const char* str, int ch);
void qsort(void* base, size_t nmemb, size_t size, 
           int (*compar)(const void*, const void*));
```

**Format Specifiers:**
- `%d`, `%u` - Decimal integer
- `%x` - Hexadecimal
- `%s` - String
- `%c` - Character
- `%08x` - Zero-padded hex (8 digits)

---

## Build System

### Compilation

The kernel uses cross-compilation for x86_64:

```makefile
CC      := x86_64-elf-gcc
LD      := x86_64-elf-ld
AS      := nasm
OBJCOPY := x86_64-elf-objcopy

CFLAGS := -m64 -mcmodel=kernel -mno-red-zone \
          -mno-mmx -mno-sse -mno-sse2 \
          -ffreestanding -nostdinc -fno-builtin -O2 \
          -Wall -Wextra
```

**Key Flags:**
- `-m64` - Generate 64-bit code
- `-mcmodel=kernel` - Kernel code model (high 2GB of address space)
- `-mno-red-zone` - Disable red zone (required for interrupt handlers)
- `-mno-mmx/sse` - Disable floating point (not initialized)
- `-ffreestanding` - Freestanding environment (no hosted libs)
- `-nostdinc` - Don't use standard includes
- `-fno-builtin` - Don't use compiler built-in functions

### Linking

The kernel is linked with a custom linker script:

```ld
ENTRY(_start)

SECTIONS {
    . = 0x100000;  /* Load at 1MB */
    
    .text : {
        *(.text)
    }
    
    .rodata : {
        *(.rodata*)
    }
    
    .data : {
        *(.data)
    }
    
    .bss : {
        *(.bss)
        *(COMMON)
        __bss_end = .;
    }
}
```

### Build Process

```bash
# Compile C sources
x86_64-elf-gcc $(CFLAGS) -c src/kernel/core/main.c -o build/core/main.o

# Assemble entry point
nasm -f elf64 src/bootloader/entry.asm -o build/entry.o

# Link kernel
x86_64-elf-ld -T stage2.ld -nostdlib \
    build/entry.o build/**/*.o -o build/stage2.elf

# Extract binary
x86_64-elf-objcopy -O binary build/stage2.elf build/stage2.bin
```

---

## Debugging

### Serial Output

All debug output goes to COM1 (serial port):

```c
kprintf("MEMORY: %d MB detected\n", mem_mb);
kprintf("HEAP: Allocated %d bytes at %x\n", size, ptr);
```

View in QEMU:
```bash
qemu-system-x86_64 -serial stdio -drive file=os.img,format=raw
```

### QEMU Debugging

**Enable interrupt logging:**
```bash
qemu-system-x86_64 -d int,cpu_reset,guest_errors -D qemu.log \
                   -drive file=os.img,format=raw
```

**GDB debugging:**
```bash
# Terminal 1: Start QEMU with GDB stub
qemu-system-x86_64 -s -S -drive file=os.img,format=raw

# Terminal 2: Connect GDB
gdb build/stage2.elf
(gdb) target remote localhost:1234
(gdb) break os_main
(gdb) continue
```

### Memory Debugging

**Heap statistics:**
```c
heap_stats_t stats;
heap_get_stats(&stats);
kprintf("Heap: %d KB used, %d KB free\n", 
        stats.used_size / 1024, stats.free_size / 1024);
```

**Page table dump:**
```c
kprintf("PML4[0] = %llx (present=%d, rw=%d)\n",
        pml4->entries[0].frame << 12,
        pml4->entries[0].present,
        pml4->entries[0].rw);
```

**Physical memory stats:**
```c
kprintf("Physical memory: %d MB total, %d KB free\n",
        get_total_memory() / 1024 / 1024,
        get_free_memory() / 1024);
```

---

## Common Patterns

### Allocating Memory

**Small allocations (<16MB):**
```c
void* buffer = kmalloc(1024);
// Use buffer
kfree(buffer);
```

**Large allocations (>16MB):**
```c
void* buffer = kmalloc_virtual(64 * 1024 * 1024);  // 64MB
// Use buffer
kfree_virtual(buffer, 64 * 1024 * 1024);
```

**DMA buffers:**
```c
void* dma_buf = dma_alloc(4096);  // Must be <1MB
// Use for disk I/O
dma_free(dma_buf);
```

### Creating Objects

```c
// Create object
const char* content = "Hello, world!";
object_id_t id = metafs_create(ctx, OBJ_TYPE_DOCUMENT, 
                                content, strlen(content));

// Read object
char buffer[256];
metafs_read(ctx, id, buffer, sizeof(buffer));

// Update metadata
object_metadata_t meta;
metafs_metadata_get(ctx, id, &meta);
meta.core.modified = get_timestamp();
metafs_metadata_set(ctx, id, &meta);

// Delete object
metafs_delete(ctx, id);
```

### Working with Views

```c
// Create view
metafs_view_create(ctx, "mywork", OBJ_TYPE_DOCUMENT);

// List objects in view
metafs_view_entry_t* entries;
int count = metafs_view_list(ctx, "/mywork", &entries);

for (int i = 0; i < count; i++) {
    kprintf("%s (type=%d, size=%lld)\n", 
            entries[i].name,
            entries[i].type,
            entries[i].size);
}

metafs_view_list_free(entries);
```

### Handling Interrupts

```c
// Define handler
void my_irq_handler(void) {
    // Handle interrupt
    // Send EOI to PIC
    outb(0x20, 0x20);
}

// Install handler
extern void irq3_handler(void);  // Defined in irq_asm.asm
idt_set_gate(35, (uint64_t)irq3_handler, 0x08, IDT_GATE_INTERRUPT);

// Unmask IRQ
uint8_t mask = inb(0x21);
mask &= ~(1 << 3);
outb(0x21, mask);
```

---

## Future Enhancements

### Short Term
1. **Disk Driver** - Replace in-memory disk with IDE/AHCI driver
2. **Network Stack** - TCP/IP networking
3. **Multitasking** - Preemptive scheduling
4. **User Mode** - Ring 3 processes with syscalls
5. **Shell Improvements** - Command history, tab completion

### Medium Term
1. **Graphics** - VESA VBE or GOP framebuffer support
2. **USB** - USB host controller driver
3. **Sound** - AC'97 or HD Audio driver
4. **Filesystem** - Add ext2/ext4 support alongside exFAT
5. **ELF Loader** - Full ELF loading with relocations

### Long Term
1. **SMP** - Multi-core support
2. **ACPI** - Advanced power management
3. **Security** - User permissions, encryption
4. **IPC** - Message passing, shared memory
5. **Package Manager** - Object-based package system

---

## Troubleshooting

### Kernel Panics

**Page Fault:**
- Check CR2 register for faulting address
- Verify page tables are set up correctly
- Ensure virtual address is mapped before access

**General Protection Fault:**
- Check segment selectors are valid
- Verify privilege levels (ring 0 vs ring 3)
- Check for NULL pointer dereference

**Triple Fault:**
- System resets - check QEMU log
- Usually caused by page fault during exception handling
- Verify stack is valid and mapped

### Memory Issues

**Out of Memory:**
```
HEAP: Out of memory! Requested: 16777216 bytes
```
- Increase initial heap size in `heap_init()`
- Check for memory leaks (missing `kfree()` calls)
- Use `heap_get_stats()` to track usage

**DMA Allocation Failed:**
```
DMA: Out of memory (requested 16 KB, 0 KB available)
```
- DMA region is only 576KB
- Reduce buffer sizes or use regular heap

### Filesystem Issues

**Mount Failed:**
```
EXFAT: Invalid boot signature
```
- Run first boot to format volume
- Check disk buffer is allocated correctly

**Object Not Found:**
```
METAFS: Object not found
```
- Verify ObjectID is correct (case-sensitive hex)
- Check object exists in index: `metafs_list_all_objects()`

---

## API Reference Summary

### Memory Management
```c
// Physical pages
void* alloc_page(void);
void* alloc_pages(uint32_t count);
void free_page(void* page);

// Kernel heap
void* kmalloc(size_t size);
void kfree(void* ptr);
void* kmalloc_virtual(size_t size);
void kfree_virtual(void* ptr, size_t size);

// DMA buffers
void* dma_alloc(uint32_t size);
void dma_free(void* ptr);

// Paging
void map_page(page_directory_t* pml4, uint64_t virt, uint64_t phys, uint64_t flags);
void unmap_page(page_directory_t* pml4, uint64_t virt);
```

### Filesystem
```c
// exFAT
int exfat_format(uint32_t total_sectors);
int exfat_mount(exfat_volume_t* volume);
int exfat_create(exfat_volume_t* vol, const char* path);
int exfat_open(exfat_volume_t* vol, const char* path, exfat_file_t* file);
int exfat_read(exfat_volume_t* vol, exfat_file_t* file, void* buf, uint32_t size);
int exfat_write(exfat_volume_t* vol, exfat_file_t* file, const void* buf, uint32_t size);

// MetaFS
object_id_t metafs_create(metafs_context_t* ctx, object_type_t type, 
                          const void* data, size_t size);
int metafs_read(metafs_context_t* ctx, object_id_t id, void* buf, size_t size);
int metafs_delete(metafs_context_t* ctx, object_id_t id);
```

### I/O
```c
// Terminal
void terminal_printf(const char* format, ...);
void terminal_setcolor(uint8_t fg, uint8_t bg);

// Keyboard
int keyboard_readline(char* buffer, int max_len);
char keyboard_getchar(void);

// Serial
void kprintf(const char* format, ...);
```

### System
```c
// Logging
void log_write(log_level_t level, const char* subsystem, const char* message);
void log_printf(log_level_t level, const char* subsystem, const char* format, ...);

// System management
metafs_context_t* system_boot(exfat_volume_t* volume);
void system_shutdown(void);
```

---

## Contributing

When contributing to the kernel:

1. **Follow coding style** - K&R style, 4-space indents
2. **Document functions** - Use header comments
3. **Add logging** - Use `kprintf()` for debug output
4. **Test thoroughly** - Run in QEMU before real hardware
5. **Update documentation** - Keep this README current

### Coding Style

```c
// Good
void function_name(int param1, const char* param2) {
    if (param1 > 0) {
        kprintf("Positive: %d\n", param1);
    } else {
        kprintf("Non-positive\n");
    }
}

// Avoid
void FunctionName(int Param1,const char *Param2){
if(Param1>0){kprintf("Positive: %d\n",Param1);}
else{kprintf("Non-positive\n");}}
```

### Adding New Features

1. Design API in header file
2. Implement in C source
3. Add initialization to `stage2.c` or `main.c`
4. Test with simple examples
5. Document in this README

---

## References

- [OSDev Wiki](https://wiki.osdev.org/)
- [Intel 64 and IA-32 Architectures Software Developer's Manual](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html)
- [exFAT Specification](https://docs.microsoft.com/en-us/windows/win32/fileio/exfat-specification)

---
