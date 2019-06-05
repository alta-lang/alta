# Frequently Asked Questions

## Isn't C++ fine? And doesn't it get updated every few years?
Unions (*proper* unions). Interfaces. **Modules**.
C++ doesn't have any of these.
And at times, C++'s syntax is just plain ugly and hard to read.

C+\+20 is suppossed to provide some of these: namely modules, a feature similar to interfaces (which they call "concepts"), and a few more.
However, tacking  on features is exactly the reason C++ is so hard to work with.
What we need is language designed from the ground up to incorporate these essential language features.
Ergo, Alta was born.

Modules were the main reason Alta was created.
C and C++ handle dependencies and code inclusion by [literally just copy-pasting other code](https://en.wikipedia.org/wiki/Include_directive) into your code.
This is big no-no in modern languages and causes many headaches, and C++20's plans to handle modules is *not* a solution (it imports whole modules at a time and provides no way to namespace their exports or import only individual components).

Also, Alta has deviated from C++ in another important area: readability. Alta strives to be as easy to understand and straightforward as possible.
It's the reason why many things that in C++ use puncutation use keywords in Alta (e.g. `ptr` instead of `*` for pointers, `ref` instead of `&` for references, `function` instead of some funky syntax, etc).

## What about Rust?
Rust is a language with great potential and power, and in fact it solves many problems with C and C++ (memory safety, for example).
Unfortunately, it has quite a learning curve, especially for those of us used to more object-oriented languages like C++.
Alta hopes to provide as much safety as possible, like Rust, but without the learning curve.
Alta code should be simple to read and write.

## Why transpilation?
It might seem strange to output C code as a means of compilation, but the advantadge there is that Alta doesn't have to reinvent the wheel when it comes to optimization; it can rely on decades of progress and experience in C compilers.
Transpilation to C also means easy interoperability with most existing C code (and C++ code with C bindings).
Another plus is portability: since Alta transpiles to C, Alta code can target any platform that C can.

Of course, if we were to ever decide that want to compile directly to machine code, we could do that as well, because of the way the project is set up (see the [Subprojects](#subprojects) section below).
