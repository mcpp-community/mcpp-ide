-- What `:Mcpp test` says about each test (design 2026-11-07, D16): a results buffer (`mcpp://tests`)
-- with one section per test and per case, its status and its whole output, passed or not; the
-- failures, at the place their output names, in the quickfix list; and in the sign column of a
-- test's source a ▶ on its `main` and each case, which a result turns into ✓ or ✗. A program's `main`
-- has its ▶ too. The places and the statuses are the backend's (`source/entries`, `test/cases`,
-- `test/result`, `test/caseResult`).
local session = require('mcpp.session')

local M = {}
local t = session.t

M.RESULTS = 'mcpp://tests'
local NS = vim.api.nvim_create_namespace('mcpp_tests')
local results_buf
-- The last result of each test and case, by `root\0test` and `root\0test\0case`.
local last = {}

local SIGN = { entry = '▶', passed = '✓', failed = '✗', timedOut = '✗', errored = '✗', skipped = '–' }
local HL = { entry = 'DiagnosticInfo', passed = 'DiagnosticOk', failed = 'DiagnosticError', timedOut = 'DiagnosticError',
             errored = 'DiagnosticError', skipped = 'DiagnosticHint' }

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

local function write(lines)
  local buf = results_buffer()
  vim.bo[buf].modifiable = true
  local count = vim.api.nvim_buf_line_count(buf)
  local empty = count == 1 and (vim.api.nvim_buf_get_lines(buf, 0, 1, false)[1] or '') == ''
  vim.api.nvim_buf_set_lines(buf, empty and 0 or count, -1, false, lines)
  vim.bo[buf].modifiable = false
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

-- A new run: the buffer and the quickfix list start over.
function M.begin(root)
  local buf = results_buffer()
  vim.bo[buf].modifiable = true
  vim.api.nvim_buf_set_lines(buf, 0, -1, false, { '# ' .. t('mcpp test in {0}', root or '?') })
  vim.bo[buf].modifiable = false
  M.failures = {}
end

-- One result (`test/result` or `test/caseResult`) said in the buffer, kept for the signs, and its
-- failure put in the quickfix list.
function M.add(method, r)
  local name = method == 'test/caseResult' and (r.test .. ' › ' .. r.case) or r.test
  local key = (r.root or '') .. '\0' .. r.test .. (method == 'test/caseResult' and ('\0' .. r.case) or '')
  last[key] = r.status
  local lines = { '', string.format('## %s %s — %s (%d ms)', SIGN[r.status] or '?', name, status_text(r), r.durationMs or 0) }
  if r.reason and r.reason ~= '' then
    lines[#lines + 1] = r.reason
  end
  if method == 'test/caseResult' and r.message and r.message ~= '' and r.status == 'failed' then
    for _, l in ipairs(output_lines(r.message)) do
      lines[#lines + 1] = '> ' .. l
    end
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

-- The run ended: the quickfix list holds its failures, and the signs show the results.
function M.finish()
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

-- The signs of a buffer: ▶ on a program's or a test's `main` and on each case, or the last result.
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
      for _, c in ipairs(cases.cases or {}) do
        if c.line then
          place(buf, c.line, last[key .. '\0' .. c.name] or 'entry')
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

M.namespace = NS
return M
