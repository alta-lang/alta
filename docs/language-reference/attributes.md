# Attributes
Attributes are a simple way to add extra information to modules and scope items, often enabling extra behavior in the compiler itself.

Attributes come in two varieties: normal and general.

## Normal Attributes
Normal attributes add information to individual statements and expressions. For example, the `@read` attribute on a function indicates that a function should be used as a read accessor:
```alta
@read function imAVariable(): int {
  # hee hee, i'm actually not a variable :)
  return 6
}
```

How to add attributes to items and expressions that can have them is documented in each item/expression's reference.

## General Attributes
General attributes add information to the current module. For example, the `@CTranspiler.include` attribute tells Alta to include a C header in the output code for that module:
```alta
@@CTranspiler.include("stdio.h");
```
