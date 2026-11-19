# MiniCalc Compiler (C++)

A small compiler for arithmetic expressions with three operations: **Add (+), Subtract (-), Multiply (*)**.
Compiler Construction practical.

## Phases (each one really runs)
1. **Lexical analysis** – characters -> tokens (NUMBER, PLUS, MINUS, MULTIPLY, LEFT_PAREN, RIGHT_PAREN)
2. **Syntax analysis** – recursive-descent parser builds a parse tree (AST)
3. **Semantic analysis** – walks the tree; rejects numbers/results too large to represent
4. **Intermediate code** – AST -> three-address code (`t1 = 5 * 2`)
5. **Code generation** – AST -> stack-machine assembly (`PUSH 5`, `PUSH 2`, `MUL`, ...)
6. **Execution** – a small virtual machine runs the generated code, showing the stack at each step

It stops at the first phase that fails and says which phase (Lexical / Syntax / Semantic) found the error.
As a safety check, the VM result is compared with the parse-tree result.
The target language is a stack-machine assembly run by a built-in VM (not native x86 machine code).

## Target instruction set
`PUSH n`, `ADD`, `SUB`, `MUL`, `NEG`, `HALT`. Operands are pushed first, then the operator pops two values and pushes the result.

## Grammar
```
expression -> ['-'] term (('+' | '-') term)*
term       -> factor ('*' factor)*
factor     -> NUMBER | '(' expression ')'
```
- `*` binds tighter than `+` and `-` (that is why term is below expression).
- `+` and `-` are left-associative: `2 - 5 - 3` = `(2 - 5) - 3` = -6.
- The only unary operator is a leading `-`, allowed at the start of an expression or right after `(`.
  So `-5 + 2` and `(-5) * 2` are valid, while `2-+5`, `2--5`, `2*-3` are **syntax errors**.
- Numbers: integers and decimals (`10`, `2.5`, `.5`). No exponent form.

## Build and run
```
g++ -std=c++17 -Wall -Wextra -o minicalc main.cpp
./minicalc          (Windows: minicalc.exe)
```
Commands inside the program: `help`, `brief` (result only), `full` (all phases, default), `test` (27 built-in tests), `exit`.
You can also run one expression directly: `./minicalc "10 + 5 * 2"`.

## Files
- `main.cpp` – complete source
- `demo_test_cases.txt` – inputs and expected outputs
- `presentation_guide.md` – outline for the class presentation
