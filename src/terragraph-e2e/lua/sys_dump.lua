#!/usr/bin/env lua

-- Copyright (c) Meta Platforms, Inc. and affiliates.
--
-- This source code is licensed under the MIT license found in the
-- LICENSE file in the root directory of this source tree.

--- Create a system dump archive (.tgz) to be used for debugging.
--
-- This iterates recursively through an input directory with two file types:
--
-- - `sysdump_log_files` file listing absolute paths of log files (one path per
--   line) to add to the archive at `/LogFiles/<path>`.
-- - Scripts to execute, where all console output is redirected to a file
--   and added to the archive at `<script_path>/<script_filename>.out`. Each
--   script is called with one argument containing a directory path, to which
--   the script can add additional files to be copied into the archive.
--
-- @script sys_dump

local tg_utils = require "tg.utils"
local logger = require "tg.logger"
local argparse = require "argparse"
local dir = require "pl.dir"
local path = require "pl.path"
local glob = require "posix.glob"
local stdlib = require "posix.stdlib"
require("pl.stringx").import()

local C = {}

local SYSDUMP_INPUT_DIR = "/etc/sysdump/"
local SYSDUMP_LOGFILE_NAME = "sysdump_log_files"
local SYSDUMP_ARCHIVE_PATH = "LogFiles/"
local SYSDUMP_OUTPUT_DIR = "/tmp/"
local SYSDUMP_TMP_DIR = "/tmp/sysdump-XXXXXX"

--- Generate sysdump output for a given log file (i.e. file containing logfile
-- entries to collect).
function C.generateLogfileOutput(input, inputDir, outputDir)
  logger.info("Copying log files from: %s", input)

  -- Read file
  local contents = tg_utils.readFile(input)
  if contents == nil then
    return false
  end

  -- Iterate lines...
  local outputLogDir = path.join(outputDir, SYSDUMP_ARCHIVE_PATH)
  for _, line in ipairs(contents:splitlines()) do
    -- Take care of file globbing
    local files = glob.glob(line)
    if files == nil then
      logger.error("-> Skipping: %s", line)
    else
      for __, file in ipairs(files) do
        -- Copy to output directory
        local outputPath = outputLogDir .. file
        dir.makepath(path.dirname(outputPath))
        if path.isdir(file) then
          -- Copy entire directory tree
          logger.info("-> Dumping tree: %s", file)
          local ret, err = dir.clonetree(file, outputLogDir, dir.copyfile)
          if not ret then
            logger.error("--> Directory copy failed: %s", err)
          end
        else
          -- Copy single file
          logger.info("-> Dumping file: %s", file)
          if not dir.copyfile(file, outputPath) then
            logger.error("--> File copy failed.")
          end
        end
      end
    end
  end

  return true
end

--- Generate sysdump output for a given script.
function C.generateScriptOutput(input, inputDir, outputDir)
  logger.info("Executing script: %s", input)

  local outputScriptDir = path.join(outputDir, path.relpath(input, inputDir))
  local outputScriptFile = path.join(
    outputScriptDir, path.basename(input) .. ".out"
  )
  dir.makepath(outputScriptDir)

  -- Execute script
  local success, ret, code = os.execute(
    string.format("%s '%s' >%s 2>&1", input, outputScriptDir, outputScriptFile)
  )
  if not success then
    logger.error("-> Failed: %s %s", ret, code)
    return false
  end

  return true
end

--- Generate sysdump output from the given input directory.
function C.generateOutput(inputDir, outputDir)
  for root, dirs, files in dir.walk(inputDir) do
    for _, f in ipairs(files) do
      local filePath = path.join(root, f)
      if path.basename(f) == SYSDUMP_LOGFILE_NAME then
        C.generateLogfileOutput(filePath, inputDir, outputDir)
      else
        C.generateScriptOutput(filePath, inputDir, outputDir)
      end
    end
  end
end

--- Create a compressed tarball.
function C.createTarball(inputPath, outputTgz)
  local cmd = string.format(
    "tar -C %s -czf %s %s",
    path.dirname(inputPath), outputTgz, path.basename(inputPath)
  )
  logger.info("Creating tarball: %s", cmd)
  local success, ret, code = os.execute(cmd)
  if not success then
    logger.error("Command failed: %s %s", ret, code)
    return false
  end
  logger.info("Sysdump saved to: %s", outputTgz)
  return true
end

function C.main()
  local parser = argparse(
    "sys_dump", "Create a system dump archive (.tgz) to be used for debugging."
  )
  parser:option(
    "-i --input_dir", "The sysdump input directory", SYSDUMP_INPUT_DIR
  )
  parser:option(
    "-o --output", "The sysdump output file or directory", SYSDUMP_OUTPUT_DIR
  )
  parser:option(
    "-t --tmp_dir", "The temporary directory pattern", SYSDUMP_TMP_DIR
  )
  local args = parser:parse()

  -- Construct/create paths:
  --   tmpDir => temporary directory (random name)
  --   outputDir => one level within tmpDir (human-readable name)
  --   tgzPath => output archive path
  local tmpDir, err, errnum = stdlib.mkdtemp(args.tmp_dir)
  if tmpDir == nil then
    logger.error("mkdtemp failed: %s %s", err, errnum)
    os.exit(1)
  end
  local tgzPath, tgzName
  if path.isdir(args.output) then
    tgzName = "sysdump-" .. os.date("%Y%m%d-%H_%M_%S")
    tgzPath = path.join(args.output, tgzName .. ".tgz")
  else
    tgzName = path.basename(args.output)
    tgzPath = args.output
  end
  local outputDir = path.join(tmpDir, tgzName)
  dir.makepath(outputDir)

  -- Generate sysdump output to temporary directory
  C.generateOutput(args.input_dir, outputDir)
  logger.info("==== Finished ====")

  -- Create archive and delete temporary directory
  C.createTarball(outputDir, tgzPath)
  dir.rmtree(tmpDir)
end

if tg_utils.isMain() then
  C.main()
else
  return C
end
