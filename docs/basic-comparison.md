# BASIC dialect comparison

These are fictional dialects implemented by the shipped machine-code ROMs, not compatibility claims about a historical BASIC.

| Feature | Kestrel BASIC V2.0 | Kestrel 16 BASIC 2.0 |
| --- | --- | --- |
| Numeric representation | Five-byte floating point | Signed 16-bit integer; wraps |
| Strings | Supported | Supported, up to 31 characters per slot |
| Arrays | Supported | Numeric and string, one or two dimensions; DIM required |
| Input | INPUT, GET | No INPUT or GET |
| Flow | IF, GOTO, GOSUB, FOR, NEXT | IF/ELSE, GOTO, GOSUB, FOR, NEXT |
| User functions | DEF FN | DEF FN, one argument; nested calls unsupported |
| Disk | Kestrel DOS 1.0 | Kestrel DOS 2.0 |
| Cassette | Monitor S/L memory blocks | CSAVE/CLOAD named BASIC program blocks |
| Machine access | PEEK/POKE | PEEK/POKE in bank zero |

Both support the following examples, independently verified with the CLI:

```basic
PRINT 2+2
A$="HELLO"
PRINT A$
PRINT 30000+1
PRINT 1000*20
```

See the [User Guide](users-guide.md), [Kestrel manual](kestrel/README.md), and [Kestrel 16 manual](kestrel16/README.md) for syntax and limits. The [sample programs](../samples/) have expected output used by the workspace regression tests.
