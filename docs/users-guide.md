# User Guide

How to use the software these two machines run: Kestrel BASIC on the 1982
machine, the ROMs its socket takes, Kestrel DOS on the disk, and Kestrel 16
BASIC on the 1992 machine. It assumes [the instruction manual](instruction-manual.md)
has already got a prompt on the screen.

Everything here can be typed at a prompt and checked. Where a table is the
machine's own, it comes from the ROM: `HELP` prints the command list on the
Kestrel, and the error names are the ones the ROM holds.

---

## Contents

- [Kestrel BASIC: the shape of a line](#kestrel-basic-the-shape-of-a-line)
- [Statements](#statements)
- [Expressions](#expressions)
- [Functions](#functions)
- [Variables and arrays](#variables-and-arrays)
- [Errors](#errors)
- [The monitor ROM](#the-monitor-rom)
- [The other ROMs in the socket](#the-other-roms-in-the-socket)
- [Cartridges](#cartridges)
- [Kestrel DOS](#kestrel-dos)
- [Kestrel 16 BASIC](#kestrel-16-basic)
- [Worked examples](#worked-examples)
- [The samples](#the-samples)

---

## Kestrel BASIC: the shape of a line

A line that begins with a number is stored in the program. A line that does not
is run at once.

```basic
10 PRINT "STORED"
PRINT "AT ONCE"
```

Statements are separated by `:`, in a program or at the prompt:

```basic
PRINT 1:PRINT 2
```

Keywords are case-blind — `print` is `PRINT` — and a program lists back the way
it was typed. A string is anything between two quotes, and a `:` inside a string
is a colon, not a statement separator.

`RUN` starts the program, `LIST` prints it, `LIST 20` prints from line 20, `NEW`
throws it away, and `DELETE 20` or `DELETE 10,30` removes lines.

A line that starts with a number is stored and nothing is printed: the prompt
marks a command that has finished, and a line of program is not a command. A
blank line is nothing at all; the machine just prints its prompt again.

---

## Statements

### Output and input

| Statement | Notes |
| --- | --- |
| `PRINT item;item,item` | A `;` leaves the cursor where it is, so the next thing printed follows on. A `,` moves to the next column. A trailing `;` leaves the line open, which is why `PRINT 5;` shows `5READY.` |
| `PRINT` | On its own, moves to a new line |
| `?` | Shorthand for `PRINT` |
| `INPUT A` | Asks with a `?`, then waits for a line and stores what was typed. A number that will not read asks again rather than stopping the program; `INPUT A$` takes the whole line as text, quotes and all being just characters |
| `GET A$` | One key, without waiting for Return |

Strings print as they are; numbers print in their shortest exact form, and a
number too large or too small for that prints in `E` notation (`1E+10`,
`9.99999988E+29`).

### Variables and data

| Statement | Notes |
| --- | --- |
| `LET A=41` | or just `A=41` |
| `DIM A(20)` | one dimension; `DIM M(5,5)`, and up to four dimensions |
| `READ X`,`DATA 1,2,3` | the data is part of the program and is read in order |
| `RESTORE` | points `READ` back at the first `DATA` |

A variable that has never been used is zero; a string variable is empty.

### Control flow

| Statement | Notes |
| --- | --- |
| `IF cond THEN statement` | `THEN` is required. `ELSE` takes the other branch |
| `IF cond THEN 100` | a line number after `THEN` jumps there |
| `FOR I=1 TO 10 STEP 2` … `NEXT I` | `STEP` may be negative; `STEP 0` loops for ever, as it did then |
| `GOTO 100` / `GOSUB 100` / `RETURN` | seven `FOR` frames and sixteen `GOSUB` frames |
| `ON 2 GOTO 100,200` | the index chooses which line; a zero index falls through |
| `ON 1 GOSUB 100,200` | the same, and `RETURN` comes back to the next statement |
| `END` | finishes the program |
| `STOP` | stops and says where, so `CONT` can carry on |
| `CONT` | carries on after `STOP` or BREAK, unless the program was edited |

A `FOR` whose start is already past its limit runs its body no times and leaves
the variable holding the start. That is what makes

```basic
FOR J=I*I TO 100 STEP I:P(J)=1:NEXT J
```

safe when `I` has grown past the end of the array: the body never runs, so the
subscript is never used.

### The machine

| Statement | Notes |
| --- | --- |
| `POKE address,value` | writes a byte; `$8000` is the tube, `$8400` the colours, `$8800` the I/O page |
| `PEEK(address)` | reads one |
| `CLS` | clears the screen and homes the cursor |
| `HELP` | prints the command list |
| `REM anything` | a remark, to the end of the line |

Anything after a statement that is not a `:` is treated as the next statement,
and a word that is not a statement is refused:

```basic
PRINT 1 GARBAGE
1

?UNDEF'D STATEMENT ERROR
```

---

## Expressions

| Group | Operators | Notes |
| --- | --- | --- |
| Power | `^` | binds tightest |
| Sign | `-` | unary minus |
| Multiply | `*` `/` | |
| Add | `+` `-` | `+` also joins two strings |
| Compare | `=` `<>` `<` `>` `<=` `>=` | true is `-1`, false is `0` |
| Not | `NOT` | |
| And | `AND` | |
| Or | `OR` | |

`AND`, `OR` and `NOT` combine comparisons rather than bits, which is what a
1982 BASIC did:

```basic
PRINT (1<2) AND (3<2)
0
PRINT NOT (1=2)
-1
PRINT (1<2) OR (3<2)
-1
```

Strings compare with the same operators. Where one string is the beginning of
the other, the shorter sorts first:

```basic
A$="AB":PRINT A$<"ABC"
-1
```

Numbers are five-byte floating point, so `1`, `2.5`, `-3` and `1E3` all parse,
and `10/4` is `2.5` rather than `2`.

The range runs from about `2.9E-39` to `1.7E38`, and both ends say what they mean:

```basic
PRINT 1E38
9.99999986E+37
PRINT 1E39

?OVERFLOW ERROR
PRINT 1E-39
0
```

A result above the top is `?OVERFLOW`. One below the bottom is zero — there is no
number to report and none to print, so the machine prints the zero it has.

---

## Functions

| Function | Gives |
| --- | --- |
| `ABS(x)` | the size, without the sign |
| `INT(x)` | the whole part below `x` |
| `SGN(x)` | `-1`, `0` or `1` |
| `SQR(x)` | the square root |
| `RND(1)` | a number from 0 up to but not including 1, from the machine's noise generator |
| `PEEK(a)` | the byte at `a` |
| `SIN` `COS` `TAN` `ATN` `EXP` `LOG` | the usual meanings. An angle too large to reduce is `?OVERFLOW`; `EXP` of a large negative number is zero |
| `LEN(A$)` | how many characters |
| `ASC(A$)` | the code of the first character |
| `CHR$(n)` | the character with that code |
| `STR$(n)` | the number as a string |
| `VAL(A$)` | the number a string begins with |
| `LEFT$(A$,n)` | the first `n` characters |
| `RIGHT$(A$,n)` | the last `n` |
| `MID$(A$,start[,n])` | `n` characters from `start`, counting from one |

`MID$` counts from one. A start of zero or less, or a negative count, is
`?ILLEGAL QUANTITY`; a start past the end gives the empty string.

### Functions of your own

```basic
10 DEF FN S(X)=X*X+1
20 PRINT FN S(3)
RUN
10
```

A definition is kept as a pointer to the expression where it was written, so it
has to be defined **in the program**. Typed at the prompt, the text it points at
has gone by the next line.

---

## Variables and arrays

- `A` to `Z`, and `A0` to `Z9`, each a number; with a `$`, a string.
- Arrays are automatic at eleven elements: `A(0)` to `A(10)`.
- `DIM A(20)` makes twenty-one; `DIM` twice is an error worth having.
- Up to four dimensions. A fifth is `?BAD SUBSCRIPT`.

```basic
DIM A(5)
DIM A(5)
RUN

?REDIMENSIONED ARRAY ERROR IN 20
```

---

## Errors

An error names its reason and, in a program, the line it happened in:

```
?SYNTAX ERROR IN 40
```

A `STOP` is not an error but says the same way: `BREAK IN 120`.

| Reason | When |
| --- | --- |
| `SYNTAX` | the line is not something the language has |
| `NEXT WITHOUT FOR` | a `NEXT` with no loop to close |
| `RETURN WITHOUT GOSUB` | a `RETURN` with nothing to return to |
| `OUT OF DATA` | a `READ` past the last `DATA` |
| `IF WITHOUT THEN` | an `IF` with no `THEN` |
| `REDIMENSIONED ARRAY` | `DIM` on a name that has one |
| `DIVISION BY ZERO` | |
| `OUT OF MEMORY` | the program, variables and strings no longer fit |
| `UNDEF'D STATEMENT` | a jump to a line that is not there, or a word that is not a statement |
| `BAD SUBSCRIPT` | a subscript outside the array, or more than four dimensions |
| `ILLEGAL QUANTITY` | a number that cannot mean what it was asked to |
| `BAD FILE NAME` | a name the disk cannot hold |
| `FILE NOT FOUND` | the volume has no such file |
| `DISK ERROR` | the controller failed |
| `BREAK` | `STOP` reported where it stopped |
| `OVERFLOW` | a result too big to hold |
| `UNDEF'D FUNCTION` | `FN` on a name that was never defined |
| `BAD DATA` | `READ` found something the variable cannot hold |
| `STRING TOO LONG` | |
| `CAN'T CONTINUE` | `CONT` with nothing to continue |
| `NO FILE` | a file word with no file |
| `TYPE MISMATCH` | a string where a number belongs, or the other way round |
| `NOT IN THIS ROM YET` | a word this ROM knows about and has not been given |

`CONT` after a `STOP` carries on from the statement after it. Editing the
program makes `CONT` refuse: `?CAN'T CONTINUE`.

---

## The monitor ROM

The other ROM the Kestrel's socket takes. It is how a machine-code program is
written and run on the machine itself, and how the cassette is used at all —
Kestrel BASIC has no tape words.

| Command | Does |
| --- | --- |
| `M aaaa [nn]` | show memory from `aaaa`, `nn` bytes |
| `: aaaa bb …` | deposit bytes at `aaaa` |
| `G aaaa` | go to `aaaa`; an `RTS` there comes back to the prompt |
| `R` | show the registers |
| `S aaaa bbbb` | save `aaaa`–`bbbb` **inclusive** to the cassette |
| `L` | load a block back |

`S 0900 0902` writes three bytes behind a four-byte header — the load address and
the length — and `L` puts them back where they came from. Both need a cassette:
with the drawer empty they say `no tape`.

A deposit and a run, from the prompt:

```
.: 0900 A9 41 20 00 E0 60
.M 0900 6
0900: A9 41 20 00 E0 60
.G 0900
A.
```

The monitor's prompt is a full stop, and the `A.` on the last line is the
character the routine printed followed by that prompt coming back.

`A9 41` loads `'A'`, `20 00 E0` calls `CHROUT` at `$E000`, and `60` returns.

---

## The other ROMs in the socket

BASIC and the monitor were in the box. The socket took more than that, and the
repository ships the two that a school and a dealer would have bought: a teaching
language and a service ROM.

| ROM | `--rom` | What it is |
| --- | --- | --- |
| Kestrel BASIC V2.0 | `basic` | the language the machine is switched on with |
| Kestrel Monitor V1.0 | `monitor` | the processor's view, above |
| Kestrel PILOT V1.0 | `pilot` | the teaching language |
| Kestrel Diagnostics V1.0 | `diagnostics` | the service ROM a dealer fitted to a faulty machine |

Changing one is a reset, because changing a chip is:

```sh
./start.sh cli --rom pilot
./start.sh cli --languages            # what this machine could take
```

### PILOT

PILOT is a language for teaching, and it is small on purpose: a lesson is a list
of numbered lines, and each line types something, accepts an answer, or decides
what to do about the answer.

| Statement | Does |
| --- | --- |
| `T: text` | type the text, then a new line; `$A` prints answer `A`, `$$` prints `$` |
| `A: letter` | accept an answer into that variable, up to fifteen characters |
| `M: text` | does the answer just accepted match this text? `*` matches anything |
| `Y: label` | go to the label when it matched |
| `N: label` | go to the label when it did not |
| `J: label` | go to the label |
| `E:` | end the program |
| `R: text` | a remark |
| `:label` | a label, which is where a jump goes |

The commands are `RUN`, `LIST` and `NEW`. A number on its own deletes that line,
and a line typed without a number is obeyed at once, which is how a lesson is
tried out before it is written down. Matching is without regard to case.

```
>10 T:WHAT IS THE CAPITAL OF FRANCE?
>20 A:C
>30 M:PARIS
>40 Y:RIGHT
>50 T:NO, IT IS PARIS.
>60 E:
>70 :RIGHT
>80 T:THAT IS RIGHT.
>90 E:
>RUN
WHAT IS THE CAPITAL OF FRANCE?
Paris
THAT IS RIGHT.
```

### Diagnostics

The service ROM measures the machine instead of using it. It tests memory, the
tube and the keyboard on the way up, and it prints the address of the first byte
that did not come back:

```
*** KESTREL DIAGNOSTICS V1.0 ***

RAM  $0800-$7FFF  OK
TUBE 40 X 25      OK
SOUND             LISTEN FOR THE NOTE
KEYBOARD          PRESS ANY KEY
                  BREAK TESTS AGAIN
KEY $41 A
```

`BREAK` runs the tests again. The note is a note to listen for rather than a
result, because the sound chip cannot be read back on this machine. A memory test
of thirty kilobytes takes several seconds on a one-megahertz machine, so give it
time:

```sh
./start.sh cli --rom diagnostics --frames 400
```

---

## Cartridges

A cartridge is a program on a board: it goes in the slot at the side of the
machine and, from 1982, the machine starts it instead of its language. Nothing
has to be typed and nothing has to be loaded — the machine is switched on and the
cartridge is running, which is why the cartridge was the way software was sold to
people who did not want to learn a language first.

The reset looks at the bottom of the slot once, on the way up. Four letters,
`KSTL`, and the address after them, are the whole of the agreement: a cartridge
that carries them is started, and a cartridge that does not is left in the slot
and the language starts as usual.

| | Kestrel, 1982 | Kestrel 16, 1992 |
| --- | --- | --- |
| The slot | `$9000`–`$9FFF`, 4 KiB | `$00:C000`–`$00:DFFF`, with the whole 128 KiB image across banks `$04`–`$07` |
| The four letters | `$9000` | `$00:C000` |
| Where it goes | the word at `$9004` | the word at `$00:C004`, an address in bank zero |

### The cartridges in this repository

They are sources, like the ROMs, and they are assembled when they are fitted.
`cartridges/README.md` is the catalogue and the notes on writing one.

| Cartridge | Machine | What it is |
| --- | --- | --- |
| `monitor` | Kestrel | Kestrel Machine Monitor: `D` dumps, `M` changes, `J` runs, `B` goes back to BASIC |
| `tutor` | Kestrel | Kestrel Tutor: the multiplication tables, marked, with a score |
| `colours` | Kestrel 16 | Kestrel 16 Colour Demonstration: the tube filled with texture and colour |

### Fitting one

```sh
./start.sh cli --cart monitor                  # by name, assembled from its source
./start.sh cli --cart monitor.bin              # or from an image already assembled
./start.sh cli --model kestrel16 --cart colours
./start.sh kestrel --cart monitor              # the window, fitted before it starts
./start.sh cli --cartridges                    # what the machine could be sold
```

The window emulator fits one from **Cartridge** in its settings sheet, or from
the **Cartridge** menu, and takes any image of exactly the slot's size. `--cart`
with no machine named fits it to the Kestrel.

A cartridge may hand the machine back: on the Kestrel, `JMP COLD_START` — the
monitor's `B` — starts the language as though the machine had just been switched
on.

---

## Kestrel DOS

The disk is 512 sectors of 256 bytes, and the controller moves one sector at a
time. Everything above that is Kestrel DOS, in the KERNAL.

| From BASIC | Does |
| --- | --- |
| `SAVE "NAME"` | writes the program to the volume |
| `LOAD "NAME"` | reads it back, replacing what is in memory |
| `CATALOG` | lists the volume |
| `DELETE n[,m]` | removes **lines** from the program, not files |

Saving over a name that is already on the volume replaces it: the old file's
sectors go back on the free map and its entry is cleared before the new one is
written, so a volume never holds two files of the same name.

A `LOAD` that cannot find its name reports on the `READY.` line:

```
LOAD "MISSING"

?FILE NOT FOUND
READY.
```

The volume's own layout — header, directory, files — is in
[the technical reference](technical-reference.md#kestrel-dos-10-on-the-disk).

---

## Kestrel 16 BASIC

A line-oriented language with sixteen-bit integers, deliberately smaller than
its predecessor:

| Statement | Notes |
| --- | --- |
| `PRINT` | expressions and strings, `;` for no space, `,` for a space, a trailing `;` leaves the line open. `?` is shorthand |
| `LET A=41` | or `A=41`; variables are `A` to `Z`, sixteen bits each |
| `LET A$="text"` | a string variable, `A$` to `Z$`, up to thirty-one characters |
| `LEN(A$)` | how many characters a string has |
| `A$+B$` | the two joined, in a temporary of thirty-one characters |
| `LEFT$(A$,n)` / `RIGHT$(A$,n)` / `MID$(A$,n,m)` | part of a string, counting from one; a count past the end is as much as there is |
| `CHR$(n)` / `ASC(A$)` | the character with that number, and the number of the first character |
| `STR$(n)` / `VAL(A$)` | the digits of a number, and the number those digits begin with |
| `ABS(n)` / `SGN(n)` | the size without its sign, and the sign on its own |
| `DEF FNA(X)=expr` | a function of your own, of one argument, called as `FNA(3)` |
| `PEEK(addr)` / `POKE addr,value` | one byte of bank zero: the tube, the I/O page and the program |
| `DIM A(10)` / `DIM M(2,3)` | an array of eleven words, `A(0)` to `A(10)`, or `M(i,j)` with two subscripts; the `DIM` comes first |
| `DIM S$(10)` / `S$(i)` | an array of strings, each a slot of thirty-one characters |
| `RUN` [`n`] / `LIST` [`n`] | run the stored program, or show it from line `n` |
| `GOTO n` | continue at line `n` |
| `IF cond THEN …` [`ELSE` …] | one branch or the other; a line number after `THEN` jumps |
| `GOSUB n` / `RETURN` | sixteen levels of subroutine |
| `FOR v=a TO b` [`STEP s`] … `NEXT v` | a loop; `s` defaults to `1` and may be negative, but not zero |
| `FORMAT` / `CATALOG` | write a volume, or list what is on it |
| `FIND "NAME"` | entry, first sector and sector count of a file on the volume |
| `SAVE "NAME"` / `LOAD "NAME"` / `DELETE "NAME"` | the program store and Kestrel DOS 2.0 |
| `CSAVE "NAME"` / `CLOAD "NAME"` | the program store and the cassette, as a named block |
| `CLS` | clears the screen and prints the banner |
| `REM` | a remark |
| `END` | ends the line |
| `NEW` | starts again |

A line that begins with a number is stored rather than run, and a number on its
own removes that line.

A colon separates statements, so one line may hold several: `PRINT 1+1: PRINT "C"`
prints `2` and then `C`.

Expressions are `+ - * /` with parentheses and unary minus; `*` and `/` bind
tighter than `+` and `-`. Comparisons (`=` `<>` `<` `>` `<=` `>=`) give `-1` or
`0`, and `AND`, `OR` and `NOT` combine them.

```basic
PRINT 30000+1
30001
PRINT 1000*20
20000
PRINT -7/2
-3
```

The quotient of a division rounds towards zero, and the sign comes from the two
operands. A divisor of zero says `?WHAT?`.

Anything the machine cannot read is `?WHAT?`, and nothing is printed for it:

```basic
PRINT A$
?WHAT?
```

### What it does not do yet

- **loops that span lines belong in a program** — a loop typed at the prompt runs
  only as far as the end of its line, because each new line starts with no loops
  pending; a loop whose `FOR` and `NEXT` share a line runs at the prompt;
- **strings, in part** — `A$` holds thirty-one characters and can be assigned,
  printed, compared, joined with `+`, sliced with `LEFT$`/`RIGHT$`/`MID$`, and
  turned into characters, digits and back with `CHR$`/`ASC`/`STR$`/`VAL`;
- **arrays, in part** — `DIM A(10)` and `A(i)` with one or two subscripts, of
  whole words or of strings: no more than two subscripts;
- **one argument** — a `DEF FN` function takes one argument and may not call
  another definition, itself included; it borrows the variable of the letter it
  calls its argument by;
- **a filesystem** — `FORMAT`, `CATALOG`, `SAVE`, `LOAD`, `DELETE` and `FIND`
  all work;
- **the cassette, in part** — `CSAVE "NAME"` and `CLOAD "NAME"` move the
  program store as named blocks, and a tape may hold as many as fit; nothing
  else can be saved or loaded, and there is no way to see what a tape holds.

---

## Worked examples

### The sieve

```basic
10 REM PRIME SIEVE TO 100
20 DIM P(100)
30 FOR I=2 TO 100
40 IF P(I) THEN 70
50 PRINT I;
60 FOR J=I*I TO 100 STEP I: P(J)=1: NEXT J
70 NEXT I
80 PRINT
```

```
RUN
2357111317192329313741434753596167717379
838997
```

The primes run together because line 50 leaves the cursor where it is. Line 60 is
the interesting one: once `I` passes ten, `I*I` is past the end of the array, and
the loop runs no times at all rather than once with a bad subscript.

### Factorials

```basic
10 F=1
20 FOR I=1 TO 10
30 F=F*I
40 PRINT I;"! IS";F
50 NEXT I
```

```
1! IS1
2! IS2
…
10! IS3628800
```

### Eight notes on the Kestrel

```basic
10 FOR N=1 TO 8
20 POKE 34824,N*20
30 FOR D=1 TO 120:NEXT D
40 NEXT N
```

`34824` is `$8808`, the sound latch: a write is a note, and the window answers
with a beep.

---

## The samples

Every file in [samples/](../samples) is typed by the samples suite and
compared with the `.expected` file beside it, so these programs run as written.

| Kestrel | What it shows |
| --- | --- |
| [01-hello](../samples/kestrel/01-hello.bas) | `PRINT`, strings, a small `FOR` |
| [02-sieve](../samples/kestrel/02-sieve.bas) | arrays, `FOR` with a start past its limit |
| [03-fibonacci](../samples/kestrel/03-fibonacci.bas) | variables in a loop |
| [04-factorial](../samples/kestrel/04-factorial.bas) | accumulating a product |
| [05-tables](../samples/kestrel/05-tables.bas) | nested loops and `;` |
| [06-average](../samples/kestrel/06-average.bas) | `READ`/`DATA` with a sentinel |
| [07-strings](../samples/kestrel/07-strings.bas) | the string functions and comparisons |
| [08-functions](../samples/kestrel/08-functions.bas) | `DEF FN` in the program |
| [09-subroutines](../samples/kestrel/09-subroutines.bas) | `GOSUB`/`RETURN` |
| [10-random](../samples/kestrel/10-random.bas) | `RND` and `INT` |
| [11-pattern](../samples/kestrel/11-pattern.bas) | `POKE` into the tube and its colours |
| [12-sound](../samples/kestrel/12-sound.bas) | the sound latch |

| Kestrel 16 | What it shows |
| --- | --- |
| [01-hello](../samples/kestrel16/01-hello.bas) | `PRINT` with strings and `;` |
| [02-arithmetic](../samples/kestrel16/02-arithmetic.bas) | sixteen-bit range and precedence |
| [03-division](../samples/kestrel16/03-division.bas) | signed division |
| [04-variables](../samples/kestrel16/04-variables.bas) | `LET` and the `A`–`Z` variables |
| [05-table](../samples/kestrel16/05-table.bas) | a table, one row to a line |
| [06-limits](../samples/kestrel16/06-limits.bas) | what the language does not do yet |
| [07-branches-and-loops](../samples/kestrel16/07-branches-and-loops.bas) | `IF`/`THEN`, `GOSUB`/`RETURN` and `FOR`/`NEXT` in one program |
| [08-for-step](../samples/kestrel16/08-for-step.bas) | positive and negative `STEP`, a loop that runs no times, `STEP 0` rejected |
| [09-strings](../samples/kestrel16/09-strings.bas) | `A$` variables, `LEN`, comparing, joining and slicing |
| [10-arrays](../samples/kestrel16/10-arrays.bas) | `DIM`, a sieve, and nested loops |

Run one:

```bash
./start.sh cli --model kestrel --file samples/kestrel/03-fibonacci.bas --type RUN
./start.sh cli --model kestrel16 --file samples/kestrel16/03-division.bas
```
