# Alta
The modern alternative to C++

Alta is a modern alternative to C++, particularly aimed at improving simplicity, readability, and flexibility. At its core, Alta is very similar to C++: both aim to make it easier to directly express intent, and both aim to make it easier for programmers to write safe code and catch errors *at compile-time*. However, where C++ is restricted by an ISO standards committee and a large userbase, Alta is a new language that can take those goals to a new level.

Alta was mainly created to make up for the deficiencies of C++ (primarily modules), but it also tries to make it easier to get into systems programming. The goal is that, with Alta, programming low-level code should be as simple as programming high-level code (while still being safe). With Alta, things should Just Work&trade;.

This repository is home to both the language documentation and Alta's compiler CLI (command line interface).

## Getting Started
> :construction:
> Please note that, as of right now, Alta is very unstable. However, don't let that scare you away from trying it. In order to grow, Alta needs people to test it and find bugs.

To get started with a simple "Hello, world!", check out the [Getting Started guide](docs/getting-started.md) in the docs.

## Other Documentation

Head on over to the [documentation](docs/README.md) for the standard library API and a language reference, as well as any other miscellaneous documentation on Alta.

## Attention! Calling all contributors!
Alta is in need of contributors to help advance the project. It's not ready for public use yet (or really any *real* use at all), so Alta needs contributors to work on it and help it grow and stabilize.

There's no commitment necessary (no pun intended), even a small change like a grammar or spelling fix in the documentation could help. Don't be shy, go ahead and dive into the code. :smile:

## Frequently Asked Questions
Check out the [FAQ](FAQ.md) to see them (it's too long to put in the README).

## Subprojects
There are various subprojects that make up Alta. Each has been separated because they might
prove useful for stuff other than just the transpiler.

  * [AltaCore](https://github.com/alta-lang/alta-core) - Alta's core frontend functionality, wrapped into a neat, no-so-little library
  * [Ceetah](https://github.com/alta-lang/ceetah) - C source code generation. Pure awesomeness. Play around with a C AST and then generate C code from it
  * [Talta](https://github.com/alta-lang/talta) - Alta's C transpiler. Uses a big helping of Ceetah along with a dash of AltaCore to bake some tasty C code from any given Alta AST

## Changelog
See [CHANGELOG.md](CHANGELOG.md) for language changes, or [CLI-CHANGELOG.md](CLI-CHANGELOG.md) for the compiler command line interface's changes. There's also changelogs for each subproject, available in each subproject's repository.
