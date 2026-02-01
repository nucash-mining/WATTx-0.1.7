//! Build script for wattx-fcmp
//!
//! Generates C header file using cbindgen

use std::env;
use std::path::PathBuf;

fn main() {
    let crate_dir = env::var("CARGO_MANIFEST_DIR").unwrap();
    let out_dir = PathBuf::from(&crate_dir).join("..");

    // Generate C header using cbindgen.toml config
    cbindgen::Builder::new()
        .with_crate(&crate_dir)
        .with_config(cbindgen::Config::from_file("cbindgen.toml").unwrap_or_default())
        .generate()
        .expect("Unable to generate C bindings")
        .write_to_file(out_dir.join("fcmp_ffi_gen.h"));

    println!("cargo:rerun-if-changed=src/lib.rs");
    println!("cargo:rerun-if-changed=cbindgen.toml");
}
