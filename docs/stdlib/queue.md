# `queue` package - Alta Standard Library
This package provides a generic queue implementation.

## Depends On
  * [`list`](list.md)
  * [`types`](types.md)

Classes
---
## `Queue<T>`
**Has copy constructor**: Yes (default)\
**Has destructor**: Yes (default)\
**Generics**:
  * `T`: Type of each node item

**Summary**: A generic queue implementation

**Notes**

This is essentially a wrapper over `List<T>`; however, that may change in the future for performance reasons.

### Constructors
#### `constructor()`
**Summary**: Constructs an empty list

#### `constructor(items: T...)`
**Summary**: Constructs a new Queue and adds copies of the `items` to it

### Methods
#### `push(data: T): ref T`
**Summary**: Adds a copy of the item to the back of the queue

**Parameters**:
  * `data`: The item to add a copy of

**Returns**: A reference to the newly added copy of the item

#### `pop(): T`
**Summary**: Removes the first item in the queue and returns a copy of it

**Returns**: A copy of the first item in the queue

#### `clear(): ref Queue<T>`
**Summary**: Removes all the items in the queue

**Returns**: A reference to `this`

### Accessors
#### `front: ref T`
**Read**: Yes\
**Write**: No

**Summary**: A reference to the first item in the queue

#### `length: [types].Size`
**Read**: Yes\
**Write**: No

**Summary**: The number of items in the queue

#### `items: @generator ref T`
**Read**: Yes\
**Write**: No

**Summary**: A generator that produces each item in the queue

**Notes**

This is an iterator. It generates references to each item in the queue. Note that these are *references* to the internal items; in other words, modifying them *will* modify the items contained in the queue.

This accessor can be used to easily iterate over the items in the queue, like so:
```alta
for item: ref MyCoolType in myQueueOfCoolStuff.items {
  # do something with your item
}
```
