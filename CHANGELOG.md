# Changelog
All the changes for the language itself will be kept in this file. For changes on the subprojects, refer to their respective changelogs.

This project and all of its subprojects follow [semantic versioning](https://semver.org).

## [Unreleased]
Nothing yet.

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
