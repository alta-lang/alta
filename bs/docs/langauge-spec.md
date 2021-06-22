# Alta Language Specification

This is the language specification for version 2 of the Alta programming language.
Note: this specification is currently incomplete. Language features and sematics will be updated as they're added/implemented.

## Outline

Version 1 of the language was generally a mixed paradigm language, with heavy OOP influences from C++. It implemented features like automatic type conversion, but lacked certain other features such as immutable objects. In addition, over time, it became apparent to the primary developer of the language that automatic type conversion was more often than not a source of confusion, with the compiler inferring undesired conversion paths and creating more complex code than what was necessary. This is why automatic type conversion and other features that are seemingly "magic" are not included in version 2 of the language.

Alta v2 heads in a new direction, drawing concepts from Rust and C (and possibly a bit of Objective-C, from the primary developer's experiences with it in another project). Alta v2 establishes clear boundaries between data and functions. This version of the language makes interfaces first-class citizens. Interfaces define the actions that can be performed with a given type and are not bound solely to structures: they can be implemented for structures but also for native types like integers.

## Interfaces

Interfaces defines actions that can be performed with data. They can be considered to have "can-do" relationships with the types that implement them (i.e. implementing types "can-do" the things described by the interface). In addition, interfaces can optionally specify any number of required interfaces. This means that types that implement this interface must also implement the other interfaces it requires.

Interfaces consist of a set of functions that types must implement to conform to the interface. These functions can either be static (meaning they're essentially standalone functions, just with a namespace) or non-static (meaning they require an instance of the type as a context argument). Future revisions of the language may allow for interfaces to provide default implementations of the interface's functions, but the current specification does not allow this.

Non-static functions must specify in their context parameter whether they capture the context just like any function can capture its parameters (see Functions).

Example interface definition:
```alta
public interface Foo {
  function touchIt(self: &!Self): void
  function printSomething(self: &Self): void
  function calculateCoolness(self: &Self): int
}
```

It is recommended that functions accept interfaces for their parameters whenever possible rather than specific types, unless there is a good reason to use a specific type. This is because interfaces enable code to become polymorphic and reusable for any type that implements the given interface, with little to no effort on the function developer's part.

It is recommended (but not required) that for each type that has interfaces implemented, there should be a base interface that provides at least initialization and destruction functions (see the next section).

### Initialization and Destruction

Initialization and destruction are special functions that should only be implemented in the base interface for a type--they *can* be implemented by any interface, but they *should* only be implemented by a type's base interface.

By default 

## Functions

Functions are one of the core components of any program.

Functions can capture their parameters in one of four ways: 1) by immutable reference, 2) by mutable reference, 3) by value, or 4) by consumption.

  * Capturing by immutable reference means that the function does not (and cannot) modify the parameter in any way (if the parameter type is a structure, this means that its members are also immutable). The argument passed in for this parameter remains valid for the caller after this function returns.
  * Capturing by mutable reference means that function can modify the context in any way it desires. The argument passed in for this parameter remains valid for the caller after this function returns.
  * Capturing by value means that a ***binary copy*** of the argument for this parameter is made at the call site. This does **NOT** mean that a logical copy of the data is made. If such a copy is desired, it **MUST** be manually made by the caller. The argument passed in for this parameter remains valid for the caller after this function returns.
    * This is the default when no modifiers are present.
    * This is essentially the same behavior of parameters in C.
    * Note that this method of capture is dangerous, particularly with structures. Their members are mutable because the function receives a binary copy of the structure; changes made to the copied binary data will not be reflected outside the function. However, shared data referenced by the structure (such as heap memory) *may be modified* and such modifications **WILL** be reflected outside the function. As such, the caller's version of the data may be outdated upon return.
  * Capturing by consumption means that the argument passed in for this parameter is consumed by the call and is no longer valid for the caller after this function returns. By default, consumption creates a binary copy of the input data, but if the parameter type is a mutable reference, then a mutable reference to the input data is passed in.

Alta v2 does not support function overloading in the traditional sense (i.e. same name, different parameter types). This is because function overloading is one of those features that appears to be "magic" (the compiler is supposed to determine which is the correct overload, which might not be the one you intended). Instead, Alta supports label-based overloading, similar to Objective-C. In some cases, this can be superior to traditional type-base overloading because it allows for two different versions of a function (that potentially perform different actions) with the same parameter types, differentiated only by their parameter labels. In addition, this provides clearer, self-documenting code.

Example label-based overload:
```alta
public function foo(withBar bar: int): int {
  return bar + 5
}

public function foo(withSomething something: int): int {
  return something + 10
}

# to call them:
foo(withBar: 2)
foo(withSomething: 9)
```

## Safety

Following in Rust's footsteps, Alta v2 aims to be as safe as possible by default and provides many constructs for this purpose.

For example, like Rust, all variables are immutable by default. Mutable references are not allowed at the same time as immutable ones.

Despite all of these safety features, Alta v2 still gives you the potential to have nearly unlimited power and completely disregard safety, like C does. The catch? Unsafe code must be marked as such, either by marking the enclosing function as unsafe or by wrapping it in an `@unsafe` block. Like it does for Rust, this feature allows you to quickly determine what the problem areas might be if and when a memory safety bug should arise.
