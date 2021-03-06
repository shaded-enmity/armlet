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

![Encoding](http://i.stack.imgur.com/tWy4q.png)

```latex
\begin{instruction}{85. LDR (register)}%
%
    \addpart[bits=5,register] {Rd};%    
    \addpart[bits=5,register] {Rn};%
    \addpart[bits=2] {0, 1};%
    \addpart[bits=1,opcode] {S};%
    \addpart[name=opc1,opcode,bits=3,name overlay=red!30] {option};%
    \addpart[bits=5,register] {Rm};%
    \addpart[bits=9] {1, 1, 0, 0, 0, 0, 1, 1, 1};%
    \addpart[name=size,bits=2,name overlay=orange!50] {x,1};%   
    %
    \newvariant[32-bit, node={size}, equals]{10};%
    %
    \newmnemonics[operand={<Wt>}, comma, open bracket, optional={<Xn|SP>}, comma, optional={<R>}, optional={<m>},
       open curly, variant={<extend>}, open curly, comma, inner variant={<amount>}, 
       close curly, close curly, close bracket]{LDR};
    %
    \newvariant[64-bit, node={size}, equals]{11};%
    %
    \newmnemonics[operand={<Xt>}, comma, open bracket, optional={<Xn|SP>}, comma, optional={<R>}, optional={<m>},
       open curly, variant={<extend>}, open curly, comma, inner variant={<amount>}, 
       close curly, close curly, close bracket]{LDR};
    %
%
\end{instruction}%
```

### Easily consumed
ARMlet allows you to store all the object representations of instructions, pseudocode, tables etc. either in default JSON representation, or you can send them to a Redis data-structure server. 

### Example run

```
shaded-enmity$ ./cli.py -f data/aarch_combined -o all.json
/********************************************************************************
 ** 
                 __    __.__ __.__
   _._ _._ __.__|  |  |   __|_   _|
  | .'|  _|     |  |__|   __| | |  
  |__,|_| |_|_|_|_____|_____| |_|  
      
 ** ============> armLET CLI <================================================ ** 
 **     2013 - 2015(C) [GNU/GPL 2.0]                                           **
 **            by Pavel Odvody                                                 **
 **                                                                            **
 *******************************************************************************/

[!] cwd "/armlet"
[+] processing filename: data/aarch_combined
    [$] successfully loaded (124071) lines
    [v] ==========================================================================
    [>] aarch32::ARM32Processor 
        [!] 293/293     instructions    [0, 0, 1.000000]
    [>] aarch32simdfp::ARM32SIMDProcessor 
        [!] 235/235     instructions    [0, 0, 1.000000]
    [>] aarch64::ARM64Processor 
        [!] 223/223     instructions    [0, 0, 1.000000]
    [>] aarch64simdfp::ARM64SIMDProcessor 
        [!] 350/350     instructions    [0, 0, 1.000000]
    [v] ==========================================================================
[-] finished running in (6.188688 seconds)
```

## Installing

TODO :)

##  Implementation checklist

TODO

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
