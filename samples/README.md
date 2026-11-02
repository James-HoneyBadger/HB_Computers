# Samples

Programs to type, run and read. Every one of them is exercised by
`kestrel_samples_tests`, which types each file at the machine it was written for
and compares the screen with the `.expected` file beside it — so a sample here
runs or the suite fails.

A `.expected` file holds the lines that must appear on the screen. A line that
begins with `~` only has to appear somewhere in the screen text, which is for
output that shares a row with the prompt.

## Running one

```bash
# The 1982 machine: the lines are stored and RUN
./build/kestrel --model kestrel --file samples/kestrel/02-sieve.bas --type RUN

# The 1992 machine: the lines are direct statements, so there is nothing to RUN
./build/kestrel --model kestrel16 --file samples/kestrel16/03-division.bas
```

Or type them in at the window, or from `./start.sh cli`.

## Kestrel (1982), Kestrel BASIC V2.0

| File | What it shows |
| --- | --- |
| `01-hello.bas` | `PRINT`, strings, a small `FOR` |
| `02-sieve.bas` | arrays, and a `FOR` whose start is past its limit |
| `03-fibonacci.bas` | variables in a loop, and a trailing `;` |
| `04-factorial.bas` | accumulating a product across ten iterations |
| `05-tables.bas` | nested loops and the `,` separator |
| `06-average.bas` | `READ`/`DATA` with a sentinel that ends the data |
| `07-strings.bas` | `LEFT$`, `RIGHT$`, `MID$`, `LEN`, joining and comparing |
| `08-functions.bas` | `DEF FN` defined in the program, as it must be |
| `09-subroutines.bas` | `GOSUB` and `RETURN` |
| `10-random.bas` | `RND` and `INT` from the noise generator |
| `11-pattern.bas` | `POKE` into the tube at `$8000` and its colours at `$8400` |
| `12-sound.bas` | the sound latch at `$8808`, which the window answers with a beep |

## Kestrel 16 (1992), Kestrel 16 BASIC 2.0

Some are direct statements, typed one at a time; the control-flow samples number
their lines and end with `RUN`, so each file can be typed at the machine as it
stands.

| File | What it shows |
| --- | --- |
| `01-hello.bas` | `PRINT` with strings, `;`, and a multiplication |
| `02-arithmetic.bas` | sixteen-bit range and operator precedence |
| `03-division.bas` | signed division, rounding towards zero |
| `04-variables.bas` | `LET` and the `A`–`Z` variables |
| `05-table.bas` | a table, one row to a line |
| `06-limits.bas` | what the language does not do yet, said plainly |
| `07-branches-and-loops.bas` | `IF`/`THEN`, `GOSUB`/`RETURN` and `FOR`/`NEXT` |
| `08-for-step.bas` | positive and negative `STEP`, a loop past its limit, `STEP 0` rejected |
| `09-strings.bas` | `A$` variables, `LEN`, comparing, joining and slicing |
| `10-arrays.bas` | `DIM`, a sieve, two loops one inside the other, and an array of strings |
| `11-functions.bas` | `DEF FN` of one argument, and the letter it borrows given back |
