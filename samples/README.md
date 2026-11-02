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
| `13-guess.bas` | a guessing game: the machine halves the range and wins in ten |
| `14-dice.bas` | two dice, a hundred throws and a histogram of the totals |
| `15-life.bas` | Conway's Life on a six-by-six board, two generations of it |
| `16-units.bas` | `DEF FN` for a conversion table |
| `17-interest.bas` | compound interest, a year to a line |
| `18-invoice.bas` | `READ`/`DATA` into an invoice, with VAT, to the penny |
| `19-quadratic.bas` | the discriminant, real roots, and the case with none |
| `20-stats.bas` | mean and standard deviation of a set of readings |
| `21-hangman.bas` | `MID$` and string building: the machine plays both parts |
| `22-roman.bas` | Roman numerals from a table of values |

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
| `12-binary.bas` | decimal to binary and to hex, by division and by remainder |
| `13-money.bas` | an invoice in whole pence, with VAT, since the arithmetic is integer |
| `14-gcd.bas` | Euclid's algorithm, and the least common multiple from it |
| `15-histogram.bas` | sixty rolls of a die from the noise register, as a histogram |
| `16-pattern.bas` | `POKE` into the tube and its colours: a pattern drawn cell by cell |
| `17-guess.bas` | the halving game in integer arithmetic |
| `18-cipher.bas` | a Caesar cipher, three shifts of the same message |
| `19-report.bas` | a regional sales report: totals, percentages and a bar for each |
| `20-board.bas` | a noughts-and-crosses board drawn from an array, and who won |
