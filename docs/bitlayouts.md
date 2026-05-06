# Bitlayouts

A `bitlayout` declares a named, fixed-width bit pattern composed of named
fields and literal bit immediates. It is the primary mechanism for describing
instruction encodings: given a 32-bit (or N-bit) integer, the runtime can
match it against every declared bitlayout, pick the unique one whose
immediates are compatible, and bind its named fields into scope.

This document describes the language-level syntax and runtime semantics.
For a complete worked example with stdlib helpers, see
[`instruction-decoding.md`](instruction-decoding.md). For details of the
matching algorithm itself, see `source/bitlayout_decoder.h`.

## Declaration

```
bitlayout AddShiftedRegister is (
  sf : 1,
  '0001011',
  shift : 2,
  '0',
  Rm : 5,
  imm6 : 6,
  Rn : 5,
  Rd : 5,
);
```

Source: `tests/bitlayout.aml:6-15`.

A declaration has the form

```
bitlayout NAME is ( MEMBER, MEMBER, ... ) [ then do PARAM <block> ] ;
```

Members are listed **MSB first**; the layout's total width is the sum of
member widths. There are exactly two kinds of member:

- **Named field** &mdash; `name : width` &mdash; introduces a `bits(width)` field
  named `name`. At dispatch time, the field's name is attached to the
  corresponding bit range of the input value.
- **Immediate** &mdash; a single-quoted bitstring like `'0001011'` &mdash; an opaque
  literal whose width equals the string length. Every bit must match exactly
  for the layout to be selected during dispatch. Immediates may contain `x`
  for "don't-care" bits (these match any value).

Whitespace around `:` is optional (`sf : 1` and `sf:1` are both valid).

The parser builds an `armlet_ast_bitlayout` (see `source/ast.h:386-461`) and
records two things for the matcher:

- `total` &mdash; the layout's total bit width.
- `compare_mask` &mdash; a `'0'`/`'1'`/`'x'` array describing the fixed bits.
  Named-field positions become `'x'`; immediate positions retain their
  literal bit. This is what the decoder uses to match input values.

## The optional `then do` handler

A bitlayout may carry a handler block that runs when `dispatch()` selects it:

```
bitlayout AddShiftedRegister is (
  sf:1, '0001011', shift:2, '0',
  Rm:5, imm6:6, Rn:5, Rd:5,
) then do input
  print("Dispatching AddShiftedRegister with input: {input}");
  if shift == '11' then EndOfDecode(Decode_UNDEF);
  ...
```

Source: `tests/bitlayout_handler.aml:6-17`.

The optional `PARAM` (`input` above) names a local that is bound to the
**original** bits value the handler was invoked with. It may be omitted, in
which case the handler can still see the named fields but not the raw input.

A bitlayout without a handler can still be instantiated by name (see below),
but cannot be the target of `dispatch()`: the runtime aborts with `Unable
to dispatch bitlayout without a handler`.

## Runtime construction

Calling a bitlayout like a function constructs a value:

```
Instruction ParsedAdd = AddShiftedRegister(0x8b438084);
```

Source: `tests/bitlayout.aml:21`.

The result is a plain `bits(total)` value &mdash; not a distinct struct type &mdash;
with **named-bits metadata** attached. Each named field of the layout is
recorded as a span (`source/interpreter.c:2460`,
`armlet_vm_apply_bitlayout_members`); the immediate members of the layout are
verified against the input bits and produce a runtime error on mismatch.
To avoid runtime errors, bitlayout can be matched against bits of fitting length:

```
bits(32) to_match = ...;
if to_match == AddShiftedRegister then ...
```

## Field access

There are three ways to read or write a named field of a bitlayout value:

**Bring all fields into local scope with `use`:**

```
use ParsedAdd;
if shift == '11' then EndOfDecode(Decode_UNDEF);
if sf == '0' && imm6<5> == '1' then EndOfDecode(Decode_UNDEF);
```

Source: `tests/bitlayout.aml:23-26`. Each named field becomes a local of
type `bits(width)`. Bit-slicing (`imm6<5>`) works on the resulting locals
just like any other `bits` value.

**Direct dot-access on the value:**

```
bits(2) fp = MYBITS.FP;
MYBITS.LL = '1';
MYBITS.xD = '1x1';
```

Source: `tests/namedbits.aml:21-30`. Reads return a `bits(width)` view
into the source value; assignments write into the corresponding range of
the source value in place.

**Inside a `dispatch` handler**, all named fields are pre-bound as locals
automatically &mdash; you do not need to write `use`. See the next section.

## `dispatch(value)`

```
dispatch(0x8b438084);
```

Source: `tests/bitlayout_handler.aml:48`.

`dispatch` is a built-in (`source/interpreter.c:2696`) that:

1. Finalises the decoder on first call, collecting every bitlayout declared
   in the program.
2. Coerces an `integer` argument to `bits(N)` where `N` is the decoder's
   bit width (all bitlayouts must agree on width).
3. Runs the value through the decoder. On match, the matched layout's
   handler is pushed in a fresh frame; the original value is bound to the
   handler's `PARAM` (if declared) and every named field is bound as a
   local. The handler then runs to completion.
4. If no layout matches, raises `No bitlayout matching bitstring: ...`.

The decoder rejects an ambiguous set of layouts at build time: if two
layouts could simultaneously match the same input (i.e.\ no fixed bit
distinguishes them), the program errors out before any dispatch runs.

## Naming bit ranges on plain `bits`

Sometimes you want field-style access without declaring a full bitlayout &mdash;
typically for a CPU register made up of named flags. Use the
`set_bits_range_name` built-in (see [`builtins.md`](builtins.md)):

```
bits(12) MYBITS = '00000000xx11';
set_bits_range_name(MYBITS, "FP", 0, 1);   // 2-bit range, LSB-indexed
set_bits_range_name(MYBITS, "LL", 11);     // single bit
bits(2) fp = MYBITS.FP;
MYBITS.LL = '1';
```

Source: `tests/namedbits.aml`. The dot-access read/write semantics described
above apply to any `bits` value with named-bit metadata, regardless of
whether that metadata came from a bitlayout or from `set_bits_range_name`.

## Implementation pointers

For contributors:

| Concern | Location |
| --- | --- |
| Member AST | `source/ast.h:386-413` |
| Bitlayout AST | `source/ast.h:451-463` |
| Parser | `source/parser.c:2313-2405` (`parse_bitlayout`) |
| Field span population | `source/interpreter.c:2460` (`armlet_vm_apply_bitlayout_members`) |
| Bits + named-bits attach | `source/interpreter.c:2494` (`armlet_vm_bits_to_bits_with_named`) |
| Dispatch built-in | `source/interpreter.c:2696` |
| Decoder API | `source/bitlayout_decoder.h` |
| Decoder build/lookup | `source/bitlayout_decoder.c` |
