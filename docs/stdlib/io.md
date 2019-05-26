# `io` module - Alta Standard Library
This module contains I/O constructs bridged over from C, wrapped up to be more Alta-style. It also conatins some Alta-specific useful utilities.

## Depends on
  * `lib/stdio` from [`libc`](libc.md)

Functions
---
## `printf(string: [types].rawconststring, data: any...): int`
This is C's native `printf` function, exposed to Alta.

**Parameters**:
  * `string` = The native string to print. Must be null-terminated
  * `data...` = Arguments that correspond to the format specifiers in `string` (if there are any)

**Returns**: An integer indicating the number of characters printed
