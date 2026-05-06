# Built-in Functions

Built-ins are the functions implemented in C and registered in the global
symbol table at VM start. They are called with the same syntax as
user-defined functions and obey the same evaluation rules, but their
arity, types, and side effects are enforced from C.

All built-ins are registered by `armlet_vm_init_builtins` at
`source/interpreter.c:3105`. Adding one means registering it there and
implementing the body via the `ARMLET_BUILTIN_DEFINE` macro.

## I/O & debugging

### print(args...) &rarr; void
*`source/interpreter.c:2855` (`armlet_vm_builtin__print`)*

Variadic. Pretty-prints each argument to stdout, separated by spaces, and
terminates with a newline. Accepts any value: integers, reals, booleans,
strings, `bits(N)`, named-bit values, etc.

```
print(neg, flt, im, large, large_int, vv, xx, fdiv, idiv);
```
Source: `examples/math.aml:17`.


String literals allow for string interpolation using `{name}` syntax.

```
print("this is a [{test}] of \"{string}\" \\ interpolation = {F.X}");
```
Source: `examples/interp.aml:6`

### inspect(args...) &rarr; void
*`source/interpreter.c:2906` (`armlet_vm_builtin__inspect`)*

Variadic. Prints a formatted table for each argument with columns
`NAME`, `TYPE`, `SIZE`, `VALUE`, `REPR` (binary representation). Useful
when `print`'s rendering is too compact for debugging.

```
inspect(R);
```
Source: `examples/bitlayout.aml:49`.

### backtrace() &rarr; void
*`source/interpreter.c:3072` (`armlet_vm_builtin__backtrace`)*

Prints the current call stack. Has no effect on control flow.
See [`diagnostics.md`](diagnostics.md) for the rendering format.

### break() &rarr; void
*`source/interpreter.c:3074` (`armlet_vm_builtin__brk`)*

Drops into the interactive debugger. Intended for development only.

## Bitlayout & named bits

### dispatch(value) &rarr; void
*`source/interpreter.c:2696` (`armlet_vm_builtin__dispatch`)*

Takes an `integer` or `bits(N)` and matches it against every declared
bitlayout. On the unique match, runs that layout's `then do` handler with
its named fields bound as locals.

- An `integer` argument is coerced to `bits` of the decoder's bit width.
- A bitlayout without a handler is not dispatchable &mdash; runtime error.
- No match is a runtime error (`No bitlayout matching bitstring: ...`).
- Ambiguity (two layouts that could match the same input) is rejected
  when the decoder is built, before any dispatch runs.

```
dispatch(0x8b438084);
```
Source: `examples/bitlayout_handler.aml:48`. See [`bitlayouts.md`](bitlayouts.md)
for the full semantic picture.

### set_bits_range_name(target, name, end[, start]) &rarr; void
*`source/interpreter.c:2942` (`armlet_vm_builtin__set_bits_range_name`)*

Attaches a name to a contiguous bit range of `target` so that field-style
access (`target.name` &mdash; read and write) becomes available.

| Param | Type | Meaning |
| --- | --- | --- |
| `target` | `bits(N)` | Variable to annotate. Mutated in place. |
| `name` | `string` | Field name. Must be unique on `target`. |
| `end` | `integer` | LSB-counted index of the low end of the range. |
| `start` | `integer` (optional) | LSB-counted index of the high end. Defaults to `end` (single bit). |

Indices are LSB-based (bit 0 is the least significant). The 4-argument form
covers indices `[end .. start]` inclusive and requires `start >= end`.

```
bits(12) MYBITS = '00000000xx11';
set_bits_range_name(MYBITS, "FP", 0, 1);
set_bits_range_name(MYBITS, "SP", 2, 3);
set_bits_range_name(MYBITS, "xD", 4, 6);
set_bits_range_name(MYBITS, "LL", 11);
```
Source: `examples/namedbits.aml:5-8`.

## Serialization

### serialize(filename, value) &rarr; void
*`source/interpreter.c:2812` (`armlet_vm_builtin__serialize`)*

Writes a binary representation of `value` to `filename` (opened with
`"wb"`). Works on any value the interpreter can round-trip.

### deserialize(filename) &rarr; value
*`source/interpreter.c:2833` (`armlet_vm_builtin__deserialize`)*

Reads back a value previously written with `serialize` (file opened with
`"rb"`). Returns the deserialised value.

```
serialize("value.bin", z);
b = deserialize("value.bin");
```
Source: `examples/serde.aml:12-13`.

## Implementation-defined value capture

These three built-ins together accumulate key/value pairs and write them to
disk. They are intended for emitting the implementation-defined slots that
an architecture spec leaves to the implementer.

### begin_implementation_defined(filename) &rarr; void
*`source/interpreter.c:2795` (`armlet_vm_builtin__begin_implementation_defined`)*

Opens `filename` for writing and starts a fresh hashtable to collect
key/value pairs.

### implementation_defined(key, value) &rarr; void
*`source/interpreter.c:2765` (`armlet_vm_builtin__implementation_defined`)*

Records a `string` key &rarr; arbitrary value pair into the open collection.
Must be called between `begin_implementation_defined` and
`end_implementation_defined`.

### end_implementation_defined() &rarr; void
*`source/interpreter.c:2777` (`armlet_vm_builtin__end_implementation_defined`)*

Serialises the accumulated map to the open file and closes it.

```
begin_implementation_defined("m1.bin");
implementation_defined("Some Value", a);
implementation_defined("Magic Bitstring V1", b);
implementation_defined("Less Magic Bistring V2", c);
end_implementation_defined();
```
Source: `examples/implementation_definition.aml:5-9`.

## Numeric / type conversion

### Real(x) &rarr; real
*`source/interpreter.c:3017` (`armlet_vm_builtin__real`)*

Converts `integer` &rarr; `real` (via GMP &rarr; `double`). A `real` argument is
returned unchanged. Other types are an error.

```
f_cast_985 = (Real(n_ten) * f_pi) ^ 2;
```
Source: `examples/binops.aml:29`.

### RoundUp(x: real) &rarr; integer
*`source/interpreter.c:3011` (`armlet_vm_builtin__round_up`)*

Returns `ceil(x)` as an `integer` (uses C's `ceilf`).

### RoundDown(x: real) &rarr; integer
*`source/interpreter.c:3014` (`armlet_vm_builtin__round_down`)*

Returns `floor(x)` as an `integer` (uses C's `floorf`).

### Log2(x: integer) &rarr; integer
*`source/interpreter.c:3060` (`armlet_vm_builtin__log2`)*

Returns the ceiling of log<sub>2</sub>(x) for a positive integer. Implemented
via GMP: it is `bit_length(x) - 1` when `x` is a power of two, otherwise
`bit_length(x)`. Asserts on non-positive input.

## Quick reference

| Name | Arity | Purpose |
| --- | --- | --- |
| `print` | variadic | Pretty-print values |
| `inspect` | variadic | Tabular value dump |
| `backtrace` | 0 | Print call stack |
| `break` | 0 | Enter debugger |
| `dispatch` | 1 | Match against bitlayouts and run handler |
| `set_bits_range_name` | 3 or 4 | Name a bit range on a `bits` value |
| `serialize` | 2 | Binary write |
| `deserialize` | 1 | Binary read |
| `begin_implementation_defined` | 1 | Open impl-defined collection |
| `implementation_defined` | 2 | Add to collection |
| `end_implementation_defined` | 0 | Flush and close |
| `Real` | 1 | `integer`/`real` &rarr; `real` |
| `RoundUp` | 1 | `real` &rarr; `integer` (ceiling) |
| `RoundDown` | 1 | `real` &rarr; `integer` (floor) |
| `Log2` | 1 | Ceiling of log<sub>2</sub> |
