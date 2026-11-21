// mcpp-ide in Zed: the backend started as a language server for mcpp.toml.
//
// The extension decides nothing (AGENTS.md §2): the backend answers completion, hover and diagnostics
// of mcpp.toml over standard LSP to an editor named `zed`. What this crate does is find the backend of
// its own series (resolve.rs, which `cargo test` covers on the host) -- the command the user's
// settings name, else the release's bundle, downloaded once into the work directory Zed gives the
// extension -- and start it. Zed starts it with a worktree's first TOML file. It reads mcpp.toml and
// build.mcpp only and finds the projects of the worktree itself, so in a worktree without one it
// answers nothing; declining there instead would show "Failed to run mcpp-ide" in Zed's status bar
// of every TOML project, and would miss a project below the worktree's root.
mod resolve;

use std::fs;

use resolve::{choose, initialization_options, Arch, Launch, Platform, Source};
use zed_extension_api::{self as zed, settings::LspSettings, LanguageServerId, LanguageServerInstallationStatus, Result};

const SERVER: &str = "mcpp-ide";

struct McppIde;

impl McppIde {
    fn downloaded_backend(&self, id: &LanguageServerId) -> Result<Launch> {
        let (os, arch) = zed::current_platform();
        let platform = match os {
            zed::Os::Windows => Platform::Windows,
            zed::Os::Mac => Platform::Mac,
            zed::Os::Linux => Platform::Linux,
        };
        let arch = match arch {
            zed::Architecture::Aarch64 => Arch::Aarch64,
            zed::Architecture::X8664 => Arch::X86_64,
            _ => Arch::Other,
        };
        let release = resolve::release();
        let bundle = resolve::bundle(&release, platform, arch)?;
        let present = |path: &str| fs::metadata(path).is_ok_and(|m| m.is_file());
        let marker = resolve::marker(&release);
        if !present(&marker) {
            zed::set_language_server_installation_status(id, &LanguageServerInstallationStatus::Downloading);
            let fetch = |url: &str| -> Result<()> {
                // A directory an interrupted download left is not reused.
                fs::remove_dir_all(&release.directory).ok();
                zed::download_file(url, &release.directory, zed::DownloadedFileType::GzipTar)?;
                if platform != Platform::Windows {
                    zed::make_file_executable(&bundle.runner)?;
                }
                if !(present(&bundle.runner) && present(&bundle.image)) {
                    return Err(format!("the archive holds no {} and {}", bundle.runner, bundle.image));
                }
                fs::write(&marker, release.tag.as_bytes()).map_err(|e| format!("{marker}: {e}"))
            };
            let github = zed::github_release_by_tag_name(resolve::REPOSITORY, &release.tag).and_then(|found| {
                let asset = found
                    .assets
                    .iter()
                    .find(|a| a.name == release.asset)
                    .ok_or_else(|| format!("the release has no {}", release.asset))?;
                fetch(&asset.download_url)
            });
            if let Err(from_github) = github {
                if let Err(from_mirror) = fetch(&resolve::mirror_url(&release)) {
                    let message = resolve::download_failed(&release, &from_github, &from_mirror);
                    zed::set_language_server_installation_status(id, &LanguageServerInstallationStatus::Failed(message.clone()));
                    return Err(message);
                }
            }
            // The backends of earlier series are not needed again.
            if let Ok(entries) = fs::read_dir(".") {
                for entry in entries.flatten() {
                    if let Some(name) = entry.file_name().to_str() {
                        if resolve::is_stale(name, &release.directory) {
                            fs::remove_dir_all(entry.path()).ok();
                        }
                    }
                }
            }
        }
        zed::set_language_server_installation_status(id, &LanguageServerInstallationStatus::None);
        // The backend runs in the worktree, not in the work directory, so it is given absolute paths.
        let work = std::env::current_dir().map_err(|e| format!("the extension's work directory: {e}"))?;
        let absolute = |relative: &str| work.join(relative).to_string_lossy().into_owned();
        Ok(Launch { command: absolute(&bundle.runner), args: vec![absolute(&bundle.image), "serve".to_string()] })
    }
}

impl zed::Extension for McppIde {
    fn new() -> Self {
        Self
    }

    fn language_server_command(&mut self, id: &LanguageServerId, worktree: &zed::Worktree) -> Result<zed::Command> {
        let mut env = worktree.shell_env();
        let binary = LspSettings::for_worktree(SERVER, worktree).ok().and_then(|s| s.binary);
        if let Some(extra) = binary.as_ref().and_then(|b| b.env.clone()) {
            env.extend(extra);
        }
        let setting = binary.and_then(|b| b.path.map(|path| (path, b.arguments)));
        let Launch { command, args } = match choose(setting) {
            Source::Command(launch) => launch,
            Source::Release => self.downloaded_backend(id)?,
        };
        // The backend finds mcpp itself (the project's pin, mcpp.path, PATH), so the worktree's
        // environment is passed as it is.
        Ok(zed::Command { command, args, env })
    }

    fn language_server_initialization_options(
        &mut self,
        _id: &LanguageServerId,
        worktree: &zed::Worktree,
    ) -> Result<Option<zed::serde_json::Value>> {
        let settings = LspSettings::for_worktree(SERVER, worktree).ok();
        let (user, values) = match settings {
            Some(s) => (s.initialization_options, s.settings),
            None => (None, None),
        };
        Ok(Some(initialization_options(user, values)))
    }
}

zed::register_extension!(McppIde);
