// Links libamix. AMIX_LIB_DIR names the folder with libamix.so / amix.dll / libamix.dylib (default: ../../../build, the engine's CMake
// build dir). AMIX_STATIC=1 links the static twin instead (then the C++ runtime is pulled in too).
use std::{env, path::PathBuf};
fn main() {
    let manifest = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap());
    let dir = env::var("AMIX_LIB_DIR").map(PathBuf::from).unwrap_or_else(|_| manifest.join("../../../build"));
    println!("cargo:rerun-if-env-changed=AMIX_LIB_DIR");
    println!("cargo:rerun-if-env-changed=AMIX_STATIC");
    println!("cargo:rustc-link-search=native={}", dir.display());
    if env::var("AMIX_STATIC").map(|v| v == "1").unwrap_or(false) {
        println!("cargo:rustc-link-lib=static=amix_static");
        for l in ["audiomixer_runtime", "audiomixer_engine", "amixos_license", "amixos_paths"] { println!("cargo:rustc-link-lib=static={l}"); }
        println!("cargo:rustc-link-lib=stdc++");
    } else {
        println!("cargo:rustc-link-lib=dylib=amix");
        // at run time the loader must find libamix: LD_LIBRARY_PATH / DYLD_LIBRARY_PATH / PATH, or install it (scripts/package-linux.sh)
    }
}
