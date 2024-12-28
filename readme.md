# Extending a Virtual Memory Implementation with Paging

This project, part of Sabancı University's Operating Systems (CS 307) course, involves extending an existing Virtual Memory (VM) implementation. It introduces paging for address translation, multiple processes, and system calls to manage process memory dynamically.

## Table of Contents
- [Introduction](#introduction)
- [Features](#features)
- [Implementation Details](#implementation-details)
  - [Virtual Address Space (VAS)](#virtual-address-space-vas)
  - [Physical Memory](#physical-memory)
  - [New Trap Types](#new-trap-types)
  - [System Calls](#system-calls)
  - [Key Functions](#key-functions)
- [How to Compile and Run](#how-to-compile-and-run)
- [Sample Outputs](#sample-outputs)
- [Resources](#resources)
- [System Requirements](#system-requirements)
- [Troubleshooting](#troubleshooting)

## Introduction
In this project, a simple VM was extended to include:
1. Paging for address translation between Virtual Address Space (VAS) and physical memory.
2. System calls (`yield` and `brk`) for managing context switching and heap allocation.
3. Enhanced support for multiple processes with Process Control Blocks (PCBs).

## Features
- **Paging:** Each process maps virtual addresses to physical frames.
- **Multiple Processes:** Context switching is supported using PCBs.
- **Dynamic Heap Management:** Implemented using the `brk` system call.
- **System Calls:** 
  - `yield`: Switches between processes.
  - `brk`: Allocates or frees heap pages dynamically.
- **Error Handling:** Includes segmentation faults, memory protection errors, and heap allocation errors.

## Implementation Details
### Virtual Address Space (VAS)
- Addressed by 16-bit unsigned integers (`uint16_t`).
- Composed of:
  - Reserved region for the OS.
  - Fixed 8KB code segment (2 pages).
  - Dynamic heap segment (initially 8KB).

### Physical Memory
- Total size: 128KB (32 pages of 4KB each).
- Layout:
  - OS occupies the first 8KB (2 pages).
  - Third page reserved for page tables.
  - Remaining space allocated dynamically for user processes.
- **Page Bitmap:** Tracks free/used pages.

### New Trap Types
- **Yield (`tyld`):** Triggers context switch.
- **Break (`tbrk`):** Allocates or frees memory pages in the heap.

### System Calls
#### `yield`
- Saves current process context to its PCB.
- Switches to the next runnable process.
- Prints: `We are switching from process <oldProcID> to <newProcID>`.

#### `brk`
- Allocates or frees a heap page based on the value in register `R0`.
- Example:
  - Request to allocate: `Heap increase requested by process <curProcID>`.
  - Error (already allocated): `Cannot allocate memory for page <VPN> of pid <CurProcId> since it is already allocated.`

### Key Functions
#### `initOS()`
- Initializes OS memory structures and free page bitmap.

#### `createProc(char *code, char *heap)`
- Creates a new process and allocates its code and heap pages.

#### `allocMem(uint16_t ptbr, uint16_t vpn, uint16_t read, uint16_t write)`
- Allocates a page and updates its Page Table Entry (PTE).

#### `freeMem(uint16_t vpn, uint16_t ptbr)`
- Frees a page and updates the bitmap.

#### `loadProc(uint16_t pid)`
- Loads a process's context from its PCB to the CPU registers.

## How to Compile and Run
1. Clone the repository:
   ```bash
   git clone https://github.com/username/project-name.git
   cd project-name
   ```
2. Compile the code:
   ```bash
   make
   ```
3. Run the VM:
   ```bash
   ./vm code.obj heap.obj
   ```
   To run multiple programs:
   ```bash
   ./vm code1.obj heap1.obj code2.obj heap2.obj
   ```

## Sample Outputs
Sample test cases and expected outputs are included in the `tests/` and `samples/` directories. Use the following commands to verify correctness:
```bash
$ ./tests/initos-test
$ ./samples/sample1.sh > samples/my-sample1-result.txt
$ diff samples/sample1-result.txt samples/sample1-expected.txt
```

## Resources
- [LC-3 Virtual Machine](https://github.com/nomemory/lc3-vm)
- [Writing a Simple VM](https://www.andreinc.net/2021/12/01/writing-a-simple-vm-in-less-than-125-lines-of-c)

## System Requirements
- GCC compiler (version 5.0+)
- Make build system
- 256MB RAM minimum
- Linux/Unix environment (or Windows with WSL)

## Troubleshooting
Common issues and solutions:
- Segmentation fault: Check page table entries
- Heap allocation fails: Verify free bitmap state
- Context switch error: Ensure PCB integrity
