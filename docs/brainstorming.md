# Brainstorming
This is where some concepts and ideas that might be added to Alta are kept

## Range Safety for Pointers
Basically, this means that a certain number of bytes after that start of a pointer
are guaranteed to be accessible and usable by the receiver of the pointer. This
would enable much safer interaction with C code in Alta, as well as enable more 
safety for code that can't make the switch to references (or smart pointers, when 
those are added).

Range safety would be guaranteed by the runtime, but, as much as possible, static
analysis will also be done at compile time in order to warn developers of possible
errors sooner rather than later.

You may be thinking "gee, this sure sounds *a lot* like C99's variable-sized 
arrays", and you'd be right. The key differences, however, are that
  * these can be attached on-the-fly to any random pointer via a type cast,
  * they don't allocate anything, they simply describe the data they point to, and
  * they're monitored for safety at both compile-time *and* runtime

### Syntax

My current favorite possible syntax for it is:
```alta
let foo: ptr[80] byte
# which also makes sense because pointers can be accessed via subscript
# so the syntax would remind you that you're working with pointers
#
# when compounded:
let foo: ptr[3] ptr[7] byte
```

Another possiblity that reuses existing syntax via attributes is:
```alta
let foo: @safe(80) ptr byte
# which, when compounded would look like this:
let foo: @safe(3) ptr @safe(7) ptr byte
```
But, in my opinion, that's uglier, and syntax should try to be as sweet as possible
