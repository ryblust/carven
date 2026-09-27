# Text and interpolation

[Language](README.md)

This page defines Unicode values, owning text, borrowing, and interpolation.
Direct output and structural display are defined in [Printing](execution.md#printing).

- [Unicode text](#unicode-text)
- [String literals and multiline layout](#string-literals-and-multiline-layout)
- [Unchecked text construction](#unchecked-text-construction)
- [Owning String and text borrowing](#owning-string-and-text-borrowing)
- [String interpolation](#string-interpolation)

## Unicode text

`char` is one immutable, copyable Unicode scalar value. It supports equality,
inequality, and matching, but not numeric arithmetic, ordering, truthiness, or
implicit numeric conversion. The explicit conversion `character as u32` yields
its Unicode scalar number. The UTF library provides checked integer-to-char
conversion.

`str` is an immutable, copyable, value-passed UTF-8 view represented by a pointer
and byte length. Length defines its contents, including internal NUL bytes;
the view does not promise a trailing NUL. It has no owning
storage, `&str` type, or source lifetime syntax. Its backing can be static
literal storage, a checked borrow of a `String`, or externally supplied storage. Copying a view preserves its known backing relationship.

Decoded string and character literal values are not Unicode-normalized.

Both `str` and `String` provide:

- `text.len() -> usize` returns the UTF-8 byte count.
- `text.is_empty() -> bool` tests that byte count.
- `text.bytes` returns the read-only slice `[u8]`.
- `text.chars` is a copyable read-only range that decodes `char` values.

`bytes` and `chars` are computed projections, not general properties.
The `chars` view type cannot be spelled and supports only Read range iteration
and inferred value bindings. Byte views support all slice operations. User structures may declare same-named fields because member
resolution depends on the receiver type.


## String literals and multiline layout

| Kind | Single-line | Multiline | Escapes | Interpolation |
| --- | --- | --- | --- | --- |
| Ordinary | `"..."` | `"""..."""` | Yes | No |
| Raw | `r"..."` | `r"""..."""` | No | No |
| Interpolated | `f"..."` | `f"""..."""` | Yes | Yes |

Ordinary and raw literals produce `str` with static storage. Multiline
interpolation follows the same type, ownership, and evaluation rules as
single-line interpolation. All text must be valid UTF-8; no Unicode normalization
is performed. `c"..."` is single-line and NUL-free, with ordinary escapes.
Prefixes are adjacent to their delimiters. `r` and `f` are ordinary identifiers
elsewhere. Prefix combinations such as `fr`, `rf`, and
`cr` are not string forms.

Raw strings preserve backslashes and braces literally. Matching `#` characters
extend their delimiters to distinguish the closing boundary from body text:

```carven
let path = r"C:\tools\bin\";
let json = r#"{"name": "{name}"}"#;
let example = r#"""
    A literal """ sequence.
    Backslashes such as \n remain text.
"""#;
```

The first complete closing sequence ends the literal: one or three quotes,
followed by the number of `#` characters in the opening delimiter. Extra `#`
characters after that sequence are outside the literal.

Single-line text cannot contain physical line endings. Multiline opening
markers must be immediately followed by LF or CRLF. A closing marker must be
on a separate line, preceded only by spaces or tabs; normal code such as `;`,
`,`, or `)` may follow it. The closing line's indentation does not determine
the string value.

```carven
let text = """
    Hello
      Carven
    World
""";
// Equivalent to "Hello\n  Carven\nWorld".
```

Multiline layout uses these rules:

1. Exclude the opening line ending and the closing line's indentation. Exclude
   the line ending immediately preceding the closing line, if distinct from
   the opening line ending. Additional blank body lines remain content.
2. Find the longest common space/tab prefix of nonblank body lines. Match
   characters, not display columns. Blank lines do not determine this prefix.
3. Remove that prefix from nonblank lines. On blank lines, remove only the
   leading characters matching the prefix, stopping at its end, the line's end,
   or the first mismatch. Preserve remaining spaces, tabs, and trailing spaces.
   A nonblank line with no indentation makes the common prefix empty.
4. If every body line is blank, remove all spaces and tabs on those lines while
   retaining their separating line endings. Zero or one blank body line gives
   an empty string; two blank body lines give one LF.
5. Normalize physical LF and CRLF to LF. A standalone CR in literal text is
   invalid. Then interpret escapes and interpolation; their produced whitespace
   is never treated as source indentation.

In multiline interpolation, each hole counts as one nonblank content unit.
The hole's code, including nested strings, comments, format specifications, and
physical line endings, does not participate in the surrounding text's layout.
Text after the hole continues the same logical body line until a text line
ending. Format specifications follow the escape and newline rules in
[Grammar](grammar.md#24-character-and-string-literals). Inserted values retain
their own whitespace; no indentation is removed or added.

Multiline layout applies equally to ordinary, raw, and interpolated strings.
Escapes such as `\t` and `\u{20}` express content indentation independently of
source indentation in ordinary and interpolated strings. Backslash line
continuation and implicit adjacent-literal concatenation are not supported.

## Unchecked text construction

`char::from_u32_unchecked(value: u32) -> char` requires a Unicode scalar value:
at most U+10FFFF, excluding U+D800 through U+DFFF.
`str::from_utf8_unchecked(bytes: [u8]) -> str` requires valid UTF-8 and borrows
the input storage without copying or allocating. Both are builtin factories
called directly through their type names; no runtime header import is needed.

These operations do not validate content. The caller must satisfy their content
preconditions; violating them is outside the language's valid text contract.
Carven checks argument types and tracks the returned text's backing through
ordinary borrow analysis. Construction does not extend the backing lifetime.
Use the UTF standard library's checked functions for input that has not been
validated. Validation algorithms and their error types belong to that library.

## Owning String and text borrowing

`String` is a builtin owning UTF-8 value, available without imports and distinct
from `str`, user declarations, and C++ types. Unqualified `String` in type
position or a factory qualifier selects the builtin before ordinary declarations.
Ordinary value lookup is unchanged; `::String` selects an external C++ name.

A String owns contiguous, valid UTF-8 bytes, including internal NUL. It performs
no normalization, case folding, or BOM removal. Equality and inequality between
two Strings compare their bytes. Length counts bytes. Storage layout, capacity,
address stability, trailing NUL, and allocation count are unspecified.

| Operation | Access and result |
| --- | --- |
| `String {}` | Empty owning `String` |
| `String::from_str(text: str)` | Read text; independent owning copy |
| `s.len()` / `s.is_empty()` | Read receiver; `usize` / `bool` |
| `s.as_str()` | Read receiver; borrowed `str` |
| `s.bytes` / `s.chars` | Read receiver; borrowed byte/scalar range |
| `s.append(text: str)` | Write receiver, Read text; `void` |
| `s.append_format(f"...")` | Write receiver, Read interpolation holes; `void` |
| `s.push(value: char)` | Write receiver, Read scalar; `void` |
| `s.clear()` | Write receiver; `void` |

The queries `len`, `is_empty`, and `as_str` are O(1); `as_str` does not allocate
or transcode. `push` encodes one Unicode scalar into UTF-8. Mutable fields and
array elements can be Write receivers; temporaries and Read parameters cannot.
The dot receiver supplies its access, while ordinary arguments follow the usual
explicit-marker rules. Factories and methods require direct calls, including
grouped direct calls; they do not produce first-class method values.

String literals default to `str`. In a `String` context, a literal constructs an
owning value; this applies to annotated bindings, assignments, arguments, returns,
and aggregate initializers. Grouping preserves that context. An existing `str`
value requires `text as String` or `String::from_str(text)` to create an independent
owning copy.

A `String` value in a `str` destination context creates a borrowed view with the
same lifetime and ownership constraints as `.as_str()`. Write arguments still
require matching storage types. Without a `str` context, `let copy = owner`
retains String value semantics; `let view = owner.as_str()` explicitly creates a
view. Borrowing neither allocates nor extends the owner's lifetime.

Equality requires operands of the same type; a string literal on the right can
adopt a String left operand's type through ordinary operand context. Constant
expressions and functions can construct owning String values, but a constant initializer freezes
an owning text result to `str`; there is no stored owning String constant.
String literal patterns, indexing, ordering, truthiness, concatenation,
direct iteration, and access to C++ container members are unsupported.
Nonempty `String { ... }`, `String(...)`, and `String as str` are invalid.
Iterate `s.bytes` or `s.chars`.

Copying String creates independently owned content. Whole-owner Take transfers
the value and makes its source unavailable. Read String parameters alias the
caller's storage. Fields, elements, captures, results, and cleanup use ordinary
Carven value rules. Borrowing does not extend a String owner's lifetime.

A borrowed view keeps referring to its original storage when copied or passed
through aggregates, captures, functions, Write outputs, branches, loops, and
failure payloads. Owning copies made by `as String` or `from_str` and extracted `u8`/`char`
values are independent of the source. A named value holding a view keeps the
borrow until that value is replaced, taken, or leaves scope; its last use does
not end the borrow.

While a view borrows a String, that String cannot be modified, replaced, or
taken, including by taking a containing owner. The check distinguishes fields
and elements; an unknown index may overlap any possible element. Write access
is nonexclusive: creating a Write alias or capture is allowed, but actual
writes through it must respect live borrows. Native Write access counts as a
possible write. This also applies to `append` and `clear` when contents would
not change. Self-append `s.append(s.as_str())` is rejected; copy to an independent
owner first.

Temporary views keep their backing borrowed until the consuming operation
finishes, including evaluation of its remaining operands. An independent result
ends that borrow when no other view remains. For example:

```carven
var text: String = "hello";
text = text.as_str() as String;
let snapshot = text;
text.append(snapshot);
```

The RHS copy finishes before assignment writes. Similarly,
`f(String::from_str(s.as_str()), &&s)` can be valid, while
`f(s.as_str(), &&s)` is rejected. Each receiver/operand is evaluated once in
source order, with the receiver or assignment target selected first. A selected
Read receiver observes its content when the operation executes.

A view may be returned from a Read String parameter when the caller backing
outlives the returned view. Views into callee-local Strings or Take parameters
cannot escape. Saving `String::from_str("x").as_str()` into a named view is
invalid; immediate consumption is valid. A range loop retains String owners
created while evaluating its header, including those passed through view-returning
functions; `for c in String::from_str("x").chars` is valid. The borrow lasts
through the loop; retained temporaries are destroyed on every loop exit.

An aggregate can contain both String and str fields, but cannot store a view
into its own String storage. Copying a String field creates independent content;
copying a view field keeps referring to its original storage.
A view may borrow an owning String stored in a live closure.

Nominal failure values can contain Strings under the ordinary copyable failure
contract; builtin String itself is not a failure type. Throw copies unless Take
is explicit. Borrowed text must remain valid through throw, propagation, catch
selection and guards, and rethrow. The original failure payload retains its
borrows independently of copied catch bindings. Unwinding must not destroy
their backing. A handler can copy borrowed content into an independent String result.
Completed effects are retained on failure; mutation is not rolled back.

Builtin String construction accepts valid text and scalars. Allocation failure
and unrepresentable lengths terminate.


## String interpolation

An `f"..."` or `f"""..."""` expression produces an independent owning `String`, including `f""`
and text without holes. `String.append_format` consumes this syntax directly as
formatting content, as described below. Ordinary string literals remain `str`.

```carven
f"Hello, {name}!"
f"Total: {price * count:.2f}"
f"ID: {id:08x}"
f"{{value}} = {value}"
f"{value:{width}.{precision}f}"
```

A hole contains an ordinary expression and an optional `:` format specification.
Formatting follows `std::format` rules for escaped braces, alignment, width,
precision, and type options. Dynamic width and precision holes contain Carven
expressions. Carven may precompute supported builtin formatting for all or part
of an ordinary interpolation or directly perform supported integer conversions,
while preserving these formatting rules, owning results, and required operand
evaluation. Other format checks
and formatter availability are delegated to the C++ compiler. Custom C++
formatters receive their format specifications through the same mechanism.

Hole expressions, including dynamic format arguments, are evaluated once in
source order before formatting. Each hole has Read access. Direct `&` and `&&`
argument markers are rejected; nested calls retain their declared access rules.
Scalar Read values are saved, while String Read values alias their owners.
String formatting reads the contents after all holes finish evaluating. Text
views keep their backing borrowed throughout hole evaluation and formatting.
The result is independent of its inputs; named views keep their usual lifetimes.

`String` uses standard string formatting. A `char` is encoded as UTF-8 and also
uses standard string formatting, including width and precision. Integer formatting
follows the corresponding standard integer formatter's rules. Other values use
their C++ representation and corresponding `std::formatter`; Carven supplies no
aggregate, enum, or callable formatting protocol.

Interpolation supports ordinary expression composition and failure propagation.
An early failure skips later holes and formatting, retaining completed effects
and destroying temporary values. Discarding the result still executes formatting.
Direct interpolation and interpolation in functions executed at compile time use the same
[supported builtin formatting subset](constants.md#compile-time-function-execution)
in required constant contexts. For example,
`const title = f"build-{42:04}";` produces static `str` text, and
`const bytes = f"{'我'}".len();` produces `3usize`. Nested calls, `String {}`,
`String::from_str(...)`, `.as_str()`, `.len()` and `.is_empty()` compose before
initializer completion. Each constant interpolation is limited to 1 MiB of
result text; unsupported formatting is diagnosed without runtime fallback.
Interpolation remains excluded from literal patterns and positions requiring
an ordinary literal. Ordinary runtime interpolation retains String ownership.

`destination.append_format(f"...")` appends formatted content to an existing
writable String and returns `void`. It requires exactly one direct interpolation;
grouping is allowed, but a String value, plain literal, or argument access marker
is not accepted. The method does not construct an intermediate source-level
String from the interpolation.

The receiver is selected once before the holes. All holes then follow the Read
evaluation and observation rules above, before appending begins. Formatting inputs
must not borrow or alias the destination's storage during formatting; this includes
String Read aliases. Existing views also prevent mutation, even when the content
is known or empty. Scalar queries such as `text.append_format(f"{text.len()}")`
can supply a snapshot without borrowing text storage. Native callers and formatter
providers must preserve this separation through indirect aliases and reentrant calls.

A failing receiver or hole skips the append and later operands, preserving completed
effects, including mutations performed by earlier holes. Once formatting begins,
there is no rollback guarantee on termination. The method is also available in
compile-time execution under the same formatting subset and execution budgets.

Formatting preserves internal NUL and produces valid UTF-8 on normal completion.
The compiler may omit the final UTF-8 scan when operand types and supported static
specifications prove the output valid. Other outputs retain runtime validation;
this implementation choice does not change the source operation's contract.
Invalid UTF-8, allocation failure, and runtime formatting errors terminate;
they do not introduce typed failures. C++ formatter retention and reentry remain
provider/caller responsibilities. Callable-view and Write-capture boundary
restrictions still apply.
