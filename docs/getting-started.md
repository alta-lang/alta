# Getting Started
:warning: :construction: Warning! This guide (and Alta itself) is still under construction and some parts may not work! :construction: :warning:

So, you wanna program in Alta? Well, good choice! It's very simple to get started, and there's only a few things you'll need to understand before printing your first "Hello, world!" in Alta.

## Installing the transpiler
First things first, you gotta install Alta's C transpiler, `altac`. This is what's going to translate Alta code into C code for us, and then it'll help us compile the generated C code.

If you haven't done so already, head on over to the [releases page](https://github.com/alta-lang/alta/releases) and download the right installer for your system.

Second, make sure that you have a working C compiler. If not, install the right compiler for your system:
  * **Windows**
    * [MSVC Build Tools](https://visualstudio.microsoft.com/thank-you-downloading-visual-studio/?sku=BuildTools&rel=15) or...
    * [MinGW](https://osdn.net/projects/mingw/releases/)
  * **macOS**
    * [macOS Command Line Tools (macOS Clang)](http://osxdaily.com/2014/02/12/install-command-line-tools-mac-os-x/)
  * **Ubuntu**
    * `sudo apt-get install gcc`
  * **Other Linux**
    * Search the internet for "\<your-linux-distro> c compiler"

Finally, you'll need to install [CMake](https://cmake.org/) (Alta uses it to simplify the process of finding a C compiler and adding the right compiler flags to compile the resulting code).

Great! Now you're ready to jump into the code!

## Getting something on the screen
With that set up, let's go ahead and throw you into the deep end. Put the following code into a file called `hello-world.alta` and see if you can figure out what this does all on your own:

```alta
import printf from "io"

literal function main(): int {
  printf("Hello, world!\n")

  return 0
}
```

Just kidding! Of course you don't have to figure that out on your own! Here's the rundown of what this does:

### Line 1
```alta
import printf from "io"
```

As you can probably assume, this imports only the `printf` function from the `io` module in the standard library.

`printf` is a function that Alta borrows from C, and what it does is that it prints the string given to it to the screen, and the `io` module is a component of Alta's standard library that exposes many useful I/O constructs.

If you'd like to know more about the `io` module, [check out its API page](stdlib/io.md). How about an explaination of importing and exporting in Alta? You can hit up [the reference on that](language-reference/imports-and-exports.md). Jonesing for some more information on modules and packages? Head on over to [the relevant reference](language-reference/packages-and-modules.md).

### Line 3

```alta
literal function main(): int {
```

This defines a `literal` (i.e. un-mangled) function called `main` that takes no parameters and returns an integer (`int`).

`main` is our special entry point; all programs (in Alta, C, and C++) must have one, since it's where the operating system begins executing code when our program is launched.

For more info on functions and a rationale for why the `literal` modifier is necessary here, see [the reference page on functions](language-reference/functions.md). To read more on Alta's types and type system, [check this out](language-reference/types.md).

### Line 4
```alta
  printf("Hello, world!\n")
```

This calls the `printf` function with a string to print: "Hello, world!\n".
Know what that `\n` does? It's an escape sequence that tells `printf` to print a new line.

So, technically, this will print "Hello, world!" and then add an new line to the screen.

### Line 6
```alta
  return 0
```

This, as you can guess, returns the integer `0` when the function exits.
Why is this necessary?
This is the exit code of the program i.e. a value that indicates whether the program was successful or not.
A value of `0` tells the operating system that nothing went wrong.

## Running the example
With that out of the way, compiling the code is as easy as 1, 2, 3!

### 1
Open up a shell (Command Prompt or PowerShell on Windows, Terminal on macOS/Linux) and make sure you're in the same folder as the `hello-world.alta` source file.

### 2
Run the following in that shell:
```bash
altac hello-world.alta
```

### 3
Your `hello-world` binary is now avilable in the `alta-build` folder. Go ahead and run it:

**PowerShell/Bash**:
```bash
./alta-build/hello-world
```

**Command Prompt**:
```batch
.\alta-build\hello-world.exe
```

## Onwards!
Congrats! You should feel proud of yourself! You just printed "Hello, world!" in a language you just started learning!

I'm sure you're excited to learn more and delve into some more complex language features.
Remember, take it one step at a time and make sure that you fully (or at least mostly) understand a feature before trying to use it.

Alright, you now have an Alta learner's license, and it's time to explore the road ahead.
Go ahead and jump into whatever you like. I'd recommend [visiting the language reference](language-reference/README.md).

Bye!
