-- mcpp-nvim: mcpp's project features in Neovim (0.10 and later) over the mcpp-ide backend,
-- `mcpp-ide serve` -- builds, tests, cleanups and packs as jobs streamed into a buffer, the program
-- in a terminal, mcpp's diagnostics, targets, toolchains, libraries, the cache and the graphs, a
-- statusline component. Every decision is the backend's, the one mcpp-vscode and mcpp-clion ask.
--
-- C++ files are mcppls's, through its own plugin (mcppls-nvim). mcpp.toml and build.mcpp are the
-- backend's: its LSP client serves their completion, hover, jumps and diagnostics.
--
--   require('mcpp').setup({ backend = ..., settings = { ['mcpp.build.target'] = ... }, trusted = true })
--   :Mcpp [subcommand]                the backend's command table; no subcommand: the quick menu
--   require('mcpp').status()          a statusline component
local session = require('mcpp.session')
local commands = require('mcpp.commands')
local diagnostics = require('mcpp.diagnostics')

local M = {}
local t = session.t

local defaults = {
  -- The mcpp-ide program, an ok-image bundle (`*.okb`, run by okrun on PATH) or an argv; default:
  -- `mcpp-ide` on PATH (`xlings install mcpp-ide`).
  backend = nil,
  -- mcpp's settings (spec/settings.json: `mcpp.path`, `mcpp.build.target`, `mcpp.task.runArgs`, ...),
  -- validated by the backend.
  settings = {},
  -- Neovim has no workspace trust: a project is trusted unless this is false, which denies what
  -- `mcpp.trust.denyEffects` lists.
  trusted = true,
}

-- The files whose save builds their project when mcpp.build.onSave asks for it (as mcpp-vscode).
local BUILT_ON_SAVE = { '*.cppm', '*.cpp', '*.cc', '*.cxx', '*.ixx', '*.mpp', '*.h', '*.hpp', 'mcpp.toml', 'build.mcpp' }

local attached = false
local said = false

-- What the backend said at its start that the user should hear once (whichever started it): an
-- mcppls that is not what it should be, and the self-check when mcpp.diagnostics.selfCheckOnStartup
-- asks for it.
local function on_started()
  if said or not session.state.ready then
    return
  end
  said = true
  local ls = session.state.info and session.state.info.languageService
  if ls and ls.state ~= 'ok' then
    commands.language_service(ls)
  end
  if session.value('mcpp.diagnostics.selfCheckOnStartup') then
    commands.run('selfCheck')
  end
end

-- The projects the backend watches (their manifests' problems, their result records). When it ends,
-- the next backend, however it is started, learns them again once it is initialized.
local known = {}
local relearn = {}

local function learn(root)
  known[root] = true
  session.request('projects/forFile', { path = root .. '/mcpp.toml' }, function() end)
end

-- A buffer of an mcpp project: the backend starts for its project, or learns of it. A backend that
-- could not start is tried again by `:Mcpp`, not by every buffer entered.
local function enter(a)
  session.attach(a and a.buf)
  local root = commands.root()
  if not root or known[root] then
    return
  end
  if session.state.ready then
    learn(root)
  elseif not session.state.starting and not session.state.failed then
    session.start(root, function(ok)
      known[root] = ok or nil
    end)
  end
end

function M.setup(opts)
  local options = vim.tbl_deep_extend('force', defaults, opts or {})
  session.configure(options)
  -- A backend that could not start is looked for again with the options given now.
  session.state.failed = false
  if not attached then
    attached = true
    diagnostics.attach()
    session.on('ended', function()
      relearn = vim.tbl_extend('force', relearn, known)
      known = {}
    end)
    session.on('started', function()
      local folder = session.state.folder
      if folder then
        known[folder] = true
      end
      for root in pairs(relearn) do
        if not known[root] then
          learn(root)
        end
      end
      relearn = {}
    end)
  end
  vim.api.nvim_create_user_command('Mcpp', function(c)
    commands.run(c.fargs[1], vim.list_slice(c.fargs, 2))
  end, { nargs = '*', complete = commands.complete, desc = 'mcpp: the mcpp-ide command table' })

  local group = vim.api.nvim_create_augroup('mcpp', { clear = true })
  vim.api.nvim_create_autocmd({ 'VimEnter', 'BufEnter', 'DirChanged' }, { group = group, callback = enter })
  vim.api.nvim_create_autocmd('BufWritePost', {
    group = group, pattern = 'mcpp.toml',
    callback = function(a)
      diagnostics.check_manifest(vim.fs.dirname(vim.fs.normalize(vim.api.nvim_buf_get_name(a.buf))))
    end,
  })
  vim.api.nvim_create_autocmd('BufWritePost', {
    group = group, pattern = BUILT_ON_SAVE,
    callback = function()
      if session.state.ready and session.value('mcpp.build.onSave') then
        commands.run('build')
      end
    end,
  })
  vim.api.nvim_create_autocmd('User', { group = group, pattern = 'McppStatus', callback = on_started })
  -- mcppls attached: it serves build.mcpp now, and the backend leaves it what it answers better.
  vim.api.nvim_create_autocmd('LspAttach', {
    group = group,
    callback = function(a)
      local client = vim.lsp.get_client_by_id(a.data and a.data.client_id or -1)
      if client and client.name == 'mcppls' then
        session.push_settings()
      end
    end,
  })
  vim.api.nvim_create_autocmd('VimLeavePre', { group = group, callback = session.stop })
  -- The editor regained focus: a `xlings use` or a changed pin happened outside it (§4.2).
  vim.api.nvim_create_autocmd('FocusGained', {
    group = group,
    callback = function()
      if session.state.ready then
        session.request('project/refresh', {}, function() end)
        session.request('languageService/status', { candidates = session.mcppls_candidates(), root = commands.root() }, function() end)
      end
    end,
  })
  if session.state.ready then
    session.push_settings()
  elseif vim.v.vim_did_enter == 1 then
    enter()
  end
end

-- The statusline component: the target the project builds for, and the jobs running or the last
-- one's end. Empty before the backend runs, outside a project, or with mcpp.ui.statusBar.show off.
--
--   vim.o.statusline = '%f %= %{v:lua.require("mcpp").status()} '
function M.status()
  local s = M.status_data()
  if not s.ready or not s.root or session.value('mcpp.ui.statusBar.show') == false then
    return ''
  end
  local text = 'mcpp ' .. s.target
  if s.running > 0 then
    return text .. ' · ' .. t('{0} job(s) running', s.running)
  end
  if s.last then
    text = text .. ' · ' .. (s.last.kind or '') .. ' ' .. (s.last.status or '')
  end
  return text
end

-- What the statusline shows, as data; `User McppStatus` fires when it changes.
function M.status_data()
  local st = session.state
  local root = commands.root()
  local target = session.value('mcpp.build.target')
  local plan = root and st.plans[root] or nil
  return {
    ready = st.ready,
    root = root,
    target = (target and target ~= '') and target or t('this machine'),
    platform = plan and plan.platformText or nil,
    running = #vim.tbl_keys(st.running),
    last = st.last and { kind = st.last.kind, status = st.last.status, root = st.last.root } or nil,
    languageService = st.info and st.info.languageService or nil,
  }
end

return M
