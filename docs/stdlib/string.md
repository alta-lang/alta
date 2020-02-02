# `string` module - Alta Standard Library
This module contains 

## Depends on
  * [`memory`](memory.md)
  * [`types`](types.md)
  * `lib/string.alta` from [`libc`](libc.md)
  * [`exceptions`](exceptions.md)
  * [`unicode`](unicode.md)
  * [`vector`](vector.md)

Functions
---
## `rawstringLength(str: [types].rawconststring): [types].Size`
**Summary**: Determines the length of the given raw string

**Parameters**:
  * `str`: The raw string whose length will be determined

**Returns**: The length of the given raw string

## `rawstringsEqual(str1: [types].rawconststring, str2: [types].rawconststring): bool`
**Summary**: Checks if the two given raw strings are equal

**Parameters**:
  * `str1`: The first raw string to check
  * `str2`: The second raw string to check

**Returns**: `true` if the two raw strings are equal, `false` otherwise

Classes
---
## `String`
**Has copy constructor**: Yes\
**Has destructor**: Yes

### Constructors
#### `constructor()`
**Summary**: Constructs a `String` with a length of 0

#### `constructor(data: [types].rawconststring, length: [types].Size)`
**Summary**: Constructs a String by copying the number of characters specified by `length` from `data`

**Parameters**:
  * `data`: The raw string to copy from
  * `length`: The number of characters to copy from `data`

#### <a name="constructor(data: [types].rawconststring)">`constructor(data: [types].rawconststring)`</a>
**Summary**: Constructs a String by copying the characters in `data`

**Parameters**:
  * `data`: The raw string to copy from

**Is a `from` cast-constructor**: Yes

**Notes**

Identical to `constructor(data: [types].rawconststring, length: [types].Size)`, except that the length of the data is determined automatically using `rawstringLength`.

#### `constructor(length: [types].Size)`
**Summary**: Creates a new String of the specified `length`, filled with null bytes (`\0`)

**Parameters**:
  * `length`: The length of the new String

#### <a name="constructor(data: ref CodePoint)">`constructor(data: ref CodePoint)`</a>
**Summary**: Creates a new String with a single character

**Parameters**:
  * `data`: The character to add to the new string

**Is a `from` cast-constructor**: Yes

### Methods
#### `charAt(i: [types].Size): CodePoint`
**Summary**: Gets a CodePoint reference to the character at the specified index

**Parameters**:
  * `i`: The index of the character to retrieve

**Returns**: A CodePoint that references the character at the specified index

**Notes**

This method returns a CodePoint that references the character the specified index in order to easily handle multibyte UTF8 code points. Any modification of the returned CodePoint will be reflected in the String. Note that reference CodePoints automatically become standalone when they're copied. As such, if the returned CodePoint is copied, updates to the copy will no longer update the character in the String.

If the index does not exist (i.e. the string isn't long enough), it will automatically resize the string to have a length of `i + 1`.

#### `byteAt(i: [types].Size): ref byte`
**Summary**: Get a reference to the byte at the specified index

**Parameters**:
  * `i`: The index of the byte to retrieve

**Returns**: A reference to the byte at the specified index

**Notes**

This method returns a reference to the raw byte at the byte index given. This means that it doesn't take into account multibyte Unicode code points. Instead, each byte is treated as an individual index, regardless of whether it belongs to a multibyte code point or not.

#### `append(data: [types].rawconststring, length: [types].Size): ref String`
**Summary**: Appends `length` number of characters from the given string to this String

**Parameters**
  * `data`: The raw string to copy from
  * `length`: The number of characters to copy from `data`

**Returns**: A reference to this String, to allow call chaining

**Notes**

This is an in-place modification.

#### `append(data: [types].rawconststring): ref String`
**Summary**: Appends the characters from the given string to this String

**Parameters**:
  * `data`: The raw string to copy from

**Returns**: A reference to this String, to allow call chaining

**Notes**

This method is equivalent to calling `append(data: [types].rawconststring, length: [types].Size): ref String` with the following arguments:
  * `data`: `data`
  * `length`: `rawstringLength(data)`

This is an in-place modification.

#### `append(data: String): ref String`
**Summary**: Appends the characters from the given String to this String

**Parameters**:
  * `data`: The String to copy from

**Returns**: A reference to this String, to allow call chaining

**Notes**

This method is equivalent to calling `append(data: [types].rawconststring, length: [types].Size): ref String` with the following arguments:
  * `data`: `data.data`
  * `length`: `data.length`

This is an in-place modification.

#### `append(data: byte): ref String`
**Summary**: Appends the ASCII character to this String

**Parameters**:
  * `data`: The ASCII character to append

**Returns**: A reference to this String, to allow call chaining

**Notes**

This method is equivalent to calling `append(data: [types].rawconststring, length: [types].Size): ref String` with the following arguments:
  * `data`: `&data`
  * `length`: `1`

This is an in-place modification.

#### `append(data: CodePoint): ref String`
**Summary**: Appends the Unicode character represented by the CodePoint to this String

**Parameters**:
  * `data`: A CodePoint representing the Unicode character to append

**Returns**: A reference to this String, to allow call chaining

**Notes**

This is an in-place modification.

#### `append(data: [types].uint32): ref String`
**Summary**: Appends the Unicode character represented by the given UTF32 value to this String

**Parameters**:
  * `data`: A UTF32 value representing the Unicode character to append

**Returns**: A reference to this String, to allow call chaining

**Notes**

This is an in-place modification.

#### `prepend(data: [types].rawconststring, length: [types].Size): ref String`
**Summary**: Prepends `length` number of characters from the given string to this String

**Parameters**
  * `data`: The raw string to copy from
  * `length`: The number of characters to copy from `data`

**Returns**: A reference to this String, to allow call chaining

**Notes**

This is an in-place modification.

#### `prepend(data: [types].rawconststring): ref String`
**Summary**: Prepends the characters from the given string to this String

**Parameters**:
  * `data`: The raw string to copy from

**Returns**: A reference to this String, to allow call chaining

**Notes**

This method is equivalent to calling `prepend(data: [types].rawconststring, length: [types].Size): ref String` with the following arguments:
  * `data`: `data`
  * `length`: `rawstringLength(data)`

This is an in-place modification.

#### `prepend(data: String): ref String`
**Summary**: Prepends the characters from the given String to this String

**Parameters**:
  * `data`: The String to copy from

**Returns**: A reference to this String, to allow call chaining

**Notes**

This method is equivalent to calling `prepend(data: [types].rawconststring, length: [types].Size): ref String` with the following arguments:
  * `data`: `data.data`
  * `length`: `data.length`

This is an in-place modification.

#### `prepend(data: byte): ref String`
**Summary**: Prepends the ASCII character to this String

**Parameters**:
  * `data`: The ASCII character to prepend

**Returns**: A reference to this String, to allow call chaining

**Notes**

This method is equivalent to calling `prepend(data: [types].rawconststring, length: [types].Size): ref String` with the following arguments:
  * `data`: `&data`
  * `length`: `1`

This is an in-place modification.

#### `prepend(data: CodePoint): ref String`
**Summary**: Prepends the Unicode character represented by the CodePoint to this String

**Parameters**:
  * `data`: A CodePoint representing the Unicode character to prepend

**Returns**: A reference to this String, to allow call chaining

**Notes**

This is an in-place modification.

#### `prepend(data: [types].uint32): ref String`
**Summary**: Prepends the Unicode character represented by the given UTF32 value to this String

**Parameters**:
  * `data`: A UTF32 value representing the Unicode character to prepend

**Returns**: A reference to this String, to allow call chaining

**Notes**

This is an in-place modification.

#### `substring(from: [types].Size, to: [types].Size): String`
**Summary**: Creates a copy of this String containing only the characters in the given range

**Parameters**:
  * `from`: The index to start copying from; inclusive
  * `to`: The index to stop copying at; exclusive

**Returns**: A String with a copy of the characters of this String in the given range

#### `substring(from: [types].Size, length: [types].Size): String`
**Summary**: Creates a copy of this String containing only the characters in the given range

**Parameters**:
  * `from`: The index to start copying from; inclusive
  * `length`: The number of characters to copy

**Returns**: A String with a copy of the characters of this String in the given range

#### `substring(from: [types].Size): String`
**Summary**: Creates a copy of this String containing only the characters from the index forward

**Parameters**:
  * `from`: The index to start copying from; inclusive

**Returns**: A String with a copy of the characters of this String after and including the index

#### `substring(to: [types].Size): String`
**Summary**: Creates a copy of this String containing only the characters up to the index

**Parameters**:
  * `to`: The index to stop copying at; exclusive

**Returns**: A String with a copy of the characters of this String up to but not including the index

#### `substring(length: [types].Size): String`
**Summary**: Creates a copy of this String containing only the first `length` number of characters

**Parameters**:
  * `length`: The number of characters to copy, starting from the beginning of this String

**Returns**: A String with a copy of the first `length` number of characters of this String 

#### `includes(data: CodePoint | byte...): bool`
**Summary**: Checks if any of the given characters are present in this String

**Parameters**:
  * `data`: Unicode or ASCII characters (CodePoints or bytes, respectively) to check for in this String

**Returns**: `true` if any of the given characters are present in this String, `false` otherwise

#### `includes(test: (CodePoint) => bool): bool`
**Summary**: Tests the provided callback against each character in this String

**Parameters**:
  * `test`: A callback to test each character with

**Returns**: `true` if the callback returns `true` for any character, `false` otherwise

**Notes**

This method will short-circuit for the first character the callback returns true on. In other words, as long as the callback returns `false`, this method will keep callling the callback with the next character; however, as soon as the callback returns `true`, the method will return `true` as well and no more characters will be tested.

#### `indexOf(data: CodePoint | byte, after: [types].Size): [types].Size`
**Summary**: Finds the index of the first character in this String that matches the provided character, after the given index

**Parameters**:
  * `data`: The Unicode or ASCII character (CodePoint or byte, respectively) to search for
  * `after`: The index to start searching at

**Returns**: The index of the first matching character found after the given index

#### `indexOf(data: CodePoint | byte): [types].Size`
**Summary**: Finds the index of the first character in this String that matches the provided character

**Parameters**:
  * `data`: The Unicode or ASCII character (CodePoint or byte, respectively) to search for

**Returns**: The index of the first matching character found

#### `indexOf(test: (CodePoint) => bool, after: [types].Size): [types].Size`
**Summary**: Finds the index of the first character that makes the callback `true`, after the given index

**Parameters**:
  * `test`: A callback to use to test if the character is the desired character
  * `after`: The index to start searching at

**Returns**: THe index of the first character that satisfies the callback after the given index

#### `indexOf(test: (CodePoint) => bool): [types].Size`
**Summary**: Finds the index of the first character that makes the callback `true`

**Parameters**:
  * `test`: A callback to use to test if the character is the desired character

**Returns**: THe index of the first character that satisfies the callback

#### `lastIndexOf(data: CodePoint | byte, after: [types].Size): [types].Size`
**Summary**: Finds the index of the last character in this String that matches the provided character, after the given index

**Parameters**:
  * `data`: The Unicode or ASCII character (CodePoint or byte, respectively) to search for
  * `after`: The index to start searching at

**Returns**: The index of the last matching character found after the given index

#### `lastIndexOf(data: CodePoint | byte): [types].Size`
**Summary**: Finds the index of the last character in this String that matches the provided character

**Parameters**:
  * `data`: The Unicode or ASCII character (CodePoint or byte, respectively) to search for

**Returns**: The index of the last matching character found

### Accessors
#### `data: [types].rawstring`
**Read**: Yes\
**Write**: No

**Summary**: The raw string respresentation (i.e. byte array) of this String

#### `length: [types].Size`
**Read**: Yes\
**Write** No

**Summary**: The total number of characters in this string, excluding the null terminator

#### `byteLength: [types].Size`
**Read**: Yes\
**Write**: No

**Summary**: The total number of bytes necessary to represent the characters in this string, excluding the null terminator

#### `size: [types].Size`
**Read**: Yes\
**Write**: No

**Summary**: The total number of characters in this string, including the null terminator

#### `items: @generator CodePoint`
**Read**: Yes\
**Write**: No

**Summary**: A generator that produces each character in this String

**Notes**

This is an iterator. It generates CodePoints that reference the characters in this String. These CodePoints work the same as the ones returned by `charAt`: modifying them modifies the character they reference in this String, but copying creates standalone CodePoints that do not modify the characters in this String.

This accessor is intended to make it easy to iterate over the characters in this String, like so:
```alta
for char: CodePoint in myCoolString.items {
  # do whatever you want with the character
}
```

### Cast Operators
#### from `[types].rawconststring`
**Is from cast-constructor**: Yes

**Delegates to**: [`constructor(data: [types].rawconststring)`](#constructor%28data%3A%20%5Btypes%5D%2Erawconststring%29).

#### from `ref CodePoint`
**Is from cast-constructor**: Yes

**Delegates to**: [`constructor(data: ref CodePoint)`](#constructor%28data%3A%20ref%20CodePoint%29).

#### to `[types].rawstring`
**Summary**: Returns the internal raw string managed by this String

**Notes**

This raw string is automatically managed by this String, meaning that it is allocated and deallocated as necessary when the String is constructed, copied, or destroyed. Consumers of this raw string **must not** keep a reference to it for longer than the String that created it is alive.

#### to `bool`
**Summary**: Returns a boolean indicating whether the string is empty or not

**Notes**

`true` if the String is **not** empty, `false` otherwise

### Operator Methods
#### `this + String: String`
**Summary**: Creates a copy of this String and appends the given String to it

**Parameters**:
  * The String to append to the copy of this String

**Returns**: A copy of this String with a copy of the input String appended to it

**Notes**

This methods is equivalent to making a copy of this String and then calling `append` on it with the input String as the only argument to it.

#### `String + this: String`
**Summary**: Creates a copy of the input String and appends a copy of this String to it

**Parameters**:
  * The String to append a copy of this String to

**Returns**: A copy of the input String with a copy of this String appended to it

**Notes**

This right-hand-this operator exists to make it easy to prepend non-String left-hand values. It is effectively the same as converting the left-hand value to a String and then appending `this` to it.

#### `this + CodePoint: String`
**Summary**: Creates a copy of this String and appends the given Unicode character to it

**Parameters**:
  * A CodePoint representing the Unicode character to append to the copy of this String

**Returns**: A copy of this String with the Unicode character appended to it

**Notes**

This methods is equivalent to making a copy of this String and then calling `append` on it with the input CodePoint as the only argument to it.

#### `CodePoint + this: String`
**Summary**: Creates a copy of this String and prepends the given Unicode character to it

**Parameters**:
  * A CodePoint representing the Unicode character to prepend to the copy of this String

**Returns**: A copy of this String with the Unicode character prepended to it

#### `this + [types].uint32: String`
**Summary**: Creates a copy of this String and appends the given UTF32 Unicode character to it

**Parameters**:
  * A UTF32 value representing the Unicode character to append to the copy of this String

**Returns**: A copy of this String with the Unicode character appended to it

**Notes**

This methods is equivalent to making a copy of this String and then calling `append` on it with the input `uint32` as the only argument to it.

#### `[types].uint32 + this: String`
**Summary**: Creates a copy of this String and prepends the given UTF32 Unicode character to it

**Parameters**:
  * A UTF32 value representing the Unicode character to prepend to the copy of this String

**Returns**: A copy of this String with the Unicode character prepended to it

#### `this + byte: String`
**Summary**: Creates a copy of this String and appends the given ASCII character to it

**Parameters**:
  * A single byte representing the ASCII character to append to the copy of this String

**Returns**: A copy of this String with the ASCII character appended to it

**Notes**

This methods is equivalent to making a copy of this String and then calling `append` on it with the input `byte` as the only argument to it.

#### `byte + this: String`
**Summary**: Creates a copy of this String and prepends the given Unicode character to it

**Parameters**:
  * A single byte representing the ASCII character to prepend to the copy of this String

**Returns**: A copy of this String with the ASCII character prepended to it

#### `this += String: ref String`
**Summary**: Appends the given String to this String

**Parameters**:
  * The string to append a copy of

**Returns**: A reference to `this`

**Notes**

This is exactly the same as calling `append` on this String with the input String as the only argument.

#### `this += CodePoint: ref String`
**Summary**: Appends the given Unicode character to this String

**Parameters**:
  * A CodePoint representing the Unicode character to append

**Returns**: A reference to `this`

**Notes**

This is exactly the same as calling `append` on this String with the input CodePoint as the only argument.

#### `this += byte: ref String`
**Summary**: Appends the given ASCII character to this String

**Parameters**:
  * A byte representing the ASCII character to append

**Returns**: A reference to `this`

**Notes**

This is exactly the same as calling `append` on this String with the input `byte` as the only argument.

#### `this += [types].uint32: ref String`
**Summary**: Appends the given Unicode character to this String

**Parameters**:
  * A UTF32 value representing the Unicode character to append

**Returns**: A reference to `this`

**Notes**

This is exactly the same as calling `append` on this String with the input `uint32` as the only argument.

#### `this ^= String: ref String`
**Summary**: Prepends the given String to this String

**Parameters**:
  * The string to prepend a copy of

**Returns**: A reference to `this`

**Notes**

This is exactly the same as calling `prepend` on this String with the input String as the only argument.

#### `this ^= CodePoint: ref String`
**Summary**: Prepends the given Unicode character to this String

**Parameters**:
  * A CodePoint representing the Unicode character to prepend

**Returns**: A reference to `this`

**Notes**

This is exactly the same as calling `prepend` on this String with the input CodePoint as the only argument.

#### `this ^= byte: ref String`
**Summary**: Prepends the given ASCII character to this String

**Parameters**:
  * A byte representing the ASCII character to prepend

**Returns**: A reference to `this`

**Notes**

This is exactly the same as calling `prepend` on this String with the input `byte` as the only argument.

#### `this ^= [types].uint32: ref String`
**Summary**: Prepends the given Unicode character to this String

**Parameters**:
  * A UTF32 value representing the Unicode character to prepend

**Returns**: A reference to `this`

**Notes**

This is exactly the same as calling `prepend` on this String with the input `uint32` as the only argument.

#### `this[[types].Size]: CodePoint`
**Summary**: Retrieves the character at the given index

**Parameters**:
  * The index of the character to reference

**Returns**: A CodePoint that references the character at the given index

**Notes**

This is a convenience operator that is exactly the same as calling `charAt` with the given index as the argument.

See `charAt` for the details on the behavior of the returned CodePoint.

#### `this == String: bool`
**Summary**: Checks if this String and the given String are equal

**Parameters**:
  * The String to compare this String to

**Returns**: `true` if this String is equal to the input String, `false` otherwise

**Notes**

Equality here is determined by 2 conditions:
  * The two strings must be exactly the same length
  * Each character in one string must be equal to the character at the same index as the first in the second string
    * Equality of each character is determined by CodePoint's self-equality operator (i.e. `CodePoint == CodePoint`)

#### `this != String: bool`
**Summary**: Checks if this string and the given String are **not** equal

**Parameters**:
  * The String to compare this String to

**Returns**: `true` if this String is **not** equal to the input String, `false` otherwise

**Notes**

This is equivalent to `!(this == $)`, where `$` is the input String

#### `String == this: bool`
**Summary**: Checks if this String and the given String are equal

**Parameters**:
  * The String to compare this String to

**Returns**: `true` if this String is equal to the input String, `false` otherwise

**Notes**

This operator exists for the same reason that `String + this` exists: to make it easy to use non-String-but-convertable-to-String left-hand values.

This is exactly the same as `this == $`, where `$` is the input String.

#### `String != this: bool`
**Summary**: Checks if this string and the given String are **not** equal

**Parameters**:
  * The String to compare this String to

**Returns**: `true` if this String is **not** equal to the input String, `false` otherwise

**Notes**

This operator exists for the same reason that `String + this` exists: to make it easy to use non-String-but-convertable-to-String left-hand values.

This is exactly the same as `this != $`, where `$` is the input String.

## `CodePoint`
**Has copy constructor**: Yes\
**Has destructor**: Yes (default)

**Summary**: A simple interface for handling UTF8 code points

**Notes**

CodePoints can be created for String characters or they can be standalone code point representations. CodePoints that are copied automatically become standalone code point representations, regardless of whether or not the original CodePoints they were copied from represented entries in a string.

When a CodePoint is created to represent a character in a String, any modifications to that CodePoint will update the character in the String.

### Constructors
#### <a name="constructor(data: [types].uint32)">`constructor(data: [types].uint32)`</a>
**Summary**: Creates a new standalone CodePoint from the given UTF32 code point value

**Parameters**:
  * `data`: The UTF32 code point value to initially assign to this CodePoint

**Is a `from` cast-constructor**: Yes

#### <a name="constructor(data: byte)">`constructor(data: byte)`</a>
**Summary**: Creates a new standalone CodePoint from the given ASCII character value

**Parameters**:
  * `data`: The ASCII character value to initially assign ot this CodePoint

**Is a `from` cast-constructor**: Yes

#### `constructor(data: ref [types].rawstring, length: ref [types].Size, index: [types].Size)`
**Summary**: Creates a CodePoint to represent a character in the given raw string

**Parameters**:
  * `data`: A reference to the raw string the character to represent is in
  * `length`: A reference to a variable that tracks the length of the raw string `data`
  * `index`: The index of the character the new CodePoint will represent

**Notes**

This constructor was originally only added to be used internally by Strings to use CodePoints as simple interfaces for working with multibyte UTF8 code points, but it can be used more generally to treat any character in any raw string like a UTF8 code point. Note that it will automatically handle any necessary operations on the string, such as extending or shrinking the string when the multibyte code point requires it. For example, going from a single byte code point to a multibyte one will automatically extend the string. This also means that the CodePoint expects the input string to have been allocated dynamically (with `[memory].allocate`, `[memory].zeroAllocate`, or their unsafe variants).

### Accessors
#### `index: [types].Size`
**Read**: Yes\
**Write**: No

**Summary**: The index of the character that this CodePoint represents

**Notes**

This is only valid for CodePoints that represent characters in strings. If the CodePoint is standalone, this will always be `0`.

#### `byteIndex: [types].Size`
**Read**: Yes\
**Write**: No

**Summary**: The byte-based index of the character that this CodePoint represents

**Notes**

This is similar to the `index` accessor, but where the `index` accessor counts each code point as an index, this returns the raw byte index of the character. This means that all the bytes in multibyte characters are counted instead of taking them as a whole.

Like `index`, this is only valid for CodePoints that represent characters in strings. If the CodePoint is standalone, this will always be `0`.

#### `byteLength: [types].Size`
**Read**: Yes\
**Write**: No

**Summary**: The number of bytes required to represent the current code point in UTF8

#### `bytes: [types].rawconststring`
**Read**: Yes\
**Write**: No

**Summary**: The underlying raw string containing the character this CodePoint represents

**Notes**

For representative CodePoints, this contains the whole string that has the character the CodePoint represents.

For standalone CodePoints, this contains a string that contains only the represented code point, which is managed by the CodePoint and automatically allocated and deallocated when the CodePoint is constructed, copied, or destroyed.

### Cast Operators
#### from `[types].uint32`
**Is from cast-constructor**: Yes

**Delegates to**: [`constructor(data: [types].uint32)`](#constructor%28data%3A%20%5Btypes%5D%2Euint32%29).

#### from `byte`
**Is from cast-constructor**: Yes

**Delegates to**: [`constructor(data: byte)`](#constructor%28data%3A%20byte%29).

#### to `[types].uint32`
**Summary**: Returns a UTF32 representation of the current code point

#### to `byte`
**Summary**: Tries to return an ASCII representation of the current code point

**Notes**

This operator **will** throw an exception if the current code point cannot be represented in ASCII.

### Operator Methods
#### `this = [types].uint32: ref CodePoint`
**Summary**: Assigns the provided UTF32 code point to this CodePoint

**Parameters**:
  * The new value for this CodePoint

**Returns**: A reference to `this`

**Notes**

This override for the assignment operator is necessary in order to support using CodePoints as simple Unicode interfaces for String characters. On standalone CodePoints, this operator is effectively the same as simple assignment. On representative CodePoints, however, the character that this CodePoint represents is modified in the underlying String.

#### `this = [types].rawconststring: ref CodePoint`
**Summary**: Assigns the provided UTF8 code point to this CodePoint

**Parameters**:
  * The new value for this CodePoint

**Notes**

The provided string **must** be *exactly* as long as necessary to represent the Unicode code point (not counting the null terminator). The reason for the existence of this operator is mainly convenience: Alta source code is expected to be in UTF8 format, and as such, Unicode characters in the source code may be multiple bytes long. This operator allows strings like `"😀"` to be used as code points.

Other than that, the same notes apply as the ones for `this = [types].uint32`:

This override for the assignment operator is necessary in order to support using CodePoints as simple Unicode interfaces for String characters. On standalone CodePoints, this operator is effectively the same as simple assignment. On representative CodePoints, however, the character that this CodePoint represents is modified in the underlying String.

#### `this == CodePoint: bool`
**Summary**: Checks whether this CodePoint equals another CodePoint

**Parameters**:
  * The CodePoint to compare against

**Returns**: `true` if this CodePoint is equal to the other CodePoint, `false` otherwise

**Notes**

Equality is defined as representing the same code point value. In other words, a CodePoint that represents U+1F600 will only be equal to another CodePoint that represents U+1F600.

#### `this != CodePoint: bool`
**Summary**: Checks whether this CodePoint does **not** equal another CodePoint

**Parameters**:
  * The CodePoint to compare against

**Returns**: `true` if this CodePoint is **not** equal to the other CodePoint, `false` otherwise

**Notes**

This is exactly the same as doing `!(this == $)`, where `$` is the input CodePoint.
