# `util` package - Alta Standard Library
This package contains various miscellaneous utilities.

## Depends On
  * [`types`](types.md)
  * [`math`](math.md)
  * [`string`](string.md)
  * `lib/math` from [`libc`](libc.md)
  * [`exceptions`](exceptions.md)

Exceptions
---
## `InvalidDigit`
**Summary**: Indicates that an invalid digit was encountered while trying to parse string into a number

## `InvalidRadix`
**Summary**: Indicates that an invalid radix (also known as a "base") was provided

## `IntegerOverflow`
**Summary**: Indicates that the provided type is not large enough to accommodate the number in the string

Classes
---
## `Pair<T1, T2>`
**Has copy constructor**: Yes (default)\
**Has destructor**: Yes (default)\
**Generics**:
  * `T1`: The type of the first item
  * `T2`: The type of the second item

**Summary**: A simple container for two values of two different types

**Notes**

Note that both types must be default-constructible. This will change in the future, but for now, it is a requirement.

### Constructors
#### `constructor(first: T1, second: T2)`
**Summary**: Creates a new pair instance with copies of the given values

**Parameters**:
  * `first`: The value for the first part of the pair
  * `second`: The value for the second part of the pair

### Public Members
#### `first: T1`
**Summary**: The first part of the pair

#### `second: T2`
**Summary**: The second part of the pair

## `WeakArray<T>`
**Has copy constructor**: Yes (default)\
**Has destructor**: Yes (default)\
**Generics**:
  * `T`: The type of the elements in the array

**Summary**: A non-owning container to view and modify elements in an array

### Constructors
#### `constructor(handle: ptr T, size: [types].Size)`
**Summary**: Creates a new WeakArray for the given array with `size` number of elements

**Parameters**":
  * `handle`: A pointer to the beginning of the array
  * `size`: The number of elements in the array

### Accessors
#### `length: [types].Size`
**Read**: Yes\
**Write**: No

**Summary**: The number of elements in the array

### Cast Operators
#### to `ptr T`
**Summary**: Returns a pointer to the beginning of the underlying array

### Operator Methods
#### `this[[types].Size]: ref T`
**Summary**: Returns the element at the given index in the array

**Parameters**:
  * The index of the desired element

**Returns**: The element at the given index

Functions
---
## `parseNumber<T>(string: [string].String, [radix]: [types].Size): T`
**Summary**: Parses the integer contained in the given string, optionally in a different radix than base-10

**Generics**:
  * `T` = The type to use for the integer's value. This type should be usable in the `power` function of the `math` module, able to be added to itself, able to multiplied by itself, and able to be constructed from an integer literal.

**Parameters**:
  * `string` = The string to parse
  * `radix` = The radix (also known as a "base") of the integer in the string
    * **Defaults to**: `10`

**Returns**: A value of type `T` representing the integer contained in the string

**Notes**

This function **will** throw an `IntegerOverflow` exception if the type is not large enough to accommodate the number in the string.

## `parseFloatingPoint<T>(string: [string].String, [radix]: [types].Size): T`
**Summary**: Parses the floating point number contained in the given string, optionally in a different radix than base-10

**Generics**:
  * `T` = The type to use for the number's value. This type should be usable in the `power` function of the `math` module, able to be added to itself, able to multiplied by itself, and able to be constructed from an integer literal.

**Parameters**:
  * `string` = The string to parse
  * `radix` = The radix (also known as a "base") of the number in the string
    * **Defaults to**: `10`

**Returns**: A value of type `T` representing the integer contained in the string

**Notes**

This function **will** throw an `IntegerOverflow` exception if the type is not large enough to accommodate the number in the string.

## `numberToString<T>(number: T, [radix]: [types].Size): [string].String`
**Summary**: Returns the string representation of the given integer, optionally in a different radix than base-10

**Generics**:
  * `T` = The type to use for the number's value. This type should be usable in the `power` function of the `math` module, able to be subtracted from itself, able to multiplied by itself, able to be divided by itself, able to be used in a modulo with itself, and able to be constructed from an integer literal.

**Parameters**:
  * `number` = The number to stringify
  * `radix` = The radix (also known as a "base") in which to represent the number in the string
    * **Defaults to**: `10`

**Returns**: The string representation of the number in the given radix

## `floatingPointToString<T>(number: T, [radix]: [types].Size): [string].String`
**Summary**: Returns the string representation of the given floating point number, optionally in a different radix than base-10

**Generics**:
  * `T` = The type to use for the number's value. This type should be usable in the `power` function of the `math` module, able to be subtracted from itself, able to multiplied by itself, able to be divided by itself, able to be used in a modulo with itself, and able to be constructed from an integer literal.

**Parameters**:
  * `number` = The number to stringify
  * `radix` = The radix (also known as a "base") in which to represent the number in the string
    * **Defaults to**: `10`

**Returns**: The string representation of the number in the given radix
