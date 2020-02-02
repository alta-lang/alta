# `types` package - Alta Standard Library
This package contains various useful type definitions used commonly throughout the standard library and most Alta code.

## Depends On
  * `lib/stddef` from [`libc`](libc.md)
  * `lib/stdint` from [`libc`](libc.md)

Type Aliases
---
## `Size`
**Alias for**: `[libc/lib/stddef].size_t`

## `rawstring`
**Alias for**: `ptr byte`

## `rawconststring`
**Alias for**: `ptr const byte`

## `int8`
**Alias for**: `[libc/lib/stdint].int8_t`

## `int16`
**Alias for**: `[libc/lib/stdint].int16_t`

## `int32`
**Alias for**: `[libc/lib/stdint].int32_t`

## `int64`
**Alias for**: `[libc/lib/stdint].int64_t`

## `uint8`
**Alias for**: `[libc/lib/stdint].uint8_t`

## `uint16`
**Alias for**: `[libc/lib/stdint].uint16_t`

## `uint32`
**Alias for**: `[libc/lib/stdint].uint32_t`

## `uint64`
**Alias for**: `[libc/lib/stdint].uint64_t`

## `Int8`
**Alias for**: `[libc/lib/stdint].int8_t`

## `Int16`
**Alias for**: `[libc/lib/stdint].int16_t`

## `Int32`
**Alias for**: `[libc/lib/stdint].int32_t`

## `Int64`
**Alias for**: `[libc/lib/stdint].int64_t`

## `UInt8`
**Alias for**: `[libc/lib/stdint].uint8_t`

## `UInt16`
**Alias for**: `[libc/lib/stdint].uint16_t`

## `UInt32`
**Alias for**: `[libc/lib/stdint].uint32_t`

## `UInt64`
**Alias for**: `[libc/lib/stdint].uint64_t`

Constants
---
## `Int8Minimum`
**Summary**: The minimum value of an Int8

## `Int8Maximum`
**Summary**: The maximum value of an Int8

## `UInt8Maximum`
**Summary**: The maximum value of a UInt8

## `Int16Minimum`
**Summary**: The minimum value of an Int16

## `Int16Maximum`
**Summary**: The maximum value of an Int16

## `UInt16Maximum`
**Summary**: The maximum value of a UInt16

## `Int32Minimum`
**Summary**: The minimum value of an Int32

## `Int32Maximum`
**Summary**: The maximum value of an Int32

## `UInt32Maximum`
**Summary**: The maximum value of a UInt32

## `Int64Minimum`
**Summary**: The minimum value of an Int64

## `Int64Maximum`
**Summary**: The maximum value of an Int64

## `UInt64Maximum`
**Summary**: The maximum value of a UInt64

## `SizeMaximum`
**Summary**: The maximum value of a Size

Bitfields
---
## `UInt64ToUInt32`
**Summary**: Splits a UInt64 into two UInt32s

**Members**:
  * `part1: uint32` - Bits 0 up to 32: The first UInt32
  * `part2: uint32` - Bits 32 up to 64: The second UInt32

## `UInt64ToUInt16`
**Summary**: Splits a UInt64 into four UInt16s

**Members**:
  * `part1: uint16` - Bits 0 up to 16: The first UInt16
  * `part2: uint16` - Bits 16 up to 32: The second UInt16
  * `part3: uint16` - Bits 32 up to 48: The third UInt16
  * `part4: uint16` - Bits 48 up to 64: The fourth UInt16

## `UInt64ToUInt8`
**Summary**: Splits a UInt64 into eight UInt8s

**Members**:
  * `part1: uint8` - Bits 0 up to 8: The first UInt8
  * `part2: uint8` - Bits 8 up to 16: The second UInt8
  * `part3: uint8` - Bits 16 up to 24: The third UInt8
  * `part4: uint8` - Bits 24 up to 32: The fourth UInt8
  * `part5: uint8` - Bits 32 up to 40: The fifth UInt8
  * `part6: uint8` - Bits 40 up to 48: The sixth UInt8
  * `part7: uint8` - Bits 48 up to 56: The seventh UInt8
  * `part8: uint8` - Bits 56 up to 64: The eighth UInt8

## `UInt32ToUInt16`
**Summary**: Splits a UInt32 into two UInt16s

**Members**:
  * `part1: uint16` - Bits 0 up to 16: The first UInt16
  * `part2: uint16` - Bits 16 up to 32: The second UInt16

## `UInt32ToUInt8`
**Summary**: Splits a UInt32 into four UInt8s

**Members**:
  * `part1: uint8` - Bits 0 up to 8: The first UInt8
  * `part2: uint8` - Bits 8 up to 16: The second UInt8
  * `part3: uint8` - Bits 16 up to 24: The third UInt8
  * `part4: uint8` - Bits 24 up to 32: The fourth UInt8

## `UInt16ToUInt8`
**Summary**: Splits a UInt16 into two UInt8s

**Members**:
  * `part1: uint8` - Bits 0 up to 8 - The first UInt8
  * `part2: uint8` - Bits 8 up to 16 - The second UInt8
