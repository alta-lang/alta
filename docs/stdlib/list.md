# `list` package - Alta Standard Library
This package contains a generic doubly-linked list implementation that works for most use cases.

## Depends On
  * [`memory`](memory.md)
  * [`exceptions`](exceptions.md)
  * [`types`](types.md)

Classes
---
## `List<T>`
**Has copy constructor**: Yes\
**Has destructor**: Yes\
**Generics**:
  * `T`: Type of each node item

**Summary**: A generic doubly-linked list implementation

### Constructors
#### `constructor()`
**Summary**: Constructs an empty list

### Methods
#### `pushFront(data: T): ref T`
**Summary**: Adds a copy of the item to the front of the list

**Parameters**:
  * `data`: The item to add a copy of

**Returns**: A reference to the newly added copy of the item

#### `pushBack(data: T): ref T`
**Summary**: Adds a copy of the item to the back of the list

**Parameters**:
  * `data`: The item to add a copy of

**Returns**: A reference to the newly added copy of the item

#### `popFront(): T`
**Summary**: Removes the first item in the list and returns a copy of it

**Returns**: A copy of the first item in the list

#### `popBack(): T`
**Summary**: Removes the last item in the list and returns a copy of it

**Returns**: A copy of the last item in the list

#### `insert(data: T, at: [types].Size): ref T`
**Summary**: Inserts a copy of the item to the list at the specified index

**Parameters**:
  * `data`: The item to add a copy of
  * `at`: The index to insert the item at

**Returns**: A reference to the newly added copy of the item

#### `remove(index: [types].Size): T`
**Summary**: Removes the item at the specified index from the list and returns a copy of it

**Parameters**:
  * `index`: The index of the item to remove

**Returns**: A copy of the item at the index

#### `clear(): ref List<T>`
**Summary**: Removes all the items in the list

**Returns**: A reference to `this`

### Accessors
#### `first: ref T`
**Read**: Yes\
**Write**: No

**Summary**: A reference to the first item in the list

#### `last: ref T`
**Read**: Yes\
**Write**: No

**Summary**: A reference to the last item in the list

#### `length: [types].Size`
**Read**: Yes\
**Write**: No

**Summary**: The number of items in the list

#### `items: @generator ref T`
**Read**: Yes\
**Write**: No

**Summary**: A generator that produces each item in the list

**Notes**

This is an iterator. It generates references to each item in the list. Note that these are *references* to the internal items; in other words, modifying them *will* modify the items contained in the list.

This accessor can be used to easily iterate over the items in the list, like so:
```alta
for item: ref MyCoolType in myListOfCoolStuff.items {
  # do something with your item
}
```

### Operator Methods
#### `this[[types].Size]: ref T`
**Summary**: Retrieves the item in the list at the given index

**Parameters**:
  * The index of the item to retrieve

**Returns**: A reference to the item at the given index
