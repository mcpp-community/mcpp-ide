-- What mcpp-nvim shows: a job's narration in one scratch buffer (`mcpp://output`), a program in a
-- terminal buffer, and the backend's view models (targets and platforms, the cache, the graphs) as
-- text in a floating window. The texts are the backend's; this module only lays them out.
local session = require('mcpp.session')

local M = {}
local t = session.t

M.OUTPUT = 'mcpp://output'
local output

local function output_buffer()
  if output and vim.api.nvim_buf_is_valid(output) then
    return output
  end
  output = vim.api.nvim_create_buf(false, true)
  vim.api.nvim_buf_set_name(output, M.OUTPUT)
  vim.bo[output].bufhidden = 'hide'
  vim.bo[output].modifiable = false
  return output
end

-- Shows the output in a window at the bottom, unless one shows it already; the cursor stays where it
-- is unless mcpp.task.focusTerminal asks for it.
function M.reveal()
  local buf = output_buffer()
  if vim.fn.bufwinid(buf) == -1 then
    local from = vim.api.nvim_get_current_win()
    vim.cmd('botright 12split')
    vim.api.nvim_win_set_buf(0, buf)
    if not session.value('mcpp.task.focusTerminal') then
      vim.api.nvim_set_current_win(from)
    end
  end
end

-- Appends text as it came (a piece may end mid-line), and keeps every window on it at its end.
function M.append(text)
  local buf = output_buffer()
  local pieces = vim.split((text:gsub('\r\n', '\n'):gsub('\27%[[%d;]*[A-Za-z]', '')), '\n', { plain = true })
  local last = vim.api.nvim_buf_line_count(buf)
  local tail = vim.api.nvim_buf_get_lines(buf, last - 1, last, false)[1] or ''
  pieces[1] = tail .. pieces[1]
  vim.bo[buf].modifiable = true
  vim.api.nvim_buf_set_lines(buf, last - 1, last, false, pieces)
  vim.bo[buf].modifiable = false
  local lines = vim.api.nvim_buf_line_count(buf)
  for _, win in ipairs(vim.fn.win_findbuf(buf)) do
    vim.api.nvim_win_set_cursor(win, { lines, 0 })
  end
end

-- A line of its own.
function M.line(text)
  local buf = output_buffer()
  local last = vim.api.nvim_buf_get_lines(buf, -2, -1, false)[1] or ''
  M.append((last ~= '' and '\n' or '') .. text .. '\n')
end

function M.clear()
  local buf = output_buffer()
  vim.bo[buf].modifiable = true
  vim.api.nvim_buf_set_lines(buf, 0, -1, false, {})
  vim.bo[buf].modifiable = false
end

function M.lines()
  return vim.api.nvim_buf_get_lines(output_buffer(), 0, -1, false)
end

-- Inside a flow: a job, its narration streamed into the output, its end said there and, when it
-- did not succeed, as a notification. mcpp.task.clearTerminal and mcpp.task.revealTerminal apply
-- as they do to mcpp-vscode's terminal. Returns `job/finished`, or nil when it did not start.
function M.job(method, params, what)
  if session.value('mcpp.task.clearTerminal') ~= false then
    M.clear()
  end
  local reveal = session.value('mcpp.task.revealTerminal') or 'always'
  if reveal == 'always' then
    M.reveal()
  end
  params.color = false
  local handle, err = session.job(method, params, {
    started = function(p)
      M.line('$ ' .. table.concat(p.argv or {}, ' '))
    end,
    output = M.append,
  })
  if not handle then
    M.line(t('mcpp: {0}', err.message))
    if reveal ~= 'never' then
      M.reveal()
    end
    vim.notify(t('mcpp: {0}', err.message), vim.log.levels.ERROR)
    return nil
  end
  local done = handle:wait()
  local code = done.status ~= 'ok' and done.exitCode and (' (' .. t('Exit code {0}', done.exitCode) .. ')') or ''
  M.line(t('mcpp {0}: {1}', what, done.status) .. code)
  if done.status ~= 'ok' then
    if reveal == 'onFailure' then
      M.reveal()
    end
    vim.notify(t('mcpp {0}: {1}', what, done.status), vim.log.levels.WARN)
  end
  return done
end

-- The program `run/launch` describes, in a terminal buffer of its own (argv, its working directory,
-- the environment mcpp adds), where it reads its input.
function M.terminal(launch)
  local from = vim.api.nvim_get_current_win()
  vim.cmd('botright 12new')
  local opts = { cwd = launch.cwd }
  if type(launch.env) == 'table' and next(launch.env) ~= nil then
    opts.env = launch.env
  end
  local buf = vim.api.nvim_get_current_buf()
  local ok, job = pcall(function()
    if vim.fn.has('nvim-0.11') == 1 then
      opts.term = true
      return vim.fn.jobstart(launch.argv, opts)
    end
    return vim.fn.termopen(launch.argv, opts)
  end)
  if not ok then
    vim.cmd('close')
    vim.api.nvim_set_current_win(from)
    vim.notify(t('mcpp: {0}', tostring(job)), vim.log.levels.ERROR)
    return nil
  end
  vim.b[buf].mcpp_program = launch.program or launch.argv[1]
  if not session.value('mcpp.task.focusTerminal') then
    vim.api.nvim_set_current_win(from)
  end
  return buf, job
end

-- Text in a floating window; q or <Esc> closes it.
function M.float(title, lines)
  local buf = vim.api.nvim_create_buf(false, true)
  vim.api.nvim_buf_set_lines(buf, 0, -1, false, lines)
  vim.bo[buf].modifiable = false
  vim.bo[buf].bufhidden = 'wipe'
  local width = 20
  for _, line in ipairs(lines) do
    width = math.max(width, vim.fn.strdisplaywidth(line))
  end
  width = math.min(width + 2, vim.o.columns - 4)
  local height = math.max(1, math.min(#lines, vim.o.lines - 6))
  local win = vim.api.nvim_open_win(buf, true, {
    relative = 'editor', style = 'minimal', border = 'rounded',
    width = width, height = height,
    row = math.floor((vim.o.lines - height) / 2) - 1, col = math.floor((vim.o.columns - width) / 2),
    title = ' ' .. title .. ' ', title_pos = 'center',
  })
  for _, key in ipairs({ 'q', '<Esc>' }) do
    vim.keymap.set('n', key, '<Cmd>close<CR>', { buffer = buf, nowait = true })
  end
  return buf, win
end

-- ── the view models as text ───────────────────────────────────────────────────────────────────

-- targets/list: each target, the platform that configures it, its toolchains.
function M.targets_text(r)
  local lines = {}
  for _, row in ipairs(r.rows or {}) do
    local head = (row.selected and '✓ ' or '  ') .. row.target
    if row.host then
      head = head .. '  (' .. t('this machine') .. ')'
    end
    if (row.platformText or '') ~= '' then
      head = head .. '  · ' .. row.platformText
    end
    lines[#lines + 1] = head
    local toolchains = vim.tbl_map(function(tc)
      return string.format('%s (%s)', tc.spec ~= '' and tc.spec or '—', tc.status or '')
    end, row.toolchains or {})
    if #toolchains > 0 then
      lines[#lines + 1] = '    ' .. table.concat(toolchains, ', ')
    end
  end
  if (r.hostPlatformText or '') ~= '' then
    vim.list_extend(lines, { '', t('Host platform') .. ': ' .. r.hostPlatformText })
  end
  return lines
end

local function rows(into, list, label)
  for _, r in ipairs(list or {}) do
    into[#into + 1] = string.format('  %-32s %10s  %d', label(r), r.bytesText or '', r.entries or 0)
  end
  if not list or #list == 0 then
    into[#into + 1] = '  ' .. t('none')
  end
end

-- cache/inventory: the total, by kind, the largest packages, by last use.
function M.cache_text(inv)
  local where = (inv.root or '') ~= '' and (' · ' .. inv.root) or ''
  local lines = { inv.totalText .. ' ' .. t('in {0} entries', inv.totalEntries) .. where, '' }
  lines[#lines + 1] = t('By kind')
  rows(lines, inv.byKind, function(r) return r.kind end)
  vim.list_extend(lines, { '', t('Largest packages') })
  rows(lines, inv.topLabels, function(r) return r.label end)
  vim.list_extend(lines, { '', t('By last use') })
  rows(lines, inv.ageBuckets, function(b)
    return b.toDays == nil and t('older than {0} days', b.fromDays) or t('{0}–{1} days', b.fromDays, b.toDays)
  end)
  if inv.incomplete and #inv.incomplete > 0 then
    vim.list_extend(lines, { '', t('Incomplete entries') })
    for _, e in ipairs(inv.incomplete) do
      lines[#lines + 1] = '  ' .. (e.label or e.key or '')
    end
  end
  return lines
end

-- graph/packages, graph/modules: each node, and what it depends on.
function M.graph_text(g)
  local to = {}
  for _, e in ipairs(g.edges or {}) do
    to[e.from] = to[e.from] or {}
    table.insert(to[e.from], e.to)
  end
  local lines = {}
  for _, node in ipairs(g.nodes or {}) do
    lines[#lines + 1] = node.id .. (((node.kind or '') ~= '') and ('  (' .. node.kind .. ')') or '')
    for _, dep in ipairs(to[node.id] or {}) do
      lines[#lines + 1] = '  → ' .. dep
    end
  end
  return lines
end

-- Inside a flow: shows a view model the backend answers, as text.
function M.show(title, method, params, text)
  local r, err = session.call(method, params)
  if err then
    vim.notify(t('mcpp: {0}', err.message), vim.log.levels.ERROR)
    return
  end
  local lines = text(r or {})
  M.float(title, #lines > 0 and lines or { t('none') })
  return lines
end

return M
