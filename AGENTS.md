<!-- SPDX-License-Identifier: MIT-0 -->

## Building and testing

The project supports build directories. It is recommended to build and test
with the dev profile in the dev build directory. Pass the build
directory to `make` explicitly.

```sh
./configure --profile=dev
make -C build/dev test
```

## General coding style

Your code should be well-structured, easy to read, maintainable, and
well-commented. Comments should explain why we are doing something, and
sometimes what we are doing if it's not obvious (but prefer clear code that
doesn't need comments to explain what it's doing). For large enough units, like
functions, structures, and files, there must be comments describing their
purpose and behavior.

Tests should also be commented, explaining what we are testing (and maybe why)
and what the expected behavior is.

Your comments should be clear and grammatically correct. Avoid using the word
"one" instead of an article "the/a/an", i.e. instead of "print one error
message" prefer "print an error message".

If the logic is complex, comments are mandatory.

## C Style And Coding Recommendations

- Use `CamelCase` for type names.
- Prefer `typedef struct TypeName { ... } TypeName;` idiom (same for enums).
- Add function descriptions for non-trivial helpers.
- For functions returning allocated memory, explicitly document ownership and
  who frees.
- Add brief body comments for non-obvious logic, not for obvious statements.
- Avoid helper APIs that hide ownership transfer in risky ways; inline simple
  ownership-sensitive paths when clearer.
- If you allocate resources in a function that has multiple return paths, use a
  single exit point to ensure proper cleanup (i.e. goto cleanup).
- Declare loop variables in the `for` header when practical.
- Use `int` for `argc`-indexed loops; use `size_t` for collection-length loops.
- Drop {} around one-line single-statement bodies. However, if an if-statement
  has an else branch that requires {}, keep {} on both branches for consistency.
- Declare variables as close as possible to their first use, unless they
  participate in cleaning up (goto cleanup idiom), in which case declare them at
  the top of the function and initialize them (likely to NULL) right away.
- When there are too many arguments being passed to a function, and the passed
  values are not descriptive enough, annotate the arguments with comments in the
  call site, e.g. `foo(arg1, arg2, /*buffer=*/arg3);`. If the values are
  descriptive enough, this is not necessary, e.g.
  `foo(num_items, item_size, buffer);`.
- When doing CLI parsing, always use the option parsing utilities from
  `src/util/options.h`.

### Documenting function parameters

- All output parameters should be documented. Prefer grouping purely output
  parameters together at the end of the parameter list.
- If a function has more than 3 parameters, all of them must be explained in the
  function description, and every one of them deserves a separate line in the
  description.
- An exception to the above rules is when parameters are really obvious. For
  example, when it is a parsing function, its signature is standard `(void *,
  const char *, size_t, String *)`, so there is no need explaining it each time.

### Strings

For owned dynamically allocated strings, use the `String` type from
`util/string.h`.
- Use `String` when returning new strings from functions and storing owned
  strings in structs.
- When passing strings to functions, prefer `const char *` (C strings).
- If you need to modify a string (including output parameters), use `String *`.
- If you use a string as an output parameter `String *out`, document it, and
  call `str_free(*out)` before assigning to it, to ensure the caller doesn't
  leak memory.
- Avoid passing `String` by value, unless you want to transfer ownership to the
  callee, in which case document it explicitly and assign an invalid value to
  the `String` in the caller after the call (e.g. `str = {NULL};`).

## Shell scripting recommendations

- Never use bash, only sh. Avoid non-portable feature.
- Avoid saving the output to a variable just to print it a few lines later. Just
  pass through the output whenever possible. Same with exit codes.
  Bad:
  ```sh
  output=$(some_command)
  exit_code=$?
  printf "%s" "$output"
  printf "Exit code: %d\n" "$exit_code"
  ```
  Good:
  ```sh
  some_command
  printf "Exit code: %d\n" "$?"
  ```

- In shell functions, prefix argument variables with `arg_` and local variables
  with `local_`; reserve unprefixed names for global state. Use uppercase names
  only for exported variables.
- Don't forget to surround variable expansions with double quotes.

## Testing

Prefer shell run-and-check tests for testing CLI. C tests should be used for
unit testing C code.

Every test should have a clear description explaining what we are testing and
why.

## Addressing review comments

If the user asks you to address review comments, search for the comments
containing the `REVIEW:` marker in the files you are working on.

IMPORTANT: You don't have access to github, never try to get "review comments"
via gh.

IMPORTANT: NEVER remove REVIEW comments without addressing them. If it is in the
way and you can't address it, move it a little bit, but don't delete it.

## Don't assume the code changes for no reason

Sometimes the user will modify or delete code that you have written. Usually
it's for a good reason, so don't try to restore it, unless you are absolutely
certain that this was by mistake.
