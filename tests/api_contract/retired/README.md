# Retired-API contract

Each file here references exactly one symbol that has been deliberately removed
from a public header, and **must fail to compile**. `run_p4_host_tests.ps1`
compiles every file in this directory with `-fsyntax-only
-Werror=implicit-function-declaration` and fails the run if any of them
*succeeds*.

This replaces `Assert-FileDoesNotContain` on the header. Grepping for a name
also matches it in a comment, and passes if the declaration is reformatted or
renamed rather than removed; asking the compiler answers the real question —
can a caller still reach this symbol?

One symbol per file, named after it, so a failure names the API that came back.
