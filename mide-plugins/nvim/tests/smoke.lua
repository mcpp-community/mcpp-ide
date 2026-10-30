-- mcpp-nvim end to end in a headless Neovim, against the backend this repository built:
--
--   nvim --headless --clean -l mide-plugins/nvim/tests/smoke.lua
--
-- The backend is MCPP_IDE_BACKEND, else the newest `mcpp-ide` under target/; mcpp is the mock this
-- repository built (`mcpp-ide-mock-mcpp`, replaying protocol/fixtures), or the real mcpp
-- MCPP_NVIM_MCPP names (then a project is really built, with the toolchain MCPP_NVIM_TOOLCHAIN,
-- default gcc@16.1.0, and what only the mock can play -- a cancel mid-build, an index search, a pack --
-- is left out). Each check prints PASS or FAIL with what it saw; the exit status is the number of
-- failures.

local uv = vim.uv or vim.loop
local here = vim.fn.fnamemodify(debug.getinfo(1, 'S').source:sub(2), ':p:h')
local plugin = vim.fn.fnamemodify(here, ':h')
local repo = vim.fn.fnamemodify(plugin, ':h:h')
local is_windows = vim.fn.has('win32') == 1
local exe = is_windows and '.exe' or ''

-- Straight to standard output, a line each: Neovim's own messages (print) share lines with redraws.
local function say(text)
  io.stdout:write(text, '\n')
  io.stdout:flush()
end

local failures = 0
local function check(name, ok, detail)
  say(string.format('%s %s%s', ok and 'PASS' or 'FAIL', name, detail ~= nil and (': ' .. tostring(detail)) or ''))
  if not ok then
    failures = failures + 1
  end
end

-- The newest program of that name the build produced (target/<triple>/<fingerprint>/bin/).
local function built(name)
  local best, when
  for _, path in ipairs(vim.fn.glob(repo .. '/target/*/*/bin/' .. name .. exe, false, true)) do
    local stat = uv.fs_stat(path)
    if stat and (not when or stat.mtime.sec > when) then
      best, when = path, stat.mtime.sec
    end
  end
  return best
end

local backend = os.getenv('MCPP_IDE_BACKEND') or built('mcpp-ide')
local real = os.getenv('MCPP_NVIM_MCPP')
local mcpp = (real and real ~= '') and real or built('mcpp-ide-mock-mcpp')
local mock = not (real and real ~= '')
if not backend or not mcpp then
  io.stderr:write('build the backend first (mcpp build), or name it with MCPP_IDE_BACKEND\n')
  os.exit(2)
end
say(string.format('backend %s, mcpp %s (%s)', backend, mcpp, mock and 'mock' or 'real'))

-- A project of its own, so nothing a build writes lands in the repository.
local root = vim.fs.normalize(vim.fn.tempname() .. '-mcpp-nvim')
local function write(path, text)
  vim.fn.mkdir(vim.fs.dirname(root .. '/' .. path), 'p')
  local f = assert(io.open(root .. '/' .. path, 'wb'))
  f:write(text)
  f:close()
end
local manifest = '[package]\nname = "app"\nversion = "0.1.0"\n'
if not mock then
  manifest = manifest .. string.format('\n[target.\'cfg(linux)\']\ntoolchain = "%s"\n', os.getenv('MCPP_NVIM_TOOLCHAIN') or 'gcc@16.1.0')
end
write('mcpp.toml', manifest)
write('src/main.cpp', 'int main() { return 0; }\n')
write('tests/test_smoke.cpp', 'int main() { return 0; }\n')
-- The mock's builds take a moment, so one can be cancelled while it runs.
if mock then
  vim.env.MOCK_MCPP_SLOW_MS = '1200'
end
vim.env.XDG_STATE_HOME = root .. '/.state'
vim.fn.chdir(root)

-- What the plugin says, and what it asks.
local said = {}
vim.notify = function(message, level)
  said[#said + 1] = message
  say(string.format('  notify[%s] %s', level or '-', (message:gsub('\n', ' | '))))
end
local function heard(pattern)
  for _, m in ipairs(said) do
    if m:find(pattern, 1, true) then
      return m
    end
  end
end
local answers = {}
local asked = {}
vim.ui.select = function(items, opts, done)
  asked[#asked + 1] = { prompt = opts.prompt, items = items }
  local pick = table.remove(answers, 1)
  done(type(pick) == 'function' and pick(items) or pick)
end
vim.ui.input = function(opts, done)
  asked[#asked + 1] = { prompt = opts.prompt }
  done(table.remove(answers, 1))
end
local function first(items) return items[1] end

vim.opt.runtimepath:prepend(plugin)
local mcppide = require('mcpp')
local session = require('mcpp.session')
local diagnostics = require('mcpp.diagnostics')
local views = require('mcpp.views')

-- ── no backend ────────────────────────────────────────────────────────────────────────────────
-- PATH without any mcpp-ide: the plugin says what installs it.
local path = vim.env.PATH
vim.env.PATH = table.concat(vim.tbl_filter(function(dir)
  return vim.fn.executable(dir .. '/mcpp-ide') == 0
end, vim.split(path, is_windows and ';' or ':', { plain = true })), is_windows and ';' or ':')
mcppide.setup({ settings = { ['mcpp.path'] = mcpp } })
vim.cmd('Mcpp build')
vim.wait(2000, function() return heard('xlings install mcpp-ide') ~= nil end, 20)
check('without a backend the plugin names `xlings install mcpp-ide`', heard('xlings install mcpp-ide') ~= nil)
check('and starts nothing', not session.state.ready and session.state.client == nil)
vim.env.PATH = path

-- ── a backend that ends while it starts ───────────────────────────────────────────────────────
-- mcpp, which answers `serve` by ending: the start fails, and nothing is left waiting on it.
mcppide.setup({ backend = { mcpp }, settings = { ['mcpp.path'] = mcpp } })
local started
session.start(root, function(ok) started = ok end)
vim.wait(10000, function() return started ~= nil end, 20)
check('a backend that ends while it starts fails the start', started == false, started)
check('and leaves no start in progress', not session.state.starting and session.state.client == nil and next(session.state.pending) == nil)

-- ── start, initialize, the command table ──────────────────────────────────────────────────────
mcppide.setup({ backend = backend, settings = { ['mcpp.path'] = mcpp, ['mcpp.task.focusTerminal'] = false } })
vim.cmd.edit(vim.fn.fnameescape(root .. '/src/main.cpp'))
check('a buffer of the project starts the backend',
  vim.wait(30000, function() return session.state.ready end, 20), session.state.failed and 'failed' or nil)
local info = session.state.info or {}
check('initialize answers as mcpp-ide, protocol 1', (info.server or {}).name == 'mcpp-ide' and info.protocol == 1, vim.inspect(info.server))
check('the project is the nearest mcpp.toml upward', info.projects and info.projects[1] and vim.fs.normalize(info.projects[1].root) == root,
  info.projects and info.projects[1] and info.projects[1].root)
check('mcpp is the one setup() names', info.mcpp and vim.fs.normalize(info.mcpp.path) == vim.fs.normalize(mcpp), info.mcpp and info.mcpp.path)

local by = {}
for _, row in ipairs(session.state.commands) do
  by[row.subcommand] = row
end
check('the command table arrives, each row with its subcommand', #session.state.commands > 20 and by.build ~= nil, #session.state.commands)
check('cancel and problems are Neovim\'s, settings is not', by.cancel and by.cancel.id == 'mcpp.cancelJob' and by.problems ~= nil and by.openSettings == nil)
local editors = vim.tbl_filter(function(r) return r.action.kind == 'editor' end, session.state.commands)
check('no command of another editor', #editors == 0, #editors)
-- mcppls's actions only under the mcppls-nvim command mcppls states; offered only where it is defined.
local forwards = vim.tbl_filter(function(r) return r.action.kind == 'forward' end, session.state.commands)
check('a forward names mcppls-nvim\'s command', vim.tbl_isempty(vim.tbl_filter(function(r)
  return not (r.action.command or ''):match('^Mcppls') end, forwards)), vim.inspect(forwards))
check('a forward whose command is not defined is not offered', vim.tbl_isempty(vim.tbl_filter(function(r)
  return r.action.kind == 'forward' and vim.fn.exists(':' .. r.action.command) ~= 2 end, require('mcpp.commands').offered())))

-- ── completion ────────────────────────────────────────────────────────────────────────────────
local all = vim.fn.getcompletion('Mcpp ', 'cmdline')
check(':Mcpp completes the subcommands', vim.tbl_contains(all, 'build') and vim.tbl_contains(all, 'test') and vim.tbl_contains(all, 'cancel')
  and vim.tbl_contains(all, 'problems') and vim.tbl_contains(all, 'library.search'), #all)
check(':Mcpp bu completes build', vim.deep_equal(vim.fn.getcompletion('Mcpp bu', 'cmdline'), { 'build' }), vim.inspect(vim.fn.getcompletion('Mcpp bu', 'cmdline')))
check('no debug without nvim-dap', not vim.tbl_contains(all, 'debug'))

-- ── a build ───────────────────────────────────────────────────────────────────────────────────
local function finished(kind, after)
  vim.wait(mock and 20000 or 600000, function()
    local last = session.state.last
    return last ~= nil and last ~= after and last.kind == kind
  end, 50)
  return session.state.last
end
local function output()
  return table.concat(views.lines(), '\n')
end

local before = session.state.last
vim.cmd('Mcpp build')
check('a build runs (the statusline says so)', vim.wait(10000, function() return mcppide.status():find('1 job', 1, true) ~= nil end, 20),
  mcppide.status())
local done = finished('build', before)
check('the build finishes ok', done and done.status == 'ok', done and done.status)
check('its narration is streamed into mcpp://output', output():find('$ ', 1, true) and output():find('build', 1, true)
  and output():find('Compiling', 1, true) ~= nil, output():sub(1, 200))
check('its end is said there', output():find('mcpp build: ok', 1, true) ~= nil)
check('the statusline shows the last build', mcppide.status():find('build ok', 1, true) ~= nil, mcppide.status())

-- ── diagnostics ───────────────────────────────────────────────────────────────────────────────
write('src/main.cpp', 'int main() { return undeclared_name; }\n')
write('.mock-mcpp-fail', 'build\n')
vim.cmd('checktime')
before = session.state.last
vim.cmd('Mcpp build')
done = finished('build', before)
check('a build with an error fails', done and done.status == 'failed', done and done.status)
local main = vim.fn.bufnr(root .. '/src/main.cpp')
local found = vim.diagnostic.get(main, { namespace = diagnostics.build })
check('its problem lands in the `mcpp` namespace, on main.cpp', #found >= 1 and found[1].message:find('undeclared_name', 1, true) ~= nil
  and found[1].lnum == 0 and found[1].severity == vim.diagnostic.severity.ERROR, found[1] and found[1].message)
vim.cmd('Mcpp problems')
local qf
vim.wait(5000, function()
  qf = vim.fn.getqflist()
  return #qf > 0
end, 20)
check(':Mcpp problems fills the quickfix list', #qf >= 1 and vim.fn.bufname(qf[1].bufnr):find('main.cpp', 1, true) ~= nil, #qf)

write('src/main.cpp', 'int main() { return 0; }\n')
write('.mock-mcpp-fail', 'manifest\n')
vim.cmd.edit(vim.fn.fnameescape(root .. '/mcpp.toml'))
vim.api.nvim_buf_set_lines(0, 0, 1, false, { '[package' })
vim.cmd('silent write')
local toml = vim.api.nvim_get_current_buf()
check('a broken mcpp.toml gets mcpp\'s problem in the `mcpp.toml` namespace',
  vim.wait(mock and 10000 or 120000, function() return #vim.diagnostic.get(toml, { namespace = diagnostics.manifest }) > 0 end, 50),
  (vim.diagnostic.get(toml, { namespace = diagnostics.manifest })[1] or {}).message)
vim.api.nvim_buf_set_lines(0, 0, 1, false, { '[package]' })
vim.cmd('silent write')
os.remove(root .. '/.mock-mcpp-fail')
vim.cmd.edit(vim.fn.fnameescape(root .. '/src/main.cpp'))

-- ── the backend's views as text ───────────────────────────────────────────────────────────────
local function float_text()
  local text
  vim.wait(mock and 5000 or 60000, function()
    local win = vim.api.nvim_get_current_win()
    if vim.api.nvim_win_get_config(win).relative ~= '' then
      text = table.concat(vim.api.nvim_buf_get_lines(0, 0, -1, false), '\n')
    end
    return text ~= nil
  end, 20)
  if text then
    vim.cmd('close')
  end
  return text or ''
end
vim.cmd('Mcpp showTargets')
local targets = float_text()
check(':Mcpp showTargets lists the targets in a float', targets:find('linux', 1, true) ~= nil, targets:sub(1, 120))
vim.cmd('Mcpp showCachePanel')
check(':Mcpp showCachePanel shows the cache by kind', float_text():find('By kind', 1, true) ~= nil)
vim.cmd('Mcpp project.showDependencyGraph')
check(':Mcpp project.showDependencyGraph shows the packages', float_text() ~= '')

-- ── run, debug ────────────────────────────────────────────────────────────────────────────────
-- The mock's launch names the program its fixture's build wrote; here it is a script that says hello.
if mock and not is_windows then
  write('target/x86_64-linux-gnu/77ba4e6c37abd416/bin/app', '#!/bin/sh\necho "hello from app"\n')
  uv.fs_chmod(root .. '/target/x86_64-linux-gnu/77ba4e6c37abd416/bin/app', 493)
end
local windows = #vim.api.nvim_list_wins()
vim.cmd('Mcpp run')
local term
vim.wait(mock and 10000 or 300000, function()
  for _, buf in ipairs(vim.api.nvim_list_bufs()) do
    if vim.bo[buf].buftype == 'terminal' and vim.b[buf].mcpp_program then
      term = buf
    end
  end
  return term ~= nil
end, 20)
check(':Mcpp run starts the launch in a terminal buffer', term ~= nil and #vim.api.nvim_list_wins() > windows, term and vim.b[term].mcpp_program)
if term and (mock and not is_windows) then
  local hello = vim.wait(10000, function()
    return table.concat(vim.api.nvim_buf_get_lines(term, 0, -1, false), '\n'):find('hello from app', 1, true) ~= nil
  end, 50)
  check('the program runs there', hello)
end

local launched
package.loaded.dap = { adapters = { codelldb = {} }, run = function(config) launched = config end }
check('debug is offered once nvim-dap is there', vim.tbl_contains(vim.fn.getcompletion('Mcpp de', 'cmdline'), 'debug'))
vim.cmd('Mcpp debug')
vim.wait(mock and 10000 or 300000, function() return launched ~= nil end, 20)
check(':Mcpp debug hands nvim-dap the backend\'s configuration, as the adapter registered',
  launched and launched.type == 'codelldb' and launched.request == 'launch' and (launched.program or '') ~= '', launched and launched.program)
package.loaded.dap = nil

-- ── a cleanup, the quick menu, the language service, the self-check ───────────────────────────
answers = { first, first }
before = session.state.last
vim.cmd('Mcpp clean')
done = finished('clean', before)
check(':Mcpp clean confirms the plan and applies it as a job', done and done.status == 'ok' and #asked > 0, done and done.status)

answers = { nil }
asked = {}
said = {}
vim.cmd('Mcpp')
vim.wait(5000, function() return #asked > 0 end, 20)
check(':Mcpp alone opens the quick menu', #asked == 1 and #asked[1].items > 5, #asked > 0 and #asked[1].items or 0)

require('mcpp.commands').language_service({ state = 'missing', message = 'mcppls was not found', actions = { { id = 'install', title = 'Install mcppls' } } })
check('a missing mcppls is said with `xlings install mcppls-nvim`', heard('xlings install mcppls-nvim') ~= nil)
said = {}
vim.cmd('Mcpp languageServer.refreshState')
vim.wait(10000, function() return heard('mcppls') ~= nil end, 20)
check(':Mcpp languageServer.refreshState says which mcppls answers', heard('mcppls') ~= nil)
said = {}
vim.cmd('Mcpp selfCheck')
vim.wait(20000, function() return heard('Self-check') ~= nil end, 20)
local report = heard('Self-check') or ''
check(':Mcpp selfCheck reports the backend, mcpp and the checks', report:find('mcpp-nvim ', 1, true) and report:find('mcpp-ide ', 1, true)
  and report:find(vim.fs.basename(mcpp), 1, true) ~= nil, report:sub(1, 120))

-- The IDE report (design 2026-10-30 part 2 §3.2), to a file named on the command line.
said = {}
local report_path = vim.fn.tempname() .. '-mcpp-ide-report.json'
vim.cmd('Mcpp exportReport ' .. vim.fn.fnameescape(report_path))
vim.wait(20000, function() return heard('IDE report') ~= nil end, 20)
local written = vim.fn.filereadable(report_path) == 1 and vim.json.decode(table.concat(vim.fn.readfile(report_path), '\n')) or {}
check(':Mcpp exportReport writes the report the backend collects', written.kind == 'mcpp-ide.report' and written.backend ~= nil,
  vim.inspect(written):sub(1, 120))

-- ── what only the mock plays ──────────────────────────────────────────────────────────────────
if mock then
  before = session.state.last
  vim.cmd('Mcpp build')
  vim.wait(10000, function() return next(session.state.running) ~= nil end, 20)
  vim.cmd('Mcpp cancel')
  done = finished('build', before)
  check(':Mcpp cancel ends the running build', done and done.status == 'cancelled', done and done.status)

  answers = { first, first }
  before = session.state.last
  vim.cmd('Mcpp library.search boost')
  done = finished('add', before)
  check(':Mcpp library.search adds the package chosen (mcpp add)', done and done.status == 'ok' and output():find('add', 1, true) ~= nil,
    done and done.status)

  answers = { first, first, first }
  before = session.state.last
  vim.cmd('Mcpp pack')
  done = finished('pack', before)
  check(':Mcpp pack runs mcpp pack as a job', done ~= nil and done.kind == 'pack', done and done.status)

  -- The backend ends during a build: the build ends as failed, and the next command starts another.
  local function count(text)
    local n = 0
    for _ in output():gmatch(vim.pesc(text)) do
      n = n + 1
    end
    return n
  end
  local failed_before = count('mcpp build: failed')
  vim.cmd('Mcpp build')
  vim.wait(10000, function() return next(session.state.running) ~= nil end, 20)
  session.state.client.terminate()
  vim.wait(10000, function() return count('mcpp build: failed') > failed_before end, 20)
  check('a backend that ends mid-build ends the build as failed', count('mcpp build: failed') > failed_before, count('mcpp build: failed'))
  check('and is said to have ended', heard('The mcpp-ide backend ended.') ~= nil)
  before = session.state.last
  vim.cmd('Mcpp build')
  done = finished('build', before)
  check('the next command starts another backend, which builds', session.state.ready and done ~= nil, done and done.status)
end

-- ── the target, last: a build after it would build for it ─────────────────────────────────────
answers = { function(items) return vim.tbl_filter(function(r) return not r.host end, items)[1] end, nil }
vim.cmd('Mcpp selectTarget')
vim.wait(10000, function() return (session.value('mcpp.build.target') or '') ~= '' end, 20)
local chosen = session.value('mcpp.build.target') or ''
check(':Mcpp selectTarget sets mcpp.build.target, and the statusline names it', chosen ~= '' and mcppide.status():find(chosen, 1, true) ~= nil,
  mcppide.status())

session.stop()
check('the backend ends on shutdown and exit', session.state.client == nil)
vim.fn.delete(root, 'rf')
say(string.format('%d failure(s)', failures))
os.exit(failures)
