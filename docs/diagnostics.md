# Diagnostics and Error Handling

This guide covers Armlet's runtime diagnostics: assertions, traps, error
messages, backtraces, and debugging tools.

---

## Assertions

### Syntax

```aml
assert condition;
```

The condition must evaluate to a `boolean`. If it is `FALSE`, execution halts
immediately with an error diagnostic.

### Examples

```aml
assert N > 0;
assert n >= 0 && n <= 31;
assert width IN {8, 16, 32, 64};
assert shift >= 0 && shift < datasize;
```

### What Happens on Failure

When an assertion fails, Armlet:

1. Prints a **backtrace** of the call stack
2. Prints a **source diagnostic** pointing to the assert statement
3. **Terminates** the program with a non-zero exit code

The output looks like:

```
Backtrace:
 frame(2:0x...): DecodeAndExecute
 frame(1:0x...): ValidateEncoding
 frame(0:0x...): <top>

tests/example.aml:15:3:28: ERROR: Assertion Failed
15 |   assert width IN {8, 16, 32, 64};
   |   ^~~~~~~~~~~~~~~~~~~~~~~~~~~~~~^
```

### Assertion Condition Type

The condition **must** be a boolean. Passing a non-boolean value produces a
type error:

```
ERROR: Assert condition must be T_BOOLEAN, got 'T_INTEGER'
```

### When to Use Assertions

- **Parameter validation**: Check function arguments are in range
- **Encoding constraints**: Verify instruction field relationships
- **Invariants**: Confirm assumptions about data sizes and alignments

```aml
bits(N) SafeExtend(bits(M) x, integer N)
    assert N >= M;
    return Zeros(N - M) : x;
```

---

## Traps

Traps signal exceptional conditions in hardware specifications. They represent
situations that are architecturally defined as problematic.

### UNPREDICTABLE

```aml
UNPREDICTABLE;
```

Indicates that the processor's behaviour is architecturally unpredictable.
Execution halts with:

```
Backtrace:
 frame(0:0x...): <top>

tests/traps.aml:3:18:32: ERROR: Execution trapped: unpredictable
3 | if foo == 1 then UNPREDICTABLE;
  |                  ^~~~~~~~~~~~^
```

Use this when an instruction encoding violates constraints but is not
explicitly undefined:

```aml
if d == 15 then UNPREDICTABLE;
if n == d then UNPREDICTABLE;
```

### UNDEFINED

```aml
UNDEFINED;
```

Indicates that the instruction is undefined. Output:

```
ERROR: Execution trapped: undefined
```

Typically used for reserved encodings:

```aml
if opc == '11' then UNDEFINED;
```

### SEE

```aml
SEE "Other encoding variant";
```

A cross-reference trap that indicates the current encoding should be handled by
a different decoder. Output includes the referenced description:

```
ERROR: Execution trapped see: Other encoding variant
```

### Conditional Traps

Traps are typically guarded by conditions:

```aml
if condition then UNPREDICTABLE;
if sf == '0' && imm6<5> == '1' then UNDEFINED;
```

---

## Backtrace

### Manual Backtrace

Call `backtrace()` anywhere to print the current call stack:

```aml
bits(1) One()
    backtrace();
    return '1';
```

Output:

```
Backtrace:
 frame(2:0x...): One
 frame(1:0x...): CallingFunction
 frame(0:0x...): <top>
```

Each frame shows:
- **Frame index**: numbered from the current frame (highest) down to the
  top-level scope (0)
- **Address**: internal memory address (useful for debugging the interpreter)
- **Context name**: the function name, or `<top>` for the top-level scope

### Automatic Backtrace

Backtraces are automatically printed before every error diagnostic. You do not
need to call `backtrace()` manually to get a stack trace on errors -- it
happens automatically for assertion failures, traps, and runtime errors.

---

## Runtime Errors

Beyond assertions and traps, various runtime conditions produce error
diagnostics.

### Common Runtime Errors

#### Symbol Not Found

```aml
a = NonExistent();
```

```
ERROR: Symbol 'NonExistent' not found
```

Caused by referencing an undefined variable or function.

#### Symbol Already Exists

```
ERROR: Symbol 'x' already exists
```

Caused by redeclaring a variable that is already in scope.

#### Type Mismatch in Bitwise Operations

```
ERROR: Arguments to bitstring operators must be of same size greater
       than zero, got 4 and 8
```

Bitwise `AND`, `OR`, `EOR` require both operands to have the same bit width.

#### Unsupported Operation

```
ERROR: Unsupported binary ADD operator for T_BOOLEAN
```

Attempting an operation on a type that does not support it.

#### Invalid Dereference

```
ERROR: Type 'T_INTEGER' cannot be dereferenced into via 'field'
```

Trying to access a field on a non-structure type.

#### Polymorphic Dispatch Failure

When no matching overload is found for a polymorphic function call, the error
lists the available overloads and the types of the provided arguments.

#### Invalid IN Operand

```
ERROR: Right operand of the IN operator must be a set, got: 'T_INTEGER'
```

The right side of `IN` must be a set literal `{...}`.

---

## Error Diagnostic Format

All error messages follow a consistent format:

```
file.aml:LINE:COL_START:COL_END: ERROR: message
LINE_NO | source line content
        | ^~~~~~~~~~~~~~~~^
```

The components are:

| Part | Description |
|------|-------------|
| `file.aml` | Source file path |
| `LINE` | Line number (1-based) |
| `COL_START` | Starting column of the error |
| `COL_END` | Ending column of the error |
| `ERROR` | Severity (always ERROR; displayed in red when the terminal supports colour) |
| `message` | Human-readable error description |

The second and third lines show the source code with a caret underline
highlighting the problematic span. For single-character errors, a single `^` is
shown. For multi-character spans, `^~~~^` brackets the range.

### Multi-Line Spans

When an error spans multiple lines, each line is shown with its own underline:

```
10 |   constant bits(datasize) operand2 = ShiftReg(
   |   ^~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~^
11 |       m, shift_type, shift_amount, datasize
   | ^~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~^
12 |   );
   | ^~^
```

### Tab Handling

Tabs in source code are expanded to 8-space tabstops for consistent alignment
in diagnostics.

---

## Debugging Tools

### print

Print values in a formatted table:

```aml
print(a, b, c);
```

Output is displayed as a table with variable names, types, and values.

### String Interpolation in print

```aml
print("register X{d} = {X[d, 64]}");
```

Embed expressions in strings with `{...}` for formatted output.

### inspect

Provides more detailed information than `print`:

```aml
inspect(result);
```

Shows internal type information and complete value representation.

### Debug Mode

When the interpreter is run with debug mode enabled, error messages include the
interpreter source location that generated the error:

```
ERROR: Assertion Failed [interpreter.c:4321]
```

This is primarily useful for debugging the interpreter itself, not user code.

---

## Summary of Diagnostic Mechanisms

| Mechanism | Purpose | Recoverable? |
|-----------|---------|-------------|
| `assert` | Runtime invariant check | No -- terminates |
| `UNPREDICTABLE` | Architecture-level unpredictable behaviour | No -- terminates |
| `UNDEFINED` | Undefined instruction | No -- terminates |
| `SEE` | Cross-reference to another encoding | No -- terminates |
| `EndOfDecode(...)` | Structured decode result dispatch | Depends on reason |
| `backtrace()` | Print call stack | Yes -- informational only |
| `print(...)` | Print values | Yes -- informational only |
| `inspect(...)` | Detailed value dump | Yes -- informational only |

All terminating diagnostics follow the same pattern: backtrace, then source
diagnostic with location and underline, then exit.
