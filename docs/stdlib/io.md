# `io` module - Alta Standard Library
This module contains I/O constructs bridged over from C, wrapped up to be more Alta-style. It also conatins some Alta-specific useful utilities.

## Depends on
  * C
    * [`stdio.h`](https://en.cppreference.com/w/cpp/header/cstdio)

## Functions
### `printf(data: ptr byte): int`
This is C's native `printf` function, exposed to Alta, although it doesn't support
variable arguments (varargs) yet, so no printing values for now.

  * **Parameters**
    * `data` = The native string to print. Must be null-terminated
  * **Returns**: A integer indicating the number of characters printed
