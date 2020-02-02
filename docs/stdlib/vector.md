# `vector` package - Alta Standard Library
This package implements a generic dynamically-sized array class.

## Depends On
  * [`memory`](memory.md)
  * [`types`](types.md)
  * [`exceptions`](exceptions.md)

Exceptions
---
## `ResizeFailure`
**Summary**: Indicates a failure to resize the vector

Classes
---
## `Vector<T>`
**Has copy constructor**: Yes\
**Has destructor**: Yes\
**Generics**:
  * `T`: The type of each element in the vector

### Constructors
#### `constructor()`
**Summary**: Constructs an empty vector

#### `constructor(length: [types].Size)`
**Summary**: Constructors a vector with a minimum length of `length`

**Parameters**:
  * `length`: The length of the new vector

#### `constructor(length: [types].Size, initializer: T)`
**Summary**: Constructs a vector with a minimum length of `length` and copies `initializer` to each item

**Parameters**:
  * `length`: The length of the new vector
  * `initializer`: The initial value of each new element in the vector

**Notes**

Note that *copies* of the initializer will be used to initialize each new element. Also, the initializer is only used for the elements created by this constructor. It is **not** used to initialize new elements created after the vector has been constructed.

#### `constructor(items: T...)`
**Summary**: Constructs a vector with the given items

**Parameters**:
  * `items`: Items to push the vector upon creation

**Notes**

The items are inserted in order received. For example, this code...

```alta
let myVec = new Vector<int>(3, 4, 5, 6)
```

...creates an `int` vector containing `[3, 4, 5, 6]`, in that order (i.e. `3` as the first element, `4` as the second, and so on).

### Methods
#### `get(index: [types].Size): ref T`
**Summary**: Gets a reference to the element at `index`

**Parameters**:
  * `index`: The index to lookup

**Returns**: A reference to the element at `index`

#### ~~`push(): ref T`~~
***Deprecated***

**Summary**: Pushes a new element to the back of the vector

**Returns**: A reference to the newly added element

**Notes**

This method is deprecated. It may be removed from the public Vector API in a future release.

Care should be taken when working with this method, as it returns a reference to a new, uninitialized element. It cannot be reasonably used by external code, due the fact that a plain assignment (i.e. `a = b`) does traverse references (which is what we want) but also tries to call destructors on the value to be overwritten (which in this case, is an uninitialized value, thus causing undefined behavior) and strict assignment (i.e. `a @strict = b`) doesn't call destructors on the value to be overwritten (which is what we want) but it doesn't traverse references either (which is not what we want).

#### `push(value: T): ref T`
**Summary**: Pushes a new element to the back of the vector and assigns `value` to it

**Parameters**:
  * `value`: The value to assign

**Returns**: A reference to the newly added element, after assigning `value` to it

#### `pop(): T`
**Summary**: Removes the last element in the vector and returns a copy of it

**Returns**: A copy of the last element in the vector

**Notes**

This method creates a copy of the last element, then destroys the last element, and then returns the copy.

#### `from(index: [types].Size): Vector<T>`
**Summary**: Gets a copy of this vector, with only the elements at and after the `index`

**Parameters**:
  * `index`: The index of the element to start copying from; inclusive

**Returns**: A copy of this vector, with only the elements at and after the `index`

#### `to(index: [types].Size): Vector<T>`
**Summary**: Gets a copy of this vector, with only the elements up to and including the `index`

**Parameters**:
  * `index`: The index of the element to stop copying at; inclusive

**Returns**: A copy of this vector, with only the elements up to and including the `index`

#### `clear(): void`
**Summary**: Removes all the elements from the vector

**Returns**: Nothing

**Notes**

Destructors **will** be called for the elements being removed. In addition, this method does **not** reduce the size of the vector but it *does* make its length `0`. The difference is that "length" refers to how many elements are currently in the vector, while "size" refers to the space reserved by the vector for current and future elements.

### Accessors
#### `data: ptr T`
**Read**: Yes\
**Write**: No

**Summary**: The allocated array of elements contained by this vector

**Notes**

It's actually a pointer to the first element in the array. While the pointer itself isn't modifiable because it doesn't have a write accessor, the values it points to *are* modifiable.

#### `length: [types].Size`
**Read**: Yes\
**Write**: No

**Summary**: The length of the vector

**Notes**

This is the number of elements currently being used, *not* the size of the vector (i.e. number of *available* elements).

#### `first: ref T`
**Read**: Yes\
**Write**: No

**Summary**: A reference to the first element in the vector

**Notes**

This accessor will throw an `IndexOutOfBounds` exception if there are no elements in the vector.

#### `last: ref T`
**Read**: Yes\
**Write**: No

**Summary**: A reference to the last element in the vector

**Notes**

This accessor will throw an `IndexOutOfBounds` exception if there are no elements in the vector.

#### `items: @generator ref T`
**Read**: Yes\
**Write**: No

**Summary**: A generator that produces each element in this vector

**Notes**

This is an iterator. It generates reference to elements in this vector.

This accessor is intended to make it easy to iterate over the elements in this vector, like so:
```alta
for elm: ref MyCoolType in myCoolVector.items {
  # do whatever you want with the character
}
```

### Cast Operators
#### to `ptr T`
**Summary**: Returns a pointer to the beginning of the underlying array for the elements

**Notes**

The array pointed to by this pointer is automatically managed by this vector, meaning that it will only be valid as long as the vector is alive. In addition, due to the way that memory reallocation works, it may become invalid after an operation is performed on the vector that changes its size (e.g. pushing a new element into the vector). Therefore, it is recommended that you do not modify the vector in any way as long as someone has a copy of this pointer.

### Operator Methods
#### `this[[type].Size]: ref T`
**Summary**: Returns a reference to the element at the given index

**Parameters**:
  * The index of the desired element

**Returns**: A reference to the element at the given index

**Notes**

This is a convenience operator that is equivalent to calling the `get` method with the given index as the only argument.
