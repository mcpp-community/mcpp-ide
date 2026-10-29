-- Problems from mcpp, never from a pattern over its output: each `job/diagnostic` of a build goes
-- into the namespace `mcpp` where mcpp locates it, a build or test of a project clears that
-- project's, and the manifest's own problems (`manifest/diagnostics`, `manifest/check`) are the
-- namespace `mcpp.toml`'s, on mcpp.toml. `:Mcpp problems` lists both in the quickfix list.
local session = require('mcpp.session')

local M = {}
local t = session.t

M.build = vim.api.nvim_create_namespace('mcpp')
M.manifest = vim.api.nvim_create_namespace('mcpp.toml')

local S = vim.diagnostic.severity
-- mcpp's severities (as mcpp-vscode reads them): a note or a help is a hint, anything else not named
-- an information.
local SEVERITY = { error = S.ERROR, warning = S.WARN, note = S.HINT, help = S.HINT }

-- The files each project's last build reported problems in: root -> { [bufnr] = diagnostics }.
local byRoot = {}

-- mcpp's positions are 1-based lines and columns; Neovim's 0-based. A point is widened to one column.
function M.item(d)
  local range = d.range or {}
  local start = range.start or { line = 1, column = 1 }
  local stop = range['end'] or start
  local col = math.max(0, start.column - 1)
  return {
    lnum = math.max(0, start.line - 1),
    col = col,
    end_lnum = math.max(0, stop.line - 1),
    end_col = math.max(stop.column - 1, col + 1),
    severity = SEVERITY[d.severity] or S.INFO,
    message = d.message,
    source = d.source or 'mcpp',
    code = d.code,
  }
end

-- The buffer of a file, without loading it.
local function buffer(path)
  return vim.fn.bufadd(vim.fs.normalize(path))
end

local function absolute(root, path)
  if path:sub(1, 1) == '/' or path:match('^%a:[/\\]') then
    return path
  end
  return root .. '/' .. path
end

function M.add(root, d)
  if not d.path or not root then
    return
  end
  local files = byRoot[root] or {}
  byRoot[root] = files
  local buf = buffer(absolute(root, d.path))
  local list = files[buf] or {}
  files[buf] = list
  local item = M.item(d)
  for _, seen in ipairs(list) do
    if seen.message == item.message and seen.lnum == item.lnum then
      return
    end
  end
  list[#list + 1] = item
  vim.diagnostic.set(M.build, buf, list)
end

function M.clear(root)
  for buf in pairs(byRoot[root] or {}) do
    vim.diagnostic.reset(M.build, buf)
  end
  byRoot[root] = nil
end

function M.set_manifest(root, diagnostics)
  local enabled = session.value('mcpp.toml.diagnostics.enabled') ~= false
  vim.diagnostic.set(M.manifest, buffer(root .. '/mcpp.toml'), enabled and vim.tbl_map(M.item, diagnostics or {}) or {})
end

-- The manifest's problems, asked for (on a save of mcpp.toml).
function M.check_manifest(root)
  session.request('manifest/check', { root = root }, function(err, r)
    if not err and r then
      M.set_manifest(r.root, r.diagnostics)
    end
  end)
end

-- Both namespaces in the quickfix list, which opens when it holds anything.
function M.quickfix()
  local all = vim.diagnostic.get(nil, { namespace = M.build })
  vim.list_extend(all, vim.diagnostic.get(nil, { namespace = M.manifest }))
  local items = vim.diagnostic.toqflist(all)
  vim.fn.setqflist({}, ' ', { title = 'mcpp', items = items })
  if #items > 0 then
    vim.cmd('botright copen')
  else
    vim.notify('mcpp: ' .. t('No problems were reported.'), vim.log.levels.INFO)
  end
  return items
end

function M.attach()
  session.on('job/started', function(p)
    if p.kind == 'build' or p.kind == 'test' then
      M.clear(p.root)
    end
  end)
  session.on('job/diagnostic', function(p)
    M.add(p.root, p)
  end)
  -- The envelope's own diagnostics that the stream did not carry (a manifest error ends a build
  -- before any event).
  session.on('job/finished', function(p)
    for _, d in ipairs(p.diagnostics or {}) do
      M.add(p.root, d)
    end
  end)
  session.on('manifest/diagnostics', function(p)
    M.set_manifest(p.root, p.diagnostics)
  end)
end

return M
