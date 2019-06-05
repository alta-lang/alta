# Attributes
Attributes are a simple way to add extra information to modules and scope items, often enabling extra behavior in the compiler itself.

Attributes come in two varieties: normal and general.

## Normal Attributes
Normal attributes add information to individual statements and expressions. For example, the `@read` attribute on a function indicates that a function should be used as a read accessor:
```alta
@read function imAVariable(): int {
  # hee hee, i'm actually not a variable :)
  return 6
}
```

How to add attributes to items and expressions that can have them is documented in each item/expression's reference.

## General Attributes
General attributes add information to the current module. For example, the `@CTranspiler.include` attribute tells Alta to include a C header in the output code for that module:
```alta
@@CTranspiler.include("stdio.h");
```

## Built-in Attributes
Alta has a few built-in attributes:

### `@read`
**Applies To**: Function definitions

This attributes tells Alta that the function it applies to is actually a [read accessor](functions.md#accessor-functions).
Functions marked with this attribute must take no parameters and return a value (of any type except `void`).

```alta
# this read accessor is in a class called `Vector`
# returns the length of this vector
@read function length(): int {
  return myLengthCalculatedSomehow
}
```

### `@copy`
**Applies To**: Class constructors

This attribute indicates that the constructor should be used when copying objects of its class. Constructors marked with this attribute must take a single parameter: a reference to its class.

```alta
# this copy constructor is in a class called `Greeter`
public @copy constructor(other: ref Greeter) {
  # copy the old greeter to this new greeter
}
```

### `@external`
**Applies To**: Structure definitions

This attribute indicates that the structure definition isn't really a definition; instead, it acts like a declaration, making Alta aware of the members of a structure defined in external code.

```alta
# a structure called `net_response` has been defined in C library being used
# this will produce `struct net_response` whenever the structure is used as a type in Alta
@external literal struct net_response {
  time: int;
  code: int;
  data: ptr byte;
  # more network-response-related stuff...
}
```

### `@typed`
**Applies To**: Structure definitions

This attribute indicates that the structure in question has already been `typedef`ed in external code. This only has meaning when used together with `@external`.

```alta
# same situation as the example for `@external`, except that this time the C library has provided a typedef for the structure
# this will produce `net_response` whenever the structure is used as a type in Alta
@external @typed literal struct net_response {
  time: int;
  code: int;
  # etc.
}
```

### `@strict`
**Applies To**: Assignment expressions

This attribute indicates that the assignment should be performed strictly:
  * Destructors for the destination will not be called
  * References not be resolved when assigning
This is used in the standard library's [vector](../../stdlib/vector/main.alta) implementation to prevent overwriting the value of any stored references in the vector and to avoid calling destructors for elements that have not been initialized yet.

```alta
let someRef: ref Something = getMySomething()
# now you've changed your mind and decided you want `someRef` to reference something else
# just do this:
someRef @strict = myNewSomething()
```

### `@@CTranspiler.include(header: rawstring)`
**Applies To**: Modules

This general attribute tells Alta to include a C header in the code it produces. Currently, only non-local headers (e.g. `<header.h>`) can be included with this attribute.

```alta
@@CTranspiler.include("stdio.h");
```

### `@CTranspiler.vararg`
**Applies To**: Parameters (only variable parameters) in function declarations

This attribute can be used with variable parameters in function declarations to indicate that the parameter is actually actually a C [vararg](https://en.cppreference.com/w/c/variadic) parameter. This is usually used together with the `any` type for the parameter.

```alta
declare literal function printf(format: ptr byte, @CTranspiler.vararg data: any...): int
```

### `@@CTranspiler.link(libName: rawstring)`
**Applies To**: Modules

This general attribute tells Alta to link the package to the specified native library when compiling (which it does using CMake).

```alta
# link to OpenSSL and LibCrypto
@@CTranspiler.link("ssl");
@@CTranspiler.link("crypto");
```
