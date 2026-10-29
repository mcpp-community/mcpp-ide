-- The session with the backend: one `mcpp-ide serve` per Neovim, spoken to through Neovim's own
-- JSON-RPC client (vim.lsp.rpc, which frames each message with Content-Length as the protocol
-- does). Started for the nearest directory upward that holds an mcpp.toml, initialized as the
-- editor `nvim` with the locale, trust, folders and setup()'s settings; then the runtime strings and
-- the command table are read from it. Everything else reaches the backend through this module.
local flow = require('mcpp.flow')

local M = {}

local uv = vim.uv or vim.loop
local is_windows = vim.fn.has('win32') == 1

-- mcpp-nvim's version: mcpp-ide's, whose release carries it.
M.version = '0.1.1'

-- Neovim's version, as its release names it.
function M.nvim_version()
  local v = vim.version()
  return string.format('%d.%d.%d', v.major, v.minor, v.patch)
end

-- What one session holds. `waiters` are called once it is initialized (or cannot be).
local state = {
  options = { settings = {}, trusted = true },
  client = nil,
  folder = nil,
  ready = false,
  starting = false,
  failed = false,
  waiters = {},
  info = nil,
  strings = {},
  commands = {},
  values = {},
  listeners = {},
  claimed = {},
  unclaimed = {},
  running = {},
  last = nil,
  plans = {},
  stopping = false,
}
M.state = state

-- A runtime string in the session's language (the backend's bundle), `{0}`, `{1}` replaced. Before
-- the backend answers, the English text.
function M.t(key, ...)
  local text = state.strings[key] or key
  for i = 1, select('#', ...) do
    text = text:gsub('{' .. (i - 1) .. '}', (tostring(select(i, ...)):gsub('%%', '%%%%')))
  end
  return text
end
local t = M.t

-- JSON's null as nothing: Neovim decodes it as vim.NIL (0.12), or as nil only in objects (0.10).
local function denull(value)
  if value == vim.NIL then
    return nil
  end
  if type(value) == 'table' then
    for k, v in pairs(value) do
      value[k] = denull(v)
    end
  end
  return value
end

-- An empty table as the JSON object `{}`, not `[]`.
local function object(value)
  if value == nil or (type(value) == 'table' and next(value) == nil) then
    return vim.empty_dict()
  end
  return value
end
M.object = object

function M.configure(options)
  state.options = options
end

-- Where the backend is, as the command that starts it: setup's `backend` (a program, an ok-image
-- bundle `*.okb` that okrun on PATH starts, or an argv), else `mcpp-ide` on PATH, which
-- `xlings install mcpp-ide` puts there (okrun on its bundle). Nil when neither is found.
function M.locate(backend)
  if type(backend) == 'table' and #backend > 0 then
    return vim.deepcopy(backend)
  end
  local program
  if type(backend) == 'string' and backend ~= '' then
    program = vim.fn.expand(backend)
    if program:match('%.okb$') then
      local okrun = vim.fn.exepath('okrun')
      return okrun ~= '' and { okrun, program } or nil
    end
    program = vim.fn.exepath(program) ~= '' and vim.fn.exepath(program) or program
  else
    program = vim.fn.exepath('mcpp-ide')
    if program == '' then
      return nil
    end
  end
  -- A batch file (the xlings command on Windows, `mcpp-ide.cmd`) runs through the command interpreter.
  local extension = program:lower():match('%.(%w+)$')
  if is_windows and (extension == 'cmd' or extension == 'bat') then
    return { 'cmd.exe', '/c', program }
  end
  return { program }
end

-- The language: mcpp.ui.language when it names one, else Neovim's (v:lang).
local function locale()
  local chosen = state.options.settings['mcpp.ui.language']
  if type(chosen) == 'string' and chosen ~= '' and chosen ~= 'auto' then
    return chosen
  end
  return vim.v.lang ~= '' and vim.v.lang or 'en'
end

-- Neovim has no workspace trust: a project is trusted unless setup says `trusted = false`, which
-- denies what mcpp.trust.denyEffects lists.
local function trusted()
  return state.options.trusted ~= false
end

-- The mcppls Neovim starts for C++ (mcppls-nvim's client): its payload directory when it runs a
-- payload's image, else its program; then MCPPLS_SERVER and MCPPLS_PAYLOAD. PATH, xlings and the user
-- data directory's payload are the backend's to try after these.
function M.mcppls_candidates()
  local out = {}
  local get = vim.lsp.get_clients or vim.lsp.get_active_clients
  for _, client in ipairs(get({ name = 'mcppls' })) do
    local cmd = type(client.config.cmd) == 'table' and client.config.cmd or {}
    local image = vim.tbl_filter(function(part)
      return type(part) == 'string' and part:match('%.okb$') ~= nil
    end, cmd)[1]
    local payload = image and vim.fs.dirname(vim.fs.dirname(image))
    if payload and uv.fs_stat(payload .. '/payload.json') then
      out[#out + 1] = payload
    elseif type(cmd[1]) == 'string' then
      out[#out + 1] = vim.fn.exepath(cmd[1]) ~= '' and vim.fn.exepath(cmd[1]) or cmd[1]
    end
  end
  for _, name in ipairs({ 'MCPPLS_SERVER', 'MCPPLS_PAYLOAD' }) do
    local value = os.getenv(name)
    if value and value ~= '' then
      out[#out + 1] = value
    end
  end
  return out
end

function M.on(method, fn)
  state.listeners[method] = state.listeners[method] or {}
  state.listeners[method][fn] = true
  return function()
    state.listeners[method][fn] = nil
  end
end

local function emit(method, params)
  for fn in pairs(state.listeners[method] or {}) do
    local ok, err = pcall(fn, params)
    if not ok then
      vim.notify(string.format('mcpp: %s: %s', method, err), vim.log.levels.ERROR)
    end
  end
end

-- The statusline's data changed.
local function changed()
  pcall(vim.api.nvim_exec_autocmds, 'User', { pattern = 'McppStatus', modeline = false })
  pcall(vim.cmd.redrawstatus)
end
M.changed = changed

-- ── jobs ──────────────────────────────────────────────────────────────────────────────────────
-- A job's notifications can arrive before the reply that names its id (the job's thread and the
-- reply race), so what carries an id not yet claimed is kept, and replayed to the handle that
-- claims it.

local Handle = {}
Handle.__index = Handle

-- Inside a flow: the job's `job/finished`.
function Handle:wait()
  if self.done then
    return self.done
  end
  return flow.wait(function(done)
    self.waiters[#self.waiters + 1] = done
  end)
end

local function deliver(owner, method, params)
  local on = owner.events
  if method == 'job/output' and on.output then
    on.output(params.text or '')
  elseif method == 'job/started' and on.started then
    on.started(params)
  elseif method == 'job/finished' then
    state.claimed[params.jobId] = nil
    owner.handle.done = params
    for _, done in ipairs(owner.handle.waiters) do
      done(params)
    end
  end
end

local function route(method, params)
  local id = params.jobId
  if not id then
    return
  end
  if method == 'job/started' then
    state.running[id] = params
    changed()
  elseif method == 'job/finished' then
    state.running[id] = nil
    state.last = params
    changed()
  elseif method == 'job/plan' then
    state.plans[params.root or ''] = params
  end
  local owner = state.claimed[id]
  if owner then
    deliver(owner, method, params)
    return
  end
  local kept = state.unclaimed[id] or {}
  kept[#kept + 1] = { method, params }
  state.unclaimed[id] = kept
  -- A job nobody claims (one an agent or another command started) is kept only until it ends.
  if method == 'job/finished' then
    vim.defer_fn(function()
      state.unclaimed[id] = nil
    end, 30000)
  end
end

local JOB_EVENTS = {
  ['job/started'] = true, ['job/output'] = true, ['job/plan'] = true, ['job/progress'] = true,
  ['job/diagnostic'] = true, ['job/artifact'] = true, ['job/record'] = true, ['job/finished'] = true,
  ['test/result'] = true, ['test/summary'] = true,
}

-- Inside a flow: asks for a job (`job/start`, or another method that answers a job id) and follows
-- it. `events`: output(text), started(params); the handle's wait() answers its end. Returns the
-- handle, or nil and the error.
function M.job(method, params, events)
  local result, err = M.call(method, params)
  if not result then
    return nil, err
  end
  local handle = setmetatable({ id = result.jobId, result = result, waiters = {} }, Handle)
  local owner = { events = events or {}, handle = handle }
  state.claimed[handle.id] = owner
  for _, kept in ipairs(state.unclaimed[handle.id] or {}) do
    deliver(owner, kept[1], kept[2])
  end
  state.unclaimed[handle.id] = nil
  return handle
end

-- ── the wire ──────────────────────────────────────────────────────────────────────────────────

-- A request; `callback(err, result)` on the main loop.
function M.request(method, params, callback)
  local client = state.client
  if not client or client.is_closing() then
    callback({ code = -32099, message = t('the mcpp-ide backend is not running') })
    return
  end
  local ok = client.request(method, object(params), function(err, result)
    callback(denull(err), denull(result))
  end)
  if not ok then
    callback({ code = -32099, message = t('the mcpp-ide backend is not running') })
  end
end

-- Inside a flow: a request's result and its error (one of them nil; a result may be null too).
function M.call(method, params)
  local err, result = flow.wait(function(done)
    M.request(method, params, done)
  end)
  return result, err
end

-- A setting as the backend resolved it (spec/settings.json's default when not set).
function M.value(key)
  local v = state.values[key]
  if v == nil then
    v = state.options.settings[key]
  end
  return v
end

-- settings/update's parameters: setup()'s settings, the trust, the language.
local function settings_params()
  return { settings = object(state.options.settings), trusted = trusted(), locale = locale() }
end

-- The settings changed (setup again, a target chosen): the backend validates and resolves them.
function M.push_settings()
  if not state.ready then
    return
  end
  M.request('settings/update', settings_params(), function(err, resolved)
    if not err and resolved then
      state.values = resolved.values or {}
      changed()
    end
  end)
end

function M.set_setting(key, value)
  state.options.settings[key] = value
  M.push_settings()
end

local function finish_start(ok)
  state.starting = false
  state.ready = ok
  state.failed = not ok
  local waiters = state.waiters
  state.waiters = {}
  for _, waiter in ipairs(waiters) do
    waiter(ok)
  end
  changed()
end

-- The settings the backend did not accept, said once: their defaults are used.
local function report_rejected(resolved)
  for _, r in ipairs(resolved.rejected or {}) do
    local why = t('Not accepted ({0}); expected {1}. The default is used.', r.reason, r.expectation)
    vim.notify(string.format('mcpp: %s: %s', r.key, why), vim.log.levels.WARN)
  end
end

local function initialize()
  local info, err = M.call('initialize', {
    client = { name = 'Neovim', version = M.nvim_version() },
    editor = 'nvim',
    locale = locale(),
    trusted = trusted(),
    settings = object(state.options.settings),
    workspaceFolders = state.folder and { state.folder } or {},
    languageService = { candidates = M.mcppls_candidates() },
  })
  if not info then
    vim.notify('mcpp-ide: initialize failed: ' .. tostring(err and err.message), vim.log.levels.ERROR)
    local client = state.client
    state.client = nil
    if client then
      client.terminate()
    end
    return finish_start(false)
  end
  state.info = info
  state.strings = M.call('i18n/bundle', {}) or {}
  local listed = M.call('commands/list', { editor = 'nvim' })
  state.commands = listed and listed.commands or {}
  local resolved = M.call('settings/update', settings_params())
  if resolved then
    state.values = resolved.values or {}
    report_rejected(resolved)
  end
  -- What cannot work, said once; the language service is said with what installs it (init.lua).
  for _, check in ipairs(info.checks or {}) do
    if check.status == 'error' and check.id ~= 'languageService' then
      vim.notify('mcpp: ' .. check.message, vim.log.levels.ERROR)
    end
  end
  finish_start(true)
end

local function on_exit(client, code, signal)
  vim.schedule(function()
    if state.client ~= client then
      return
    end
    state.client, state.ready, state.running = nil, false, {}
    changed()
    if not state.stopping then
      vim.notify(string.format('mcpp: %s (%s)', t('The mcpp-ide backend ended.'), signal ~= 0 and ('signal ' .. signal) or code),
        vim.log.levels.WARN)
    end
  end)
end

-- Starts the backend for `folder` (a project root, or nil). `done(ok)` once it is initialized.
function M.start(folder, done)
  done = done or function() end
  if state.ready and state.client and not state.client.is_closing() then
    return done(true)
  end
  state.waiters[#state.waiters + 1] = done
  if state.starting then
    return
  end
  local cmd = M.locate(state.options.backend)
  if not cmd then
    vim.notify(t("The mcpp-ide backend was not found. Install it with `xlings install mcpp-ide`, or name it with setup()'s `backend`."),
      vim.log.levels.ERROR)
    return finish_start(false)
  end
  state.starting, state.stopping, state.folder = true, false, folder
  cmd[#cmd + 1] = 'serve'
  local client
  local ok, err = pcall(function()
    client = vim.lsp.rpc.start(cmd, {
      notification = function(method, params)
        params = denull(params) or {}
        if JOB_EVENTS[method] then
          route(method, params)
        end
        emit(method, params)
      end,
      on_exit = function(code, signal)
        on_exit(client, code, signal)
      end,
    }, folder and { cwd = folder } or nil)
  end)
  if not ok then
    vim.notify(t('The mcpp-ide backend could not be started: {0}', tostring(err)), vim.log.levels.ERROR)
    return finish_start(false)
  end
  state.client = client
  flow.run(initialize)
end

-- Inside a flow: the session, started for the current project when it is not running; nil when it
-- cannot start (what is missing has been said).
function M.ensure(folder)
  local ok = flow.wait(function(done)
    M.start(folder, done)
  end)
  return ok and M or nil
end

-- Ends the backend as the protocol asks: shutdown, then exit. Neovim waits briefly for the answer.
function M.stop()
  local client = state.client
  if not client or client.is_closing() then
    return
  end
  state.stopping = true
  local answered = false
  client.request('shutdown', vim.empty_dict(), function()
    answered = true
  end)
  vim.wait(1000, function()
    return answered
  end, 10)
  client.notify('exit', vim.empty_dict())
  vim.wait(500, function()
    return client.is_closing()
  end, 10)
  if not client.is_closing() then
    client.terminate()
  end
  state.client, state.ready = nil, false
end

return M
