# `io` package - Alta Standard Library
This package contains I/O constructs bridged over from C, wrapped up to be more Alta-style. It also contains some Alta-specific useful utilities.

## Depends on
  * `lib/stdio` from [`libc`](libc.md)
  * [`string`](string.md)
  * [`types`](types.md)
  * [`exceptions`](exceptions.md)
  * [`util`](util.md)
  * [`lib/limits`] from [`libc`](libc.md)

Exceptions
---
## `OpenFailure`
Indicates a failure to open the given file

## `CloseFailure`
**Summary**: Indicates a failure to close the given file

## `ReadFailure`
**Summary**: Indicates a failure to read the given file

## `EndOfFile`
**Summary**: Indicates an unexpected end-of-file

Type Aliases
---
## `ConsoleData`
**Alias for**: `[string].String | double | int | char | bool`

**Summary**: Union of types that can be printed by the `print`, `printLine`, and `printError` functions

Classes
---
## `File`
**Has copy constructor**: Yes\
**Has destructor**: No

**Summary**: A wrapper around C's native stdio `FILE` handles

### Constructors
#### `constructor()`
**Summary**: Creates an empty File instance, unassociated with any native file handle

#### `constructor(path: [types].rawconststring, mode: [types].rawconststring = "r")`
**Summary**: Creates a File instance and associates with a newly-opened native file handle to file at the given `path`, with the given `mode`

**Parameters**:
  * `path`:  The path to the file to open new native file handle to
  * `mode`:  The mode to open the file in. See [the C reference on fopen](https://en.cppreference.com/w/c/io/fopen) for possible values

**Notes**

By default, the file is opened for reading only (i.e. `mode` is set to "r" for "read").

### Methods
#### `open(path: [types].rawconststring, mode: [types].rawconststring): void`
**Summary**: Opens a new native file handle at the given `path` with the given `mode` and associates this File instance with it

**Parameters**:
  * `path`:  The path to the file to open new native file handle to
  * `mode`:  The mode to open the file in. See [the C reference on fopen](https://en.cppreference.com/w/c/io/fopen) for possible values

**Returns**: Nothing

**Notes**

Note that this *will* discard any handle this File is currently associated with.

#### `close(): void`
**Summary**: Closes the native file handle this File instance is associated with

#### `read(): byte`
**Summary**: Reads a single byte from the native file handle.

#### `read(count: [types].Size): [string].String`
**Summary**: Reads the specified number of bytes from native file handle

**Parameters**:
  * `count`:  The number of bytes to read form the file

**Returns**: A string containing the bytes read, in the order they were found in the file

### Accessors
#### `valid: bool`
**Read**: Yes\
**Write**: No

**Summary**: `true` if this File instance has a native file handle associated with it; otherwise, `false`

#### `file: ptr [libc/lib/stdio].FILE`
**Read**: Yes\
**Write**: No

**Summary**: Gets the native file handle currently associated with this File instance

#### `ended: bool`
**Read**: Yes\
**Write**: No

**Summary**: `true` if the native file handle has reached the end-of-file marker; otherwise, `false`

### Cast Operators
#### `to ptr [libc/lib/stdio].FILE`
**Summary**: Returns the same value as `this.file`.

#### `to bool`
**Summary**: Returns the same value as `this.valid`.

Functions
---
## `print(data: ConsoleData...): void`
**Summary**: Prints the data to the standard output (i.e. C's `stdout`)

**Parameters**:
  * `data`:  The items to print to the console

**Returns**: Nothing

**Notes**

Items can be any type in the `ConsoleData` union and are formatted according to their type:
  * `String`s are printed literally
  * `double`s are string-ified with `[util].floatingPointToString`
  * `int`s are string-ified with `[util].numberToString`
  * `char`s are printed literally
  * `bool`s are printed as `true` or `false`

## `printLine(message: [types].rawconststring): void`
**Summary**: Prints the message to the standard output (i.e. C's `stdout`), adding a new line to the console after doing so

**Parameters**:
  * `message`:  Hi

**Returns**: Nothing

**Notes**

Identical to calling `[libc/lib/stdio].printf` with a string containing a newline as its last character.

## `printLine(data: ConsoleData...): void`
**Summary**: Prints the data to the standard output (i.e. C's `stdout`), adding a new line to the console after doing so

**Parameters**:
  * `data`:  The items to print to the console

**Returns**: Nothing

**Notes**

Follows the same formatting rules as `print`.

## `printError(message: [types].rawconststring): void`
**Summary**: Prints the message to the standard error output (i.e. C's `stderr`), adding a new line to the console after doing so

**Parameters**:
  * `message`:  The message to print

**Returns**: Nothing

**Notes**

Identical to calling `[libc/lib/stdio].fprintf` with `stderr` as the first argument and a string containing a newline as its last character.

## `printError(data: ConsoleData...): void`
**Summary**: Prints the data to the standard error output (i.e. C's `stderr`), adding a new line to the console after doing so

**Parameters**:
  * `data`:  The items to print to the console

**Returns**: Nothing

**Notes**

Follows the same formatting rules as `print`.

## `printf(string: [types].rawconststring, data: any...): int`
**Summary**: C's native `printf` function, exposed to Alta

**Parameters**:
  * `string`:  The native string to print. Must be null-terminated
  * `data`:  Arguments that correspond to the format specifiers in `string` (if there are any)

**Returns**: An integer indicating the number of characters printed
