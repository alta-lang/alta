# Scopes
Many languages have the concept of scopes, and Alta is no different.

Any variables and temporary objects are automatically destroyed when the scope ends. Persistent objects, however, must be manually destroyed.

## What has a scope?
Scopes are most commonly found as part of blocks. However, the following statements have their own scopes, regardless of whether a block is included or not.

### Function Definitions
```alta
function hi(): int {
  # scope starts here
  let myNum = 9
  # scope ends here
  return myNum
}
```

### Conditional Statements
```alta
if someAPICheck {
  # scope starts here
  let myValue = doSomething()
  respondWith(myValue)
  # scope ends here
}

if firstCheck && secondCheck
  alert("What?!") # scope starts and ends here
else if thirdCheck {
  # scope starts here
  say("Goodbye.")
  # scope ends here
} else eatSugar() # scope starts and ends with `eatSugar()`
```

### While Loops
```alta
while somethingIsTrue {
  # scope starts here
  # ...
  # scope ends here
}
while somethingElse
  foo() # scope starts and ends here
```

### For Loops
```alta
for (let i = 0; i < 6; ++i) {
  # scope starts here
  # ...
  # scope ends here
}

for (let i = 0; i < 6; ++i)
  doAThing() # scope starts and ends here

for i: int in 0..6 {
  # scope starts here
  # ...
  # scope ends here
}

for i: int in 0..6
  doAThing() # scope starts and ends here
```

## RAII
In order to simplify resource management, Alta follows the [RAII](https://en.wikipedia.org/wiki/Resource_acquisition_is_initialization) (Resource Acquisition Is Initialization) idiom. [Vectors](../stdlib/vector.md) are a good example of this. The underlying array is allocated in the constructor and freed in the destructor. Plus, the copy constructor allocates an array of the same size as the one in the source vector and copies values from the source array into the new array.

RAII is also recommended for writing interfaces with C code. For example, the standard library provides a raw `FILE` type declaration for the C opaque `FILE` structure in `libc/lib/stdio`. However, for general use, it's recommend to instead use the `io` package's `File` class, which is an Alta wrapper around the `FILE` type.

Objects are destroyed opposite the order in which they were constructed. All objects (including non-local, persistent objects) are guaranteed to be called at normal exit (but not when `abort` is used).
