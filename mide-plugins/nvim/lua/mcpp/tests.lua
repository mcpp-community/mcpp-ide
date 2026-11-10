-- What `:Mcpp test` says about each test (design 2026-11-07, D16; design 2026-11-10): a results buffer
-- (`mcpp://tests`) with one section per test, per part of a program of several files and per case, its
-- status and its whole output, passed or not; the failures, at the place their output names, in the
-- quickfix list, a failure that states both sides with them; and in the sign column of a test's source
-- a ▶ on its `main` and each case, which a result turns into ✓ or ✗, with the case's duration and its
-- metrics against their bounds at the end of its line. Cases arrive as the program writes them when the
-- project's mcpp states them (`test/caseEvent`), else with their part's record. The places and the
-- statuses are the backend's (`source/entries`, `test/cases`, `test/result`, `test/caseResult`,
-- `test/caseEvent`).
local session = require('mcpp.session')

local M = {}
local t = session.t

M.RESULTS = 'mcpp://tests'
local NS = vim.api.nvim_create_namespace('mcpp_tests')
local results_buf
-- The last result of each test and case, by `root\0test` and `root\0test\0case`, and what its line says.
local last = {}
local said = {}
-- The cases already reported in this run (a case that streamed is not said again with its record).
local reported = {}
-- Lines waiting to be written: the buffer is written at most every 100 ms while a run streams.
local pending = {}
local flushing = false

local SIGN = { entry = '▶', running = '◌', passed = '✓', failed = '✗', timedOut = '✗', errored = '✗', skipped = '–' }
local HL = { entry = 'DiagnosticInfo', running = 'DiagnosticHint', passed = 'DiagnosticOk', failed = 'DiagnosticError',
             timedOut = 'DiagnosticError', errored = 'DiagnosticError', skipped = 'DiagnosticHint' }

local function results_buffer()
  if results_buf and vim.api.nvim_buf_is_valid(results_buf) then
    return results_buf
  end
  results_buf = vim.api.nvim_create_buf(false, true)
  vim.api.nvim_buf_set_name(results_buf, M.RESULTS)
  vim.bo[results_buf].bufhidden = 'hide'
  vim.bo[results_buf].filetype = 'markdown'
  vim.bo[results_buf].modifiable = false
  return results_buf
end

local function flush()
  flushing = false
  if #pending == 0 then
    return
  end
  local lines = pending
  pending = {}
  local buf = results_buffer()
  vim.bo[buf].modifiable = true
  local count = vim.api.nvim_buf_line_count(buf)
  local empty = count == 1 and (vim.api.nvim_buf_get_lines(buf, 0, 1, false)[1] or '') == ''
  vim.api.nvim_buf_set_lines(buf, empty and 0 or count, -1, false, lines)
  vim.bo[buf].modifiable = false
end

local function write(lines)
  vim.list_extend(pending, lines)
  if not flushing then
    flushing = true
    vim.defer_fn(flush, 100)
  end
end

local function output_lines(text)
  local lines = vim.split((text or ''):gsub('\r\n', '\n'):gsub('\27%[[%d;]*[A-Za-z]', ''), '\n', { plain = true })
  if lines[#lines] == '' then
    table.remove(lines)
  end
  return lines
end

local function status_text(r)
  if r.status == 'timedOut' then
    return t('timed out')
  elseif r.compileFailed then
    return t('did not compile')
  elseif r.builtOnly then
    return t('built, not run')
  end
  return r.status
end

-- The file a result's location names: as printed, else beneath the project.
local function located(root, location)
  if type(location) ~= 'table' or type(location.path) ~= 'string' then
    return nil
  end
  local path = location.path
  if not (path:match('^/') or path:match('^%a:[/\\]')) then
    path = (root or '') .. '/' .. path
  end
  return { filename = vim.fs.normalize(path), lnum = (location.line or 0) + 1 }
end

-- A number as a person writes it: `15`, `4.2`, `0.913`.
local function number(v)
  if v == math.floor(v) and math.abs(v) < 1e15 then
    return string.format('%d', v)
  end
  return string.format('%.3g', v)
end

-- A case's metrics against their bounds: `ready 4.2 s ≤ 15 s`.
function M.metrics_text(r)
  local out = {}
  for _, m in ipairs(r.metrics or {}) do
    local unit = (m.unit and m.unit ~= '1') and (' ' .. m.unit) or ''
    local text = string.format('%s %s%s', m.name, number(m.value or 0), unit)
    for _, b in ipairs(r.bounds or {}) do
      if b.metric == m.name and b.max then
        text = text .. ' ≤ ' .. number(b.max) .. unit
      elseif b.metric == m.name and b.min then
        text = text .. ' ≥ ' .. number(b.min) .. unit
      end
    end
    out[#out + 1] = text
  end
  return table.concat(out, ' · ')
end

-- What a case's line says after a run: its duration and its metrics.
local function line_text(r)
  local parts = {}
  if r.status ~= 'skipped' and r.durationMs then
    parts[#parts + 1] = string.format('%d ms', math.floor(r.durationMs))
  end
  local metrics = M.metrics_text(r)
  if metrics ~= '' then
    parts[#parts + 1] = metrics
  end
  return #parts > 0 and table.concat(parts, ' · ') or nil
end

-- A new run: the buffer and the quickfix list start over.
function M.begin(root)
  pending = {}
  local buf = results_buffer()
  vim.bo[buf].modifiable = true
  vim.api.nvim_buf_set_lines(buf, 0, -1, false, { '# ' .. t('mcpp test in {0}', root or '?') })
  vim.bo[buf].modifiable = false
  M.failures = {}
  reported = {}
end

-- One case as it ended: its section, its sign, its failure in the quickfix list.
local function add_case(root, program, part, r)
  local key = (root or '') .. '\0' .. program .. '\0' .. r.case
  if reported[key] then
    return
  end
  reported[key] = true
  last[key] = r.status
  said[key] = line_text(r)
  local where = part and part ~= program and (part .. ' › ') or (program .. ' › ')
  local lines = { '', string.format('## %s %s%s — %s (%d ms)', SIGN[r.status] or '?', where, r.case, status_text(r),
                                    math.floor(r.durationMs or 0)) }
  if r.reason and r.reason ~= '' then
    lines[#lines + 1] = r.reason
  end
  -- A failure: what failed, then both sides when it states them; else the message as written.
  local failure = (r.failures or {})[1]
  local what = failure and failure.message or r.message or ''
  if r.status ~= 'passed' and r.status ~= 'skipped' and what ~= '' then
    for _, l in ipairs(output_lines(what)) do
      lines[#lines + 1] = '> ' .. l
    end
  end
  if failure and failure.expected ~= nil then
    lines[#lines + 1] = '> ' .. t('expected') .. ': ' .. failure.expected
    lines[#lines + 1] = '> ' .. t('actual') .. ':   ' .. (failure.actual or '')
  end
  local metrics = M.metrics_text(r)
  if metrics ~= '' then
    lines[#lines + 1] = metrics
  end
  local out = output_lines(r.output)
  if #out > 0 then
    lines[#lines + 1] = '```'
    vim.list_extend(lines, out)
    lines[#lines + 1] = '```'
  end
  write(lines)
  if r.status == 'failed' or r.status == 'errored' then
    local item = located(root, r.location)
    if item then
      item.text = string.format('%s › %s: %s', program, r.case, what ~= '' and what or status_text(r))
      if failure and failure.expected ~= nil then
        item.text = item.text .. string.format(' (%s %s, %s %s)', t('expected'), failure.expected, t('actual'), failure.actual or '')
      end
      M.failures[#M.failures + 1] = item
    end
  end
end

-- A program's result without cases (or a part's), as before cases existed.
local function add_program(r)
  local name = r.part or r.program or r.test
  local key = (r.root or '') .. '\0' .. (r.program or r.test)
  last[key] = r.status
  local lines = { '', string.format('## %s %s — %s (%d ms)', SIGN[r.status] or '?', name, status_text(r), r.durationMs or 0) }
  if r.reason and r.reason ~= '' then
    lines[#lines + 1] = r.reason
  end
  local out = output_lines(r.output)
  if #out > 0 then
    lines[#lines + 1] = '```'
    vim.list_extend(lines, out)
    lines[#lines + 1] = '```'
  end
  if r.outputTruncated then
    lines[#lines + 1] = t('(the output was longer; its head and tail are shown)')
  end
  write(lines)
  if r.status == 'failed' or r.status == 'timedOut' or r.status == 'errored' then
    local item = located(r.root, r.location)
    if item then
      item.text = name .. ': ' .. status_text(r)
      M.failures[#M.failures + 1] = item
    end
  end
end

-- One notification of a test job, said in the buffer, kept for the signs, and its failures put in
-- the quickfix list.
function M.add(method, r)
  if method == 'test/caseEvent' then
    if r.event == 'begin' then
      last[(r.root or '') .. '\0' .. r.program .. '\0' .. r.case] = 'running'
    else
      add_case(r.root, r.program, r.part, r)
    end
    return
  end
  if method == 'test/caseResult' then
    add_case(r.root, r.program or r.test, r.part, r)
    return
  end
  local cases = r.cases or {}
  if #cases == 0 then
    add_program(r)
    return
  end
  local program = r.program or r.test
  last[(r.root or '') .. '\0' .. program] = r.status
  for _, c in ipairs(cases) do
    add_case(r.root, program, r.part, c)
  end
end

-- The run ended: the quickfix list holds its failures, and the signs show the results.
function M.finish()
  flush()
  vim.fn.setqflist({}, ' ', { title = 'mcpp test', items = M.failures or {} })
  for _, buf in ipairs(vim.api.nvim_list_bufs()) do
    if vim.api.nvim_buf_is_loaded(buf) then
      M.signs(buf)
    end
  end
end

function M.reveal()
  local buf = results_buffer()
  if vim.fn.bufwinid(buf) == -1 then
    local from = vim.api.nvim_get_current_win()
    vim.cmd('botright 15split')
    vim.api.nvim_win_set_buf(0, buf)
    vim.api.nvim_set_current_win(from)
  end
end

local function place(buf, line, status, text)
  pcall(vim.api.nvim_buf_set_extmark, buf, NS, line, 0, {
    sign_text = SIGN[status] or SIGN.entry, sign_hl_group = HL[status] or HL.entry, priority = 20,
    virt_text = text and { { text, 'Comment' } } or nil, virt_text_pos = text and 'eol' or nil,
  })
end

-- The signs of a buffer: ▶ on a program's or a test's `main` and on each case its file declares, or
-- the last result, with what the case's line says.
function M.signs(buf)
  local name = vim.api.nvim_buf_get_name(buf)
  if name == '' or vim.bo[buf].buftype ~= '' or not session.state.ready then
    return
  end
  session.request('source/entries', { file = name }, function(err, entries)
    if err or not entries or (entries.kind ~= 'program' and entries.kind ~= 'test') or not vim.api.nvim_buf_is_valid(buf) then
      return
    end
    vim.api.nvim_buf_clear_namespace(buf, NS, 0, -1)
    local key = (entries.root or '') .. '\0' .. (entries.test or '')
    if entries.main and entries.main.line then
      place(buf, entries.main.line, entries.kind == 'test' and last[key] or 'entry')
    end
    if entries.kind ~= 'test' then
      return
    end
    session.request('test/cases', { root = entries.root, test = entries.test }, function(cerr, cases)
      if cerr or not cases or not vim.api.nvim_buf_is_valid(buf) then
        return
      end
      local here = vim.fs.normalize(name)
      for _, c in ipairs(cases.cases or {}) do
        local own = not c.file or vim.fs.normalize(c.file) == here
        if c.line and own then
          local ckey = key .. '\0' .. c.name
          place(buf, c.line, last[ckey] or 'entry', said[ckey])
        end
      end
    end)
  end)
end

-- What the sign of a key would show: the last result, or ▶ (the tests).
function M.sign_of(root, test, case)
  local status = last[(root or '') .. '\0' .. test .. (case and ('\0' .. case) or '')]
  return SIGN[status or 'entry']
end

-- What a case's line says after a run (the tests).
function M.said_of(root, test, case)
  return said[(root or '') .. '\0' .. test .. '\0' .. case]
end

-- ── the insights as text (`:Mcpp test.insights`) ──────────────────────────────────────────────

local BLOCKS = { '▁', '▂', '▃', '▄', '▅', '▆', '▇', '█' }

-- A series as block characters, lowest to highest.
function M.sparkline(values)
  if #values == 0 then
    return ''
  end
  local lo, hi = math.huge, -math.huge
  for _, v in ipairs(values) do
    lo, hi = math.min(lo, v), math.max(hi, v)
  end
  local out = {}
  for _, v in ipairs(values) do
    local i = hi > lo and math.floor((v - lo) / (hi - lo) * 7 + 0.5) + 1 or 4
    out[#out + 1] = BLOCKS[i]
  end
  return table.concat(out)
end

-- A worker's processes as a bar of 40 cells over the run (█ passed, ▓ failed).
function M.lane_bar(lane, finish)
  local bar = vim.split(string.rep(' ', 40), '')
  for _, item in ipairs(lane.items or {}) do
    local from = math.floor((item.startMs or 0) / finish * 40) + 1
    local to = math.min(40, math.max(from, math.floor(((item.startMs or 0) + (item.ms or 0)) / finish * 40)))
    local cell = item.status == 'fail' and '▓' or '█'
    for i = from, to do
      bar[i] = cell
    end
  end
  return table.concat(bar)
end

-- A run's time as a person reads it: `2026-11-08 10:00` from an ISO time or a run's identifier.
function M.when(text)
  local y, mo, d, h, mi = (text or ''):match('^(%d%d%d%d)-(%d%d)-(%d%d)T(%d%d):(%d%d)')
  if not y then
    y, mo, d, h, mi = (text or ''):match('^(%d%d%d%d)(%d%d)(%d%d)T(%d%d)(%d%d)')
  end
  return y and string.format('%s-%s-%s %s:%s', y, mo, d, h, mi) or (text or '')
end

local function duration(ms)
  if ms < 1000 then
    return string.format('%d ms', math.floor(ms + 0.5))
  end
  return string.format('%.1f s', ms / 1000)
end

local function who(r)
  local part = r.part and r.part ~= r.program and (' (' .. r.part .. ')') or ''
  return (r.case ~= '' and r.case or r.program) .. '  ' .. (r.case ~= '' and r.program or '') .. part
end

-- test/insights as lines: the run, its failures, the metrics, the slowest, the flaky, the schedule.
function M.insights_text(x)
  if not x.run or x.run == vim.NIL then
    return { t('No run is kept yet: mcpp test keeps each run in {0} (mcpp 2026.11.9.1 and later).', x.store or '') }
  end
  local c = x.run.counts or {}
  local lines = {
    t('{0} runs kept · the last began {1}', x.runs or 0, M.when(x.run.started ~= '' and x.run.started or x.run.id)),
    string.format('✓ %d  ✗ %d  ○ %d  %s', (c.pass or 0) + (c.xfail or 0), (c.fail or 0) + (c.xpass or 0) + (c.invalid or 0),
                  c.skip or 0, duration(x.run.ms or 0)),
  }
  local function section(title, rows)
    if #rows > 0 then
      lines[#lines + 1] = ''
      lines[#lines + 1] = title
      vim.list_extend(lines, rows)
    end
  end
  local rows = {}
  for _, f in ipairs(x.failures or {}) do
    rows[#rows + 1] = string.format('  ✗ %s  %s', who(f), f.new and t('new') or t('since {0}', M.when(f.since)))
    if f.expected ~= nil then
      rows[#rows + 1] = string.format('      %s %s · %s %s', t('expected'), f.expected, t('actual'), f.actual or '')
    end
  end
  section(t('Failures'), rows)
  rows = {}
  for _, m in ipairs(x.metrics or {}) do
    local values = {}
    for _, v in ipairs(m.values or {}) do
      values[#values + 1] = v.value
    end
    local bound = (m.bound and m.bound ~= vim.NIL) and m.bound or {}
    rows[#rows + 1] = string.format('  %s %s  %s', M.sparkline(values),
                                    M.metrics_text({ metrics = { { name = m.name, value = m.last, unit = m.unit } },
                                                     bounds = { { metric = m.name, max = bound.max, min = bound.min } } }), who(m))
  end
  section(t('Metrics'), rows)
  rows = {}
  for _, s in ipairs(x.slowest or {}) do
    rows[#rows + 1] = string.format('  %8s  %s  (%s)', duration(s.ms), who(s), t('median {0}', duration(s.medianMs)))
  end
  section(t('Slowest'), rows)
  rows = {}
  for _, f in ipairs(x.flaky or {}) do
    rows[#rows + 1] = string.format('  ⟳ %s  %s', who(f), t('{0} passed · {1} failed · {2} changes', f.passed, f.failed, f.flips))
  end
  section(t('Flaky'), rows)
  rows = {}
  local schedule = (x.schedule and x.schedule ~= vim.NIL) and x.schedule or { lanes = {} }
  local finish = schedule.ms or 1
  for _, lane in ipairs(schedule.lanes or {}) do
    for _, item in ipairs(lane.items or {}) do
      finish = math.max(finish, (item.startMs or 0) + (item.ms or 0))
    end
  end
  for _, lane in ipairs(schedule.lanes or {}) do
    rows[#rows + 1] = string.format('  %-10s %s', t('worker {0}', lane.worker), M.lane_bar(lane, finish))
  end
  if #(schedule.criticalPath or {}) > 0 then
    rows[#rows + 1] = '  ' .. t('Critical path') .. ': ' .. table.concat(schedule.criticalPath, ' → ')
  end
  section(t('Schedule'), rows)
  return lines
end

M.namespace = NS
return M
