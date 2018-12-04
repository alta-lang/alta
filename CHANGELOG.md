# Changelog
All the changes for the language itself will be kept in this file. For changes on the subprojects, refer to their respective changelogs.

This project and all of its subprojects follow [semantic versioning](https://semver.org).

## [Unreleased]
Nothing yet.

## [0.3.0] - 2018-12-03
### Added
  * Function-pointer types
    * e.g. `(int, byte) -> bool` defines a function-pointer type that accepts 2 arguments (an integer and a byte) and returns a boolean
    * Enables variables to be called like functions
  * Alias imports
    * e.g. `import "foo" as bar` imports all of foo's exports into a namespace called `bar`. If, for example, `thing` is a variable that foo exports, then it can be accessed as `bar.thing`
  * Aliased cherry-pick imports
    * e.g. `import thing as something from "foo"` will import `thing` from foo under a different name: `something`
  * String literals
    * e.g. `"Hello, world!"`
    * Simply an array of byte values (or it can also be interpreted as a pointer to the first element in an array of byte values)
  * Function declarations
    * e.g. `declare function bob(): bool` tells Alta that a function called `bob` that accepts no parameters and returns a boolean exists
    * It's up to the backend to find defintions of declared functions during compilation
    * This is mainly useful for telling Alta about external, non-Alta functions (in which case, you would probably also like to use the `literal` modifier, like so: `declare literal function bob(): bool`)
  * General attributes
    * Attributes are used to add metadata to AST nodes, and can provide useful information to the frontend or backend
      * e.g. the `CTranspiler.include` attribute tells the Talta C transpiler to include the specified header in the generated C code (e.g. `@@CTranspiler.include("stdio.h")` includes `<stdio.h>` in the output header for that module)
    * So far, only general attributes (i.e. attributes that can apply to any node or no node at all) have been tested
    * Theoretically, support for narrow attributes has already been fully implemented, but it hasn't been tested yet
### Changed
  * Preprocessor substitutions now have a different set of delimiters: instead of `@something@`, it's now `@[something]`
    * This change occurred to facilitate cleaner attribute syntax

## [0.2.0] - 2018-11-18
### Added
  * Preprocessing! To quote AltaCore v0.2.1's changelog:
    * Enables compile-time code alternation
      * This is just a fancy way of saying that it allows different code to be enabled given certain conditions at compile time
    * Currently supports 5 different directives:
      * `if <expression>`
      * `else if <expression>`
      * `else`
      * `define <definition-name> [value-expression]`
      * `undefine <definition-name>`
    * Sufficient variety of expressions:
      * Boolean logic operators: `&&` and `||`
      * Boolean literals: `true` and `false`
      * String literals (e.g. `"foobar"`)
      * Macro calls (e.g. `defined(foobar)`; note: only builtin macros are supported at the moment)
      * Comparative operators: `==`  (only `==` is supported for now)
    * Definition substitution (`@<definition-name>@`, where `<definition-name>` is the name of the definition to substitute)
      * Not supported in string literals or import statements
    * 4 different expression types:
      * Boolean
      * String
      * Null
      * Undefined

## [0.1.0] - 2018-11-13
### Added
  * Assignment expressions!
  * Variable definitions are now expressions that return a reference to the newly defined variable
  * Binary operators! Basic math is now available via `+`, `-`, `*`, and `/`
  * Booleans
    * Note: only the boolean type and boolean literals have been added, no conditional logic
  * Working module system using `import` and `export`
    * Functions and variables can be exported
    * Currently, only cherry-pick imports (e.g. `import foo, bar, foobar from "something.alta"`) are supported

## [0.0.0] - 2018-11-08
### Added
  * Basic type support
    * Native types supported: `int` and `byte`
    * Type modifiers supported: `const`, `ptr`, `ref` (identical to `ptr`, for now)
  * Basic function definition support
    * Function names are properly mangled in the output
    * Optional `literal` modifier to prevent function name mangling
  * `return` support - With and without a return value (i.e. `return` and `return xyz`)
  * Integer literal support (note: *positive* integer literal support)
  * Basic package parsing supported
  * Variable definition and retrieval
    * Variable names are mangled in the same way function names are mangled
    * `literal` variables are also a thing
