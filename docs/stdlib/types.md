# `types` module - Alta Standard Library
This module contains various useful type definitions used commonly throughout the standard library.

## Depends On
  * `lib/stddef` from [`libc`](libc.md)
  * `lib/stdint` from [`libc`](libc.md)

Types
---
## `Size`
**Defined as**: `size_t` from [`libc`](libc.md)

## `rawstring`
**Defined as**: `ptr byte`

## `rawconststring`
**Defined as**: `ptr const byte`

## `int8`, `int16`, `int32`, `int64`, `uint8`, `uint16`, `uint32`, `uint64`
**Defined as**: their respective bit-sized types in [`libc`](libc.md)(e.g. `uint8` here = `uint8_t` in libc)
