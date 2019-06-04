# Functions
Functions are the pillars of any programming language, allowing code to be re-used or compartmentalized.

Every function has its own [scope](scopes.md) that ends when the function returns (either explicitly or automatically).

## Defining a Function
A function definition consists of a number of modifiers and [attributes](attributes.md), a name, any number of parameters, a return type, and a body. For example, to define a function called `foo` that takes a single integer named `bar` and returns an integer, the definition would look like this:
```alta
function foo(bar: int): int {
  # do whatever you want with `bar`
  # for example:
  return bar + 2
}
```

Modifiers and [attributes](attributes.md) can optionally be added before the `function` keyword to give Alta extra instructions on what to do with a function. For example, this defines a function that will be exported from the module:
```alta
export function add(x: int, y: int): int {
  # ...
}
```

Possible modifiers for a function:
  * `export` = Indicates that the function is part of the [module's](packages-and-modules.md) exports.
  * `literal` = Indicates that the function name should not be [mangled](#name-mangling)
  * `generic` = Indicates that the function is [generic](#generic-functions)

## Calling a Function
Function calls are pretty straightforward and similar to calls in almost every other language. Each argument corresponds to the parameter in the target function. The return value of the function becomes the value of the function call expression. For example, calling an `add` function that takes two integers and returns their sum:

```alta
add(5, 6)
# returns 11
```

However, Alta also has optional parameter naming for arguments (called, quite simply, "named arguments"). This makes some function calls able to look more like JavaScript-style objects and makes calls with more arguments readable. Plus, it also enables some more complex function [overloading](#overloading) possible.

```alta
createNewUser(
  name: "John Doe",
  age: 30,
  username: "the_doe_man",
  password: "pleasedonthackme"
)
```

You can also mix regular arguments and named arguments. All unnamed arguments after a named argument will continue after the position of the named argument's corresponding parameter. For example, the following function:

```alta
function doSomething(a: int, b: ptr byte, c: double, d: int): void {
  # ...
}
```

can be called like so:

```alta
doSomething(8, c: 8.5, 9, b: "something")
# this is the same as calling it like:
doSomething(8, "something", 8.5, 9)
```

# Name Mangling
Alta mangles function names (and class and variable names, too) in the output code to prevent name clashes. It does this by adding extra information to the name such as module name, package version, parameters names and types, return type, and&mdash;in the case of generic functions&mdash;generic arguments.

For example, the following function, with a path of `math/basic.alta`, a part of the `basic` module of a package named `math` which is on version 1.0.0, would be mangled as `math_47_basic_5_1a0a0_0_subtract_9_x_1_int_9_y_1_int`:

```alta
function subtract(x: int, y: int): int {
  return x - y
}
```

Name mangling is what enables functions (and other scope items) to have the same name as scope items in different modules&mdash;for example, a `power` function in `math/basic.alta` and another, different `power` function in `math/advanced.alta`&mdash;and it also enables function [overloading](#overloading).

To disable name mangling for a specific function, just add the `literal` modifier to it:
```alta
literal function everyoneCanSeeMe(): void {
  # ...
}
```

# Overloading
A very common feature in many programming languages (C++, Java, C#, etc.) is the ability to define different versions of a function with the same name but with different parameters. These are multiple versions of `respond`, a function that's supposed to respond to a request:
```alta
# respond with a status code
function respond(status: int): void {
  # do your response logic here
}

# respond with a message
function respond(message: String): void {
  # other response logic
}

# respond with both, a status code and a message
function respond(status: int, message: String): void {
  # more response logic
}
```

Each does something different based on what the arguments are, but they can also call each other. In this case, the last version (the one that takes both a status code and a message) can call the other ones to respond with each value:
```alta
function respond(status: int, message: String): void {
  # respond with the status code first
  respond(status)

  # and then respond with the message
  respond(message)
}
```

However, unlike other languages, Alta takes this a step further and allows you to overload functions based on parameter names. This is made possible because of the optional parameter names for arguments in function calls. For example, the following is perfectly valid:
```alta
# gets a substring of `string`, starting at the `from` index,
# all the way to the `to` index, excluding the character at `to`
function substring(string: String, from: int, to: int): String {
  # ...
}

# gets a substring of `string`, starting at the `from` index,
# and gets `length` number of characters
function substring(string: String, from: int, length: int): String {
  # ...
}
```

To use the different versions, you would do something like this:
```alta
let myString = new String("this is just a test string")

# returns "is j"
substring(myString, 5, to: 8)

# returns "is"
substring(myString, 5, length: 2)
```

# Generic Functions
Alta also has another, very powerful feature that maintains type safety while enabling flexibility: generics. Both functions and classes can be generic, and what it means for functions is pretty simple: all the references to generic type arguments in a generic function change with each instantiation.

For example, here's a pretty useless but simple generic function that will create a new instance of whatever type it's given using its default constructor:
```alta
generic function makeDefault<T>(): T {
  return new T
}
```

It can be instantiated multiple times with different arguments to produce different values:
```alta
let first = makeDefault<Foo>()
# first is now a default-constructed Foo

let second = makeDefault<Bar>()
# second is now a default-constructed Bar
```

However, note that once it's instantiated, all the same rules apply as if it were a regular function. For example, you can't instantiate it using an `int`:
```alta
# nope, this is an error because you can't do `new int`
let third = makeDefault<int>()
```

## Declaring a Function
It's important to be able to declare functions in addition to being able to define them, particularly when working with external code. Declaring a function is almost the same as defining one, the only difference is that there's no body and you add the `declare` keyword before any modifiers or attributes.

```alta
declare function doAThing(): void
```

Often, you'll want to include the `literal` modifier, in order to make Alta aware of external functions (e.g. code from C) properly. Otherwise, Alta will try to mangle the function name and you'll end up with the wrong declaration:

```alta
# wrong: Alta produces `some_module_5_1a0a0_0_printf_9_message...`
declare function printf(message: ptr byte): int

# correct: Alta produces `printf`
declare literal function printf(message: ptr byte): int
```
