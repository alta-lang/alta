# CLI Changelog
All the changes for Alta's transpiler command line interface will be kept in this file.

This project follows [semantic versioning](https://semver.org).

## [Unreleased]
### Added
  * Standard library module/package resolution
    * At the moment, our stdlib implementation only has a `string` module with 2 simple test functions (`something` and `otherThing`) that returns integers when they're called
    * The stdlib directory is resolved relative to the location of the executable and expects it to be:
      * Named `stdlib` in the same directory as the executable on Windows
        * e.g. `C:\\Program Files\\Alta\\stdlib`
      * Named `alta-stdlib` in a `lib` directory in the parent directory of the executable's parent directory on Linux/macOS
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
  * Talta v0.7.1

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
