# `math` module - Alta Standard Library
This module provides various math-related utilities.

## Depends On
  * `lib/stdlib` from [`libc`](libc.md)

Functions
---
## `power<T>(num: T, to: T): T`
**Summary**: A generic, pure-Alta implemention of a power function.

**Generics**:
  * `T`: The type to use for all the values.
          This type must support multiplication, equal-to comparison, and greater-than comparison.

**Parameters**:
  * `num`: The number to raise (i.e. to multiply by itself; the base)
  * `to`: The power to raise it to (i.e. to the number of times to multiply `num` by itself; the exponent)

**Returns**: The result of raising `num` to the `to`th power (i.e. multiplying `num` by itself `to` times)

## `abs(n: int): int`
**Summary**: This is C's native `abs` function, exposed to Alta.

**Parameters**:
  * `n`: The integer whose absolute value will be determined

**Returns**: An integer representing `n`'s absolute distance from 0

## `rand(): int`
**Summary**: This is C's native `rand` function, exposed to Alta.

**Returns**: A psuedo-randomly generated integer
