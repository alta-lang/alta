# `exceptions` package - Alta Standard Library
This package contains a base class that should be used for all exceptions and a few common exceptions.

All documentation for standard library packages list classes that inherit from the base exception class (called `Exception`) in a section called "Exceptions", including this one.

Classes
---
## `Exception`
**Has copy constructor**: Yes (default)\
**Has destructor**: Yes (default)

**Summary**: A basic exception class

**Notes**

All Alta exceptions should inherit from this class. While it's not required to do so, it's a nice way of providing some basic information about the exception to anyone who might encounter it.

Note that since Alta is still highly unstable, this class currently contains no useful members or methods. In fact, the entire exception system might be eliminated since different options for exception handling are being investigated.

Exceptions
---
## `NullPointerAccess`
**Summary**: Indicates that there was an attempt to access a null pointer

**Notes**

This is thrown by checks performed by the recipient of the null pointer. An attempt to access a null pointer will **not** automatically throw this error (at the moment), it will perform the same action it would in C: undefined behavior.

## `InvalidArgument`
**Summary**: Indicates that an invalid argument was provided

## `IndexOutOfBounds`
**Summary**: Indicates that there was an attempt to access an index outside the permitted bounds

**Notes**

This is usually thrown when trying to access an element that doesn't exist in a container.

## `UnsupportedPlatform`
**Summary**: Indicates that an operation that was trying to be performed is not supported on the current platform

## `Impossible`
**Summary**: Indicates that something has occurred that never should have been possible

**Notes**

This indicates that there has been a violation of the expections of the program and should be carefully investigated.
