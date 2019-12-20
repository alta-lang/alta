# CLI Changelog
All the changes for Alta's transpiler command line interface will be kept in this file.

This project follows [semantic versioning](https://semver.org).

## [Unreleased]
Nothing yet.

## [0.6.1] - 2019-12-19
### Fixed
  * A fix in AltaCore now allows the error logging function in the CLI to find the right line when a parsing error occurs

## [0.6.0] - 2019-12-14
TBD

## [0.5.2] - 2019-06-05
TBD

## [0.5.1] - 2019-06-02
TBD

## [0.5.0] - 2019-05-26
TBD

## [0.4.0] - 2018-12-20
### Added
  * CMakeLists.txt generation for generated C code
  * Command line interface options (via [TCLAP](http://tclap.sourceforge.net/))
    * Verbose output option
    * Custom output directory option
  * Support individual target compilation or full package compilation
  * New example: [Fibonacci](examples/fibonacci.alta)
### Updated
  * [Talta v0.8.0](https://github.com/alta-lang/talta/blob/v0.8.0/CHANGELOG.md#080---2018-12-20)

## [0.3.1] - 2018-12-04
### Fixed
  * Fixed a module resolution error when the CLI is launched using a relative path on Linux
    * Possibly affected macOS as well
    * Fixed by absolutifying the program's path relative to the current working directory
      * Has no effect when the path is already absolute, so :+1:

## [0.3.0] - 2018-12-04
### Added
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
  * Module inclusion index generation
    * The problem was that the output directory was being implicitly created when the first module was transpiled, but the index file handle is created before this happens, so when the index file handle was created and tried to create `index.h`, it failed because the output directory hadn't been created
    * The fix, quite simply, was to make sure the output directory is created *before* we try to create the index file
### Changed
  * Debug builds now use the local stdlib in-place, instead of copying it to the build directory
    * They also fallback to using the executable-relative stdlib path when the debug stdlib can't be found
  * Build artificats are now put into their own folders in the build directory: `bin` for executables and `lib` for libraries
### Updated
  * [Talta v0.7.1](https://github.com/alta-lang/talta/tree/v0.7.1)
### Additional Release Notes
  * We now have a policy of changelog modularization, and this is in effect for the various subprojects as well
    * Basically, what it means is that only the changes made to each individual project will be added to their changelogs. Any changes or fixes that do not result from a direct change to a project (i.e. they're the result of updating a submodule) will not be reflected in that project's changelog.
    * e.g. In this release, we updated to Talta v0.7.1, and that brought along with it **a lot** of new features. However, the changes can be viewed in Talta's changelog, and they will not be repeated here

## [0.2.0] - 2018-11-18
### Added
  * File preprocessing support
### Updated
  * Talta v0.2.1

## [0.1.0] - 2018-11-13
### Updated
  * Talta v0.2.0

## [0.0.0] - 2018-10-30
### Added
  * Single file support
  * Token, AST, and DET printing support (though it's not in use at the moment)
  * Output directory support
  * Colored terminal output support
