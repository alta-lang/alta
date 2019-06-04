# Packages and Modules
Alta organizes code into packages, which can be further organized into modules. Packages follow the Node philosophy: they should do one thing and do it well. Modules, then, are only meant to be a way to further organize and separate different sections of your code.

Say you're writing a simple JSON parser (such as [Jason](https://github.com/alta-lang/jason)). The goal of your package is to parse (and possible stringify) some JSON data. That's all your package should do, no more, no less. Possible modules might be:
  * A module to define the classes you'll use to represent your data
  * A module to implement the parser
  * A module to implement the stringifier (if you'll have one)
  * A module for string parsing utilities
  * A module for assorted utilities that don't really fit anywhere

The way you choose to organize and structure your package is up to you, just remember that it should be simple and uncluttered. If you find yourself implementing lots of functionality that isn't directly related to the goal of your package, you should try to split it off into another package and instead just import it when you need to use it. Following the previous example, if you see that you're writing lots of code related to string parsing or miscellaneous utilities, you would probably want to make those their own package and import them into your JSON parser as necessary.

## Importing
Organizing code into packages and modules is useless without a way to access that code. Code can be easily and intuitively imported using an `import` statement.

You can import items individually like this:
```alta
import simpleAdd from "my-math-package"

import one, two, three from "numbers"

import { hi, hola, bonjour } from "greetings"
```

And you can also import entire modules under a namespace like so:
```alta
import "my-math-package" as myMath

# access myMath's items like this:
myMath.simpleAdd
```

## Exporting
Scope items can be exported with either an `export` modifier or an `export` statement. Classes, functions, variables, and type aliases can be exported using the `export` modifier when they're declared/defined. Exporting items after importing them or after they're declared/defined can be done with an `export` statement.

Export modifiers:
```alta
# to define an exported function
export function foo(): void {
  # ...
}

# to define an exported class
export class Foo {
  # ...
}

# likewise for variables and type aliases
export var foo: int = 4
export type foo = ptr int
```

Export statements:
```alta
export someItem

export item1, item2, item3

export { first, second, third }
```

There's also a shortcut for re-exporting exports from other modules:
```alta
# this:
export foo from "bar"
# is the same as this:
import foo from "bar"
export foo

# this:
export * from "bar"
# is the same as re-exporting each item from bar
import foo, bar, foobar from "bar"
export foo, bar, foobar

# this:
export * as bar from "bar"
export * from "bar" as bar
# is the same as:
import * as bar from "bar"
export bar
```

## Package Structure
All packages must have a `package.alta.yaml` file at their root containing some basic information about the package. There are only 2 required fields: `name` and `version`. Here are all the available fields:
| Field name | Meaning                                                               | Possible values                                               |
| ---------- | --------------------------------------------------------------------- | ------------------------------------------------------------- |
| `name`     | The name of the package                                               | A string of characters in this range: A-Za-z0-9-_             |
| `version`  | The version of the package                                            | A version string that satisfies [SemVer](https://semver.org/) |
| `main`     | The default (i.e. main) module to import when the package is imported | A path (absolute or relative to the package root)             |
| `targets`  | The different targets to produce when the package is compiled         | An array of objects, as described in the next table           |

All fields for the items in the `target` array are required:
| `target` object field | Meaning                                             | Possible values                                   |
| --------------------- | --------------------------------------------------- | ------------------------------------------------- |
| `name`                | The name of the output binary                       | A string of characters allowed for a filename     |
| `main`                | The entry module for the target                     | A path (absolute or relative to the package root) |
| `type`                | What kind of binary will be produced for the target | `exe` for an executable, `lib` for a library      |
