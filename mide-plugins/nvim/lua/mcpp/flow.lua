-- A command as one sequence: ask the backend, then the user, then the backend again. Each step that
-- answers through a callback (a request, vim.ui.select, vim.ui.input, a job's end) resumes the
-- coroutine the command runs in, so the command reads in the order it happens.
local M = {}

local unpack = table.unpack or unpack

-- Resumes a flow; an error it raises is said, not lost in a callback.
local function resume(co, ...)
  local ok, err = coroutine.resume(co, ...)
  if not ok then
    vim.notify('mcpp: ' .. tostring(err), vim.log.levels.ERROR)
  end
end

-- Runs fn(...) as a flow.
function M.run(fn, ...)
  resume(coroutine.create(fn), ...)
end

-- Inside a flow: starts an operation that takes a callback, and returns what the callback is given.
-- A callback that runs at once (before the flow waits) is answered without waiting.
function M.wait(start)
  local co = coroutine.running()
  assert(co, 'mcpp: flow.wait outside a flow')
  local results, waiting
  start(function(...)
    if results then
      return
    end
    results = { n = select('#', ...), ... }
    if waiting then
      resume(co)
    end
  end)
  if not results then
    waiting = true
    coroutine.yield()
  end
  return unpack(results, 1, results.n)
end

function M.select(items, opts)
  return M.wait(function(done)
    vim.ui.select(items, opts, done)
  end)
end

function M.input(opts)
  return M.wait(function(done)
    vim.ui.input(opts, done)
  end)
end

-- Whether the user goes on: `yes` chosen over Cancel.
function M.confirm(prompt, yes, cancel)
  return M.select({ yes, cancel }, { prompt = prompt }) == yes
end

return M
