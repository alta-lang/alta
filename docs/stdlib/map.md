# `map` module - Alta Standard Library
This module contains a generic Map class that works for most use cases.

## Depends On
  * [`vector`](vector.md)
  * [`types`](types.md)

Classes
---
## `Map<K, V>`
**Has copy constructor**: Yes (default)\
**Has destructor**: Yes (default)\
**Generics**:
  * `K` = Key type
  * `V` = Value type

### Constructors
#### `constructor()`
Constructs an empty map

#### `constructor(keys: [vector].Vector<K>, values: [vector].Vector<V>)`
Constructs a map with the given keys and values.

> The length of `keys` must match the length of `values`.

### Methods
#### `get(key: K): ref V`
Gets a reference to the value associated with the given key. Creates a new entry with the given key if it's not found.

**Parameters**:
  * `key` = The key to search for

**Returns**: A reference to the value

#### `has(key: K): bool`
Checks whether a given key exists in this Map

**Parameters**:
  * `key` = The key to search for

**Returns**: `true` if the key is present, `false` otherwise
