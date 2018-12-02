# Alta
The modern alternative to C and C++

This repository is home to both the language docs and its C transpiler command line interface.

## Getting Started
To get started with a simple "Hello, world!", check out the [Getting Started guide](docs/getting-started.md) in the docs.

## Other Documentation

Head on over to the [documentation](docs/README.md) for the standard library API and a language reference, as well as any other miscellaneous documentation on Alta.

## Frequently Asked Questions

### What's Alta?
Alta (pronounced "all-tah") is a brand new mid-level programming language (not as high-level as Python, for example, but not as low level as Assembly), mainly created to make up for the deficiences of C++, but also to make it easier to get into systems programming.

### Isn't C++ fine? And doesn't it get updated every few years?
Unions (*proper* unions). Interfaces. **Modules**.
C++ doesn't have any of these.
And at times, C++'s syntax is just plain ugly and hard to read.

C+\+20 is suppossed to provide some of these: namely modules, interfaces (which they call "concepts"), and a few more.
However, tacking  on features is exactly the reason C++ is so hard to work with.
What we need is language designed from the ground up to incorporate these essential language features.
Ergo, Alta was born.

Modules were they main reason Alta was created.
C and C++ handle dependencies and code inclusion by [literally just copy-pasting other code](https://en.wikipedia.org/wiki/Include_directive) into your code.
This is big no-no in modern languages and causes many headaches, and C++20's plans to handle modules is *not* a solution (it imports whole modules at a time and provides no way to namespace their exports or import only individual components).

Also, Alta has deviated from C++ in another important area: readability. Alta strives to be as easy to understand and straightforward as possible.
It's the reason why many things that in C++ use puncutation use keywords in Alta (e.g. `ptr` instead of `*` for pointers, `ref` instead of `&` for references, `function` instead of some funky syntax, etc).

### What about Rust?
Rust is a language with great potential and power, and in fact it solves many problems with C (memory safety, for example).
Unfortunately, it has a steep learning curve, especially for those of us used to more object-oriented languages like C++.
Alta hopes to provide as much safety as possible, like Rust, but without the learning curve.
Alta code should be simple to read and write.

### Why transpilation?
It might seem strange to output C code as a means of compilation, but the advantadge there is that Alta doesn't have to reinvent the wheel when it comes to optimization; it can rely on decades of progress and experience in C compilers.
Transpilation to C also means easy interoperability with most existing C code (and C++ code with C bindings).
Another plus is portability: since Alta transpiles to C, Alta code can target any platform that C can.

Of course, if we were to ever decide that want to compile directly to machine code, we could do that as well, because of the way the project is set up (see the [Subprojects](#subprojects) section below).

## Attention! Calling all contributors!
Alta is in dire need of contributors to help advance the project. It's not ready for public use yet (or really, *any* use at all), so Alta needs contributors to work on it and help it grow.

There's no commitment necessary, even a small pull request could help. Don't be shy, go ahead and dive into the code. :smile:

## Subprojects
There are various subprojects that make up Alta. Each has been separated because they might
prove useful for stuff other than just the transpiler.

  * [AltaCore](https://github.com/alta-lang/alta-core) - Alta's core frontend functionality, wrapped into a neat, no-so-little library
  * [Ceetah](https://github.com/alta-lang/ceetah) - C source code generation. Pure awesomeness. Play around with a C AST and then generate C code from it
  * [Talta](https://github.com/alta-lang/talta) - Alta's C transpiler. Uses a big helping of Ceetah along with a dash of AltaCore to bake some tasty C code from any given Alta AST

## Changelog
See [CHANGELOG.md](CHANGELOG.md) for language changes, or [CLI-CHANGELOG.md](CLI-CHANGELOG.md) for the compiler command line interface's changes. There's also changelogs for each subproject, available in each subproject's repository.
