# Grammar

This document defines which source characters and token sequences form a
syntactically well-formed Carven program.

The grammar owns:

- source encoding, whitespace, comments, and lexical token forms;
- syntactic productions and source-level delimiters;
- expression precedence and associativity;
- parser-level disambiguation that can be decided from tokens and delimiters.

The grammar does not define the meaning of accepted programs, name resolution,
type compatibility, control-flow validity, pattern coverage, diagnostics, or
generated output.

Lexer and parser behavior conform to the productions and disambiguation rules
in this document. Accepted syntax may still violate semantic constraints;
only the complete productions defined here are accepted as AST forms.

## Contents

- [1. Notation](#1-notation)
- [2. Lexical Grammar](#2-lexical-grammar)
- [3. Source Modules and Items](#3-source-modules-and-items)
- [4. Types](#4-types)
- [5. Statements and Blocks](#5-statements-and-blocks)
- [6. Branch Blocks](#6-branch-blocks)
- [7. Expressions](#7-expressions)
- [8. Conditional Forms](#8-conditional-forms)
- [9. Try and Match Forms](#9-try-and-match-forms)
- [10. Syntactic Disambiguation](#10-syntactic-disambiguation)

## 1. Notation

Productions use an ISO-style extended Backus-Naur form.

```ebnf
production = term, term;
choice     = first | second;
optional   = [ element ];
repeated   = { element };
grouped    = ( first | second ), suffix;
```

The notation has the following meaning:

- quoted text denotes a terminal spelling;
- names in `UPPER_SNAKE_CASE` denote lexical tokens;
- names in `lower-kebab-case` denote syntactic nonterminals;
- `,` denotes concatenation;
- `|` denotes alternatives;
- `[ x ]` denotes zero or one occurrence of `x`;
- `{ x }` denotes zero or more occurrences of `x`;
- `( x )` groups terms;
- prose following `where` denotes a syntactic predicate that is not
  conveniently expressed in context-free EBNF.

Productions describe token sequences. Whitespace and comments are discarded by
lexical analysis except inside literals and opaque C++ source fragments.

Statement parsing also carries a test context. A `test-block` enables it;
structurally nested loop and control-flow blocks inherit it, and a lambda body
disables it for that body. When enabled, the `statement` production admits a
`test-operation-statement`. This context is a formal grammar parameter omitted
from nonterminal names for readability.

## 2. Lexical Grammar

### 2.1 Source Text

Carven-tokenized source is UTF-8 encoded. C++ source-fragment payloads are
byte-opaque and are not validated as UTF-8. The identifier alphabet is
ASCII-only. Unicode source characters may occur in comments, string literals,
and C++ header names, but not in identifiers.

```ebnf
line-terminator = U+000A | U+000D, [ U+000A ];
horizontal-space = U+0020 | U+0009;
whitespace = horizontal-space | line-terminator;

line-comment = "//", { source-character - line-terminator },
               [ line-terminator ];
```

Block comments are not Carven tokens. Their byte spellings may occur inside an
opaque C++ source fragment because Carven does not tokenize its payload.

### 2.2 Identifiers and Keywords

```ebnf
ASCII_LETTER = "A" | ... | "Z" | "a" | ... | "z";
DIGIT        = "0" | ... | "9";

IDENTIFIER = ( ASCII_LETTER | "_" ),
             { ASCII_LETTER | DIGIT | "_" };
```

The following spellings are reserved keywords and are not emitted as
`IDENTIFIER` tokens:

```text
as break catch const continue else enum export false fn for if import in is let
match private rethrow return struct test throw true try using var while
```

`as` participates in the cast-expression production. Spellings without a
language production, including `new`, `delete`, and `nullptr`, remain
ordinary identifiers.

Canonical naming conventions are style guidance only. They do not change the
set of syntactically valid identifiers.

### 2.3 Numeric Literals

```ebnf
HEX_DIGIT    = DIGIT | "a" | ... | "f" | "A" | ... | "F";
BINARY_DIGIT = "0" | "1";
OCTAL_DIGIT  = "0" | ... | "7";

decimal-exponent = ( "e" | "E" ), [ "+" | "-" ], DIGIT, { DIGIT };
decimal-digits = DIGIT, { DIGIT };

integer-suffix = "i8" | "i16" | "i32" | "i64"
               | "u8" | "u16" | "u32" | "u64"
               | "isize" | "usize";

floating-suffix = "f32" | "f64";

decimal-integer-literal = decimal-digits, [ integer-suffix ];

decimal-floating-core = decimal-digits, ".", decimal-digits,
                        [ decimal-exponent ]
                      | decimal-digits, decimal-exponent;

decimal-floating-literal = decimal-floating-core, [ floating-suffix ]
                         | decimal-digits, floating-suffix;

hexadecimal-literal = "0", ( "x" | "X" ), HEX_DIGIT, { HEX_DIGIT },
                      [ integer-suffix ];

binary-literal = "0", ( "b" | "B" ), BINARY_DIGIT, { BINARY_DIGIT },
                 [ integer-suffix ];

octal-literal = "0", ( "o" | "O" ), OCTAL_DIGIT, { OCTAL_DIGIT },
                [ integer-suffix ];

NUMBER_LITERAL = hexadecimal-literal
               | binary-literal
               | octal-literal
               | decimal-floating-literal
               | decimal-integer-literal;
```

Numeric signs are prefix operators and are not part of `NUMBER_LITERAL`.
Suffixes are contiguous with their literal. C++ suffix aliases such as `f`,
`l`, and `ul` are not Carven tokens.

A decimal spelling with a floating suffix is a `decimal-floating-literal` even
when its value portion contains neither a decimal point nor an exponent. Thus
`1f32` and `1f64` are floating literals, while `1` and `1i32` are integer
literals.

The lexical grammar does not assign a type or perform range checking. Those
rules belong to the language semantics and are not lexical validity checks.

### 2.4 Character and String Literals

```ebnf
simple-escape = "\\'" | "\\\"" | "\\\\" | "\\n" | "\\t"
              | "\\r" | "\\0";

unicode-escape = "\\u{", HEX_DIGIT, { HEX_DIGIT }, "}"
                 where the hexadecimal digit count is at most six;

CHAR_LITERAL = "'",
               ( character-scalar | simple-escape | unicode-escape ),
               "'";

STRING_LITERAL = "\"",
                 { string-scalar | simple-escape | unicode-escape },
                 "\"";
```

`character-scalar` is one directly encoded UTF-8 Unicode scalar other than
`'`, `\`, or a line terminator. `string-scalar` is one directly encoded UTF-8
Unicode scalar other than `"`, `\`, or a line terminator. A literal must
terminate on the line on which it begins.

The decoded value of a character literal must contain exactly one Unicode
scalar. A Unicode escape must denote a scalar in `U+0000..U+10FFFF` excluding
the surrogate range `U+D800..U+DFFF`. The spellings `\xNN`, `\uXXXX`, and
`\UXXXXXXXX` are not accepted. Literal decoding performs no Unicode
normalization.

Adjacent string literal tokens do not form one token and are not implicitly
concatenated by the Carven grammar.

### 2.5 C++ Header Names

```ebnf
CPP_ANGLE_HEADER_NAME = "<", cpp-angle-header-content, ">";
CPP_QUOTE_HEADER_NAME = "\"", cpp-quote-header-content, "\"";
cpp-angle-header-content = cpp-angle-header-character,
                           { cpp-angle-header-character };
cpp-angle-header-character = source-character - ">" - line-terminator;
cpp-quote-header-content = cpp-quote-header-character,
                           { cpp-quote-header-character };
cpp-quote-header-character = source-character - "\"" - line-terminator;
```

Immediately after an `import` token and intervening whitespace or line comments,
`<` and `"` begin dedicated C++ header-name tokens. Their content is nonempty,
ends at the matching delimiter, and cannot contain a line terminator. No Carven
escape decoding or interpolation occurs. Outside that lexical context, `<` is
an operator token and `"` begins an ordinary Carven string literal.

### 2.6 C++ Source Fragments

```ebnf
cpp-fence = "-", "-", "-", { "-" };

CPP_SOURCE_FRAGMENT = "#[cpp]",
                      horizontal-space, { horizontal-space },
                      cpp-fence, { horizontal-space }, line-terminator,
                      cpp-source-tail;
```

The exact introducer is `#[cpp]`; no whitespace is allowed inside it. At least
one horizontal-space character separates it from an opening fence of at least
three `-` characters. Only horizontal space may follow that fence before the
required line terminator.

`cpp-source-tail` is scanned as bytes through the first matching closing fence
line. The closing fence uses exactly the same number of `-` characters as the
opening fence. It is the only non-horizontal-space content on its line and may
be indented. The payload is every byte before that closing line and may be
empty. A line terminator after the closing line is not part of the token. The
complete form, including both fences, is one `CPP_SOURCE_FRAGMENT` token and has
no terminating semicolon.

Scanning is line-based and byte-opaque. Carven does not recognize C++ braces,
comments, character or string literals, raw strings, preprocessing directives,
declarations, names, types, or effects. Consequently, a payload line that is a
matching closing fence ends the fragment even when C++ would treat that line as
part of another construct. The author selects a longer fence when the payload
contains such a line.

### 2.7 Punctuators

Lexical analysis uses maximal munch. The punctuator set is:

```text
( ) [ ] { } , . .. : ;
+ - * / % ! ? = < > & | ^ ~
+= -= *= /= %= != == <= >= ++ --
&& || << >> &= |= ^= <<= >>=
-> => ::
```

## 3. Source Modules and Items

The source-module grammar describes the order and shape of source items. Caller-provided
module identity, lexical module-path resolution, filesystem policy, visibility,
and generated artifacts belong to later compiler or host boundaries.

```ebnf
source-module = { import-declaration },
              { top-level-item };

top-level-item = module-item
               | cpp-import-function-declaration
               | cpp-export-function-definition
               | test-declaration
               | CPP_SOURCE_FRAGMENT;

module-item = [ visibility-modifier ], module-declaration;

visibility-modifier = "private" | "export";

module-declaration = enum-declaration
                   | struct-declaration
                   | function-definition
                   | module-constant-declaration;

cpp-import-function-declaration = [ "private" ],
                                  "import", "(", "cpp", ")",
                                  function-head, ";";

cpp-export-function-definition = "export", "(", "cpp", ")",
                                 function-head, ordinary-block;

module-constant-declaration = "const", declaration-name,
                              [ ":", type ],
                              "=", expression, ";";

declaration-name = IDENTIFIER
                   where the token spelling is not "_";
```

Imports form one contiguous prefix. A later `import-declaration` cannot occur
after a `top-level-item`.

Top-level `let` and `var` declarations are not productions. A module constant
uses the dedicated form above rather than the local variable-declaration
production. The grammar has no namespace-declaration block.

### 3.1 Imports

```ebnf
import-declaration = module-import-declaration
                   | cpp-header-import-declaration;

module-import-declaration = "import", module-reference,
                            using-clause, ";";

cpp-header-import-declaration = "import",
                                ( CPP_ANGLE_HEADER_NAME
                                | CPP_QUOTE_HEADER_NAME ),
                                ";";

module-reference = module-path
                 | ".", module-path
                 | IDENTIFIER, "::", module-path;

module-path = IDENTIFIER, { ".", IDENTIFIER };

using-clause = "using", import-selection;

import-selection = IDENTIFIER | "*" | using-list;

using-list = "{", IDENTIFIER, { ",", IDENTIFIER }, [ "," ], "}";
```

The semicolon terminates the complete import declaration. A closing brace ends
only the nested `using-list`.

Accepted shapes include:

```carven
import math using answer;
import .math using *;
import json::parser using parse;
import geometry.vector using { Point, length, };
import <cstdint>;
import "native/provider.hpp";
```

Quoted strings, `/`, and `..` are not module-reference productions. `craft` is
an ordinary identifier. The grammar preserves the three token-distinct module
reference forms and the two header-name forms but does not assign resolution,
filesystem, or header-search behavior to them.

### 3.2 Enumerations

```ebnf
enum-declaration = "enum", IDENTIFIER,
                   [ ":", type ],
                   "{", enum-case-list, "}";

enum-case-list = enum-case, { ",", enum-case }, [ "," ];

enum-case = IDENTIFIER, [ enum-payload | "=", expression ];

enum-payload = "(", type, { ",", type }, [ "," ], ")";
```

An enum declaration has at least one case and has no trailing semicolon.
Payload syntax, underlying-type syntax, and initializer syntax are parsed
independently; the semantic specification defines which combinations are
valid.

### 3.3 Structures

```ebnf
struct-declaration = "struct", IDENTIFIER,
                     "{", [ struct-field-list ], "}";

struct-field-list = struct-field,
                    { ",", struct-field },
                    [ "," ];

struct-field = IDENTIFIER, ":", type;
```

The declaration has no trailing semicolon in Carven syntax. Struct bodies
contain fields only.

### 3.4 Functions

```ebnf
function-definition = function-head, ordinary-block;

function-head = "fn", IDENTIFIER,
                "(", [ parameter-list ], ")",
                [ "->", function-result-type ],
                [ throw-clause ];

parameter-list = parameter, { ",", parameter }, [ "," ];

parameter = [ access-marker ], binding-target, [ ":", type ];

access-marker = "&" | "&&";

function-result-type = type;

throw-clause = "throw", named-type, { "+", named-type };
```

Function definitions are top-level items. `import(cpp)` uses the same function
head followed by `;`; `export(cpp)` uses it followed by an ordinary block.
`throw` introduces the callable's failure contract after the success result.
`throws` is an ordinary identifier. Nested functions, default arguments,
variadic parameters, and explicit generic parameter lists have no syntax.

### 3.5 Module Constants

The `module-constant-declaration` production is defined with the other
top-level items in section 3. It always has a named target, an initializer, and
a terminating semicolon. The spelling `_` is not a declaration name at module
scope. `private` and `export` may prefix the declaration through the common
`module-item` rule.

### 3.6 Tests

```ebnf
test-declaration = "test", STRING_LITERAL, test-block;

test-block = "{", { statement }, "}";

test-operation-statement = test-operation-name,
                           "(", [ argument-list ], ")", ";";

test-operation-name = "check" | "require" | "fail";
```

A test declaration is a top-level item and cannot follow `export`. It has no
parameter or result syntax. Its string literal and the treatment of tests in a
compilation are language and compiler concerns rather than grammar rules.

`check`, `require`, and `fail` remain `IDENTIFIER` tokens rather than reserved
keywords. At a statement boundary with test context enabled, the exact form
above is a `test-operation-statement`. The argument list uses the ordinary
expression and trailing-comma rules. Argument count is a semantic constraint.

A same-spelled form that is not the complete semicolon-terminated statement
above is parsed through the ordinary expression grammar. Inside a test context,
the complete statement form is contextual syntax even if an ordinary callable
with the same spelling is visible.

## 4. Types

```ebnf
type = named-type | array-type | function-type;

named-type = qualified-type-name;

qualified-type-name = type-name-component,
                      { "::", type-name-component };

type-name-component = IDENTIFIER;

qualified-name = IDENTIFIER, { "::", IDENTIFIER };

array-type = "[", type, ";", expression, "]";

function-type = "fn",
                "(", [ function-type-parameter-list ], ")",
                "->", function-result-type,
                [ throw-clause ];

function-type-parameter-list = function-type-parameter,
                               { ",", function-type-parameter },
                               [ "," ];

function-type-parameter = [ access-marker ], type;
```

There is no initial type-alias declaration, tuple type syntax, generic
parameter declaration, or reference/pointer type syntax.

## 5. Statements and Blocks

### 5.1 Ordinary Blocks

```ebnf
ordinary-block = "{", { statement }, "}";
```

An ordinary block contains statements only and never contains an implicit result
expression. An empty ordinary block is valid.

### 5.2 Statement Categories

```ebnf
statement = variable-declaration
          | return-statement
          | throw-statement
          | rethrow-statement
          | break-statement
          | continue-statement
          | while-statement
          | for-statement
          | assignment-statement
          | update-statement
          | expression-statement
          | control-flow-statement
          | test-operation-statement
            where test context is enabled;

assignment-statement = assignment-form, ";";

update-statement = update-form, ";";

expression-statement = expression, ";";

control-flow-statement = if-form | match-form | try-form;
```

A direct unparenthesized `if-form`, `match-form`, or `try-form` at the beginning
of a statement is terminated by its own structure.

`test-operation-statement` is a contextual statement alternative. In
particular, the grammar has no standalone `{ ... }` block statement.

### 5.3 Assignment and Update Forms

```ebnf
assignment-form = expression, assignment-operator, expression;

assignment-operator = "=" | "+=" | "-=" | "*=" | "/=" | "%="
                    | "&=" | "|=" | "^=" | "<<=" | ">>=";

update-form = update-operator, prefix-expression;

update-operator = "++" | "--";
```

Assignment and update forms are actions, not expressions. They occur only in
the statement and `for` productions that explicitly admit them. Semantic
analysis determines whether the parsed target is mutable and assignable.

### 5.4 Variable Declarations

```ebnf
variable-declaration = variable-declaration-head, ";";

variable-declaration-head = binding-kind, binding-target,
                            [ ":", type ],
                            "=", expression;

binding-kind = "let" | "var" | "const";

binding-target = IDENTIFIER;
```

Every variable declaration has an initializer. The
identifier spelling `_` is a discard binding target; every other identifier,
including one beginning with `_`, is named. The same `binding-target`
classification is used by parameters and range bindings.

### 5.5 Control Transfer

```ebnf
return-statement = "return", [ expression ], ";";

throw-statement = "throw", expression, ";";

rethrow-statement = "rethrow", ";";

break-statement = "break", ";";

continue-statement = "continue", ";";
```

The grammar does not decide whether a control-transfer form is valid at its
location.

### 5.6 While Statements

```ebnf
while-statement = "while", expression, ordinary-block;
```

The loop body is always braced.

### 5.7 For Statements

```ebnf
for-statement = "for", for-header, ordinary-block;

for-header = range-for-header | c-style-for-header;

range-for-header = for-binding, "in", range-for-source;

range-for-source = expression, [ "..", expression ];

for-binding = [ "&" ], binding-target, [ ":", type ];

c-style-for-header = [ for-initializer ], ";",
                         [ expression ], ";",
                         [ for-step-list ];

for-initializer = variable-declaration-head
                | assignment-form
                | expression;

for-step-list = for-step, { ",", for-step };

for-step = assignment-form | update-form | expression;
```

The first semicolon in a C-style header terminates the optional
`for-initializer`; it is not part of `variable-declaration-head`.

`..` is one maximal-munch token. The optional suffix above is recognized only
after `in` in a `range-for-header`; ordinary expression grammar does not consume
`..`. When `..` is present, both surrounding expressions are required.
Inclusive, omitted-bound, step, implicit-reverse, and general range-value forms
are not productions.

## 6. Branch Blocks

`if-form` branches, protected `try-form` bodies, and braced match or catch arms
use `branch-block`.

```ebnf
branch-block = "{", { statement }, [ branch-result ], "}";

branch-result = expression;
```

A branch block is a structural component of `if-form`, `match-form`, or
`try-form`; it is not a general expression and cannot appear independently.

## 7. Expressions

### 7.1 Precedence and Associativity

The expression grammar is ordered from the loosest binding form to the tightest.

```ebnf
expression = access-expression | logical-or-expression;

access-expression = access-marker, expression;

logical-or-expression = logical-and-expression,
                        { "||", logical-and-expression };

logical-and-expression = bitwise-or-expression,
                         { "&&", bitwise-or-expression };

bitwise-or-expression = bitwise-xor-expression,
                        { "|", bitwise-xor-expression };

bitwise-xor-expression = bitwise-and-expression,
                         { "^", bitwise-and-expression };

bitwise-and-expression = comparison-expression,
                         { "&", comparison-expression };

comparison-expression = shift-expression,
                        [ comparison-operator, shift-expression ];

comparison-operator = "==" | "!=" | "<" | "<=" | ">" | ">=";

shift-expression = additive-expression,
                   { ( "<<" | ">>" ), additive-expression };

additive-expression = multiplicative-expression,
                      { ( "+" | "-" ), multiplicative-expression };

multiplicative-expression = cast-expression,
                            { ( "*" | "/" | "%" ), cast-expression };

cast-expression = prefix-expression,
                  { "as", type };
```

Binary operators expressed with repetition are left-associative. Comparison
operators are one non-associative level: an unparenthesized expression contains
at most one comparison operator. Cast expressions are left-associative. Prefix
and postfix operations bind more tightly than `as`; `as` binds more tightly
than multiplication, division, remainder, addition, and subtraction.

Carven has no comma expression. Commas belong only to explicit lists, separators,
or alternative productions.

### 7.2 Prefix Expressions

```ebnf
prefix-expression = prefix-operator, prefix-expression
                  | postfix-expression;

prefix-operator = "!" | "-" | "~";
```

### 7.3 Postfix Expressions

```ebnf
postfix-expression = primary-expression, { postfix-operation };

postfix-operation = call-operation
                  | index-operation
                  | member-operation
                  | propagation-operation;

call-operation = "(", [ argument-list ], ")";

argument-list = call-argument, { ",", call-argument }, [ "," ];

call-argument = expression;

index-operation = "[", expression, "]";

member-operation = ( "." | "::" ), IDENTIFIER;

propagation-operation = "?";
```

### 7.4 Primary Expressions

```ebnf
primary-expression = literal
                   | construction-expression
                   | contextual-case-expression
                   | IDENTIFIER
                   | grouped-expression
                   | array-expression
                   | lambda-expression
                   | if-form
                   | match-form
                   | try-form;

literal = NUMBER_LITERAL | STRING_LITERAL | CHAR_LITERAL
        | "true" | "false";

contextual-case-expression = ".", IDENTIFIER;

grouped-expression = "(", expression, ")";

construction-expression = construction-type,
                          "{", [ construction-initializer-list ], "}";

construction-type = named-type | function-type;

construction-initializer-list = positional-initializer-list
                              | field-initializer-list;

positional-initializer-list = expression,
                              { ",", expression },
                              [ "," ];

field-initializer-list = field-initializer,
                         { ",", field-initializer },
                         [ "," ];

field-initializer = IDENTIFIER, ":", expression;

array-expression = "[", [ array-element-list ], "]";

array-element-list = expression,
                     { ",", expression },
                     [ "," ];

lambda-expression = "[", [ capture-list ], "]",
                    "(", [ lambda-parameter-list ], ")",
                    [ "->", type ], [ throw-clause ], ordinary-block;

capture-list = capture, { ",", capture }, [ "," ];
capture = [ "&" ], IDENTIFIER;

lambda-parameter-list = parameter, { ",", parameter }, [ "," ];
```

An access expression begins only at the start of an `expression` production.
Its marker therefore covers the complete expression to its right. Infix `&`
and `&&` remain bitwise-and and logical-and at their existing precedence
levels. Calls add no argument-specific marker syntax: a marked argument is an
ordinary access expression. Captures and range bindings admit only their
separate optional `&` marker, not `&&`.

Parentheses group an expression and do not introduce a block. The grammar
provides no C-style cast syntax or parenthesized object-construction alternative.
`.Case` is a contextual-case expression; `.Case(arguments)` is that expression
followed by an ordinary call operation. The semantic specification determines
whether the selected case is a value or callable and whether contextual type
information is sufficient.

## 8. Conditional Forms

```ebnf
if-form = "if", expression, branch-block,
          { "else", "if", expression, branch-block },
          [ "else", branch-block ];
```

The same syntactic form may occur wherever an expression is admitted and at the
beginning of a statement. Whether it requires branch results is a language
semantic rule, not a grammar alternative.

## 9. Try and Match Forms

A `try` form uses the following typed catch shell:

```ebnf
try-form = "try", branch-block, "catch", "{", [ catch-arm-list ], "}";
catch-arm-list = catch-arm, { ",", catch-arm }, [ "," ];
catch-arm = catch-pattern, [ "if", expression ], "=>", match-arm-body;
catch-pattern = catch-atom, { "|", catch-atom };
catch-atom = "_" | named-type, "(", pattern, ")";
```

### 9.1 Match Form and Patterns

```ebnf
match-form = "match", expression,
             "{", [ match-arm-list ], "}";

match-arm-list = match-arm, { ",", match-arm }, [ "," ];

match-arm = pattern, [ "if", expression ], "=>", match-arm-body;

pattern = or-pattern;

or-pattern = atomic-pattern, { "|", atomic-pattern };

atomic-pattern = wildcard-pattern
               | literal-pattern
               | negative-number-pattern
               | binding-pattern
               | constraint-pattern
               | case-pattern;

wildcard-pattern = "_";

literal-pattern = NUMBER_LITERAL | STRING_LITERAL | CHAR_LITERAL
                | "true" | "false";

negative-number-pattern = "-", NUMBER_LITERAL;

binding-pattern = IDENTIFIER;

case-pattern = case-name, [ "(", [ pattern-list ], ")" ];

case-name = ".", IDENTIFIER
          | qualified-name, "::", IDENTIFIER;

pattern-list = pattern, { ",", pattern }, [ "," ];

constraint-pattern = "is", constraint-operand;

constraint-operand = qualified-name | array-type;
```

Patterns are a syntactic category independent from `expression`. A parser
constructs pattern nodes rather than parsing patterns as expressions with later
flags. Guard evaluation and pattern-binding meaning belong to the language
specification.

### 9.2 Match Arm Bodies

```ebnf
match-arm-body = match-expression-arm
               | control-transfer-form
               | branch-block;

control-transfer-form = return-form | throw-form | "rethrow"
                      | "break" | "continue";

return-form = "return", [ expression ];

throw-form = "throw", expression;

match-expression-arm = expression;
```

The comma separating arms is not part of an arm body. A concise control-transfer
arm has no semicolon. Declarations, loops, and multiple statements require a
`branch-block`.

## 10. Syntactic Disambiguation

The following rules resolve grammar ambiguities without name lookup or type
lookup.

### 10.1 Statement and Expression Boundaries

- At the beginning of a statement, an unparenthesized `if`, `match`, or `try`
  starts a complete `control-flow-statement`.
- An expression statement is terminated by `;`; its value is not part of the
  enclosing syntax.
- An ordinary expression immediately followed by the closing brace of a
  `branch-block` is a `branch-result` candidate.
- A trailing `if-form`, `match-form`, or `try-form` without `;` occupies the role
  required by its branch position.
- In test context, an exact `check(...) ;`, `require(...) ;`, or `fail(...) ;`
  at a statement boundary is a test operation before ordinary expression-
  statement parsing is considered. Source lambda bodies clear that context.

### 10.2 Branch Parsing

The parser first recognizes statement forms in a branch block. An ordinary
expression followed by `;` is a statement. An ordinary expression directly
followed by the branch closing brace is the optional branch result. A nested
`if-form`, `match-form`, or `try-form` inherits the syntactic role of the branch
position in which it appears.

### 10.3 Lists and Separators

Every explicitly delimited comma-separated list admits at most one optional
trailing comma when its production says `[ "," ]`. A trailing comma never
creates an empty element. A comma not owned by such a production is invalid.

### 10.4 Patterns

The keyword `is` always starts a constraint pattern. A bare identifier is parsed
as `binding-pattern` without consulting a symbol table. Enum case-pattern syntax
uses `.Case` or `Owner::Case`, optionally followed by a parenthesized pattern
list; semantic analysis validates the selected case and its payload arity. The
`|` tokens between
`atomic-pattern` nodes belong to the pattern grammar; bitwise `|` belongs to the
expression grammar outside a pattern.

### 10.5 Construction and Calls

`T { ... }` is the only source form introduced by `construction-expression`.
Parentheses following a parsed expression always start a call operation. A
parser does not classify a name as a type to reinterpret `T(...)` as a
construction. The production accepts a general construction type; semantic
analysis requires it to denote a structure and the initializer to cover every
field.

In a control-flow header whose expression is followed immediately by a required
body, the `{` at the header's outer delimiter depth always begins that body. A
construction expression at that depth must therefore be grouped to remain part
of the header expression. This applies equally to named and function
construction types. As syntax-only examples, `while ready {}` has the name
expression `ready` and an empty body, while `while (Flag {}) {}` and
`while (fn() -> bool { predicate }) {}` use grouped construction expressions as
their conditions; semantic validity is separate. The same boundary rule
applies to `if` and `match` headers, the container iterable and each bound in a
range-for source, and the final step position of a C-style `for` header.
