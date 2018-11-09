# Alta
(pronounced "all-tah")
The modern alternative to C and C++

This repository is home to both the language docs and its C transpiler CLI.

## Subprojects
There are various subprojects that make up Alta. Each has been separated because they might
prove useful for stuff other than just the transpiler.

  * [AltaCore](https://github.com/alta-lang/alta-core) - Alta's core frontend functionality, wrapped into a neat, no-so-little library
  * [Ceetah](https://github.com/alta-lang/ceetah) - C source code generation. Pure awesomeness. Play around with a C AST and then generate C code from it
  * [Talta](https://github.com/alta-lang/talta) - Alta's C transpiler. Uses a big helping of Ceetah along with a dash of AltaCore to bake some tasty C code from any given Alta AST

## Changelog
See [CHANGELOG.md](CHANGELOG.md) for language changes, or [CLI-CHANGELOG.md](CLI-CHANGELOG.md) for the compiler command line interface's changes. There's also changelogs for each subproject, available in each subproject's repository.