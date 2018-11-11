# Changelog
All the changes for the language itself will be kept in this file. For changes on the subprojects, refer to their respective changelogs.

This project and all of its subprojects follow [semantic versioning](https://semver.org).

## [Unreleased]
### Added
  * Assignment expressions!
  * Variable definitions are now expressions that return a reference to the newly defined variable

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
