# `stack` package - Alta Standard Library
This package provides a generic stack implementation

## Depends On
  * [`vector`](vector.md)
  * [`types`](types.md)

Classes
---
## `Stack<T>`
**Has copy constructor**: Yes (default)\
**Has destructor**: Yes (default)\
**Generics**:
  * `T`: Type of each element in the stack

**Summary**: A generic stack implementation

**Notes**

This is essentially a wrapper around a vector.

### Methods
#### `push(value: T): ref T`
**Summary**: Adds a copy of the item to the top of the stack

**Parameters**:
  * `data`: The item to add a copy of

**Returns**: A reference to the new top of the stack

#### `pop(): T`
**Summary**: Removes the item at the top of the stack and returns a copy of it

**Returns**: A copy of the item at the top of the stack

### Accessors
#### `top: ref T`
**Read**: Yes\
**Write**: No

**Summary**: A reference to the item at the top of the stack

#### `length: [types].Size`
**Read**: Yes\
**Write**: No

**Summary**: The number of items on the stack

#### `items: @generator ref T`
**Read**: Yes\
**Write**: No

**Summary**: A generator that produces each item on the stack

**Notes**

This is an iterator. It generates references to each item in the queue. Note that these are *references* to the internal items; in other words, modifying them *will* modify the items contained in the queue.

It iterates from the bottom of the stack to the top.

This accessor can be used to easily iterate over the items on the stack, like so:
```alta
for item: ref MyCoolType in myStackOfCoolStuff.items {
  # do something with your item
}
```
