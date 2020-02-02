# `libc` package - Alta Standard Library
This package declares most of the functions, structures, types, constants, and variables from the C99 standard library.

If a declaration is missing, it's most likely because it's not relevant to or usable in Alta (e.g. `va_start` and `va_end`, `iso646.h`, etc.).

## Depends On
This modules depends on the C99 Standard Library (obviously). Specifically, these headers:
  * [`assert.h`](http://www.cplusplus.com/reference/cassert/)
  * [`ctype.h`](http://www.cplusplus.com/reference/cctype)
  * [`errno.h`](http://www.cplusplus.com/reference/cerrno)
  * [`float.h`](http://www.cplusplus.com/reference/cfloat)
  * [`inttypes.h`](http://www.cplusplus.com/reference/cinttypes)
  * [`limits.h`](http://www.cplusplus.com/reference/climits)
  * [`locale.h`](http://www.cplusplus.com/reference/clocale)
  * [`math.h`](http://www.cplusplus.com/reference/cmath)
  * [`setjmp.h`](http://www.cplusplus.com/reference/csetjmp)
  * [`signal.h`](http://www.cplusplus.com/reference/csignal)
  * [`stdarg.h`](http://www.cplusplus.com/reference/cstdarg)
  * [`stddef.h`](http://www.cplusplus.com/reference/cstddef)
  * [`stdint.h`](http://www.cplusplus.com/reference/cstdint)
  * [`stdio.h`](http://www.cplusplus.com/reference/cstdio)
  * [`stdlib.h`](http://www.cplusplus.com/reference/cstdlib)
  * [`string.h`](http://www.cplusplus.com/reference/cstring)
  * [`time.h`](http://www.cplusplus.com/reference/ctime)
  * [`wchar.h`](http://www.cplusplus.com/reference/cwchar)

## Declarations
Each module can be imported individually, like so:
```alta
import "libc/lib/stdio.alta" as stdio

# to access printf:
stdio.printf("blah blah\n")
```

Or they can be imported all at once:
```alta
import "libc" as libc

# to access stdio:
libc.stdio
# to access printf:
libc.stdio.printf("blah blah\n")
```

For a list of exports, see the references linked in the [Depends On](#depends-on) section above.
