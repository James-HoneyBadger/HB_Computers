# Development history

HB Computers began with a C++20/Qt 6 implementation and migrated to Rust and egui crate by crate. The older implementation remains in Git history; it is not required to build or run the current repository.

During the migration, differential comparisons against separately generated C++ artifacts helped locate differences in processor behavior, including missing INC/DEC flag updates in the 6502 core. These historical comparisons are not a claim that the current CI reruns the old C++ implementation.

For current implementation and verification see [Architecture](ARCHITECTURE.md), [Programmer’s Guide](programmers-guide.md), and the [Rust workspace guide](../rust/README.md).
