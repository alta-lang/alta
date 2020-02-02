# `memory` package - Alta Standard Library
This package exposes lots of necessary and useful functions and classes for working with memory.

## Depends on
  * `lib/stdlib` from [`libc`](libc.md)
  * [`types`](types.md)
  * [`exceptions`](exceptions.md)

Exceptions
---
## `MemoryAllocationFailure`
**Summary**: Indicates a failure to allocate memory

Classes
---
## `Box<T>`
**Has copy constructor**: Yes\
**Has destructor**: Yes\
**Generics**:
  * `T`: Type of the item to manage

**Summary**: A generic reference-counted container for values

**Notes**

Boxes keep a reference count to how many Boxes have a reference to each value. When a Box is copied, the reference count of the value it manages is incremented. When a Box is destroyed, the reference count of the value it manages is decremented. If the count reaches zero, the value is destroyed.

### Constructors
#### `constructor()`
**Summary**: Constructs an empty Box

**Notes**

Empty boxes are the same as Boxes for `nullptr`.

#### <a name="constructor(pointer: ptr T)">`constructor(pointer: ptr T)`</a>
**Summary**: Constructs a Box that manages the value pointed to by `pointer`

**Parameters**:
  * `pointer`: A pointer to the value to manage

**Is a `from` cast-constructor**: Yes

#### <a name="constructor(pointer: ptr T)">`constructor(pointer: ref T)`</a>
**Summary**: Constructs a Box that manages the value referenced by `pointer`

**Parameters**:
  * `pointer`: A reference to the value to manage

**Is a `from` cast-constructor**: Yes

### Cast Operators
#### from `ptr T`
**Is from cast-constructor**: Yes

**Delegates to**: [`constructor(pointer: ptr T)`](#constructor%28pointer%3A%20ptr%20T%29).

#### from `ref T`
**Is from cast-constructor**: Yes

**Delegates to**: [`constructor(pointer: ref T)`](#constructor%28pointer%3A%20ptr%20T%29).

#### to `ptr T`
**Summary**: Returns a pointer to the managed value

#### to `ref T`
**Summary**: Returns a reference to the managed value

#### to `bool`
**Summary**: Returns a boolean indicating whether the Box is empty or not

Functions
---
## `allocate<T>(count: [types].Size): ptr T`
**Summary**: Allocates a memory block big enough for `count` number of `T`s and returns it

**Generics**:
  * `T`: The type of each item in the memory block

**Parameters**:
  * `count`: The number of items to make room for in the memory block

**Returns**: A pointer to the beginning of the newly allocated memory block

**Notes**

This is a safe Alta wrapper around C's `malloc` that is effectively equivalent to calling it with `count * sizeof T` as the `size` argument. In addition to type safety, it also throws a `MemoryAllocationFailure` exception if the pointer returned by `malloc` is invalid.

## `zeroAllocate<T>(count: [types].Size): ptr T`
**Summary**: Allocates and zeroes a memory block big enough for `count` number of `T`s and returns it

**Generics**:
  * `T`: The type of each item in the memory block

**Parameters**:
  * `count`: The number of items to make room for in the memory block

**Returns**: A pointer to the beginning of the newly allocated memory block

**Notes**

This is a safe Alta wrapper around C's `calloc` that is effectively equivalent to calling it with `count * sizeof T` as the `size` argument. In addition to type safety, it also throws a `MemoryAllocationFailure` exception if the pointer returned by `calloc` is invalid.

## `reallocate<T>(data: ptr T, count: [types].Size): ptr T`
**Summary**: Resizes the memory block at `data` to be big enough for `count` number of `T` and returns it

**Generics**:
  * `T`: The type of each item in the memory block

**Parameters**:
  * `data`: A pointer to the beginning of memory block to resize
  * `count`: The number of items to make room for in the memory block

**Returns** A pionter to the beginning of the newly resized memory block

**Notes**

This is a safe Alta wrapper around C's `realloc` that is effectively equivalent to calling it with `count * sizeof T` as the `newSize` argument. In addition to type safety, it also throws a `MemoryAllocationFailure` exception if the pointer returned by `calloc` is invalid.

## `free<T>(data: ptr T): void`
**Summary**: Frees an allocated block of memory

**Generics**:
  * `T`: The type of each item in the memory block

**Parameters**:
  * `data`: A pointer to the beginning of the memory block to free

**Returns**: Nothing

**Notes**

This a safe Alta wrapper around C's `free`. The only difference between it and C's version is that it is typesafe.

## `unsafeAllocate(size: [types].Size): ptr void`
**Summary**: This is the C standard library's `malloc` function, exposed to Alta. It allocates memory without zeroing it.

**Parameters**:
  * `size` = The number of bytes to allocate

**Returns**: A void pointer to the newly allocated memory. `NULL` if allocation failed

## `unsafeZeroAllocate(count: [types].Size, elementSize: [types].Size): ptr void`
**Summary**: This is C standard library's `calloc` function, exposed to Alta. It allocates memory and zeroes it.

**Parameters**:
  * `count`: The number of elements of size `elementSize` to allocate
  * `elementSize`: The size of each element in the allocated block

**Returns**: A void pointer to the newly allocated memory. `NULL` if allocation failed

## `unsafeRealloc(pointer: ptr void, newSize: Size): ptr void`
**Summary**: This is the C standard library's `realloc` function, exposed to Alta. It reallocates a previously allocated block of memory, copying data if necessary.

**Parameters**:
  * `pointer` = A pointer to the block of memory to reallocate. This must point to a block of memory previously allocated with `malloc`, `calloc`, or `realloc`
  * `newSize` = The new size for the memory block

**Returns**: A void pointer to the newly reallocated memory. `NULL` if reallocation failed

## `unsafeFree(pointer: ptr void): void`
**Summary**: This is the C standard library's `free` function, exposed to Alta. It deallocates (i.e. frees up) previously allocated memory.

**Parameters**:
  * `pointer`: A pointer to the block of memory to deallocate. This must point to a block of memory previously allocated with `malloc`, `calloc`, or `realloc`

**Returns**: Nothing
