# armlet
ARMlet is an open source toolchain for the ARM32/ARM64 architecture.

## Prerequisites

Fedora 21
Python 2.6+
pdftotext (poppler-utils)
ARMv8-A Reference Manual ([here](https://silver.arm.com/download/download.tm?pv=1879124))
Redis 2.8.19+ (optional)
TeXLive 2014+ (optional)
ViM 7.4+ (optional)

## Description

What information can be extracted just from the reference manual alone? And what can
be further done with it? Let's list some of the most awesome ways:

### Instruction encoding/decoding information
Obtaining the semantics of each bit in the instruction word is crucial for every other part of `ARMlet`. It allows for creating of assemblers/disassemblers and as outlined below also for CPU emulators and as a data for documentation generation.

### ARMv8 CPU emulator
The base pseudocode library combined with the operational instruction pseudocode has enough information for constructing a soft-CPU or a CPU emulator.

### Architectural semantincs from pseudocode
We can use the pseudocode library to derive the inner workings of the architecture down to CPU flags, memory management etc. 

### Assembler symbols
With the assembler symbols available we have enough infromation to implement ARM assembly parser, or use the data as a backend for hinting in text editors (like jedi-vim).

### Optimized disassemblers
If you need to disassemble a lot of ARM instructions and performance/efficiency matters, you can create custom disassemblers which produce only the data that you want. Besides that, the inner loop is tuned by opcode frequency analysis of still growing set of binaries. 

### Amazing documentation
Even low level documents can be made fun and readable, if we combine the instruction encoding information with the collected semantics of the target architecture we can then easily compile appealingly looking documents with the help of PGF/Tikz, TeXLive, and the included _instruction.sty_ module.

### Easily consumed
ARMlet allows you to store all the object representations of instructions, pseudocode, tables etc. either in default JSON representation, or you can send them to a Redis data-structure server. 

## Installing

If you're lazy you can use the Dockerfile. Otherwise follow these steps on any recent installation of Fedora 21:

	1.  yum install poppler-utils
	2.  git clone https://github.com/shaded-enmity/armlet
	3.  cd armlet && make
	4.  Download ARMv8-A Reference Manual into `./Data`
	5.  ./armlet

##  Implementation checklist

| Status | Name |
| ------ | ---- |
|   ✓    |  Impl. feature 1 |
|   ✓    |  Impl. feature 2 |
|   ❌    |  Unimpl. feature 1 |
|   ❌    |  Unimpl. feature 2 |
|   ❌    |  Unimpl. feature 3
|   ✓    |  Impl. feature 3 |

## License

GNU/GPL 2.0 see `LICENSE`.
