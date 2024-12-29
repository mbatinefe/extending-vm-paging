#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vm_dbg.h"

#define NOPS (16)

#define OPC(i) ((i) >> 12)
#define DR(i) (((i) >> 9) & 0x7)
#define SR1(i) (((i) >> 6) & 0x7)
#define SR2(i) ((i) & 0x7)
#define FIMM(i) ((i >> 5) & 01)
#define IMM(i) ((i) & 0x1F)
#define SEXTIMM(i) sext(IMM(i), 5)
#define FCND(i) (((i) >> 9) & 0x7)
#define POFF(i) sext((i) & 0x3F, 6)
#define POFF9(i) sext((i) & 0x1FF, 9)
#define POFF11(i) sext((i) & 0x7FF, 11)
#define FL(i) (((i) >> 11) & 1)
#define BR(i) (((i) >> 6) & 0x7)
#define TRP(i) ((i) & 0xFF)

/* New OS declarations */

// OS bookkeeping constants
#define PAGE_SIZE       (4096)  // Page size in bytes
#define OS_MEM_SIZE     (2)     // OS Region size. Also the start of the page tables' page
#define Cur_Proc_ID     (0)     // id of the current process
#define Proc_Count      (1)     // total number of processes, including ones that finished executing.
#define OS_STATUS       (2)     // Bit 0 shows whether the PCB list is full or not
#define OS_FREE_BITMAP  (3)     // Bitmap for free pages

// Process list and PCB related constants
#define PCB_SIZE  (3)  // Number of fields in a PCB
#define PID_PCB   (0)  // Holds the pid for a process
#define PC_PCB    (1)  // Value of the program counter for the process
#define PTBR_PCB  (2)  // Page table base register for the process

#define CODE_SIZE       (2)  // Number of pages for the code segment
#define HEAP_INIT_SIZE  (2)  // Number of pages for the heap segment initially

bool running = true;

typedef void (*op_ex_f)(uint16_t i);
typedef void (*trp_ex_f)();

enum { trp_offset = 0x20 };
enum regist { R0 = 0, R1, R2, R3, R4, R5, R6, R7, RPC, RCND, PTBR, RCNT };
enum flags { FP = 1 << 0, FZ = 1 << 1, FN = 1 << 2 };

uint16_t mem[UINT16_MAX] = {0};
uint16_t reg[RCNT] = {0};
uint16_t PC_START = 0x3000;

void initOS();
int createProc(char *fname, char *hname);
void loadProc(uint16_t pid);
uint16_t allocMem(uint16_t ptbr, uint16_t vpn, uint16_t read, uint16_t write);  // Can use 'bool' instead
int freeMem(uint16_t ptr, uint16_t ptbr);
static inline uint16_t mr(uint16_t address);
static inline void mw(uint16_t address, uint16_t val);
static inline void tbrk();
static inline void thalt();
static inline void tyld();
static inline void trap(uint16_t i);

static inline uint16_t sext(uint16_t n, int b) { return ((n >> (b - 1)) & 1) ? (n | (0xFFFF << b)) : n; }
static inline void uf(enum regist r) {
    if (reg[r] == 0)
        reg[RCND] = FZ;
    else if (reg[r] >> 15)
        reg[RCND] = FN;
    else
        reg[RCND] = FP;
}
static inline void add(uint16_t i)  { reg[DR(i)] = reg[SR1(i)] + (FIMM(i) ? SEXTIMM(i) : reg[SR2(i)]); uf(DR(i)); }
static inline void and(uint16_t i)  { reg[DR(i)] = reg[SR1(i)] & (FIMM(i) ? SEXTIMM(i) : reg[SR2(i)]); uf(DR(i)); }
static inline void ldi(uint16_t i)  { reg[DR(i)] = mr(mr(reg[RPC]+POFF9(i))); uf(DR(i)); }
static inline void not(uint16_t i)  { reg[DR(i)]=~reg[SR1(i)]; uf(DR(i)); }
static inline void br(uint16_t i)   { if (reg[RCND] & FCND(i)) { reg[RPC] += POFF9(i); } }
static inline void jsr(uint16_t i)  { reg[R7] = reg[RPC]; reg[RPC] = (FL(i)) ? reg[RPC] + POFF11(i) : reg[BR(i)]; }
static inline void jmp(uint16_t i)  { reg[RPC] = reg[BR(i)]; }
static inline void ld(uint16_t i)   { reg[DR(i)] = mr(reg[RPC] + POFF9(i)); uf(DR(i)); }
static inline void ldr(uint16_t i)  { reg[DR(i)] = mr(reg[SR1(i)] + POFF(i)); uf(DR(i)); }
static inline void lea(uint16_t i)  { reg[DR(i)] =reg[RPC] + POFF9(i); uf(DR(i)); }
static inline void st(uint16_t i)   { mw(reg[RPC] + POFF9(i), reg[DR(i)]); }
static inline void sti(uint16_t i)  { mw(mr(reg[RPC] + POFF9(i)), reg[DR(i)]); }
static inline void str(uint16_t i)  { mw(reg[SR1(i)] + POFF(i), reg[DR(i)]); }
static inline void rti(uint16_t i)  {} // unused
static inline void res(uint16_t i)  {} // unused
static inline void tgetc()        { reg[R0] = getchar(); }
static inline void tout()         { fprintf(stdout, "%c", (char)reg[R0]); }
static inline void tputs() {
  uint16_t *p = mem + reg[R0];
  while(*p) {
    fprintf(stdout, "%c", (char) *p);
    p++;
  }
}
static inline void tin()      { reg[R0] = getchar(); fprintf(stdout, "%c", reg[R0]); }
static inline void tputsp()   { /* Not Implemented */ }
static inline void tinu16()   { fscanf(stdin, "%hu", &reg[R0]); }
static inline void toutu16()  { fprintf(stdout, "%hu\n", reg[R0]); }

trp_ex_f trp_ex[10] = {tgetc, tout, tputs, tin, tputsp, thalt, tinu16, toutu16, tyld, tbrk};
static inline void trap(uint16_t i) { trp_ex[TRP(i) - trp_offset](); }
op_ex_f op_ex[NOPS] = {/*0*/ br, add, ld, st, jsr, and, ldr, str, rti, not, ldi, sti, jmp, res, lea, trap};

/**
  * Load an image file into memory.
  * @param fname the name of the file to load
  * @param offsets the offsets into memory to load the file
  * @param size the size of the file to load
*/
void ld_img(char *fname, uint16_t *offsets, uint16_t size) {
    FILE *in = fopen(fname, "rb");
    if (NULL == in) {
        fprintf(stderr, "Cannot open file %s.\n", fname);
        exit(1);
    }

    for (uint16_t s = 0; s < size; s += PAGE_SIZE) {
        uint16_t *p = mem + offsets[s / PAGE_SIZE];
        uint16_t writeSize = (size - s) > PAGE_SIZE ? PAGE_SIZE : (size - s);
        fread(p, sizeof(uint16_t), (writeSize), in);
    }
    
    fclose(in);
}

void run(char *code, char *heap) {
  while (running) {
    uint16_t i = mr(reg[RPC]++);
    op_ex[OPC(i)](i);
  }
}

// MY CODE STARTS HERE

void initOS() {
  mem[Cur_Proc_ID] = 0xffff; // mem[0]
  mem[Proc_Count] = 0;
  mem[OS_STATUS] = 0x0000;
  // For bitmap -> first 3 is 0 (Reserved); rest is 1 (Free)
  // 0 x 1 F F F sets as ->>> 0001 1111...
  mem[OS_FREE_BITMAP] = 0x1FFF; // Bitmap
  mem[4] = 0xFFFF; // Rest of bitmap
  return;
}

// Process functions to implement
int createProc(char *fname, char *hname) {
  // Fail 1) OS region of the memory is full and we cannot allocate a new PCB for new process
  if (((mem[OS_STATUS] & 0x1) != 0) || (mem[Proc_Count] * PCB_SIZE >= 4084)) {
    // mem[1] is ProcCount field.
    // mem[2] is OSStatus field. 
    // First if statement: We just check LSB, if 1 we are full, else we still have some 
    // Second if statement: 
      // Our PCB starts at address 12 and goes on till address 4096
      // Each process has 3(PCB_SIZE) blocks (PID, PC, PIBR)
      // We know procCount. So, procCount x 3 must not be bigger than (4096-12 = 4084)
    printf("The OS memory region is full. Cannot create a new PCB.\n");
    return 0;
  }

  // I set starting process as 0x1006. 
  reg[PTBR] = 0x1006 + (mem[Proc_Count] * 0x20);

  // --------------------------------------------------------------
  // Fail 2) Not enough free page frames for allocating code segment
  // Our code segment VPN 0011....0100 -> We try to allocate two pages (8KB) 
  if(allocMem(reg[PTBR], 0, 1, 0) != 1 
          ||  allocMem (reg[PTBR], 1, 1, 0) != 1){
    printf("Cannot create code segment.\n");
    // We deallocate what we got with allocMem
    freeMem(0, reg[PTBR]);
    freeMem(1, reg[PTBR]);
    return 0;  
  }

  // --------------------------------------------------------------
  // Fail 3) Not enough free page frames for allocating heap segment
  // Our heap segment VPN 0100...... -> We try to allocate two pages (8KB) 
  if(allocMem(reg[PTBR], 2, 1, 1) != 1 
        ||  allocMem (reg[PTBR], 3, 1, 1) != 1){
    printf("Cannot create heap segment.\n");
    // We also need to release the code since we already have it
    freeMem(0, reg[PTBR]);
    freeMem(1, reg[PTBR]);
    // We deallocate what we got with allocMem
    freeMem(2, reg[PTBR]);
    freeMem(3, reg[PTBR]);
    return 0;  
  }

  // --------------------------------------------------------------
  // Resolved, we can continue now
  uint16_t code_offsets[2];
  uint16_t heap_offsets[2];

  // 0x800 because we have 2 pages and each page is 4KB

  // Get the PFN of the first code page and calculate its physical address
  code_offsets[0] = (mem[reg[PTBR] + 0] >> 11) * 0x800;
  // Get the PFN of the second code page and calculate its physical address
  code_offsets[1] = (mem[reg[PTBR] + 1] >> 11) * 0x800;

  // Load the code image
  ld_img(fname, code_offsets, CODE_SIZE * PAGE_SIZE);

  // Get the PFN of the first heap page and calculate its physical address
  heap_offsets[0] = (mem[reg[PTBR] + 2] >> 11) * 0x800;
  // Get the PFN of the second heap page and calculate its physical address
  heap_offsets[1] = (mem[reg[PTBR] + 3] >> 11) * 0x800;

  // Load the heap image
  ld_img(hname, heap_offsets, HEAP_INIT_SIZE * PAGE_SIZE);

  // Create PCB
  uint16_t pcb_base_start = (PCB_SIZE * mem[Proc_Count]) + 12;
  mem[pcb_base_start + PID_PCB] = mem[Proc_Count]; // PID
  mem[pcb_base_start + PC_PCB] = PC_START; // PC
  mem[pcb_base_start + PTBR_PCB] = reg[PTBR] - 6; // PTBR, we subtract 6 since we started from 0x1006
  mem[Proc_Count]++;
  return 1;
}

void loadProc(uint16_t pid) {
  // Load process
  // Our Base is (PCB_SIZE * pid) + 12
  reg[RPC] = mem[(PCB_SIZE * pid) + 12 + PC_PCB]; // PC
  reg[PTBR] = mem[(PCB_SIZE * pid) + 12 + PTBR_PCB]; // PTBR
  mem[Cur_Proc_ID] = pid; // Set current process ID
}

uint16_t allocMem(uint16_t ptbr, uint16_t vpn, uint16_t read, uint16_t write) {
  int index_of_free = -1; // -1 means no free page frame is found
  int bitmap_value = 0; // 0 means no free page frame is found

  uint32_t bitmap_full = ((uint32_t)mem[3] << 16) | mem[4]; // Full bitmap combined both mem[3] + mem[4]
  // We'll find first free page frame by searching bitmap and allocate it for given VPN
  for (int k = 31; k >= 0; k--) {
    if ((bitmap_full >> k) & 1) { // If the kth bit is 1, then free
      index_of_free = 31 - k; // We found free page frame
      bitmap_full &= ~(1 << k); // We set the kth bit to 0
      bitmap_value = 1; // We found it
      break;
    }
  }

  // If no free page frame is found
  if (index_of_free == -1) {
    return 0;
  }

  mem[OS_FREE_BITMAP] = (uint16_t)(bitmap_full >> 16); // First 16 bits
  mem[4] = (uint16_t)(bitmap_full & 0xFFFF); // Rest of bitmap
  
  // Now, we have bitmap_value at index_of_free
  // If bitmap_value is 1 -> we found empty space
  // If bitmap_value is 0 -> we do not have any space left
  if(bitmap_value == 1){
    // PTBR -> page table base register of current process
    // VPN -> we'll use first 5 to identify
    // read and write -> operations
    // Then valid bit
    uint16_t pte = (index_of_free << 11); // PFN
    pte |= (read ? 0x2 : 0x0); // Read
    pte |= (write ? 0x4 : 0x0); // Write
    pte |= 0x1; // Validity

    mem[ptbr + vpn] = pte; // Set the PTE
    return 1;
  }
  // If we cannot find return 0
  return 0;
}

int freeMem(uint16_t vpn, uint16_t ptbr) {
  if(!(mem[ptbr+vpn] & 0x1)) { // If the page is not valid
    return 0;
  }

  // We get PFN and PTE
  uint16_t pfn = mem[ptbr+vpn] >> 11;
  uint16_t pte = mem[ptbr+vpn];
    
  // Update bitmap to show as free
  uint32_t bitmap_full = ((uint32_t)mem[OS_FREE_BITMAP] << 16) | mem[4];
  bitmap_full |= (1 << (31 - pfn)); // Set the pfn bit to 1
  mem[OS_FREE_BITMAP] = (bitmap_full >> 16) & 0xFFFF; // Update first 16 bits
  mem[4] = bitmap_full & 0xFFFF; // Update rest of bitmap
    
  mem[ptbr + vpn] = pte & ~0x1; // Set valid bit to 0
  return 1;
}

// Instructions to implement
static inline void tbrk() {
  // Lets get the current process ID and we get request from R0
  uint16_t current_process_ID = mem[Cur_Proc_ID];
  uint16_t vpn = reg[R0] & 0x1F; // Our VPN is encoded in the lowest 5 bits of request (bits 0 to 4)
  uint16_t flags = (reg[R0] >> 12) & 0xF; // Shift right by 12 bits and mask to get flags
  
  bool can_we_allocate = (flags & 0x1);
  bool can_we_read = (flags & 0x2);
  bool can_we_write = (flags & 0x4);

  if (can_we_allocate) {
    printf("Heap increase requested by process %u.\n", current_process_ID);
    if (mem[reg[PTBR] + vpn] & 0x1) { // If already allocated
        printf("Cannot allocate memory for page %u of pid %u since it is already allocated.\n", vpn, current_process_ID);
        return;
    }
    // Else: we try to allocate
    if (allocMem(reg[PTBR], vpn, can_we_read, can_we_write) != 1) {
        printf("Cannot allocate more space for pid %u since there is no free page frames.\n", current_process_ID);
    }
    // If we are here, its allocated, we are done
  } else {
      // Request is to deallocate
      printf("Heap decrease requested by process %u.\n", current_process_ID);
      if (!(mem[reg[PTBR] + vpn] & 0x1)) {  // If already allocated
          printf("Cannot free memory of page %u of pid %u since it is not allocated.\n", vpn, current_process_ID);
          return;
      }
      // Else: we try to deallocate
      freeMem(vpn, reg[PTBR]);
  }

}

static inline void tyld() {
  uint16_t starting_base_point = (PCB_SIZE * mem[Cur_Proc_ID]) + 12; // Starting point of PCB
  mem[starting_base_point + PC_PCB]  = reg[RPC]; // Save the current PC
  mem[starting_base_point + PTBR_PCB] = reg[PTBR]; // Save the current PTBR

  uint16_t new_PID = mem[Cur_Proc_ID];
  // We are looking for different process
  // If we only have one process total -> we continuing with same one.
  for (int i = 0; i < mem[Proc_Count]; i++) {
    new_PID++;
    if (new_PID >= mem[Proc_Count]) {
      new_PID = 0; // Wrap around to the first process
    }
    uint16_t new_pcb_starting_base = (PCB_SIZE * new_PID) + 12;
    if (mem[new_pcb_starting_base + PID_PCB] != 0xffff) {
      // If new_pid == cur_pid => only one process is alive, so do nothing
      if (new_PID != mem[Cur_Proc_ID]) {
        printf("We are switching from process %u to %u.\n", mem[Cur_Proc_ID], new_PID);
        loadProc(new_PID);
      }
      return;
    }
  }
  // If we didn’t find any different process
    // -> only one left OR all finished.
}

// Instructions to modify
static inline void thalt() {
  uint16_t starting_base_point = (PCB_SIZE * mem[Cur_Proc_ID]) + 12;

  // Lets free all allocated pages
  for (int vpn = 0; vpn < 32; vpn++) {
    if (mem[reg[PTBR] + vpn] & 0x1) { // valid bit
      freeMem(vpn, reg[PTBR]);
    }
  }

  // Only set PCB's PID_PCB to 0xffff
  mem[starting_base_point + PID_PCB] = 0xffff;

  // Same as tyld
  uint16_t new_PID = mem[Cur_Proc_ID];
  for (int i = 0; i < mem[Proc_Count]; i++) {
    new_PID++;
    if (new_PID >= mem[Proc_Count]) {
      new_PID = 0; // Wrap around to the first process
    }
    uint16_t new_pcb_starting_base = (PCB_SIZE * new_PID) + 12;
    if (mem[new_pcb_starting_base + PID_PCB] != 0xffff) {
      // Found alive process, we just load
      loadProc(new_PID);
      return;
    }
  }

  // If we are here, all processes have PID=0xffff => no more to run
  running = false;
}


static inline uint16_t mr(uint16_t address) {
  // vpn is address>>11 AND offset is address & 0x7ff
  uint16_t vpn = address >> 11; // 5 bits
  if(vpn < 3){
    // First 3 VPN reserved
    printf("Segmentation fault.\n");
    exit(1);
  }

  uint16_t pte_temp = mem[reg[PTBR] + vpn];
  // Lets check the validity of PTE
  if(!(pte_temp & 0x1)){
    printf("Segmentation fault inside free space.\n");
    exit(1);
  }

  // If all good we can write now.
  uint16_t pfn_temp = pte_temp >> 11;
  address = (pfn_temp << 11) | (address & 0x7ff); // PFN + offset
  return mem[address]; // return address
}

static inline void mw(uint16_t address, uint16_t val) {
  if((address>>11) < 3){
    // First 3 VPN reserved
    printf("Segmentation fault.\n");
    exit(1);
  }
  
  uint16_t pte_temp = mem[reg[PTBR] + (address>>11)];

  // Lets check the validity of PTE
  if(!(pte_temp & 0x1)){ // pte is mem[reg[PTBR]+vpn]
    printf("Segmentation fault inside free space.\n");
    exit(1);
  }

  // Are we allowed to write?
  if(!(pte_temp & 0x4)){
    printf("Cannot write to a read-only page.\n");
    exit(1);
  }

  // If all good we can write now.
  uint16_t pfn_temp = pte_temp >> 11;
  address = (pfn_temp << 11) | (address & 0x7ff);

  mem[address] = val;
}
