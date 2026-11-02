// SPDX-License-Identifier: MIT
//
// The ROMs, assembled from their sources.
//
// The machine's language is machine code, and its source is in `roms/` as
// assembly. The C++ build assembles those sources with its own tools; this does
// the same with the ported ones, at start-up, so the Rust tree needs no build
// step of its own and no artefact from the other tree.
//
// The two assemblers are used whole -- they are librarised ports of the tools,
// not a second implementation -- so the bytes are the ones the C++ build puts in
// `build/generated`, which the tests compare.

use std::path::PathBuf;
use std::sync::OnceLock;

use crate::profile::{Model, KESTREL, KESTREL16};

/// The images a machine boots with.
pub struct ShippedRoms {
    pub kernal: Vec<u8>,
    pub basic: Vec<u8>,
    /// The Kestrel's socket carries more than one language.
    pub monitor: Option<Vec<u8>>,
}

/// Where the assembly sources are: `KESTREL_SOURCES` if it is set, otherwise
/// found by walking up from this crate to the repository that holds `roms/`.
pub fn source_directory() -> Result<PathBuf, String> {
    if let Some(path) = std::env::var_os("KESTREL_SOURCES") {
        return Ok(PathBuf::from(path));
    }
    let mut directory = PathBuf::from(env!("CARGO_MANIFEST_DIR"));
    for _ in 0..6 {
        let candidate = directory.join("roms");
        if candidate.join("kestrel").join("kestrel.inc").is_file() {
            return Ok(candidate);
        }
        if !directory.pop() {
            break;
        }
    }
    Err("no roms/ directory: set KESTREL_SOURCES to the repository's roms directory".to_string())
}

/// The images for a model, assembled once per process.
pub fn shipped(model: Model) -> Result<&'static ShippedRoms, String> {
    static KESTREL_ROMS: OnceLock<Result<ShippedRoms, String>> = OnceLock::new();
    static KESTREL16_ROMS: OnceLock<Result<ShippedRoms, String>> = OnceLock::new();
    let slot = match model {
        Model::Kestrel => &KESTREL_ROMS,
        Model::Kestrel16 => &KESTREL16_ROMS,
    };
    slot.get_or_init(|| assemble(model)).as_ref().map_err(|error| error.clone())
}

fn assemble(model: Model) -> Result<ShippedRoms, String> {
    let root = source_directory()?;
    match model {
        Model::Kestrel => {
            let profile = &KESTREL;
            Ok(ShippedRoms {
                kernal: assemble_6502(&root, "kestrel", "kernal", profile.kernal_base, profile.kernal_size)?,
                basic: assemble_6502(&root, "kestrel", "basic", profile.basic_base, profile.basic_size)?,
                monitor: Some(assemble_6502(&root, "kestrel", "monitor", profile.basic_base, profile.basic_size)?),
            })
        }
        Model::Kestrel16 => {
            let profile = &KESTREL16;
            Ok(ShippedRoms {
                kernal: assemble_65816(&root, "kestrel16", "kernal", profile.kernal_base, profile.kernal_size)?,
                basic: assemble_65816(&root, "kestrel16", "basic", profile.basic_base, profile.basic_size)?,
                monitor: None,
            })
        }
    }
}

fn assemble_6502(
    root: &std::path::Path,
    directory: &str,
    name: &str,
    origin: u32,
    size: u32,
) -> Result<Vec<u8>, String> {
    use kestrel_asm::{Assembler, Options, SourceParser};
    let path = root.join(directory).join(format!("{name}.s"));
    let text = path.to_string_lossy().into_owned();
    let statements = SourceParser::new(0)
        .parse_file(&text)
        .map_err(|error| format!("{text}: {error}"))?;
    let mut statements = statements;
    kestrel_asm::expand_includes(&mut statements, 0).map_err(|error| format!("{text}: {error}"))?;
    let options = Options {
        origin,
        size,
        fill: 0xFF,
    };
    let assembly = Assembler::new(statements, options, name.to_string())
        .assemble()
        .map_err(|error| format!("{text}: {error}"))?;
    Ok(assembly.image)
}

fn assemble_65816(
    root: &std::path::Path,
    directory: &str,
    name: &str,
    origin: u32,
    size: u32,
) -> Result<Vec<u8>, String> {
    use kestrel16_asm::{Assembler, Options, SourceParser};
    let path = root.join(directory).join(format!("{name}.s"));
    let text = path.to_string_lossy().into_owned();
    let statements = SourceParser::new(0)
        .parse_file(&text)
        .map_err(|error| format!("{text}: {error}"))?;
    let mut statements = statements;
    kestrel16_asm::expand_includes(&mut statements, 0)
        .map_err(|error| format!("{text}: {error}"))?;
    let options = Options {
        origin,
        size,
        fill: 0xFF,
    };
    let assembly = Assembler::new(statements, options, name.to_string())
        .assemble()
        .map_err(|error| format!("{text}: {error}"))?;
    Ok(assembly.image)
}
