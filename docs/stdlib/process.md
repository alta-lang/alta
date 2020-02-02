# `process` package - Alta Standard Library
This package provides utilities for cross-platform environment and process management (including the management of the current process).

## Depends On
  * [`string`](string.md)
  * [`types`](types.md)
  * [`exceptions`](exceptions.md)
  * [`vector`](vector.md)
  * `lib/stdlib` from [`libc`](libc.md)
  * [`io`](io.md)
  * [`memory`](memory.md)
  * [`map`](map.md)

Exceptions
---
## `InvalidKey`
**Summary**: Indicates that no corresponding environment variable was found for a key provided to the `environment[String]` operator

Singletons
---
## `environment`
**Summary**: Provides access to the process's environment

### Methods
#### `get(key: [string].String): [string].String`
**Summary**: Retrieves the value associated with the key in the environment

**Parameters**:
  * `key`: The name of the environment variable to access

**Returns**: The value of the environment variable

**Notes**

The "key" can also be called an "environment variable name".

### Operator Methods
#### `this[[string].String]: [string].String`
**Summary**: Retrieves the value associated with the key in the environment

**Parameters**:
  * The name of the environment variable to access

**Returns**: The value of the environment variable

**Notes**

This operator is nothing more than a shortcut for `get(key: [string].String)`

Classes
---
## `Stream`
**Has copy constructor**: Yes\
**Has destructor**: Yes

**Summary**: Provides an interface to read or write data to a process's input or output streams

### Constructors
**Notes**

`Stream` has more constructors than the ones listed here, but those are only to be used internally by the package's functions, as they differ from platform to platform.

#### `constructor()`
**Summary**: Constructs an empty Stream

### Methods
#### `flush(): void`
**Summary** Forces the underlying native stream to flush all buffered write data to the process

**Returns**: Nothing

#### `bytesAvailable(): [types].Size`
**Summary**: Retrieves the number of bytes available to be read

**Returns**: The number of bytes available to be read

#### `read(count: [types].Size): [string].String`
**Summary**: Reads `count` number of bytes from the underlying native stream

**Parameters**:
  * `count`: The number of bytes to read from the native stream

**Returns**: The bytes read from the stream, in a UTF8 string

#### `write(input: [string].String): void`
**Summary**: Writes the bytes in `input` to the underlying native stream

**Parameters**:
  * `input`: A string containing the bytes to write to the native stream

**Returns**: Nothing

#### `close(): void`
**Summary**: Releases the handle on the underlying native stream

**Returns**: Nothing

**Notes**

Note that the native streams are reference-counted, meaning that they will only be closed if there are no `Stream`s referencing them.

## `Process`
**Has copy constructor**: Yes (default)\
**Has destructor**: Yes (default)

**Summary**: Provides an interface to manage a process

### Constructors
**Notes**

`Process` has constructors that are not listed here, but those are only to be used internally by the package's functions, as they differ from platform to platform. API-wise, `Process` has no constructors available.

### Methods
#### `waitForExit(): void`
**Summary**: Effectively pauses the current process until the process managed by this `Process` instance exits

**Returns**: Nothing

### Accessors
#### `stdin: Stream`
**Read**: Yes\
**Write**: No

**Summary**: Contains a writable stream to the managed process's `stdin` stream

#### `stdout: Stream`
**Read**: Yes\
**Write**: No

**Summary**: Contains a readable stream that returns data from the managed process's `stdout` stream

#### `stderr: Stream`
**Read**: Yes\
**Write**: No

**Summary**: Contains a readable stream that returns data from the managed process's `stderr` stream

Functions
---
## `arguments(): [vector].Vector<[string].String>`
**Summary**: Returns a vector populated with the arguments the current process was started with

**Returns**: A vector of the arguments passed in to the current process

## `spawn(program: [string].String, args: [vector].Vector<[string].String>): Process`
**Summary**: Spawns a new process of the `program` with the given `args`

**Parameters**:
  * `program`: The full path to the executable to launch
  * `args`: A vector of arguments to pass in to the new process

**Returns**: A `Process` instance that manages the new process

**Notes**

This function is guaranteed to work the same way on all platforms. This means, for example, that no special escaping should be done by the user of the function for the arguments on Windows. The arguments are guaranteed to be seen by the new process exactly as they are represented the Strings given to `spawn`.
