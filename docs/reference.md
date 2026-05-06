# Armlet Language Reference

This document is a comprehensive reference for the Armlet programming language.

---

## Table of Contents

1. [Lexical Structure](#1-lexical-structure)
2. [Types](#2-types)
3. [Literals](#3-literals)
4. [Variables and Declarations](#4-variables-and-declarations)
5. [Operators](#5-operators)
6. [Expressions](#6-expressions)
7. [Bit Selection and Manipulation](#7-bit-selection-and-manipulation)
8. [Statements](#8-statements)
9. [Control Flow](#9-control-flow)
10. [Functions](#10-functions)
11. [Getters and Setters](#11-getters-and-setters)
12. [Type Definitions](#12-type-definitions)
13. [Bit Layouts](#13-bit-layouts)
14. [Modules and Imports](#14-modules-and-imports)
15. [Named Bits](#15-named-bits)
16. [Built-in Functions](#16-built-in-functions)
17. [Special Values](#18-special-values)
18. [Traps](#19-traps)
19. [Serialisation](#20-serialisation)
20. [Operator Precedence](#21-operator-precedence)

---

## 1. Lexical Structure

### Comments

Line comments begin with `//` and extend to the end of the line.

```aml
// This is a comment
integer x = 1; // inline comment
```

There are no block comments.

### Identifiers

Identifiers start with a letter or underscore and may contain letters, digits,
underscores, and dots (for qualified names). Identifiers are case-sensitive.

### Keywords

```
if then else elsif case of when otherwise
for to downto while do repeat until
return assert constant
type is enumeration bitlayout array
import use
TRUE FALSE
AND OR EOR NOT IN DIV MOD
UNKNOWN UNPREDICTABLE UNDEFINED SEE
IMPLEMENTATION_DEFINED
integer real boolean bit bits
```

### Semicolons

Statements are terminated with semicolons. In many contexts (loop bodies,
if-then blocks) the semicolon after the last statement in a block is optional.

---

## 2. Types

### Primitive Types

| Type | Description |
|------|-------------|
| `integer` | Arbitrary-precision signed integer |
| `real` | Floating-point number |
| `boolean` | `TRUE` or `FALSE` |
| `bit` | Single bit: `'0'`, `'1'`, or `'x'` |
| `bits(N)` | Bit vector of width N |

`N` in `bits(N)` can be any integer expression, including function parameters
and arithmetic:

```aml
bits(N) x;           // width from parameter
bits(N+M) y;         // computed width
bits(2*datasize) z;  // expression
```

### Composite Types

| Type | Description |
|------|-------------|
| `type Name is (...)` | Structure with named fields |
| `type Name = BaseType` | Type alias |
| `enumeration Name { ... }` | Enumeration of named constants |
| `(T1, T2, ...)` | Tuple type |
| `array T Name[lo..hi]` | Array of T indexed from lo to hi |
| `bitlayout Name is (...)` | Instruction bit layout |

### Type Aliases

```aml
type Word = bits(32);
type Counter = integer;
```

Aliases are interchangeable with their base types.

---

## 3. Literals

### Integer Literals

```aml
42
-7
0
```

### Real Literals

Must contain a decimal point:

```aml
3.14
0.5
-1.0
```

### Boolean Literals

```aml
TRUE
FALSE
```

### Bit Literals

Single-quoted strings of `0`, `1`, `x` (unknown), and `X` (wildcard):

```aml
'0'           // single bit
'1010'        // 4-bit value
'1x0x'        // with unknown bits
'1 0 1 0'     // spaces ignored (same as '1010')
'X01'         // X is wildcard (used in pattern matching)
```

### Hexadecimal Bit Literals

Hex values followed by a bit-range selector:

```aml
0xFF<7:0>            // 8-bit value
0x8b438084<31:0>     // 32-bit value
0x7d0c2155631469e126d677ba7e04b17<127:0>  // 128-bit value
```

### String Literals

Double-quoted, with interpolation and escape sequences:

```aml
"hello"
"value = {x}"              // interpolation
"escaped \" backslash \\"  // escape sequences
"multi {a + b} values"     // expression interpolation
```

NOTE: Interpolation only works inside string literals passed to the `print` built-in function.

Supported escape sequences: `\"`, `\\`.

Interpolation syntax: `{expression}` embeds the value of `expression`.

---

## 4. Variables and Declarations

### Typed Declaration with Initialisation

```aml
integer x = 5;
bits(8) data = '11001010';
boolean flag = TRUE;
real pi = 3.14;
```

### Declaration Without Initialisation

```aml
bits(8) data;
integer count;
```

Uninitialised variables hold an undefined value.

### Type-Inferred Declaration

When the type is obvious from context, the type annotation can be omitted:

```aml
x = 5;              // integer
data = '11001010';  // bits(8)
```

### Multiple Declarations

Multiple variables of the same type:

```aml
bit u, v;
```

### Constant Declarations

```aml
constant integer WIDTH = 32;
constant bits(4) MASK = '1111';
constant ShiftType stype = DecodeShift(op);
```

Constants cannot be reassigned after initialisation. The initialiser may be a
function call or expression evaluated at declaration time.

---

## 5. Operators

### Arithmetic Operators

| Operator | Description | Operands |
|----------|-------------|----------|
| `+` | Addition | integer, real |
| `-` | Subtraction | integer, real |
| `*` | Multiplication | integer, real |
| `DIV` | Integer division | integer |
| `/` | Float division | integer, real |
| `MOD` | Modulo | integer |
| `^` | Exponentiation | integer, real |
| `-` (unary) | Negation | integer, real |

### Bitwise Operators

| Operator | Description | Operands |
|----------|-------------|----------|
| `AND` | Bitwise AND | bits(N) |
| `OR` | Bitwise OR | bits(N) |
| `EOR` | Bitwise XOR | bits(N), boolean |
| `:` | Concatenation | bits, bit |
| `<<` | Left shift | integer |
| `>>` | Right shift | integer |

### Logical Operators

| Operator | Description | Operands |
|----------|-------------|----------|
| `&&` | Logical AND | boolean |
| `\|\|` | Logical OR | boolean |
| `!` | Logical NOT | boolean |
| `EOR` | Logical XOR | boolean |

### Comparison Operators

| Operator | Description |
|----------|-------------|
| `==` | Equal |
| `!=` | Not equal |
| `>` | Greater than |
| `<` | Less than |
| `>=` | Greater than or equal |
| `<=` | Less than or equal |
| `IN` | Set membership |

Comparisons return `boolean`. Both operands must be the same type (or
compatible). Bits values can be compared with `==` and `!=`.

### Set Membership

```aml
value IN {element1, element2, ...}
```

Returns `TRUE` if `value` equals any element. Works with integers and bits
values. Bits comparison respects `'x'` wildcard matching.

---

## 6. Expressions

### Inline If-Then-Else

```aml
result = if condition then expr1 else expr2;
result = if c == 1 then 'a' elsif c == 2 then 'b' else 'c';
```

The inline if is an expression and can be nested.

### Function Calls

```aml
result = FunctionName(arg1, arg2);
result = Namespace.FunctionName(arg1, arg2);
```

### Tuple Construction

```aml
(i, b) pair = (42, TRUE);
```

### Bracket Concantenation 

```aml
c = [a, Xs(), b];
// is the same as
c = a:Xs():b;
```

### Type Conversion

```aml
real r = Real(42);        // integer to real
integer u = UInt('1010'); // bits to unsigned integer
integer s = SInt('1010'); // bits to signed integer
```

---

## 7. Bit Selection and Manipulation

### Single Bit Selection

```aml
bit b = value<index>;
```

`index` is an integer expression. Bit 0 is the LSB.

### Range Selection

```aml
bits(K) slice = value<high:low>;
```

Inclusive range. The width of the result is `high - low + 1`.

### Offset-Based Selection

```aml
bits(size) slice = value<offset+:size>;
```

Extracts `size` bits starting at bit `offset`.

### Multi-Field Selection (Bit Slurp)

Extract multiple fields as a concatenated value:

```aml
bits(K) combined = value.<field1, field2>;
```

Or with structure fields:

```aml
struct.<fieldA, fieldB> = '1010';
```

### Multi-Bit Selection from Tuple Context

```aml
(a, b, <x, y, z>, -) = (1, TRUE, '1x0', 123);
```

`<x, y, z>` splits the bits value `'1x0'` into individual bits.

### Assignment to Selections

All selection forms work as assignment targets:

```aml
value<7> = '1';
value<3:0> = '1111';
value<4+:3> = '101';
struct.field = '10';
array[i]<2> = '1';
struct.array[j]<0> = 'x';
```

---

## 8. Statements

### Assignment

```aml
variable = expression;
```

### Tuple Destructuring Assignment

```aml
(a, b) = function_returning_tuple();
(a, b, c) = (1, TRUE, '101');
(<x, y>, (b, (c, d))) = nested_tuple_expr;
(result, -) = AddWithCarry(x, y, '0');  // ignore second element
```

### Assert

```aml
assert condition;
```

Halts execution with a diagnostic if `condition` is `FALSE`.

### Print

```aml
print(expr1, expr2, ...);
print("formatted: {value}");
```

### Inspect

```aml
inspect(expr1, expr2, ...);
```

Prints detailed type and value information.

### Backtrace

```aml
backtrace();
```

Prints the current call stack.

### Return

```aml
return expression;
return;
```

Returns a value from a function. `return;` without a value is used in void
functions or to exit early.

---

## 9. Control Flow

### If-Then-Elsif-Else

```aml
if condition then
    statements
elsif condition then
    statements
else
    statements
```

`elsif` and `else` branches are optional. Multiple `elsif` branches are
allowed.

### Case-When

```aml
case expression of
    when pattern1
        statements
    when pattern2
        statements
    otherwise
        statements
```

`pattern` can be a literal value or an enumeration member. `otherwise` is the
default branch. Each `when` branch can contain one or more statements.

### For Loop

```aml
for variable = start to end
    statements

for variable = start downto end
    statements
```

The loop variable is an implicit integer, scoped to the loop body. Both `start`
and `end` are inclusive.

### While Loop

```aml
while condition do
    statements
```

### Repeat-Until Loop

```aml
repeat
    statements
until condition;
```

Executes the body at least once, then continues while the condition holds.

---

## 10. Functions

### Function Definition

```aml
ReturnType FunctionName(ParamType1 param1, ParamType2 param2)
    // body
    return value;
```

Functions without an explicit return type that don't return a value are
implicitly void:

```aml
DoSomething()
    print("done");
```

### Parameterised Bit Widths

Type parameters like `N` and `M` are inferred from arguments:

```aml
bits(N) ZeroExtend(bits(M) x, integer N)
    assert N >= M;
    return Zeros(N-M) : x;
```

Return types can be computed:

```aml
bits(N+M) Concat(bits(N) a, bits(M) b)
    return a:b;

bits(M*N) Replicate(bits(M) val, integer N)
    // ...
```

### Namespaced Functions

```aml
ReturnType Namespace.FunctionName(params)
    body
```

Called as `Namespace.FunctionName(args)`, or just `FunctionName(args)` after
`use Namespace;`.

Namespaced constants:

```aml
constant bits(4) Utils.Pattern = '1x10';
```

### Polymorphic Functions

Multiple definitions with the same name but different parameter types:

```aml
integer Abs(integer x)
    return if x < 0 then -x else x;

real Abs(real x)
    return if x < 0.0 then -x else x;
```

Dispatch is based on the types of arguments at the call site.

### Tuple Returns

```aml
(bits(N), bits(4)) AddWithCarry(bits(N) x, bits(N) y, bit carry_in)
    // ...
    return (result, nzcv);
```

---

## 11. Getters and Setters

Getters provide read access with function-call semantics. Setters provide write
access.

### Getter Without Parameters (Property-Style)

```aml
bits(1) Flag
    return register<0>;
```

Accessed without parentheses: `x = Flag;`

### Getter With Brackets

```aml
bits(width) X[integer n, integer width]
    assert n >= 0 && n <= 31;
    return _R[n]<width-1:0>;
```

Accessed with brackets: `val = X[3, 32];`

### Setter

```aml
X[integer n, integer width] = bits(width) value
    assert n >= 0 && n <= 31;
    _R[n]<width-1:0> = value;
```

Invoked by assigning: `X[3, 32] = result;`

### Getter/Setter With Type-Parameterised Width

```aml
bits(4) Slice[integer idx]
    return data<(idx*4)+:4>;

Slice[integer idx] = bits(N) val
    data<idx+:N> = val;
```

---

## 12. Type Definitions

### Type Alias

```aml
type AliasName = ExistingType;
```

Examples:

```aml
type Word = bits(32);
type Instruction = bits(32);
type MyInt = integer;
```

### Structure Definition

```aml
type StructName is (
    Type1 field1,
    Type2 field2,
    ...
);
```

Fields are accessed with dot notation. Structures can contain any type
including arrays and other structures.

```aml
type CPU is (
    integer pc,
    array[0..31] of bits(64) regs,
    bits(4) flags,
    boolean halted,
);
```

Constructor syntax:

```aml
CPU state = CPU(0, ...);  // positional initialisation
```

Or field-by-field:

```aml
CPU state;
state.pc = 0;
state.halted = FALSE;
```

### Enumeration Definition

```aml
enumeration EnumName { Member1, Member2, Member3 };
```

Members are referenced by name: `bar = Member2;`

Used in `case` statements:

```aml
case value of
    when Member1 ...
    when Member2 ...
```

### Nested Types

Tuples can contain structures, arrays, and other tuples:

```aml
type Nested = (
    TwoBit, (bits(3), (TwoBit, MyInt))
);
```

---

## 13. Bit Layouts

Bit layouts define the binary structure of fixed-width values (typically
instructions).

### Definition

```aml
bitlayout LayoutName is (
    field1 : width1,
    'literal_bits',
    field2 : width2,
    ...
);
```

Each entry is either:
- A named field with a bit width: `fieldname : N`
- A fixed literal: `'0110101'`

Fields are listed from most-significant to least-significant bit.

### Parsing

Apply a layout to a bit value to parse it:

```aml
bits(32) parsed = LayoutName(0x8b438084);
```

### Using Fields

The `use` statement brings parsed fields into scope:

```aml
use parsed;
// field1, field2, etc. are now accessible
```

### Example

```aml
bitlayout DataProcessing is (
    sf    : 1,
    opc   : 2,
    '100101',
    imm26 : 26,
);

bits(32) instr = DataProcessing(0x94000010);
use instr;

constant integer offset = SInt(imm26) * 4;
```

### Bitlayout Handlers

Bitlayouts can be dynamically dispatched using the `dispatch` built in function.
For such purposes there is the `bitlayout NAME is (...) then do [NAME] BLOCK`
syntax. For more information see the [bitlayout documentation](bitlayouts.md).

---

## 14. Modules and Imports

### File-Based Modules

Each `.aml` file is a module. The file extension is optional in import
statements.

### Import Statement

```aml
import "path/to/module";
import "stdlib/bits";
import "utils.aml";
```

Paths are relative to the project root.

### Use Statement

`use` brings a namespace's members into the current scope:

```aml
import "utils";
use Utils;

// Now Utils.Repeat can be called as just Repeat
r = Repeat('1010', 4);
```

Without `use`, qualified access is required:

```aml
r = Utils.Repeat('1010', 4);
```

---

## 15. Named Bits

Named bits allow assigning semantic names to bit ranges within a bit vector,
providing readable access to individual fields.

### Defining Named Ranges

```aml
bits(32) CPSR = Zeros(32);

set_bits_range_name(CPSR, "N", 31);        // single bit
set_bits_range_name(CPSR, "Z", 30);        // single bit
set_bits_range_name(CPSR, "C", 29);        // single bit
set_bits_range_name(CPSR, "V", 28);        // single bit
set_bits_range_name(CPSR, "Mode", 0, 4);   // bit range 0..4
```

For single-bit ranges, pass one index. For multi-bit ranges, pass start and
end indices.

### Accessing Named Bits

```aml
CPSR.N = '1';
CPSR.Mode = '10011';

bit negative = CPSR.N;
bits(5) mode = CPSR.Mode;
```

Named bits work with dot notation, just like structure fields.

---

## 16. Built-in Functions

### Output

| Function | Description |
|----------|-------------|
| `print(args...)` | Print values; supports string interpolation |
| `inspect(args...)` | Print detailed type and value information |
| `backtrace()` | Print the call stack |

### Assertions

| Function | Description |
|----------|-------------|
| `assert condition` | Halt with diagnostic if condition is FALSE |

### Type Conversion

| Function | Description |
|----------|-------------|
| `Real(x)` | Convert integer to real |
| `UInt(v)` | Interpret bits as unsigned integer |
| `SInt(v)` | Interpret bits as signed integer |
| `Int(v, unsigned)` | Interpret bits with signedness flag |

### Serialisation

| Function | Description |
|----------|-------------|
| `serialize(filename, value)` | Write value to binary file |
| `deserialize(filename)` | Read value from binary file |

### Named Bits

| Function | Description |
|----------|-------------|
| `set_bits_range_name(var, name, start)` | Name a single bit |
| `set_bits_range_name(var, name, start, end)` | Name a bit range |

### Implementation-Defined

| Function | Description |
|----------|-------------|
| `begin_implementation_defined(file)` | Start impl-defined block |
| `implementation_defined(tag, var)` | Define an impl-defined value |
| `end_implementation_defined()` | End impl-defined block |

---

## 17. Special Values

### UNKNOWN

Creates an uninitialised value of a given type:

```aml
bits(64) x = bits(64) UNKNOWN;
integer  n = integer UNKNOWN;
real     r = real UNKNOWN;
```

The value is explicitly marked as unset. Any use before assignment may produce
undefined results.

### IMPLEMENTATION_DEFINED

Marks a value as implementation-defined with a descriptive string tag:

```aml
bits(4) val = bits(4) IMPLEMENTATION_DEFINED "Magic Value";
```

Implementation-defined values can be serialised and loaded:

```aml
begin_implementation_defined("impl.bin");
implementation_defined("Magic Value", val);
end_implementation_defined();
```

---

## 18. Traps

Traps indicate exceptional conditions in hardware specifications.

| Trap | Description |
|------|-------------|
| `UNPREDICTABLE` | Behaviour is architecturally unpredictable |
| `UNDEFINED` | Instruction is undefined |
| `SEE` | "See" reference (cross-reference to another encoding) |

Usage:

```aml
if invalid_encoding then UNPREDICTABLE;
if reserved_op then UNDEFINED;
```

---

## 19. Serialisation

Armlet supports binary serialisation of values.

### Binary Format

- Magic number: `0x4C4D5241` (`ARML`)
- Version: 1
- Supports little-endian and big-endian

### Operations

```aml
serialize("output.bin", variable);
restored = deserialize("output.bin");
```

All primitive and composite types can be serialised, including structures,
arrays, and bit vectors with named bits.

---

## 20. Operator Precedence

From lowest to highest:

| Precedence | Operators | Associativity |
|------------|-----------|---------------|
| 1 | `=` (assignment) | Right |
| 2 | `if-then-else` (inline) | Right |
| 3 | `\|\|` | Left |
| 4 | `&&` | Left |
| 5 | `OR` | Left |
| 6 | `EOR` | Left |
| 7 | `AND` | Left |
| 8 | `==`, `!=`, `<`, `>`, `<=`, `>=`, `IN` | Left |
| 9 | `:` (concatenation) | Left |
| 10 | `<<`, `>>` | Left |
| 11 | `+`, `-` | Left |
| 12 | `*`, `/`, `DIV`, `MOD` | Left |
| 13 | `^` (power) | Right |
| 14 | `!`, `-` (unary) | Right |
| 15 | `<>` (bit select), `.` (field) | Left |

Use parentheses to override default precedence.
