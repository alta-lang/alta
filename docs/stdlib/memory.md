# `memory` module - Alta Standard Library
This module exposes lots of necessary and useful functions and classes for working with memory.

## Depends on
  * `lib/stdlib` from [`libc`](libc.md)

Functions
---
## `malloc(size: types.Size): ptr void`
This is the C stdandard library's `malloc` function, exposed to Alta. It allocates memory without zeroing it.

**Parameters**:
  * `size` = The number of bytes to allocate

**Returns**: A void pointer to the newly allocated memory. `NULL` if allocation failed

## `realloc(pointer: ptr void, newSize: Size): ptr void`
This is the C standard library's `realloc` function, exposed to Alta. It reallocates a previously allocated block of memory, copying data if necessary.

**Parameters**:
  * `pointer` = A pointer to the block of memory to reallocate. This must point to a block of memory previously allocated with `malloc`, `calloc`, or `realloc`
  * `newSize` = The new size for the memory block

**Returns**: A void pointer to the newly reallocated memory. `NULL` if reallocation failed

## `free(pointer: ptr void): void`
This is the C standard library's `free` function, exposed to Alta. It deallocates (i.e. frees up) previously allocated memory.

**Parameters**:
  * `pointer` = A pointer to the block of memory to deallocate. This must point to a block of memory previously allocated with `malloc`, `calloc`, or `realloc`

**Returns**: Nothing
