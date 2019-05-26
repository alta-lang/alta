# `util` module - Alta Standard Library
This modules contains various miscellaneous utilities.

## Depends On
  * [`types`](types.md)
  * [`math`](math.md)
  * [`string`](string.md)

Functions
---
## `parseNumber<T>(string: [types].rawstring, length: [types].Size): T`
Parses the number contained in the given string, checking a maximum of `length` characters.

**Generics**:
  * `T` = The type to use for the number's value.
          This type should be usable in the `power` function of the `math` module, in addition to
          being able to be added to itself and constructed from an integer literal.

**Parameters**:
  * `string` = The string to parse
  * `length` = The maximum number of characters to parse

**Returns**: A value of type `T` representing the number contained in the string

## `parseNumber<T>(string: [types].rawstring): T`
**Generics**:
  * `T` = The type to use for the number's value. This type should satisfy the requirements of `T` for `parseNumber<T>([types].rawstring, [types].Size)`

**Parameters**:
  * `string` = The string to parse

**Delegates to**: `parseNumber<T>(string: [types].rawstring, length: [types].Size): T`
  * `string` = `string`
  * `length` = `[string].rawstringLength(string)`

**Returns**: Whatever the delegate returns

## `parseNumber<T>(string: String): T`
**Generics**:
  * `T` = The type to use for the number's value. This type should satisfy the requirements of `T` for `parseNumber<T>([types].rawstring, [types].Size)`

**Parameters**:
  * `string` = The string to parse

**Delegates to**: `parseNumber<T>(string: [types].rawstring, length: [types].Size): T`
  * `string` = `string.data`
  * `length` = `string.length`

**Returns**: Whatever the delegate returns
