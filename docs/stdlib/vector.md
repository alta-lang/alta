# `vector` module - Alta Standard Library
This module implements a generic dynamically-sized array class.

## Depends On
  * [`memory`](memory.md)
  * [`types`](types.md)

Classes
---
## `Vector<T>`
**Has copy constructor**: Yes\
**Has destructor**: Yes\
**Generics**:
  * `T` = The type of each element in the vector

### Constructors
#### `constructor()`
Constructs an empty vector

#### `constructor(length: [types].Size)`
Constructors a vector of with a minimum length of `length`

**Parameters**:
  * `length` = The length of the new vector

### Methods
#### `get(index: [types].Size): ref T`
Gets a reference to the element at `index`

**Parameters**:
  * `index` = The index to lookup

**Returns**: A reference to the element at `index`

#### `push(): ref T`
Pushes a new element to the back of the vector.

**Returns**: A reference to the newly added element

#### `push(value: T): ref T`
Pushes a new element to the back of the vector and assigns `value` to it.

**Parameters**:
  * `value` = The value to assign

**Returns**: A reference to the newly added element, after assigning `value` to it

### Accessors
#### `data: ptr T`
**Read**: Yes\
**Write**: No

The allocated array of elements contained by this vector. It's actually a pointer to the first element in the array.
While the pointer itself isn't modifiable because it doesn't have a write accessor, the values it points to *are* modifiable.

#### `length: [types].Size`
**Read**: Yes\
**Write**: No

The length of the vector. This is the number of elements currently being used, *not* the size of the vector (i.e. number of *available* elements).
