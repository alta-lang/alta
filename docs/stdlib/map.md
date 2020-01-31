# `map` module - Alta Standard Library
This module contains a generic Map class that works for most use cases.

## Depends On
  * [`vector`](vector.md)
  * [`types`](types.md)
  * [`util`](util.md)

Classes
---
## `Map<K, V>`
**Has copy constructor**: Yes (default)\
**Has destructor**: Yes (default)\
**Generics**:
  * `K` = Key type
  * `V` = Value type

**Notes**

`K` must be a type that can be compared for equality. Ideally, it should have reflexive (i.e. if a = b then b = a) and symmetric (i.e. a = a) equality, but it's not a strict requirement, it just needs to have a `==` operator for itself that returns a boolean in order to test if two keys are equal.

In the future, when Alta gets interfaces, `K` will have to satisfy a basic equality interface, like:
```alta
interface Equatable {
  this == This: bool
}
```

### Constructors
#### `constructor()`
**Summary**: Constructs an empty map

#### `constructor(keys: [vector].Vector<K>, values: [vector].Vector<V>)`
**Summary**: Constructs a map with the given keys and values.

**Parameters**:
  * `keys`: The keys to add to the new Map
  * `values`: The vales corresponding to those keys

**Notes**

The length of `keys` must match the length of `values`.

### Methods
#### `get(key: K): ref V`
**Summary**: Gets a reference to the value associated with the given key.

**Parameters**:
  * `key`: The key to search for

**Returns**: A reference to the value

**Notes**

Creates a new entry with the given key if it's not found.

#### `has(key: K): bool`
**Summary**: Checks whether a given key exists in this Map

**Parameters**:
  * `key`: The key to search for

**Returns**: `true` if the key is present, `false` otherwise

#### `getKeys(): [vector].Vector<K>`
**Summary**: Gets a vector with copies of this Map's keys

**Returns**: A vector containg copies of the current keys in this Map

#### `clear(): void`
**Summary**: Deletes all the keys and values in this Map

**Notes**

This is essentially the same as overwriting this Map with a new empty Map (i.e. `this = new Map`), although the overhead for each may differ slightly: `clear()` calls the `clear()` method on the internal key and value vectors, whereas `this = new Map` constructs a new empty Map, destroys `this`, and then assigns the new map to `this`.

**Returns**: Nothing

### Accessors
#### `entries: [vector].Vector<[util].Pair<K, V>>`
**Read**: Yes\
**Write**: No

**Summary**: A vector containing pairs of copies of this Map's keys and corresponding values

#### `keys: [vector].Vector<K>`
**Read**: Yes\
**Write**: No

**Summary**: A vector containing copies of this Map's keys

#### `values: [vector].Vector<V>`
**Read**: Yes\
**Write**: No

**Summary**: A vector containing copies of this Map's values

#### `items: @generator() [util].Pair<K, V>`
**Read**: Yes\
**Write**: No

**Summary**: A generator that produces each entry in this Map

**Notes**

This is an iterator. It generates pairs of keys and their corresponding values. Note that these keys and values are copies of the internal members; in other words, modifying them will *not* modify the keys or values contained in the Map.

This accessor is intended to make it easy to iterate over the entries in the map, like so:
```alta
for entry: Pair<MyKey, MyValue> in myCoolMap.items {
  # do whatever you want with the key and value
}
```

It's also essentially the same as accessing the `items` iterator in the vector contained in `entries`:
```alta
for entry: Pair<MyKey, MyValue> in myCoolMap.entries.items {
  # same thing
}
```

### Operator Methods
#### `this[K]: ref V`
**Summary**: Retrieves the value associated with the given key

**Parameters**:
  * The key whose associated value will be retrieved

**Returns**: A reference to the value assocated with the given key

**Notes**

Note that this operator returns a *reference*, meaning that modifying the return value of this operator *will* modify the internal value associated with the given key.
