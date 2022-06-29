#!/usr/bin/env lua

-- Copyright (c) Meta Platforms, Inc. and affiliates.
--
-- This source code is licensed under the MIT license found in the
-- LICENSE file in the root directory of this source tree.

--- Print gflag-formatted flags from an E2E configuration file.
-- @script config_print_flags

local tg_utils = require "tg.utils"
local argparse = require "argparse"
require("pl.stringx").import()

local C = {}

function C.exportFlags(config)
  local flags = tg_utils.get(config, "flags")
  if flags == nil then
    return nil
  end

  local flagList = {}
  local flagNames = {}
  for k, v in pairs(flags) do
    -- strip group meta info in flag name (anything before last '.')
    local flag
    local idx = k:rfind(".")
    if idx == nil then
      flag = k
    else
      flag = k:sub(idx + 1)
    end

    -- validate flag name
    -- assume C-style variable names: only alphanumerics and underscores
    if flag:match("^[A-Za-z0-9_]+$") then
      -- add arg: flag='value' (with single quotes escaped)
      flagList[#flagList+1] =
        string.format("--%s='%s'", flag, v:gsub("'", "'\\''"))
      flagNames[#flagNames+1] = flag
    end
  end

  table.sort(flagList)
  table.sort(flagNames)

  -- tell gflags to silently ignore unknown flags
  if #flagNames >= 1 then
    flagList[#flagList+1] = string.format("--undefok=%s", (","):join(flagNames))
  end

  -- return full flags string
  return (" "):join(flagList)
end

function C.main()
  local parser = argparse(
    "config_print_flags",
    "Print gflag-formatted flags from an E2E configuration file."
  )
  parser:argument("config_file", "Path to the E2E configuration file")
  local args = parser:parse()

  -- Read config, process it, and print output
  local config = tg_utils.readJsonFile(args.config_file)
  if config == nil then
    os.exit(1)
  end
  local output = C.exportFlags(config)
  if output == nil then
    os.exit(1)
  else
    io.write(output .. "\n")
  end
end

if tg_utils.isMain() then
  C.main()
else
  return C
end
