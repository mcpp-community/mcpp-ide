// Which backend the extension starts, and how. This file names no Zed type, so `cargo test` runs it
// on the host as well as compiling it for wasm32-wasip2.

/// The operating system Zed runs on; `lib.rs` maps Zed's enum onto it.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Platform {
    Linux,
    Mac,
    Windows,
}

/// The machine's architecture, as the bundle names its runners and images.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Arch {
    X86_64,
    Aarch64,
    /// One the bundle has no image for (32-bit x86).
    Other,
}

/// What Zed is asked to run.
#[derive(Debug, PartialEq, Eq)]
pub struct Launch {
    pub command: String,
    pub args: Vec<String>,
}

/// This extension's version: its crate's, which .github/scripts/release-plan.sh holds to
/// extension.toml's and to the series.
pub const PLUGIN_VERSION: &str = env!("CARGO_PKG_VERSION");

/// The repository whose releases carry the backend.
pub const REPOSITORY: &str = "mcpp-community/mcpp-ide";

/// The GitCode mirror of the release's files, tried when GitHub cannot be reached.
pub const MIRROR: &str = "https://gitcode.com/xlings-res/mcpp-ide/releases/download";

/// Where the backend comes from: the command `lsp.mcpp-ide.binary.path` names in Zed's settings,
/// with its `arguments` (`serve` when none are given), else the backend of this extension's series,
/// which `lib.rs` downloads into the extension's work directory. A `mcpp-ide` on PATH is not
/// started: the extension cannot read the series a backend answers, so it could not refuse one of
/// another series as the other plugins do.
#[derive(Debug, PartialEq, Eq)]
pub enum Source {
    Command(Launch),
    Release,
}

pub fn choose(setting: Option<(String, Option<Vec<String>>)>) -> Source {
    match setting {
        Some((command, args)) => Source::Command(Launch { command, args: args.unwrap_or_else(|| vec!["serve".to_string()]) }),
        None => Source::Release,
    }
}

/// `X.Y` of an `X.Y.Z`.
pub fn series(version: &str) -> &str {
    match version.rmatch_indices('.').next() {
        Some((at, _)) => &version[..at],
        None => version,
    }
}

/// The release that carries this series' backend, `vX.Y.0`: the backend is always its series' X.Y.0.
#[derive(Debug, PartialEq, Eq)]
pub struct Release {
    pub tag: String,
    pub asset: String,
    /// The directory of the work directory the archive is unpacked into.
    pub directory: String,
    /// The archive's own top directory.
    pub top: String,
}

pub fn release() -> Release {
    let version = format!("{}.0", series(PLUGIN_VERSION));
    let top = format!("mcpp-ide-{version}-openkal-bundle-all");
    Release {
        tag: format!("v{version}"),
        asset: format!("{top}.tar.gz"),
        directory: format!("mcpp-ide-{version}"),
        top,
    }
}

pub fn mirror_url(release: &Release) -> String {
    format!("{MIRROR}/{}/{}", release.tag, release.asset)
}

/// The bundle's backend for this machine, relative to the work directory: this system's runner,
/// `libexec/okrun/<system>-<arch>/okrun`, starting the image of this architecture,
/// `lib/<arch>/mcpp-ide.oke` (the bundle's mcpp-package.json, `launch`). The bundle has a runner for
/// linux-x86_64, linux-aarch64, macos-aarch64 and windows-x86_64.
#[derive(Debug, PartialEq, Eq)]
pub struct Bundle {
    pub runner: String,
    pub image: String,
}

pub fn bundle(release: &Release, platform: Platform, arch: Arch) -> Result<Bundle, String> {
    let (system, arch_name) = match (platform, arch) {
        (Platform::Linux, Arch::X86_64) => ("linux-x86_64", "x86_64"),
        (Platform::Linux, Arch::Aarch64) => ("linux-aarch64", "aarch64"),
        (Platform::Mac, Arch::Aarch64) => ("macos-aarch64", "aarch64"),
        (Platform::Windows, Arch::X86_64) => ("windows-x86_64", "x86_64"),
        _ => return Err(unsupported(platform, arch)),
    };
    let exe = if platform == Platform::Windows { ".exe" } else { "" };
    let root = format!("{}/{}", release.directory, release.top);
    Ok(Bundle {
        runner: format!("{root}/libexec/okrun/{system}/okrun{exe}"),
        image: format!("{root}/lib/{arch_name}/mcpp-ide.oke"),
    })
}

fn unsupported(platform: Platform, arch: Arch) -> String {
    format!(
        "mcpp-ide's release has no backend for {platform:?} {arch:?}: it has one for Linux x86_64 and aarch64, \
         macOS aarch64 and Windows x86_64. Name a backend you built in lsp.mcpp-ide.binary.path."
    )
}

/// What the user is told when neither GitHub nor the mirror gave the bundle.
pub fn download_failed(release: &Release, github: &str, mirror: &str) -> String {
    format!(
        "mcpp-ide {} could not be downloaded ({}): from github.com/{REPOSITORY}: {github}; from {}: {mirror}. \
         Name a backend in lsp.mcpp-ide.binary.path (`xlings install mcpp-ide` installs one).",
        release.tag,
        release.asset,
        mirror_url(release)
    )
}

/// The initialization options the backend receives (src/server.cppm, `initialize`): this editor, the
/// worktree trusted (Zed starts a language server only in a worktree it trusts), and the user's
/// settings (`lsp.mcpp-ide.settings`) over the user's own initialization options.
pub fn initialization_options(
    user: Option<zed_extension_api::serde_json::Value>,
    settings: Option<zed_extension_api::serde_json::Value>,
) -> zed_extension_api::serde_json::Value {
    use zed_extension_api::serde_json::{json, Value};
    let mut options = match user {
        Some(Value::Object(map)) => Value::Object(map),
        _ => json!({}),
    };
    options["editor"] = json!("zed");
    options["trusted"] = json!(true);
    if let Some(settings @ Value::Object(_)) = settings {
        options["settings"] = settings;
    }
    options
}

/// A work directory's entry that an earlier series left: a backend directory other than `keep`.
pub fn is_stale(entry: &str, keep: &str) -> bool {
    entry.starts_with("mcpp-ide-") && entry != keep
}

#[cfg(test)]
mod tests {
    use super::*;
    use zed_extension_api::serde_json::json;

    #[test]
    fn the_constant_is_the_extensions_version() {
        let manifest = include_str!("../extension.toml");
        let line = manifest.lines().find(|l| l.starts_with("version = ")).unwrap();
        assert_eq!(line, format!("version = \"{PLUGIN_VERSION}\""));
    }

    #[test]
    fn the_settings_name_the_backend_else_the_release_is_started() {
        assert_eq!(
            choose(Some(("/opt/mcpp-ide/bin/mcpp-ide".into(), None))),
            Source::Command(Launch { command: "/opt/mcpp-ide/bin/mcpp-ide".into(), args: vec!["serve".into()] })
        );
        assert_eq!(
            choose(Some(("/b/okrun".into(), Some(vec!["/b/mcpp-ide.oke".into(), "serve".into()])))),
            Source::Command(Launch { command: "/b/okrun".into(), args: vec!["/b/mcpp-ide.oke".into(), "serve".into()] })
        );
        assert_eq!(choose(None), Source::Release);
    }

    #[test]
    fn the_release_is_the_series_own() {
        assert_eq!(series("1.6.2"), "1.6");
        let r = release();
        let v = format!("{}.0", series(PLUGIN_VERSION));
        assert_eq!(r.tag, format!("v{v}"));
        assert_eq!(r.asset, format!("mcpp-ide-{v}-openkal-bundle-all.tar.gz"));
        assert_eq!(r.directory, format!("mcpp-ide-{v}"));
        assert_eq!(mirror_url(&r), format!("https://gitcode.com/xlings-res/mcpp-ide/releases/download/v{v}/mcpp-ide-{v}-openkal-bundle-all.tar.gz"));
    }

    #[test]
    fn each_platform_takes_its_runner_and_its_architectures_image() {
        let r = release();
        let root = format!("{}/{}", r.directory, r.top);
        let b = bundle(&r, Platform::Linux, Arch::X86_64).unwrap();
        assert_eq!(b.runner, format!("{root}/libexec/okrun/linux-x86_64/okrun"));
        assert_eq!(b.image, format!("{root}/lib/x86_64/mcpp-ide.oke"));
        let b = bundle(&r, Platform::Linux, Arch::Aarch64).unwrap();
        assert_eq!(b.runner, format!("{root}/libexec/okrun/linux-aarch64/okrun"));
        assert_eq!(b.image, format!("{root}/lib/aarch64/mcpp-ide.oke"));
        assert_eq!(bundle(&r, Platform::Mac, Arch::Aarch64).unwrap().runner, format!("{root}/libexec/okrun/macos-aarch64/okrun"));
        assert_eq!(bundle(&r, Platform::Windows, Arch::X86_64).unwrap().runner, format!("{root}/libexec/okrun/windows-x86_64/okrun.exe"));
    }

    #[test]
    fn a_platform_without_a_backend_names_the_ones_there_are() {
        for (platform, arch) in [(Platform::Mac, Arch::X86_64), (Platform::Windows, Arch::Aarch64), (Platform::Linux, Arch::Other)] {
            let error = bundle(&release(), platform, arch).unwrap_err();
            assert!(error.contains("macOS aarch64"), "{error}");
            assert!(error.contains("lsp.mcpp-ide.binary.path"), "{error}");
        }
    }

    #[test]
    fn a_failed_download_names_both_places_and_the_remedy() {
        let message = download_failed(&release(), "timed out", "404");
        assert!(message.contains("timed out") && message.contains("404"));
        assert!(message.contains(&mirror_url(&release())));
        assert!(message.contains("xlings install mcpp-ide"));
    }

    #[test]
    fn the_options_name_zed_trust_and_the_settings() {
        let options = initialization_options(Some(json!({ "locale": "zh-CN" })), Some(json!({ "mcpp.path": "/x/mcpp" })));
        assert_eq!(options["editor"], "zed");
        assert_eq!(options["trusted"], true);
        assert_eq!(options["locale"], "zh-CN");
        assert_eq!(options["settings"]["mcpp.path"], "/x/mcpp");
        let bare = initialization_options(None, None);
        assert_eq!(bare["editor"], "zed");
        assert!(bare.get("settings").is_none());
    }

    #[test]
    fn only_another_series_backend_is_stale() {
        assert!(is_stale("mcpp-ide-1.5.0", "mcpp-ide-1.6.0"));
        assert!(!is_stale("mcpp-ide-1.6.0", "mcpp-ide-1.6.0"));
        assert!(!is_stale("other", "mcpp-ide-1.6.0"));
    }
}
