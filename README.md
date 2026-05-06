```
    █████╗ ██████╗ ███╗   ███╗██╗     ███████╗████████╗
   ██╔══██╗██╔══██╗████╗ ████║██║     ██╔════╝╚══██╔══╝
   ███████║██████╔╝██╔████╔██║██║     █████╗     ██║
   ██╔══██║██╔══██╗██║╚██╔╝██║██║     ██╔══╝     ██║
   ██║  ██║██║  ██║██║ ╚═╝ ██║███████╗███████╗   ██║
   ╚═╝  ╚═╝╚═╝  ╚═╝╚═╝     ╚═╝╚══════╝╚══════╝   ╚═╝
   ┌──────────────────────────────────────────────────┐
   │   1 0 1 1  0 x x 0  1 1 0 x  x 0 1 1  0 1 x 1    │
   └──────────────────────────────────────────────────┘
```

# Armlet

> A specification language for hardware architectures and instruction decoding.

Armlet is a domain-specific language — and a small interpreter that runs it —
for describing CPU instruction encodings, register behaviour, and bit-level
control logic as executable pseudocode. It treats bits as first-class values,
including a third state `'x'` for "unknown" or "don't care", so that
specifications can capture real hardware uncertainties (uninitialised
registers, implementation-defined behaviour, decode wildcards) without
fudging them. Programs read like architecture reference manuals, but they
run.

## Features

- **First-class bits** — `bit` and `bits(N)` types with parametric widths.
- **Three-valued logic** — bits literals over `'0'`, `'1'`, and `'x'`, with
  wildcard-aware comparisons throughout the language and runtime.
- **Bitlayouts** — declarative instruction-format definitions that decode a
  `bits(32)` (or any width) into named fields plus fixed pattern bits.
- **Bit slicing & concatenation** — high\:low ranges, named sub-ranges with
  getters/setters, and concatenation via `:`.
- **Rich type system** — integers, reals, booleans, enums, structs, tuples,
  arrays, and type aliases.
- **Functions** — parametric bit widths in signatures, tuple returns,
  pattern-style use-sites.
- **Module system** — `import` statements with a configurable search path
  and a small standard library under `stdlib/`.
- **Interactive debugger** — TUI debugger with breakpoints, variable
  inspection, and AST printing.
- **Bit-level dispatch** — built-in O(log N) Huffman-style decoder for
  selecting among many bitlayouts by their fixed-pattern bits.

## Quick example

An ARM-like AddShiftedRegister format, decoded and used:

```aml
bitlayout AddShiftedRegister is (
  sf    : 1,
  '0001011',
  shift : 2,
  '0',
  Rm    : 5,
  imm6  : 6,
  Rn    : 5,
  Rd    : 5,
);

type Instruction = bits(32);

Instruction parsed = AddShiftedRegister(0x8b438084);
use parsed;

constant integer datasize     = 32 << UInt(sf);
constant integer shift_amount = UInt(imm6);
```

The fixed bits `'0001011'` and `'0'` are matched at decode time; the named
fields (`sf`, `shift`, `Rm`, ...) become bound variables. See
[`tests/bitlayout.aml`](tests/bitlayout.aml) for the full worked example.

## Getting started

**Dependencies:** a C compiler (GCC or Clang), GMP (arbitrary-precision
integers), ncurses (for the TUI debugger), and pkg-config.

```sh
make armlet
./armlet tests/bitlayout.aml
```

The Makefile auto-globs `source/**/*.c`; new source files are picked up
without edits.

### CLI flags

| Flag           | Purpose                                          |
|----------------|--------------------------------------------------|
| `-d`           | Run with debug output                            |
| `-D`           | Launch the interactive TUI debugger              |
| `-p`           | Parse only and print the AST                     |
| `-i <file>`    | Provide implementation-defined values            |
| `-I <dir>`     | Add a directory to the import search path        |

## Project layout

```
armlet/
├── source/    # Interpreter, parser, AST, bitlayout decoder, debugger
├── stdlib/    # Standard library modules (numeric, bits, shifts, defs)
├── tests/     # Example programs exercising language features
├── docs/      # Tutorial, reference, and topic guides
├── tree-sitter/  # Grammar for editor syntax highlighting
├── lsp/       # Language Server Protocol implementation
├── main.c     # CLI entry point
└── Makefile
```

## Documentation

- [Language reference](docs/reference.md) — comprehensive spec of types,
  operators, declarations, and built-ins.
- [Diagnostics](docs/diagnostics.md) — how the interpreter reports errors
  and warnings.
