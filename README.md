<div align="center">

# Glyth

<img src="./logo.svg" alt="Glyth Logo" width="200"/>

**A statically-typed language where types know their limits and makes the machine bend to your will.**

[Getting Started](#your-first-ritual) • [Grammar Diagram](https://laluxx.github.io/glyth/diagram.xhtml) • [Type System](#types-that-know-their-boundaries) • [Operators](#the-hierarchy-of-operations)

![Supports LLVM](https://img.shields.io/badge/LLVM-Powered-orange?style=flat-square)
![Language](https://img.shields.io/badge/Language-C-blue?style=flat-square)
![Status](https://img.shields.io/badge/Status-Experimental-red?style=flat-square)

</div>

---

### Table of Contents
- [Introduction](#introduction)
- [Your First Ritual](#your-first-ritual)
- [Type System](#types-that-know-their-boundaries)
- [Operators](#the-hierarchy-of-operations)
- [Type Enforcement](#type-enforcement)
- [Compiler Architecture](#parsing-architecture)
- [Contributing](#contributing)

---

# Introduction

<img src="./logo.svg" align="right" width="150"/>

Glyth is a compiled programming language that channels raw computational power through LLVM. No runtime bloat, no implicit conversions, just explicit types and predictable behavior.

Every numeric type has well-defined boundaries. Cross them and the compiler stops you before the machine does. Operators are overloaded for each type with explicit semantics—same symbols, different rituals for integers, floats, booleans, characters, and strings.

Built with LLVM-C API for code generation, Glyth produces native executables with full optimization support.

**[Explore the interactive grammar diagram →](https://laluxx.github.io/glyth/diagram.xhtml)**

---

## Your First Ritual

```glyth
defn main :: i32 {
  "main is the entry
point of your program."
    display "Hello, Mortal!\n"
    return 0
}
```

That string on the first line? It's a **docstring**—optional documentation for your function. Use it or don't. The compiler doesn't judge.

### Build The Compiler

```bash
make
```

### Compile Your First Program

Save the code above to `ritual.glth`, then:

```bash
./glyth ritual.glth -o ritual
./ritual
```

You should see:
```
Hello, Mortal!
```

### Compiler Flags

```bash
./glyth [options] <source_file>
```

| Flag         | What It Does                          |
|--------------|---------------------------------------|
| `-o <file>`  | Name your executable (default: a.out) |
| `--emit-ir`  | See the LLVM IR (.ll file)            |
| `--emit-bc`  | Get LLVM bitcode (.bc file)           |
| `--emit-all` | Emit IR, bitcode, AND executable      |
| `--/version` | Print version number and exit         |

---

## Types That Know Their Boundaries

Every number has limits. Cross them and the compiler stops you before the machine does.

### Primitives

| Type   | What It Holds          | Size    |
|--------|------------------------|---------|
| `void` | The absence of value   | 0       |
| `bool` | true or false          | 1 bit   |
| `char` | Single ASCII character | 8 bits  |
| `str`  | String of characters   | pointer |

### Signed Integers (i*)

| Type   | Range                                                   |
|--------|---------------------------------------------------------|
| `i8`   | -128 to 127                                             |
| `i16`  | -32,768 to 32,767                                       |
| `i32`  | -2,147,483,648 to 2,147,483,647                         |
| `i64`  | -9,223,372,036,854,775,808 to 9,223,372,036,854,775,807 |
| `i128` | ±170,141,183,460,469,231,731,687,303,715,884,105,728    |

### Unsigned Integers (u*)

| Type   | Range                                                    |
|--------|----------------------------------------------------------|
| `u8`   | 0 to 255                                                 |
| `u16`  | 0 to 65,535                                              |
| `u32`  | 0 to 4,294,967,295                                       |
| `u64`  | 0 to 18,446,744,073,709,551,615                          |
| `u128` | 0 to 340,282,366,920,938,463,463,374,607,431,768,211,455 |

### Floating-Point

| Type  | Range          | Precision  |
|-------|----------------|------------|
| `f32` | ±3.4028235e38  | ~7 digits  |
| `f64` | ±1.7976931e308 | ~15 digits |

### Interrogate The Types

Every numeric type knows its own prison:

```glyth
defn show_bounds :: void {
    display i8.min     ; -128
    newline
    display i8.max     ; 127
    newline
    display u32.max    ; 4294967295
    newline
    display f64.min    ; -1.7976931e308
}
```

The compiler expands `type.min` and `type.max` at compile time. No runtime cost. No mercy for out-of-bounds values.

---

## The Hierarchy of Operations

Operators are overloaded for each type. Same symbols, different rules.

### Precedence (Binds Tightest to Loosest)

```
parse_expression
  ├─ parse_equality                   (lowest: == !=)
  │   ├─ parse_comparison             (< <= > >=)
  │   │   ├─ parse_additive           (+ -)
  │   │   │   ├─ parse_multiplicative (* /)
  │   │   │   │   └─ parse_primary    (literals, parens)
```

Higher in the tree = evaluated first. `3 + 4 * 5` becomes `3 + (4 * 5)` because multiplication descends deeper.

---

## Integer Arithmetic

The expected operations. Overflow wraps silently (for now).

```glyth
defn test_int_ops :: i32 {
    return 10 + 20      ; 30
}

defn precedence :: i32 {
    return 2 + 3 * 4    ; 14, not 20
}

defn parentheses :: i32 {
    return (2 + 3) * 4  ; 20
}
```

**Division respects signedness:**
- `i32 / i32` → signed division (truncates toward zero)
- `u32 / u32` → unsigned division (floor division)

### Integer Comparisons

```glyth
defn signed_compare :: bool {
    return -5 < 10      ; true (signed comparison)
}

defn unsigned_compare :: bool {
    return 255 > 10     ; true (unsigned comparison for u8)
}
```

The compiler picks the right LLVM instruction based on your type:
- Signed: `LLVMIntSLT`, `LLVMIntSGT`
- Unsigned: `LLVMIntULT`, `LLVMIntUGT`

---

## Floating-Point Arithmetic

IEEE 754 semantics. NaN propagates. Ordered comparisons.

```glyth
defn float_ops :: f32 {
    return 3.14 + 2.86  ; 6.0
}

defn float_precision :: f32 {
    return 1.5 + 2.0 * 3.0  ; 7.5
}

defn float_compare :: bool {
    return 3.14 == 3.14  ; true
}
```

All float comparisons are **ordered** (OEQ, OLT, etc.). If either operand is NaN, the result is false.

---

## Boolean Logic (Through Arithmetic)

Booleans are 1-bit integers. Operators map to bitwise logic.

```glyth
defn bool_and :: bool {
    return true * false    ; AND → false
}

defn bool_or :: bool {
    return true + false    ; OR → true
}

defn bool_xor :: bool {
    return true != false   ; XOR → true
}

defn bool_implies :: bool {
    return false <= true   ; IMPLIES → true
}
```

**Full operator mapping:**

| Operator | Logic           | Bitwise    |
|----------|-----------------|------------|
| `+`      | OR              | `a | b`    |
| `-`      | AND NOT         | `a & ~b`   |
| `*`      | AND             | `a & b`    |
| `/`      | AND             | `a & b`    |
| `==`     | XNOR            | `!(a ^ b)` |
| `!=`     | XOR             | `a ^ b`    |
| `<`      | NOT A AND B     | `!a & b`   |
| `<=`     | A IMPLIES B     | `!a | b`   |
| `>`      | A AND NOT B     | `a & !b`   |
| `>=`     | B IMPLIES A     | `a | !b`   |

### Mixing Booleans With Integers

Booleans zero-extend to `i32` when mixed:

```glyth
defn bool_plus_int :: i32 {
    return true + 5     ; 6
}

defn int_minus_bool :: i32 {
    return 10 - false   ; 10
}
```

---

## Character Manipulation

Characters are unsigned 8-bit integers. All integer operations apply.

```glyth
defn ascii_shift :: char {
    return 'A' + 32     ; 'a' (uppercase to lowercase)
}

defn char_distance :: char {
    return 'z' - 'a'    ; 25 (chars between 'a' and 'z')
}

defn char_compare :: bool {
    return 'a' < 'z'    ; true
}
```

---

## String Sorcery

Strings have overloaded operators for common transformations.

### Concatenation (+)

```glyth
defn concat :: str {
    return "Hello, " + "World!"  ; "Hello, World!"
}

defn multi_concat :: str {
    return "One" + "Two" + "Three"  ; "OneTwoThree"
}
```

### Removal (-)

**String - String:** Remove all occurrences

```glyth
defn remove_substring :: str {
    return "HelloWorldHello" - "Hello"  ; "World"
}
```

**String - Integer:** Trim N characters from end

```glyth
defn trim :: str {
    return "Hello" - 2  ; "Hel"
}
```

### Repetition (*)

**String * Integer** (or **Integer * String**)

```glyth
defn repeat :: str {
    return "ab" * 3     ; "ababab"
}
```

### Padding (+)

**String + Integer:** Pad with spaces

```glyth
defn pad :: str {
    return "Hello" + 3  ; "Hello   "
}
```

### String Comparisons

| Operation      | Behavior                                      |
|----------------|-----------------------------------------------|
| `str1 == str2` | Content equality (via `strcmp`)               |
| `str1 != str2` | Content inequality                            |
| `str1 < str2`  | Length comparison: `strlen(str1) < strlen(str2)` |
| `str1 <= str2` | Length comparison: `strlen(str1) <= strlen(str2)`|
| `str1 > str2`  | Length comparison: `strlen(str1) > strlen(str2)` |
| `str1 >= str2` | Length comparison: `strlen(str1) >= strlen(str2)`|

**Why length instead of lexicographic?** Because this is a low-level language. You work with buffers, not sorted menus.

```glyth
defn string_length_compare :: bool {
    return "hi" < "hello"  ; true (2 < 5)
}

defn string_equality :: bool {
    return "test" == "test"  ; true (strcmp)
}
```

---

## Type Enforcement

Glyth doesn't convert types behind your back. Ever.

### No Implicit Conversions

```glyth
defn float_to_int :: i32 {
    return 3.14  ; ERROR: incompatible types
}

defn out_of_range :: u8 {
    return 256   ; ERROR: 256 doesn't fit in u8 (0-255)
}

defn negative_unsigned :: u8 {
    return -1    ; ERROR: -1 not in range 0-255
}
```

### What's Allowed

| From      | To        | Allowed?             |
|-----------|-----------|----------------------|
| `i32`     | `i32`     | ✓                    |
| `i32`     | `i64`     | ✗ (no implicit cast) |
| `i32`     | `f32`     | ✗                    |
| `f64`     | `i32`     | ✗                    |
| `bool`    | `i32`     | Only via operators   |
| `char`    | `str`     | ✗                    |

---

## Parsing Architecture

Glyth uses **recursive descent** with **operator precedence climbing**.

Each precedence level is a parsing function. Higher precedence = deeper in recursion.

**Example:** `5 + 3 > 6`

1. `parse_equality` calls `parse_comparison`
2. `parse_comparison` calls `parse_additive`
3. `parse_additive` parses `5`, sees `+`, calls `parse_multiplicative` for `3`
4. Returns `BinaryOp(ADD, 5, 3)` to `parse_comparison`
5. `parse_comparison` sees `>`, calls `parse_additive` for `6`
6. Returns `BinaryOp(GT, BinaryOp(ADD, 5, 3), 6)`

Result: `(5 + 3) > 6` → `8 > 6` → `true`

---


## Planned Features

### Pattern Matching
```haskell
-- Explicit matching

defn lastInArray :: [a] -> a {
  match [a] {
    [] = error "No end for empty array"
    [x] = x
    [_:tail] = lastInArray tail
  }
}

-- Or Implicit
defn lastInList :: (a) -> a {
  () = error "No end for empty list"
  (x) = x
  (_:tail) = lastInList tail
}
```

### REPL
```bash
glyth repl              # Clean REPL
glyth repl a.out        # Load compiled binary's symbols
glyth -r src.glth       # enter repl after compiling so we still have all the informations about the program like function names etc..
```

## Contributing

The compiler is incomplete. Bugs lurk in the shadows. Contributions welcome.

---
