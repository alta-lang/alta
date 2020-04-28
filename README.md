# Alta
![Alta](docs/alta.png "Alta")

[![Build Status](https://facekapow.visualstudio.com/alta/_apis/build/status/alta-lang.alta?branchName=master)](https://facekapow.visualstudio.com/alta/_build/latest?definitionId=1&branchName=master)
[![License](https://img.shields.io/github/license/alta-lang/alta?color=%23428bff)](LICENSE)

Simple :dart:, safe :lock:, and powerful :muscle:

This repository is home to both the language documentation and Alta's compiler CLI (command line interface).

*Disclaimer*: This is currently a personal/hobby project (although I would like it to be more, if possible). This is mainly a learning experience for the inner workings of a compiler as well as how certain components of a language (e.g. Unicode support, JSON parsing, etc.) are implemented.

## Build Status

| Platform | Status |
| -------- | ------ |
| Windows  | [![Build Status](https://facekapow.visualstudio.com/alta/_apis/build/status/alta-lang.alta?branchName=master&jobName=Windows)](https://facekapow.visualstudio.com/alta/_build/latest?definitionId=1&branchName=master) |
| Linux    | [![Build Status](https://facekapow.visualstudio.com/alta/_apis/build/status/alta-lang.alta?branchName=master&jobName=Linux)](https://facekapow.visualstudio.com/alta/_build/latest?definitionId=1&branchName=master) |
| macOS    | [![Build Status](https://facekapow.visualstudio.com/alta/_apis/build/status/alta-lang.alta?branchName=master&jobName=macOS)](https://facekapow.visualstudio.com/alta/_build/latest?definitionId=1&branchName=master) |

## Design Goals
### Simplicity
Just because you're writing low-level code doesn't mean it should be hard to do things. Alta's primary goal is to make everything as easy and painless as possible.

You shouldn't have to jump through dozens of hoops just to, for example, parse some JSON. Or convert between Unicode formats. Or launch a new process. Or download a file. Or... you name it. The list goes on and on, but essentially, things should just be easy to do.

And it's not just limited to writing code: adding dependencies, dealing with cross-platform incompatibilities, compiling, etc. Alta aims to make *every* aspect of development as simple as possible.

Finally, low-level code shouldn't have to be ugly. [This](#) is a monstrosity and no human should ever have to deal with it.

### Safety
Low-level code shouldn't be dangerous. You should be able to feel confident that, if you make a mistake, you'll be caught before it's too late. Rust already takes this goal to heart, and Alta aims to follow Rust's example in this regard.

For example, dangling pointers and references should never be an issue, no two pieces of code should be able to modify a variable without both of them knowing that someone else is working with it, and anything that can't be checked at compile time should be checked at run time&mdash;all concepts derived from Rust.

### Power
Simplicity and safety shouldn't automatically translate to weakness and limitation. You should be able to write *anything* in Alta; if you can imagine it, you can program it in Alta.

This means that Alta should be able to go from bare-metal&mdash;namely, kernels and device drivers&mdash;to command line programs to graphical applications and even web sites (in the form of WebAssembly).

## Example

Parsing some JSON is as simple as 1, 2, 3:
  1. Download a JSON parsing package (like [Jason](https://github.com/alta-lang/jason)) to an `alta-packages` folder
  2. Import it in your code: `import "jason" as jason`
  3. Use it! Parse a simple string:
```alta
import "jason" as jason

literal function main(): int {
  let object = jason.parse("{\"hi\": \"hola!\"}")
  object["hi"] # returns "hola!"
  return 0
}
```

Alta packages should do one thing and do it right (much like Node.js packages), and this contributes to the simplicity of Alta. Just find the package that does what you need and stop worrying about implementation details.

## Getting Started
> :construction:
> Please note that, as of right now, Alta is very unstable. However, don't let that scare you away from trying it; in order to find bugs and fix them, Alta needs to be tested by people like you!

To get started with a simple "Hello, world!", check out the [Getting Started guide](docs/getting-started.md) in the docs.

## Other Documentation

Head on over to the [documentation](docs/README.md) for the standard library API and a language reference, as well as any other miscellaneous documentation on Alta.

## Contributions Are Welcome!
Any help is appreciated: bug reports, pull requests, documentation, language design input, etc. There's no commitment necessary (pun intended :wink:), even a small change like a grammar or spelling fix in the documentation could help. Don't be shy, go ahead and dive into the code. :smile:

## Frequently Asked Questions
Check out the [FAQ](FAQ.md).

## Subprojects
There are various subprojects that make up Alta. Each has been separated because they might
prove useful for stuff other than just the transpiler.

  * [AltaCore](https://github.com/alta-lang/alta-core) - Alta's core frontend functionality, wrapped into a neat, no-so-little library
  * [Ceetah](https://github.com/alta-lang/ceetah) - C source code generation. Pure awesomeness. Play around with a C AST and then generate C code from it
  * [Talta](https://github.com/alta-lang/talta) - Alta's C transpiler. Uses a big helping of Ceetah along with a dash of AltaCore to bake some tasty C code from any given Alta AST

## Changelog
See [CHANGELOG.md](CHANGELOG.md) for language changes, or [CLI-CHANGELOG.md](CLI-CHANGELOG.md) for the compiler command line interface's changes. There's also changelogs for each subproject, available in each subproject's repository.
