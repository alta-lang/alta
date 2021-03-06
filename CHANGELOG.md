# Changelog
All the changes for the language and CLI will be kept in this file. For changes on the subprojects, refer to their respective changelogs.

This project and all of its subprojects follow [semantic versioning](https://semver.org).

## [Unreleased]
### Fixed
#### CLI
  * Report the correct version information
    * Using information from Git like tags, commits, and modification status
    * The version information is automatically refreshed every build
  * Target output types are now respected
#### Standard Library
  * `memory` package
    * Don't allow null pointer to be dereferenced in `Box`
#### Language
  * `struct`s can now be constructed persistently (i.e. used with `new*`)
### Changed
#### CLI
  * Brand-new logging system
    * AltaCore has implemented a new logging system which allows it to send messages with different severities and codes, so we now use that to provide better feedback
    * In addition, the new format is more similar to GCC's and Clang's error reporting formats
  * The default target output type when no manifest is found is now "binary"
#### Compiler
  * We now preprocess the generated C code ourselves to reduce the load on the C compiler's preprocessor
    * The previous implementation where we left it up to the C compiler to include the proper files and sections was very fragile and sometimes caused the preprocessor to segfault
    * The new approach is to basically copy-paste the necessary declarations into source files
    * This also means *significantly* faster C build times
      * Example: the CI build time for one of our projects ([yipyap](https://github.com/alta-lang/yipyap)) was reduced to less than a quarter of it's previous time (down to 5-7 minutes from 25-26 minutes)
#### Standard Library
  * `memory` package
    * The safe allocation functions (`allocate`, `reallocate`, and `zeroAllocate`) now initialize the values in their memory blocks with invalid values, which means it's now safe to assign directly to values in the memory block without strict assignment.
#### Project
  * All automated builds are now built for release, regardless of whether they're plain or tagged commits
### Added
#### Project
  * Automated build artifacts are now hosted on [SourceForge](https://sourceforge.net/projects/alta-builds/)
#### Language
  * `@invalid` special fetch attribute
    * This attribute turns a special fetch into an invalid value expression for the given type
    * An invalid value expression is one that produces an invalid value for a given type, meaning that it can't be used for anything
    * When would you want to use this? When setting a variable to an invalid value so that it's current value is destroyed, without assigning a new, valid, & destroyable value to it
    * e.g. `@invalid(type MyCoolClass) $` produces an invalid value of type `MyCoolClass`
    * e.g. `@invalid(type int?)` produces an invalid value of type `int?`

## [0.6.3] - 2020-01-03
*Codename*: Ancast Poppy

### Fixed
#### Standard Library
  * `list` package
    * Fix the dereference operator being parsed as multiplication in the `Node` class
    * TODO in the parser: take line number into account when parsing expressions
      * Maybe do all possible parses and then analyze them later, taking into account line numbers, to determine which is the best alternative
  * `list` and `queue` packages
    * Remember to import `Size`!
  * `util` package
    * Fix floating point stringification for plain zeros (`0` or `0.0`)
### Added
  * Packaging! (for the compiler)
    * Alta now uses CPack to create distribution packages and installers for all the major platforms
    * TODO: Setup automatic RPM builds using the Ubuntu image in Azure Pipelines (it *should* be possible)

## [0.6.2] - 2019-12-21
*Codename*: Ancast Juniper

### Fixed
#### CLI
  * Newlines are no longer added between expressions in the generated C code, which allows preprocessor definitions containing expressions to work properly

## [0.6.1] - 2019-12-19
*Codename*: Ancast Icebucket

### Added
#### Language
  * `returnTypeOf` fetch/accessor attribute
    * Used to indicate that the fetch or accessor should actually fetch the return type of the target
    * e.g. if `foo` is a variable/function with a type of `(int, int) -> double`, `@returnTypeOf foo` would evaluate to a type of `double`
    * This is particularly useful for getting the return type of a function that returns a capture class (in the future, once automatic return type inference is implemented) and capture-class-like types like generators
### Fixed
#### CLI
  * A fix in AltaCore now allows the error logging function in the CLI to find the right line when a parsing error occurs

## [0.6.0] - 2019-12-14
*Codename*: Ancast

### Added
#### Language
  * Basic error handling/throwing
    * C++-like syntax and behavior
    * Runtime state is automatically managed when errors occur (e.g. variables are destroyed)
  * External code literals
    * Markdown-like syntax:
      ```alta
      literal var something = 5
      \`\`\`
      int somethingElse = something + 5;
      printf("something = %d, somethingElse = %d\n", something, somethingElse);
      \`\`\`
      ```
    * These are used to interpolate code in the output literally
    * At the moment, since the only target available is C transpilation, all external code literals are interpreted as C code
  * Union types
    * e.g. `int | double | String | ptr byte`
    * These are implemented as C tagged unions
    * They can be cast to a subset of the union or a specific type member, or they can be widened to include more types
    * `instanceof` can be used to determine what type is currently contained (or what subset)
  * `@@noruntime` general attribute
    * Used to tell the compiler not to generate any code that depends on the runtime for the scope of the attribute
    * e.g. if used in a function, the compiler will not generate any code to manage variable lifetimes and the like
  * `@@initializeModule` general attribute
    * Used to tell the compiler to generate code to initialize the current module
    * This is mainly useful when the code that will be calling into the module is not managed by Alta (e.g. not Alta code and no interface present)
  * Optional types
    * e.g. `int?`
    * These are wrappers around a type, indicating that it may or not have a value
    * When cast to a `bool` (e.g. via `as bool`, `if`, `!`, etc.), it become `true` if a value is present, `false` otherwise
    * The dereference operator (`*` or `valueof`) is used to get the contained value, if any is present
    * Values of the contained type (e.g. the `int` in `int?`) are automatically wrapped when casting to the optional type (e.g. in a function call, variable definition, assignment, plain cast, etc.)
  * Lambdas
    * a.k.a. anonymous functions
    * e.g.
      ```alta
      let myThing = 5
      let myFunc = (foo: int, bar: String) => int {
        let tmp = doSomethingWith(bar)
        myThing += foo
        return myThing + tmp
      }
      ```
    * These are anonymous functions that can be used as expressions
    * The best part, though, is that they work like lambdas in other languages; namely, they can capture their environment
    * By default, any variable from parent scope used is referenced, but the `@copy` attribute can be added to the lambda to specify a variable to copy into the lambda instance:
      ```alta
      let myThing = 5
      let myLambda = @copy(myThing) (foo: int) => void {
        # this modifies our copy, not the outside one
        myThing += 5
      }
      ```
  * Special fetch expressions
    * e.g. `$`, `$something`, etc.
    * These are special variables that mean different things in different contexts
    * Their main use at the moment is to fetch the input value in an operator method
  * `to` and `from` cast operators
    * e.g. `from String { ... }`, `to String { ... }`
    * These are used when trying to create a class from another type or cast a class to another type
    * They are user-defined methods called when trying to perform either of the aforementioned actions
    * Multiple layers of them can also be used to find a cast (e.g. `Address -> String -> ptr const byte`)
  * Virtual methods
    * These work just like they do in other languages, allowing a child class to override the method and have the child's version called even when a pointer/reference to the embedded parent instance is used
    * Enabled via the `@virtual` attribute on a method
    * Supported for methods and accessors
    * No support for virtual operators yet (although a workaround to create a `public`/`protected` implementation method and delegate to it in the operator)
    * The `@override` attribute can be used in a child class to make sure a virtual method is being overridden with the method signature (and error if no method is being overridden)
  * Multiple local exports in `export` statements
    * Pretty self-explanatory: multiple local items can now be exported in a single `export` statement
  * Enumerations
    * e.g.
      ```alta
      enum MyEnum: int {
        Foo,     # 0
        Bar = 5, # 5
        Qux,     # 6
      }
      ```
    * These work like stronger versions of C's enums
    * The underlying type (e.g. `int`, in this example) is required (there is no default type)
    * The initializer for each member (if one is provided) is cast to the underlying type before assigning it
    * Members must be fully accessed (i.e. they don't leak out into the parent scope); e.g. `MyEnum.Foo`, not just `Foo`
    * When they're used as types (e.g. `var myVar: MyEnum`), they become their underlying type (e.g. `int`, in this example)
  * Capture classes
    * These are just classes when they're used inside another scope (e.g. functions)
    * They essentially work like lambdas, but at a class level
    * Like lambdas, they can capture their environment, referencing (or copying, via the `@copy` attribute) variables if necessary
  * Generators
    * These are functions that, when called, create an instance of a state class
    * Every invocation of the `next` method on that instance advances the function state and generates a return value
    * When it is done, the `next` method returns `null`
    * The function's local variables and control flow are automatically managed by the runtime
    * Generators can be written just like regular functions, with the exception that they can `yield` a value which essentially pauses the function
    * The next invocation will resume the function from the same point
    * Unlike generators in some languages, Alta's generators can also have a value passed in on each invocation of `next`, which is the turned into the result of the `yield` expression in the function body
#### Standard Library
  * `io` package
    * `File` class
      * Simple wrapper over C file API
      * Currently only allows reading
    * `print`, `printLine`, and `printError`
      * These are typesafe and Alta-style alternatives to `printf`
      * Automatic formatting for common types, and any type with a `to String` cast operator
  * New `process` package
    * `arguments` allows you to get a `Vector<String>` with the current process's input arguments
    * `spawn` allows you to spawn and manage another process
  * `string` package
    * New `CodePoint` class
      * Easily manage UTF8 code points
      * Automatically handles multicharacter code points
    * `rawstringsEqual` - Checks if the provided `rawstring`s are equal
    * `String` class
      * Addition (`+`), addition assignment (`+=`), subscript (`[myIndex]`), equality (`==`), inequality (`!=`), and bitxor assignment (`^=`) operators added
      * `to ptr const byte` - Returns the contained raw character string
      * `to bool` - `true` if the string is non-empty, `false` otherwise
      * `includes(CodePoint | byte...)` method - Checks if any of the given code points are present in the string
      * `includes((CodePoint) => bool)` method - Calls the function with each character, returning `true` as soon as the function returns `true`; otherwise, `false` if it returned `false` for each character
      * `items` iterator - Generates a `CodePoint` for each item
  * `vector` package
    * `Vector` class
      * Subscript (`[myIndex]`) operator added
      * `from` and `to` methods - Create copies that are slices of the vector `from` and `to` the given indexes
      * `clear` method - Clears the vector, calling the destructor for each item
      * `items` iterator - Generates a `ref T` for each item
  * `map` package
    * `Map` class
      * Subscript (`[myIndex]`) operator added
      * `entries`, `keys`, `values` - Accessors for `Vector<K, V>`, `Vector<K>`, and `Vector<V>`, respectively
      * `items` iterator - Generates a `[util].Pair<K, V>` for each entry
  * New `unicode` package
    * This package provides some useful functions for converting between different Unicode encodings, as well as a few utility functions (e.g. `detectRequiredUTF8Bytes`, `split`, etc.)
  * `util` package
    * Radicies can now be used to parse and stringify numbers (e.g. base 10, base 16, base 8, etc.)
    * New `WeakArray` class - Non-owning container for an array
  * `types` package
    * New integer manipulation bitfields for the most common integer widths
  * `exceptions` package
    * `Impossible` exception added
  * `memory` package
    * New `Box` class
      * Reference-counted container for a heap-allocated value
      * Unlike C++'s `shared_ptr`, which requires that all `shared_ptr`s to the same address be derived from a single initial `shared_ptr`, a `Box`'s reference count is kept in a global table and indexed by the target address
  * New `list` package
    * `List` class - A generic doubly-linked list container
  * New `queue` package
    * `Queue` class - A generic First-In-First-Out (FIFO) container based on `[list].List`
  * New `meta` package
    * Basic introspection/reflection pacakge for Alta
    * Allows Alta code to inspect and modify the runtime's behavior
    * Currently highly experimental and the API is highly subject to change
### Changed
#### Language
  * Verbal conditional expressions (e.g. `foo if bar else qux`) have been removed
    * Punctual conditional expressions (e.g. `bar ? foo : qux`) can be used instead
    * The reasoning behind this change is that it's simpler to pick one style and stick with it
#### Standard Library
  * `string` package
    * `String` class
      * `String` is now a UTF8 string container, instead of a plain ASCII string container
  * `io` package
    * `print`, `printLine`, and `printError`
      * These functions now ensure that the output stream (e.g. stdout or stderr) is in UTF8 mode before using it
  * `util` package
    * The integer parsing methods now throw an error when the target type overflows
  * `exceptions` package
    * `IndexOutOfBoundsException` renamed to `IndexOutOfBounds`
### Fixed
TBD

## [0.5.2] - 2019-06-05
TBD

## [0.5.1] - 2019-06-02
TBD

## [0.5.0] - 2019-05-26
TBD

## [0.4.0] - 2018-12-20
### Added
#### Language
  * Variable parameters
    * e.g. `declare function someFunc(someParams: int...): void`
    * Originally added to properly support `printf`, but that required 2 other features that were also added: parameter attributes and arbitrary types
    * Fully supported for function declarations, partially supported for function definitions
  * Parameter attributes
    * e.g. `function foo(@SomeDomain.someAttribute @someOtherAttribute bar: int): void`
  * Conditional statements. Examples:
    * `if true return 9`
    * ```alta
      if 8 > 9 {
        return 1
      } else if 8 == 9 {
        return 2
      } else {
        return 3
      }
      ```
  * Conditional expressions
    * Two syntaxes are supported: verbal and punctual
    * e.g. Verbal = `3 if true else 9`
    * e.g. Punctual = `true ? 3 : 9`
    * The verbal style is inteded to be more natural to read, whereas the punctual style is inteded to be easier to use for people who come other languages where it is used (e.g. C, JavaScript, Java, C#, etc.)
  * More binary operators
    * Equality (`==`)
    * Inequality (`!=`)
    * Greater than (`>`)
    * Less than (`<`)
    * Greater than or equal to (`>=`)
    * Less than or equal to (`<=`)
  * `void` type
  * Arbitrary types for function declartions
    * e.g. `declare function foo(bar: any): void`
    * This effectively disables type checking for whatever it's used on. In the example, it will allow any value to be passed in for `bar`
    * Originally added to properly support `printf`
#### CLI
  * CMakeLists.txt generation for generated C code
  * Command line interface options (via [TCLAP](http://tclap.sourceforge.net/))
    * Verbose output option
    * Custom output directory option
  * Support individual target compilation or full package compilation
  * New example: [Fibonacci](examples/fibonacci.alta)
### Updated
#### CLI
  * [Talta v0.8.0](https://github.com/alta-lang/talta/blob/v0.8.0/CHANGELOG.md#080---2018-12-20)

## [0.3.1] - 2018-12-04
### Fixed
#### CLI
  * Fixed a module resolution error when the CLI is launched using a relative path on Linux
    * Possibly affected macOS as well
    * Fixed by absolutifying the program's path relative to the current working directory
      * Has no effect when the path is already absolute, so :+1:

## [0.3.0] - 2018-12-03
### Added
#### Language
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
#### CLI
  * Standard library module/package resolution
    * At the moment, our stdlib implementation contains:
      * A `string` module with 2 simple test functions (`something` and `otherThing`) that returns integers when they're called
      * An `io` module that exports a basic `printf` declaration
    * The stdlib directory is resolved relative to the location of the executable and expects it to be named...
      * ... `stdlib` in the same directory as the executable on Windows
        * e.g. `C:\\Program Files\\Alta\\stdlib`
      * ... `alta-stdlib` in a `lib` directory in the parent directory of the executable's parent directory on Linux/macOS
        * e.g. `/usr/local/lib/alta-stdlib`
    * For debug builds (i.e. local builds not meant to be installed), the path to the stdlib is provided via a definition created by CMake (`ALTA_DEBUG_STDLIB_PATH`)
### Fixed
#### CLI
  * Module inclusion index generation
    * The problem was that the output directory was being implicitly created when the first module was transpiled, but the index file handle is created before this happens, so when the index file handle was created and tried to create `index.h`, it failed because the output directory hadn't been created
    * The fix, quite simply, was to make sure the output directory is created *before* we try to create the index file
### Changed
#### Language
  * Preprocessor substitutions now have a different set of delimiters: instead of `@something@`, it's now `@[something]`
    * This change occurred to facilitate cleaner attribute syntax
#### CLI
  * Debug builds now use the local stdlib in-place, instead of copying it to the build directory
    * They also fallback to using the executable-relative stdlib path when the debug stdlib can't be found
  * Build artificats are now put into their own folders in the build directory: `bin` for executables and `lib` for libraries
### Updated
#### CLI
  * [Talta v0.7.1](https://github.com/alta-lang/talta/tree/v0.7.1)
### Additional Release Notes
#### CLI
  * We now have a policy of changelog modularization, and this is in effect for the various subprojects as well
    * Basically, what it means is that only the changes made to each individual project will be added to their changelogs. Any changes or fixes that do not result from a direct change to a project (i.e. they're the result of updating a submodule) will not be reflected in that project's changelog.
    * e.g. In this release, we updated to Talta v0.7.1, and that brought along with it **a lot** of new features. However, the changes can be viewed in Talta's changelog, and they will not be repeated here

## [0.2.0] - 2018-11-18
### Added
#### Language
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
#### CLI
  * File preprocessing support
### Updated
#### CLI
  * Talta v0.2.1

## [0.1.0] - 2018-11-13
### Added
#### Language
  * Assignment expressions!
  * Variable definitions are now expressions that return a reference to the newly defined variable
  * Binary operators! Basic math is now available via `+`, `-`, `*`, and `/`
  * Booleans
    * Note: only the boolean type and boolean literals have been added, no conditional logic
  * Working module system using `import` and `export`
    * Functions and variables can be exported
    * Currently, only cherry-pick imports (e.g. `import foo, bar, foobar from "something.alta"`) are supported
### Updated
#### CLI
  * Talta v0.2.0

## [0.0.0] - 2018-11-08
### Added
#### Language
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
#### CLI
  * Single file support
  * Token, AST, and DET printing support (though it's not in use at the moment)
  * Output directory support
  * Colored terminal output support
