-- `:Mcpp <subcommand>`: the backend's command table for Neovim (commands/list, editor `nvim`), each
-- row doing what its `action` says in Neovim's terms. Which argv runs, what a cleanup costs, which
-- target a project builds for and what a menu offers are the backend's answers; this module maps
-- them onto buffers, floating windows, vim.ui.select and vim.ui.input.
local session = require('mcpp.session')
local flow = require('mcpp.flow')
local views = require('mcpp.views')
local diagnostics = require('mcpp.diagnostics')

local M = {}
local t = session.t
local unpack = table.unpack or unpack

-- The project a command is for: the nearest mcpp.toml upward from the current file (kept with its
-- buffer), else from the working directory, else the folder the session started for.
function M.root()
  if vim.b.mcpp_root then
    return vim.b.mcpp_root
  end
  local name = vim.bo.buftype == '' and vim.api.nvim_buf_get_name(0) or ''
  local from = { vim.fn.getcwd() }
  if name ~= '' then
    table.insert(from, 1, vim.fs.dirname(name))
  end
  for i, dir in ipairs(from) do
    local found = vim.fs.find('mcpp.toml', { upward = true, path = dir, type = 'file' })[1]
    if found then
      local root = vim.fs.dirname(found)
      if name ~= '' and i == 1 then
        vim.b.mcpp_root = root
      end
      return root
    end
  end
  return session.state.folder
end

local function report(err)
  vim.notify(t('mcpp: {0}', err and err.message or '?'), vim.log.levels.ERROR)
end

-- nvim-dap's adapters, named as they are usually registered, and the backend's debug engine each
-- one speaks: CodeLLDB's own configuration, lldb-dap's (registered as `lldb` too), the C/C++
-- extension's OpenDebugAD7 (`cppdbg`), GDB's own DAP (`gdb -i dap`, GDB 14 and later).
local DAP_ADAPTERS = {
  { 'codelldb', 'lldb' }, { 'lldb-dap', 'lldb-dap' }, { 'lldb', 'lldb-dap' }, { 'cppdbg', 'cppdbg' }, { 'gdb', 'gdb' },
}

local function dap()
  local ok, module = pcall(require, 'dap')
  return ok and module or nil
end

-- What each action kind does. Every handler runs in a flow, with the row and the arguments typed
-- after the subcommand.
local actions = {}

function actions.job(row, args, root)
  local params = { kind = row.action.job, root = root }
  if row.action.job == 'test' and args[1] then
    params.filter = args[1]
  end
  local done = views.job('job/start', params, row.action.job)
  if done and row.action['then'] == 'languageService' then
    actions.languageService()
  end
end

function actions.run(_, args, root)
  vim.api.nvim_echo({ { t('mcpp: building…') } }, false, {})
  local launch, err = session.call('run/launch', { root = root, args = session.value('mcpp.task.runArgs') or {}, bin = args[1] })
  if err then
    if err.data and err.data.narration then
      views.append(err.data.narration)
      views.reveal()
    end
    return report(err)
  end
  views.terminal(launch)
end

function actions.debug(_, args, root)
  local module = dap()
  if not module then
    return vim.notify(t('Debugging in Neovim needs nvim-dap.'), vim.log.levels.WARN)
  end
  local available, adapter = {}, {}
  for _, pair in ipairs(DAP_ADAPTERS) do
    if module.adapters[pair[1]] and not adapter[pair[2]] then
      available[#available + 1], adapter[pair[2]] = pair[2], pair[1]
    end
  end
  local platform = vim.fn.has('mac') == 1 and 'macos' or vim.fn.has('win32') == 1 and 'windows' or 'linux'
  local r, err = session.call('debug/configuration', { root = root, available = available, platform = platform, bin = args[1] })
  if err then
    return report(err)
  end
  local config = r.configuration
  config.type = adapter[r.engine]
  if not config.type then
    local names = table.concat(vim.tbl_map(function(pair) return pair[1] end, DAP_ADAPTERS), ', ')
    return vim.notify(t('nvim-dap has no adapter for {0}; register one under one of these names: {1}.', r.engine, names), vim.log.levels.WARN)
  end
  if config.mcppNote then
    vim.notify('mcpp: ' .. config.mcppNote, vim.log.levels.INFO)
    config.mcppNote = nil
  end
  module.run(config)
end

-- plan -> the confirmation the plan's level asks for -> apply.
function actions.cache(row, _, root)
  local plan, err = session.call('cache/plan', { action = row.action.plan, root = root })
  if err then
    return report(err)
  end
  if plan.refusedUntrusted then
    return vim.notify(t('this action needs a trusted workspace'), vim.log.levels.WARN)
  end
  local confirmation = {}
  if (plan.level or 0) > 0 then
    local preview = plan.preview or {}
    local text = preview.source == 'mcpp' and (preview.text or '')
      or (preview.count and t('About {0} would be freed ({1} entries).', preview.bytesText, preview.count) or '')
    local prompt = table.concat(vim.tbl_filter(function(s) return s ~= '' end, { plan.title or '', plan.detail or '', text }), '\n\n')
    if not flow.confirm(prompt, t('Continue'), t('Cancel')) then
      return
    end
    confirmation = { confirmed = true, previewShown = plan.preview ~= nil }
    if plan.acknowledge then
      local ack = t('I understand this affects every mcpp project on this machine')
      if not flow.confirm(plan.title, ack, t('Cancel')) then
        return
      end
      confirmation.acknowledged = true
    end
  end
  views.job('cache/apply', { action = row.action.plan, root = root, confirmation = session.object(confirmation) }, row.subcommand)
end

-- A panel's view model, as Neovim has it: text, a choice, or a job.
local panels = {}

function panels.targets(row, _, root)
  views.show(row.title, 'targets/list', { root = root }, views.targets_text)
end

function panels.cache(row, _, root)
  views.show(row.title, 'cache/inventory', { root = root }, views.cache_text)
end

function panels.graph(row, _, root)
  views.show(row.title, row.action.graph == 'modules' and 'graph/modules' or 'graph/packages', { root = root }, views.graph_text)
end

-- The pack panel's choices one after another: the format, the closure, the linkage where mcpp takes
-- one; then the pack, and what it wrote.
function panels.pack(row, _, root)
  local r, err = session.call('pack/formats', { root = root })
  if err then
    return report(err)
  end
  local default = t("the project's default")
  local formats = { { label = default } }
  for _, f in ipairs(r.formats or {}) do
    local label = f.builtin and f.name or string.format('%s (%s)', f.name, table.concat(f.providers or {}, ', '))
    formats[#formats + 1] = { name = f.name, label = label }
  end
  local format = flow.select(formats, { prompt = t('Format'), format_item = function(f) return f.label end })
  if not format then
    return
  end
  local mode = flow.select({ default, unpack(r.modes or {}) }, { prompt = t('Mode') })
  if not mode then
    return
  end
  local linkage = default
  if r.linkages and #r.linkages > 0 then
    linkage = flow.select({ default, unpack(r.linkages) }, { prompt = t('Linkage') })
    if not linkage then
      return
    end
  end
  local done = views.job('pack/start', {
    root = root, pkgFormat = format.name, mode = mode ~= default and mode or nil, linkage = linkage ~= default and linkage or nil,
  }, row.subcommand)
  local data = done and done.envelope and done.envelope.data or {}
  for _, out in ipairs(data.outputs or data.artifacts or {}) do
    views.line('  ' .. (type(out) == 'table' and (out.path or vim.inspect(out)) or tostring(out)))
  end
end

-- The libraries panel as a search: the packages that match, then the version to add (`mcpp add`).
function panels.libraries(_, args, root)
  local keyword = args[1] or flow.input({ prompt = t('Search packages') .. ': ' })
  if keyword == nil then
    return
  end
  local r, err = session.call('library/search', { keyword = keyword, limit = 300, root = root })
  if err then
    return report(err)
  end
  if #(r.packages or {}) == 0 then
    return vim.notify(t('No package matches.'), vim.log.levels.INFO)
  end
  local pkg = flow.select(r.packages, {
    prompt = t('{0} packages', r.total),
    format_item = function(p)
      return string.format('%s  %s  %s', p.id, p.latest or '', p.description or '')
    end,
  })
  if not pkg then
    return
  end
  if not root then
    return vim.notify(t('No mcpp project in this workspace'), vim.log.levels.WARN)
  end
  local version = flow.select(pkg.versions or {}, { prompt = t('Add to mcpp.toml') .. ': ' .. (pkg.dependencyName or pkg.name) })
  if not version then
    return
  end
  views.job('library/add', { root = root, dependency = pkg.dependencyName or pkg.name, version = version }, 'add')
end

function actions.panel(row, args, root)
  local panel = panels[row.action.panel]
  if not panel then
    return vim.notify(t('This command is not available in this IDE.'), vim.log.levels.INFO)
  end
  panel(row, args, root)
end

function actions.cancel()
  local ids = vim.tbl_keys(session.state.running)
  if #ids == 0 then
    return vim.notify(t('No mcpp job is running.'), vim.log.levels.INFO)
  end
  for _, id in ipairs(ids) do
    session.call('job/cancel', { jobId = id })
  end
end

function actions.selectTarget(_, _, root)
  local r, err = session.call('targets/list', { root = root })
  if err then
    return report(err)
  end
  local pick = flow.select(r.rows or {}, {
    prompt = t('Select the build target'),
    format_item = function(row)
      return views.targets_text({ rows = { row } })[1]
    end,
  })
  if not pick then
    return
  end
  session.set_setting('mcpp.build.target', pick.host and '' or pick.target)
  if not pick.installed and (pick.pin or '') ~= '' then
    if flow.confirm(t('No toolchain is installed for {0}.', pick.target), t('Install'), t('Cancel')) then
      views.job('toolchains/install', { spec = pick.pin, target = pick.target, root = root }, 'toolchain')
    end
  end
end

function actions.installToolchain(_, args, root)
  local spec = args[1] or flow.input({ prompt = t('Install an mcpp toolchain') .. ' (gcc@16.1.0, llvm@23.1.3): ' })
  if spec and spec ~= '' then
    views.job('toolchains/install', { spec = spec, root = root }, 'toolchain')
  end
end

function actions.selectDefaultToolchain()
  local r, err = session.call('toolchains/list', {})
  if err then
    return report(err)
  end
  local choices = vim.tbl_filter(function(x) return x.target == r.host and (x.spec or '') ~= '' end, r.rows or {})
  local pick = flow.select(choices, {
    prompt = t('Choose the mcpp global default toolchain'),
    format_item = function(x) return string.format('%s%s (%s)', x.default and '✓ ' or '  ', x.spec, x.status or '') end,
  })
  if not pick then
    return
  end
  local _, failed = session.call('toolchains/setDefault', { spec = pick.spec })
  if failed then
    return report(failed)
  end
  vim.notify(t('Global default: {0}', pick.spec), vim.log.levels.INFO)
end

function actions.newProject(_, args)
  local ask = t('New mcpp project (1/2)') .. ' ' .. t("The project's name, which is also its directory") .. ': '
  local name = args[1] or flow.input({ prompt = ask })
  if not name or name == '' then
    return
  end
  ask = t('Choose the project location (2/2)') .. ': '
  local parent = flow.input({ prompt = ask, default = vim.fn.getcwd(), completion = 'dir' })
  if not parent or parent == '' then
    return
  end
  parent = vim.fs.normalize(vim.fn.fnamemodify(parent, ':p'))
  parent = parent:match('^(.-)/*$')
  local done = views.job('project/new', { parent = parent, name = name }, 'new')
  if done and done.status == 'ok' and flow.confirm(t('Created {0}.', name), t('Open'), t('Cancel')) then
    vim.cmd.edit(vim.fn.fnameescape(parent .. '/' .. name .. '/mcpp.toml'))
  end
end

function actions.quickMenu(_, _, root)
  local r, err = session.call('menu/quick', { root = root, editor = 'nvim' })
  if err then
    return report(err)
  end
  local item = flow.select(r.items or {}, {
    prompt = t('Open the mcpp project and toolchain quick menu'),
    format_item = function(i)
      local note = i.enabled and (i.detail or '') or (i.reason or '')
      return string.format('%-40s %s', i.title, note)
    end,
  })
  if item and item.enabled then
    M.dispatch(M.find_id(item.command), {})
  end
end

function actions.selfCheck()
  local r, err = session.call('selfCheck', {})
  if err then
    return report(err)
  end
  local lines = {
    'mcpp: ' .. t('Environment Self-check'),
    string.format('mcpp-nvim %s · Neovim %s · mcpp-ide %s', session.version, session.nvim_version(),
      r.backend and r.backend.version or '?'),
  }
  local level = vim.log.levels.INFO
  for _, check in ipairs(r.checks or {}) do
    lines[#lines + 1] = string.format('%-8s %s', check.status, check.message)
    if check.status == 'error' then
      level = vim.log.levels.ERROR
    elseif check.status == 'warning' and level ~= vim.log.levels.ERROR then
      level = vim.log.levels.WARN
    end
  end
  vim.notify(table.concat(lines, '\n'), level)
end

-- The IDE report (design 2026-10-30 part 2 §3.2): what a bug report needs, written to a file the
-- person names (default: beside the project).
function actions.report(_, args, root)
  local r, err = session.call('report/collect', root and { root = root } or {})
  if err then
    r = { kind = 'mcpp-ide.report', version = 1, error = err.message or tostring(err) }
  end
  local default = (root or vim.fn.getcwd()) .. '/mcpp-ide-report-' .. os.date('!%Y%m%dT%H%M%SZ') .. '.json'
  local path = (args and args[1]) or flow.input({ prompt = t('Export IDE Report') .. ': ', default = default })
  if not path or path == '' then
    return
  end
  local file = io.open(path, 'w')
  if not file then
    return vim.notify('mcpp: ' .. t('cannot write {0}', path), vim.log.levels.ERROR)
  end
  file:write(vim.json.encode(r), '\n')
  file:close()
  vim.notify('mcpp: ' .. t('The IDE report is written to {0}', path), vim.log.levels.INFO)
end

-- Which mcppls answers C++ files (languageService/status), said: in Neovim a missing or older mcppls
-- comes with its own plugin, mcppls-nvim.
function M.language_service(s)
  if s.state == 'ok' then
    return vim.notify('mcpp: ' .. s.message, vim.log.levels.INFO)
  end
  vim.notify('mcpp: ' .. s.message .. '\n' .. t('mcppls for Neovim: `xlings install mcppls-nvim`'), vim.log.levels.WARN)
end

function actions.languageService()
  local s, err = session.call('languageService/status', { candidates = session.mcppls_candidates() })
  if err then
    return report(err)
  end
  M.language_service(s)
end

function actions.rpc(row, _, root)
  views.job(row.action.method, { root = root }, row.subcommand)
end

function actions.problems()
  diagnostics.quickfix()
end

-- ── the table ─────────────────────────────────────────────────────────────────────────────────

-- The rows Neovim offers now: not deprecated, not hidden from a palette, and the debugger only when
-- nvim-dap is there.
function M.offered()
  local out = {}
  for _, row in ipairs(session.state.commands) do
    local debug = row.action and row.action.kind == 'debug'
    if not row.deprecated and row.palette ~= false and (not debug or dap()) then
      out[#out + 1] = row
    end
  end
  return out
end

function M.find(subcommand)
  for _, row in ipairs(M.offered()) do
    if row.subcommand == subcommand then
      return row
    end
  end
end

-- A row by its id, offered now or not: the quick menu names rows by id, and a row Neovim leaves out
-- of its table now (the debugger without nvim-dap) says why when it is chosen.
function M.find_id(id)
  for _, row in ipairs(session.state.commands) do
    if row.id == id then
      return row
    end
  end
end

-- Inside a flow: does what the row says.
function M.dispatch(row, args)
  if not row then
    return
  end
  local root = M.root()
  if (row.when or ''):find('mcpp.inProject', 1, true) and not root then
    return vim.notify(t('No mcpp project in this workspace'), vim.log.levels.WARN)
  end
  local action = actions[row.action.kind]
  if not action then
    return vim.notify(t('This command is not available in this IDE.'), vim.log.levels.INFO)
  end
  action(row, args, root)
end

-- `:Mcpp [subcommand] [arguments]`; with no subcommand, the quick menu.
function M.run(subcommand, args)
  flow.run(function()
    if not session.ensure(M.root()) then
      return
    end
    local row
    if not subcommand or subcommand == '' then
      row = vim.tbl_filter(function(r) return r.action.kind == 'quickMenu' end, M.offered())[1]
    else
      row = M.find(subcommand)
    end
    if not row then
      return vim.notify(t('There is no mcpp command {0}.', subcommand or ''), vim.log.levels.ERROR)
    end
    M.dispatch(row, args or {})
  end)
end

-- The subcommands, for `:Mcpp <Tab>`: the backend's table, read once it has started (completion waits
-- for it a little, starting it when it is not running).
function M.complete(lead, line, pos)
  if #vim.split(line:sub(1, pos), '%s+') > 2 then
    return {}
  end
  if not session.state.ready then
    session.start(M.root())
    vim.wait(5000, function() return session.state.ready or not session.state.starting end, 20)
  end
  local names = {}
  for _, row in ipairs(M.offered()) do
    if vim.startswith(row.subcommand, lead) then
      names[#names + 1] = row.subcommand
    end
  end
  table.sort(names)
  return names
end

return M
