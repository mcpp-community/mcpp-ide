# mcpp-nvim

mcpp's project features in Neovim 0.10 and later, over the mcpp-ide backend (`mcpp-ide serve`):
builds, tests, cleanups and packs as jobs whose output streams into a buffer, the program in a
terminal, mcpp's diagnostics, targets and platforms, toolchains, libraries, the build cache and the
graphs, and a statusline component. It is the Neovim adapter of
[mcpp-ide](https://github.com/mcpp-community/mcpp-ide), beside mcpp-vscode and mcpp-clion: what a
build runs, what a cleanup costs and which commands exist are the backend's answers, the same ones
those editors receive.

The plugin registers no language service. Completion, navigation and diagnostics of C++ files come
from mcppls through its own Neovim plugin (below).

| | |
|---|---|
| Plugin | mcpp-nvim, this directory (`mide-plugins/nvim` of mcpp-ide); its version is mcpp-ide's |
| Lua module | `mcpp` (`require('mcpp')`) |
| Command | `:Mcpp [subcommand]` |
| Diagnostic namespaces | `mcpp` (a build's problems), `mcpp.toml` (the manifest's) |
| Autocommand | `User McppStatus` |
| xlings package | `mcpp-ide-nvim` |

## Install

```bash
xlings install mcpp-ide-nvim     # the plugin, on Neovim's runtimepath, and the backend mcpp-ide
xlings install mcppls-nvim       # mcppls and its plugin, for C++ files
```

`mcpp-ide-nvim` installs this directory from the source tree of the mcpp-ide release, as
`mcppls-nvim` installs `editors/nvim` of mcppls's, and depends on `mcpp-ide`, the package of the
backend. It also sets the plugin up: `require('mcpp').setup()` runs at Neovim's start, unless the
user's init.lua has loaded the module already (its own `setup()` keeps its options) or sets
`vim.g.mcpp_auto_start = false`. Without xlings, put the directory on the runtimepath:

```lua
-- lazy.nvim
{
  'mcpp-community/mcpp-ide',
  config = function(plugin)
    vim.opt.rtp:append(plugin.dir .. '/mide-plugins/nvim')
    require('mcpp').setup()
  end,
}

-- without a plugin manager, after: git clone https://github.com/mcpp-community/mcpp-ide ~/.local/share/mcpp-ide
vim.opt.rtp:append(vim.fn.expand('~/.local/share/mcpp-ide/mide-plugins/nvim'))
require('mcpp').setup()
```

## Setup

```lua
require('mcpp').setup({
  backend = nil,     -- the mcpp-ide program, an ok-image bundle (*.okb, started by okrun on PATH),
                     -- or an argv; default: mcpp-ide on PATH
  settings = {       -- mcpp's settings (spec/settings.json), validated by the backend
    -- ['mcpp.path'] = '/path/to/mcpp',
    -- ['mcpp.build.target'] = 'x86_64-linux-musl',
    -- ['mcpp.build.onSave'] = true,
  },
  trusted = true,    -- false: mcpp runs with the effects mcpp.trust.denyEffects lists denied
})
```

**The backend.** `backend`, else `mcpp-ide` on `PATH`, which `xlings install mcpp-ide` provides
(okrun starting its ok-image bundle). When neither is found the plugin says so, naming
`xlings install mcpp-ide`, and starts nothing; `:Mcpp` looks again.

**The session.** A buffer whose file lies under an `mcpp.toml` (the nearest one upward, else the
working directory's) starts one backend for the Neovim instance, speaking the protocol through
Neovim's own JSON-RPC client (`vim.lsp.rpc`). It is initialized as the editor `nvim` with Neovim's
language (`v:lang`, or `mcpp.ui.language` when that names one), the trust below, that project as
its folder and the settings above; the projects of buffers entered later are added to it. The
runtime strings and the command table are then read from it. The backend is ended with `shutdown`
and `exit` when Neovim exits.

**Trust.** Neovim has no workspace trust. A project is trusted, as Neovim runs what its user opens,
unless `trusted = false`, which denies the effects `mcpp.trust.denyEffects` lists (`mcpp --deny`).

**Settings read here.** `mcpp.task.clearTerminal`, `mcpp.task.revealTerminal` (`always`,
`onFailure`, `never`) and `mcpp.task.focusTerminal` govern the output window; `mcpp.task.runArgs`
the program's arguments; `mcpp.build.onSave` a build on saving a source, `mcpp.toml` or
`build.mcpp`; `mcpp.toml.diagnostics.enabled` the manifest's problems;
`mcpp.diagnostics.selfCheckOnStartup` the self-check at start; `mcpp.ui.statusBar.show` the
statusline component. Every other setting is the backend's.

## Commands

`:Mcpp <subcommand>` offers the backend's command table for Neovim (`commands/list` with the editor
`nvim`), with completion; a subcommand is a command's id after `mcpp.` (`mcpp.build` is
`:Mcpp build`), or the name its row gives. `:Mcpp` alone opens the quick menu. Each row's action is
mapped onto Neovim:

| Action | In Neovim | Subcommands |
|---|---|---|
| job, rpc | mcpp's narration streamed into `mcpp://output`, its end said there and, when it fails, as a notification | `build`, `test [filter]`, `autoConfigureModules`, `refreshCompilationDatabase`, `library.updateIndex` |
| run | `run/launch` builds; its `argv`, `cwd` and `env` start in a terminal buffer | `run [bin]` |
| debug | `debug/configuration` handed to nvim-dap; offered only when nvim-dap is loaded | `debug [bin]` |
| cache | the backend's plan, confirmed at the level it asks for (`vim.ui.select`), applied as a job | `clean`, `cleanStaleArtifacts`, `cleanProjectArtifacts`, `gcGlobalCache`, `pruneGlobalCache`, `verifyGlobalCache`, `cleanLegacyCache` |
| panel | the view model as text in a floating window, or as choices | `showTargets`, `showToolchains`, `showCachePanel`, `refreshCacheStats`, `project.showDependencyGraph`, `project.showModuleDependencyGraph`; `library.search [keyword]` (choose a package and a version: `mcpp add`); `pack` (format, mode, linkage: `mcpp pack`) |
| cancel | `job/cancel` for every running job | `cancel` |
| selectTarget, installToolchain, selectDefaultToolchain, newProject | `vim.ui.select` and `vim.ui.input` | the same names |
| quickMenu | `menu/quick` in `vim.ui.select` | `:Mcpp`, `showMenu` |
| selfCheck, languageService | a notification | `selfCheck`, `languageServer.refreshState` |
| problems | both namespaces in the quickfix list | `problems` |

The settings panel is not offered: here the settings are `setup()`'s, which the backend validates.
A forward to mcppls's own commands is not offered either: mcppls-nvim has its own (`:McpplsStatus`,
`:McpplsRestart`, ...). `spec/commands.json` says, per row, why Neovim leaves it out (`omitted`).

A target chosen with `:Mcpp selectTarget` holds for the Neovim session; `settings` in `setup()`
keeps one.

**Debugging** needs [nvim-dap](https://github.com/mfussenegger/nvim-dap). The adapters the plugin
recognises, by the name they are registered under, and the configuration the backend writes for
each: `codelldb` (CodeLLDB's), `lldb-dap` or `lldb` (lldb-dap's), `cppdbg` (the C/C++ extension's
OpenDebugAD7), `gdb` (GDB's own DAP, GDB 14 and later). `mcpp.debug.engine` chooses among those
present. A program that runs through another (an ok image under okrun) is debugged as that runner.

## Diagnostics

A build's problems, located where mcpp reports them, are the namespace `mcpp`; a build or test of a
project clears that project's. The manifest's problems, from mcpp (`manifest/diagnostics` when it
changes, `manifest/check` when it is saved), are the namespace `mcpp.toml`, on `mcpp.toml`.
`:Mcpp problems` puts both in the quickfix list.

## Statusline

`require('mcpp').status()` is the target the project builds for and the jobs running or the last
one's end: `mcpp x86_64-linux-gnu · build ok`, `mcpp this machine · 1 job(s) running`. It is empty
before the backend runs and outside a project. `require('mcpp').status_data()` is the same as data
(with the platform the last plan named and the language service's state); `User McppStatus` fires
when it changes.

```lua
vim.o.statusline = '%f %= %{v:lua.require("mcpp").status()} '
```

## mcppls alongside

```lua
require('mcppls').setup()   -- C++ files: xlings install mcppls-nvim
require('mcpp').setup()     -- mcpp projects
```

mcppls 0.0.18 and later refresh after any build through mcpp's result record, so a build started
here needs nothing forwarded. When the mcppls found is missing or older, the plugin says so once at
start, with `xlings install mcppls-nvim`; `:Mcpp languageServer.refreshState` asks again.

## Tests

```bash
mcpp build                                                     # the backend and the mock mcpp
nvim --headless --clean -l mide-plugins/nvim/tests/smoke.lua   # over the mock mcpp
MCPP_NVIM_MCPP="$(command -v mcpp)" nvim --headless --clean -l mide-plugins/nvim/tests/smoke.lua
```

Against the backend this repository built (`MCPP_IDE_BACKEND` names another): what the plugin says
without a backend, the start and `initialize`, the command table, `:Mcpp` completion, a build
streamed into the output, a failed build's problem in the namespace and the quickfix list, the
manifest's problem, the views, a run in a terminal, nvim-dap's configuration, a cleanup, the quick
menu, the language service, the self-check, a cancelled build, a library added, a pack, the
target, and the end of the session. Over the mock mcpp the build replays protocol/fixtures; with
`MCPP_NVIM_MCPP` a real mcpp builds the project (with `MCPP_NVIM_TOOLCHAIN`, default `gcc@16.1.0`).
CI runs it on Neovim 0.10.4 and 0.12.5.
