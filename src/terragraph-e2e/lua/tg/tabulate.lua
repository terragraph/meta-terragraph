-- Copyright (c) Meta Platforms, Inc. and affiliates.
--
-- This source code is licensed under the MIT license found in the
-- LICENSE file in the root directory of this source tree.

--- Formatter for tabular data.
-- @module tg.tabulate

require("pl.stringx").import()

local tabulate = {}

--- Table display formats.
--
-- - `PLAIN` - Table with no lines
-- - `SIMPLE` - Table with lines below headers
tabulate.TableFormat = {
  PLAIN = "plain",
  SIMPLE = "simple"
}

--- Return column widths for the given rows (list of lists).
-- This is computed as the length of the longest string per column + 1.
local function getColWidths(rows, colCount)
  local widths = {}
  for _, row in ipairs(rows) do
    for i = 1, colCount do
      local data = row[i]
      widths[i] = math.max(widths[i] or 0, tostring(data):len() + 1)
    end
  end
  return widths
end

--- Return column alignments for the given rows (list of lists).
-- This is "l" unless all non-nil elements are numbers, else "r".
local function getColAligns(rows, colCount)
  local aligns = {}
  for i = 1, colCount do
    local hasNumber = false
    local hasNonNumber = false
    for _, row in ipairs(rows) do
      if type(row[i]) == "number" or tonumber(tostring(row[i])) then
        hasNumber = true
      else
        hasNonNumber = false
      end
    end
    aligns[i] = (hasNonNumber or not hasNumber) and "l" or "r"
  end
  return aligns
end

--- For each input row, split newlines to create one or more output rows
local function expandRows(rows, colCount, skipLines)
  local newRows = {}
  for rowIndex, row in ipairs(rows) do
    local newRowIndex = #newRows
    for col = 1, colCount do
      if type(row[col]) == "string" and row[col]:find("\n") ~= nil then
        -- Handle string elements
        for index, token in ipairs(row[col]:split("\n")) do
          if newRows[newRowIndex+index] == nil then
            newRows[newRowIndex+index] = {}
          end
          newRows[newRowIndex+index][col] = token
        end
      else
        -- Handle other types (e.g. numbers, nil)
        if newRows[newRowIndex+1] == nil then
          newRows[newRowIndex+1] = {}
        end
        newRows[newRowIndex+1][col] = row[col]
      end
    end
    if skipLines and rowIndex < #rows then
      newRows[#newRows+1] = {}
    end
  end
  return newRows
end

--- Return a formatted table with the given rows and options.
--
-- - `rows` is a 2D array of the tabular data (rows and columns).
-- - `opts` is an optional table of additional settings:
--     - `headers` - array of table headers
--     - `fmt` - the table format (see `TableFormat`), default `SIMPLE`
--     - `pad` - the padding size (number of spaces), default 2
--     - `skip_lines` - add empty lines between each row
function tabulate.tabulate(rows, opts)
  assert(type(rows) == "table")
  assert(type(opts) == "table" or opts == nil)

  -- Check options
  opts = opts or {}
  local headers = opts.headers
  local fmt = opts.fmt or tabulate.TableFormat.SIMPLE
  local pad = opts.pad or 2
  local skipLines = opts.skip_lines or false

  -- Calculate number of columns
  -- This is needed in case we have nil elements in tables
  local colCount = 0
  if headers ~= nil then
    colCount = #headers
  else
    for _, row in ipairs(rows) do
      colCount = math.max(colCount, #row)
    end
  end

  -- Handle newlines: split rows with \n character
  rows = expandRows(rows, colCount, skipLines)

  -- Calculate column widths and alignments
  if headers ~= nil then
    table.insert(rows, 1, headers)
  end
  local colWidths = getColWidths(rows, colCount)
  if headers ~= nil then
    table.remove(rows, 1)
  end
  local colAligns = getColAligns(rows, colCount)

  local lines = {}

  -- Print headers
  if headers ~= nil and #headers > 0 then
    local s = ""
    for i, header in ipairs(headers) do
      if colAligns[i] == "l" then
        s = s .. header
        s = s .. (" "):rep(colWidths[i] - tostring(header):len())
      elseif colAligns[i] == "r" then
        s = s .. (" "):rep(colWidths[i] - tostring(header):len())
        s = s .. header
      end
      s = s .. (" "):rep(pad)
    end
    lines[#lines+1] = s

    if fmt == tabulate.TableFormat.SIMPLE then
      s = ""
      for i, header in ipairs(headers) do
        s = s .. ("-"):rep(colWidths[i])
        s = s .. (" "):rep(pad)
      end
      lines[#lines+1] = s
    end
  end

  -- Print rows
  for rowIdx, row in ipairs(rows) do
    local s = ""
    for i = 1, colCount do
      -- Handle nil values
      local data = row[i]
      if data == nil then
        data = ""
      end

      if colAligns[i] == "l" then
        s = s .. tostring(data)
        s = s .. (" "):rep(colWidths[i] - tostring(data):len())
      elseif colAligns[i] == "r" then
        s = s .. (" "):rep(colWidths[i] - tostring(data):len())
        s = s .. tostring(data)
      end
      s = s .. (" "):rep(pad)
    end
    lines[#lines+1] = s
  end

  return table.concat(lines, "\n")
end

return tabulate
