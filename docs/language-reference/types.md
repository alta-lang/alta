# Types
Alta is a strictly typed language, and thus, one of its core components is its type system. However, this does not mean the type system is rigid and hard-to-use. Alta's type system provides enough flexibility and simplicity for most uses while still maintaining safety.

## Built-in Native Types
All of the native types Alta provides are C's native types:
  * `byte`/`char` = This is the smallest possible type, guaranteed to be a single byte and at least 8 bits wide
  * `int` = This is your garden-variety integer type. It is guaranteed to be at least 32 bits wide
  * `void` = This is a type that indicates the absence of value (i.e. *nothing*). This type is only valid for function return values
  * `float` = This is the smallest available floating-point number type, guaranteed to be at least 32 bits wide and have single-precision
  * `double` = This is a floating-point number type, guaranteed to be at least 32 bits wide and have double-precision
  * `bool` = This is equivalent to the `byte` type, except that it indicates that it contains boolean values

The following modifiers can only be applied to native types:
  * `long` = This is equivalent to C's `long` modifier, which in some cases makes the type in question wider
  * `short` = This is equivalent to C's `short` modifier, which in some cases makes the type in question narrower
  * `unsigned` = This indicates the type in question is unsigned, that is, it does not contain a sign bit. Values for types with this modifier can only be positive values
  * `signed` = The opposite of `signed`, this modifier forces the type in question to contain a sign bit, indicating whether values are positive or negative

Native types `signed` by default, with the exception of the `byte` type, which can by either signend or unsigned by default, depending on the platform (on most platforms, however, it is signed by default like the rest of the native types).

The following modifiers can be applied to *any* type:
  * `ptr` = This indicates that the type in question actually describes a pointer to that type
  * `ref` = Like `ptr`, except that it describes a reference (the difference is described later on)

Types and modifiers are composed logically&mdash;the way you would read them&mdash;from outermost to innermost. To specify a pointer to an integer, you would write `ptr int`. To specify a constant pointer to a double, you would write `const ptr double`. A pointer to a constant boolean would be `ptr const bool`. A long, long integer (which is at least 64 bits) would be `long long int`.

## Other Types
Native types are not the only types available in Alta. [Classes](classes.md) can be used as types of their own, and are in fact essential to every Alta program.

```alta
class Car {
  public var currentGas: double = 8.3
}

# a variable containing a Car object would look like:
let myCar: Car = new Car

# a reference to a variable containing a Car object:
let overThere: ref Car = myCar

# pointer to it:
let look: ptr Car = &myCar

# constant (i.e. unmodifiable) Car variable:
let dontTouch: const Car = new Car
```

## References vs Pointers
Alta provides two ways to create links to values: references and pointers. But what's the difference?

References basically create an alias to a variable. It becomes like another name for the value the original variable contains. References are automatically followed when using them anywhere. Pointers, on the other hand, must be manually dereferenced (except when accessing their members or methods).

Each provides its own advantages, however. While references are much simpler to use in many cases, they cannot be made to point to a different value. That's what pointers are for. Be careful, however. Since references cannot be reassigned, they're valid as long as the value they point to is valid. Pointers can point to *any* memory location, so they can point to invalid values and require more care.
