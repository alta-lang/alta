# Frequently Asked Questions

## Why transpilation?
It might seem strange to output C code as a means of compilation, but the advantage there is that Alta doesn't have to reinvent the wheel when it comes to optimization; it can rely on decades of progress and experience in C compilers.
Transpilation to C also means easy interoperability with most existing C code (and C++ code with C bindings).
Another plus is portability: since Alta transpiles to C, Alta code can target any platform that C can.

Of course, if we were to ever decide that want to compile directly to machine code, we could do that as well, because of the way the project is set up (see the [Subprojects](#subprojects) section below).
