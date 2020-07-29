# Editor Integration

## Visual Studio Code

### Syntax Highlighting

Install the [Alta Language Support](https://marketplace.visualstudio.com/items?itemName=alta-lang.language-alta) extension from the Marketplace.

### File Icons

#### vscode-icons

Copy `file_type_alta.svg` to your custom icons directory and add the following snippet to your VS Code settings:

```json
"vsicons.associations.files": [
  // other stuff...
  {
    "icon": "alta",
    "extensions": ["alta"],
    "format": "svg"
  },
  // more stuff...
],
```

You can find more information on how to do this [here](https://github.com/vscode-icons/vscode-icons/wiki/Custom).
