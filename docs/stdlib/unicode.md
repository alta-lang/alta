# `unicode` package - Alta Standard Library
This package provides utilities for working with and converting between various Unicode formats (like UTF8, UTF16, and UTF32).

## Depends On
  * [`types`](types.md)
  * [`exceptions`](exceptions.md)
  * [`vector`](vector.md)

Exceptions
---
## `UnpairedSurrogate`
**Summary**: Indicates that only one half of a surrogate pair was found

Bitfields
---
## `UTF8MultiByte`
**Summary**: Destructures a UTF32 value into its four UTF8 components

**Members**:
  * `first`: Bits 0 up to 6
  * `second`: Bits 6 up to 12
  * `third`: Bits 12 up to 18
  * `fourth`: Bits 18 up to 21

Functions
---
## `detectRequiredUTF8Bytes(leadingByte: [types].uint8): [types].uint8`
**Summary**: Checks how many bytes should follow the `leadingByte` (including the `leadingByte`)

**Parameters**:
  * `leadingByte`: The leading byte of a UTF8 sequence

**Returns**: The number of bytes that should follow the leading byte plus 1 (for the leading byte itself)

**Notes**

This function essentially checks what bits are set in `leadingByte` and from there it determines how many bytes are *supposed* to follow the leading byte (note that the return value includes the leading byte).

This function performs **no** checks to see if the byte is valid.

This function will always return a value of 1, 2, 3, or 4.

## `detectRequiredUTF8BytesForUTF32(utf32: [types].uint32): [types].uint8`
**Summary**: Determines how many bytes are necessary to represent the given Unicode code point in UTF8

**Parameters**:
  * `utf32`: The Unicode code point represented in UTF32

**Returns**: The number of bytes necessary to represent the given Unicode code point

**Notes**

This function will always return a value of 1, 2, 3, or 4.

## `isSurrogateLeadingUnit(leadingUnit: [types].uint16): bool`
**Summary**: Checks if the given UTF16 value is the leading unit of a surrogate pair

**Parameters**:
  * `leadingUnit`: The value to check

**Returns**: `true` if the value is the leading unit of a surrogate pair, `false` otherwise

## `isSurrogateTrailingUnit(trailingUnit: [types].uint16): bool`
**Summary**: Checks if the given UTF16 value is the trailing unit of a surrogate pair

**Parameters**:
  * `trailingUnit`: The value to check

**Returns**: `true` if the value is the trailing unit of a surrogate pair, `false` otherwise

## `needsSurrogatePair(utf32: [types].uint32): bool`
**Summary**: Checks if the code point represented by the given UTF32 value needs a surrogate to be represented in UTF16

**Parameters**:
  * `utf32`: A UTF32 value representing the code point to check

**Returns**: `true` if the code point needs a surrogate to be represented in UTF16, `false` otherwise

## `split(utf16: [vector].Vector<[types].uint16>, littleEndian: bool): [vector].Vector<[types].uint8>`
**Summary**: Splits the given UInt16 values into UInt8s

**Parameters**:
  * `utf16`: A vector of UInt16s to split
  * `littleEndian`: If `true`, splits the values in little endian format, otherwise splits the values in big endian format

**Returns**: A vector of UInt8 bytes

**Notes**

This function is primarily meant to be used internally by the UTF conversion functions, but it is exported because it can be a useful general utility for splitting values into smaller values. Due to the fact that it isn't directly related to Unicode, this function and its UInt32 variant may be moved into the `util` package in the future.

Splitting the values in little endian format means that the lower bit addresses are placed after the higher bit addresses, and big endian means the opposite (i.e. lower bit addresses are placed before the higher bit addresses).

## `split(utf32: [types].uint32, littleEndian: bool): [vector].Vector<[types].uint8>`
**Summary**: Splits the given UInt32 into UInt8s

**Parameters**:
  * `utf32`: The UInt32 to split
  * `littleEndian`: If `true`, splits the value in little endian format, otherwise splits the value in big endian format

**Returns**: A vector of UInt8 bytes

**Notes**

This function is primarily meant to be used internally by the UTF conversion functions, but it is exported because it can be a useful general utility for splitting values into smaller values. Due to the fact that it isn't directly related to Unicode, this function and its UInt16 variant may be moved into the `util` package in the future.

Splitting the values in little endian format means that the lower bit addresses are placed after the higher bit addresses, and big endian means the opposite (i.e. lower bit addresses are placed before the higher bit addresses).

## `utf16ToUTF32(utf16: [vector].Vector<[types].uint16>): [types].uint32`
**Summary**: Converts the code point representation from UTF16 to UTF32

**Parameters**:
  * `utf16`: A vector of UInt16 values for the UTF16 representation of a code point

**Returns**: A UInt32 value for the UTF32 representation of the code point

**Notes**

The input vector must contain at least one value and no more than two. The length of the input vector must also exactly match the number of UInt16s necessary to represent the given code point.

If the vector contains an unpaired surrogate, an `UnpairedSurrogate` exception will be thrown.

## `utf32ToUTF8(utf32: [types].uint32): [vector].Vector<[types].uint8>`
**Summary**: Converts the code point representation from UTF32 to UTF8

**Parameters**:
  * `utf32`: A UInt32 value for the UTF32 representation of a code point

**Returns**: A vector of UInt8s for the UTF8 representation of the code point

## `utf16ToUTF8(utf16: [vector].Vector<[types].uint16>): [vector].Vector<[types].uint8>`
**Summary**: Converts the code point representation from UTF16 to UTF8

**Parameters**:
  * `utf16`: A vector of UInt16 values for the UTF16 representation of a code point

**Returns**: A vector of UInt8 values for the UTF8 representation of a code point

**Notes**

The input vector must contain at least one value and no more than two. The length of the input vector must also exactly match the number of UInt16s necessary to represent the given code point.

If the vector contains an unpaired surrogate, an `UnpairedSurrogate` exception will be thrown.

## `utf32ToUTF16(utf32: [types].uint32): [vector].Vector<[types].uint16>`
**Summary**: Converts the code point representation from UTF32 to UTF16

**Parameters**:
  * `utf16`: A UInt32 value for the UTF32 representation of a code point

**Returns**: A vector of UInt16 values for the UTF16 representation of a code point

## `utf8ToUTF32(utf8: [vector].Vector<[types].uint8>): [types].uint32`
**Summary**: Converts the code point representation from UTF8 to UTF32

**Parameters**:
  * `utf8`: A vector of UInt8 values for the UTF8 representation of a code point

**Returns**: A UInt32 value for the UTF32 representation of a code point

**Notes**

The input vector must contain at least one value and more than four. The length of the input vector must also exactly match the number of UInt8s necessary to represent the given code point.

## `utf8ToUTF16(utf8: [vector].Vector<[types].uint8>): [vector].Vector<[types].uint16>`
**Summary**: Converts the code point representation from UTF8 to UTF16

**Parameters**:
  * `utf8`: A vector of UInt8 values for the UTF8 representation of a code point

**Returns**: A vector of UInt16 values for the UTF16 representation of a code point

**Notes**

The input vector must contain at least one value and more than four. The length of the input vector must also exactly match the number of UInt8s necessary to represent the given code point.
