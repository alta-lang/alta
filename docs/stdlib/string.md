# `string` module - Alta Standard Library
This module contains 

## Depends on
  * [`memory`](memory.md)
  * [`types`](types.md)
  * `lib/string.alta` from [`libc`](libc.md)

Functions
---
## `rawstringLength(string: types.rawconststring): types.Size`
Determines the length of the given raw string.

**Parameters**:
  * `string` = The raw string whose length will be determined

**Returns**: The length of the given raw string

Classes
---
## `String`
**Has copy constructor**: Yes\
**Has destructor**: Yes

### Constructors
#### `constructor()`
Constructs a `String` with a length of 0

#### `constructor(data: types.rawstring, length: types.Size)`
Constructs a `String` by copying the number of characters specified by `length` from `data`.

**Parameters**:
  * `data` = The raw string to copy from
  * `length` = The number of characters to copy from `data`

#### `constructor(data: types.rawstring)`
Identical to `constructor(data: types.rawstring, length: types.Size)`, except that the length of the data is determined automatically using `rawstringLength`

**Parameters**:
  * `data` = The raw string to copy from

### Methods
#### `charAt(i: types.Size): ref byte`
Gets a reference to the character at the specified index. If it does not exist (i.e. the string isn't long enough), it will automatically resize the string to have a length of `i + 1`.

**Parameters**:
  * `i` = The index of the character to retrieve

**Returns**: A reference to the character at the specified index

#### `append(data: types.rawstring, length: types.Size): ref String`
Appends the given string to this String.

**Parameters**
  * `data` = The raw string to copy from
  * `length` = The number of characters to copy from `data`

**Returns**: A reference to this String, to allow call chaining

#### `append(data: types.rawstring): ref String`
**Parameters**:
  * `data` = The raw string to copy from

**Delegates to**: `append(data: types.rawstring, length: types.Size): ref String`
  * `data` = `data`
  * `length` = `rawstringLength(data)`

**Returns**: Whatever the delegate returns

#### `append(string: String): ref String`
**Parameters**:
  * `string` = The String to copy from

**Delegates to**: `append(data: types.rawstring, length: types.Size): ref String`
  * `data` = `string.data`
  * `length` = `string.length`

**Returns** Whatever the delegate returns

### Accessors
#### `data: types.rawconststring`
**Read**: Yes\
**Write**: No

The rawstring (i.e. byte array) respresentation of this String

#### `length: types.Size`
**Read**: Yes\
**Write** No

The total number of characters in this string, excluding the null terminator

#### `size: types.Size`
**Read**: Yes\
**Write**: No

The total number of characters in this string, including the null terminator
