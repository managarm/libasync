# Contributing

Any kind of contributions to libasync are welcome, and greatly appreciated.

## Code contributions

Code contributions should follow the
[managarm coding style](https://docs.managarm.org/handbook/contributing/coding-style.html),
with the exception that all names are `snake_case` instead. In addition, any new
user-facing functionality that's added should also be appropriately documented.

## Documentation contributions

If you found typos, broken code or other mistakes in the documentation, feel
free to create an issue or make a pull request fixing them.

For writing new documentation, follow the following guidelines:

 - In class prototypes, only show user-facing methods.
 - Document the return types of every method shown.
 - Try to keep the Markdown lines between 80-90 characters long wherever reasonable.

It is also advised to follow this general outline for pages:

```md
---
short-description: ...
...

# Name

Short description of the functionality.

## Prototype

Prototype for the class or function(s).

### Requirements (optional)

Constraints placed on template types.

### Arguments (optional)

List of arguments and their description.

### Return value(s) (optional)

Description of return values and types.

## Examples (optional)

Example code

Code output
```
