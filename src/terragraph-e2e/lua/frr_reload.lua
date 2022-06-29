#!/usr/bin/env lua

-- Copyright (c) Meta Platforms, Inc. and affiliates.
--
-- This source code is licensed under the MIT license found in the
-- LICENSE file in the root directory of this source tree.

--- FRR reload script.
-- @script frr_reload
local argparse = require "argparse"
local cjson_safe = require "cjson.safe"
local logger = require "tg.logger"
local path = require "pl.path"
local tablex = require "pl.tablex"
local tg_utils = require "tg.utils"
require("pl.stringx").import()

math.randomseed(os.time())

local FrrReloadUtils = {}

-- Vtysh class
local Vtysh = {}
Vtysh.__index = Vtysh

--- Constructor for Vtysh.
function Vtysh.new(args)
  local self = setmetatable({}, Vtysh)
  self.bindir = args.bindir
  self.confdir = args.confdir
  self.pathspace = args.pathspace
  self.commonArgs = path.join(args.bindir, "vtysh")
  return self
end

--- Execute a vtysh command and return the output (see `tg.utils.exec()`).
function Vtysh:_call(strArgs)
  local cmd = self.commonArgs .. " " .. strArgs
  logger.info("Running command: %s", cmd)
  local output, ret, code = tg_utils.exec(cmd)
  if output == nil then
    logger.error("Command returned error: %s %d", ret, code)
  end
  return output
end

--- Call a vtysh CLI command or a list of commands (e.g. "show running-config").
--
-- The command input must be a list of strings.
function Vtysh:call(commandList)
  local args = {}
  for _, cmd in ipairs(commandList) do
    args[#args+1] = string.format('-c "%s"', cmd)
  end
  return self:_call((" "):join(args))
end

--- Return false if no frr daemon is running or if some other vtysh session is
-- in 'configuration terminal' mode.
function Vtysh:isConfigAvailable()
  local output = self:call({ "configure" })
  if output:find("VTY configuration is locked by other VTY") then
    logger.error("vtysh 'configure' returned\n%s", output)
    return false
  end
  return true
end

--- Execute vtysh CLI commands from a file.
function Vtysh:execFile(filename)
  self:_call(string.format("-f " .. filename))
end

--- Mark a config file using the `vtysh --markfile` command.
function Vtysh:markFile(filename)
  return self:_call("-m -f " .. filename)
end

--- Load running FRR configurations and marks it with vtysh command
function Vtysh:markShowRun(daemon)
  local cmd = "show running-config"
  if daemon ~= "" then
    cmd = cmd .. " " .. tostring(daemon)
  end
  cmd = cmd .. " no-header"

  local showRun = self:call({cmd})

  -- Save the showRun as a file and use that as input to `vtysh --markfile`
  local filename = os.tmpname()
  tg_utils.writeFile(filename, showRun)
  local stdout = self:_call("-m -f " .. filename)
  os.remove(filename)

  return stdout
end

-- Context class
local Context = {}
Context.__index = Context

--- Constructor for Context.
--
-- A context object represents a section of frr configuration such as:
--    !
--    route-map ALLOW-TG-PREFIXES-OUT permit 30
--     match ipv6 address prefix-list TG-PREFIXES-OUT
--     set ipv6 next-hop global 3001::6
--    !
--
-- It can also represent a single line context object such as this:
--    !
--    ipv6 prefix-list TG-PREFIXES-OUT seq 1 permit 1001:1001::/55
--    !
function Context.new(keys, lines)
  local self = setmetatable({}, Context)
  if keys then
    self.keys = tablex.deepcopy(keys)
  else
    self.keys = {}
  end

  -- Keep a dictionary of the lines to make it easy to tell if a line exists
  if lines then
    self.lines = tablex.deepcopy(lines)
    self.dlines = tablex.makeset(lines)
  else
    self.lines = {}
    self.dlines = {}
  end

  return self
end

--- Add lines to the current context and update dictionary of lines.
function Context:addLines(lines)
  self.lines = tablex.insertvalues(self.lines, lines)
  for k, l in pairs(lines) do
    self.dlines[l] = true
  end
end

-- Config class
local Config = {}
Config.__index = Config

--- Constructor for Config.
--
-- A FRR configuration is stored in a config object.
--
-- A config object contains a dictionary of Context objects where the Context
-- keys ('router ospf' or 'ipv6 route ', for example) are the dictionary keys.
function Config.new(vtysh)
  local self = setmetatable({}, Config)
  self.lines = {}
  self.contexts = {}
  self.keyList = {} -- make contexts behave as an ordered dictionary
  self.vtysh = vtysh
  return self
end

--- Read configuration from specified file and slurp it into internal memory.
--
-- The internal representation is marked appropriately by passing it
-- through vtysh with the `--markfile` flag.
function Config:loadFromFile(filename)
  logger.info("Loading Config object from file: %s", filename)
  local vtysh = self.vtysh
  local fileOutput = vtysh:markFile(filename)

  FrrReloadUtils.loadLinesFromFile(fileOutput, self)

  self:loadContexts()
end

--- Load lines from loaded file into a config object.
function FrrReloadUtils.loadLinesFromFile(configStr, configObject)
  for _, line in pairs(configStr:splitlines()) do
    line = line:strip()
    line = (" "):join(line:split())
    if line ~= "" then
      table.insert(configObject.lines, line)
    end
  end
end

--- Read running configuration and slurp it into internal memory.
function Config:loadFromShowRunning(daemon)
  logger.info("Loading Config object from vtysh")
  local configText = self.vtysh:markShowRun(daemon)

  for _, line in pairs(configText:splitlines()) do
    line = line:strip()
    if (
      type(line) == "string"
      and line ~= "Building configuration..."
      and line ~= "Current configuration:"
      and line ~= ""
    ) then
      table.insert(self.lines, line)
    end
  end

  self:loadContexts()
end

--- Helper function to get all lines of a config object as a single string.
function Config:getLines()
  return ("\n"):join(self.lines)
end

--- Helper function to save contexts in a dictionary and remember the order.
--
-- Uses JSON serialization to produce a valid table key from a list of strings.
function FrrReloadUtils.putContext(keyList, contexts, keys, ctx)
  local serializedKey = cjson_safe.encode(keys)
  table.insert(keyList, serializedKey)
  contexts[serializedKey] = ctx
end

--- Helper function to retrieve a context by keys list.
--
-- Uses JSON serialization to produce a valid table key from a list of strings.
--
-- Returns nil if not found.
function FrrReloadUtils.getContext(contexts, keys)
  return contexts[cjson_safe.encode(keys)]
end

--- Save the provided key and lines as a context.
function Config:saveContexts(keys, lines)
  if type(keys) ~= "table" or tablex.size(keys) == 0 then
    return
  end

  -- Add lines to the context object if `lines` is not empty and create a new
  -- context, if not found
  if type(lines) == "table" and tablex.size(lines) > 0 then
    if not FrrReloadUtils.getContext(self.contexts, keys) then
      local ctx = Context.new(keys, lines)
      FrrReloadUtils.putContext(self.keyList, self.contexts, keys, ctx)
    else
      local ctx = FrrReloadUtils.getContext(self.contexts, keys)
      ctx:addLines(lines)
    end

  else
    if not FrrReloadUtils.getContext(self.contexts, keys) then
      local ctx = Context.new(keys, {})
      FrrReloadUtils.putContext(self.keyList, self.contexts, keys, ctx)
    end
  end
end

--- Returns true if the line starts with one of the known one-line contexts.
function FrrReloadUtils.startsWithOnelineKeyword(line)
  -- The keywords that are single line contexts: bgp in this case
  -- is not the main router bgp block, but enabling multi-instance
  local onelineCtxKeywords = {
    "access-list ",
    "agentx",
    "allow-external-route-update",
    "bgp ",
    "debug ",
    "domainname ",
    "dump ",
    "enable ",
    "frr ",
    "fpm ",
    "hostname ",
    "ip ",
    "ipv6 ",
    "log ",
    "mpls lsp",
    "mpls label",
    "no ",
    "password ",
    "ptm-enable",
    "router-id ",
    "service ",
    "table ",
    "username ",
    "zebra ",
    "vrrp autoconfigure",
    "evpn mh",
  }

  for _, keyword in pairs(onelineCtxKeywords) do
    if line:startswith(keyword) then
      return true
    end
  end
  return false
end

--- Parse the configuration and create contexts for each appropriate block.
function Config:loadContexts()
  local currentContextLines = {}
  local ctxKeys = {}
  local mainCtxKey = {}
  local newCtx = true

  -- The end of a context is flagged via the 'end' keyword
  for _, line in pairs(self.lines) do
    if type(line) ~= "string" or line == "" then
      goto continue
    end

    if line:startswith("!") or line:startswith("#") then
      goto continue
    end

    if (
      tablex.size(ctxKeys) == 2
      and ctxKeys[1]:startswith("bfd")
      and ctxKeys[2]:startswith("profile ")
      and line == "end"
    ) then
      logger.debug(
        "LINE %s: popping from sub context, %s",
        line,
        ("; "):join(ctxKeys)
      )
      if tablex.size(mainCtxKey) > 0 then
        self:saveContexts(ctxKeys, currentContextLines)
        ctxKeys = tablex.deepcopy(mainCtxKey)
        tablex.clear(currentContextLines)
        goto continue
      end
    end

    -- One line contexts
    if (
      newCtx == true
      and FrrReloadUtils.startsWithOnelineKeyword(line)
      and not (
        tablex.size(ctxKeys) > 0
        and ctxKeys[1]:startswith("mlps ldp")
        and line:startswith("router-id ")
      )
    ) then
      self:saveContexts(ctxKeys, currentContextLines)

      -- Start a new context
      tablex.clear(mainCtxKey)
      tablex.clear(ctxKeys)
      tablex.clear(currentContextLines)
      table.insert(ctxKeys, line)
      logger.debug(
        "LINE %s: entering new context, %s",
        line,
        ("; "):join(ctxKeys)
      )
      self:saveContexts(ctxKeys, currentContextLines)
      newCtx = true

    elseif line == "end" then
      self:saveContexts(ctxKeys, currentContextLines)
      logger.debug(
        "LINE %s: exiting old context, %s",
        line,
        ("; "):join(ctxKeys)
      )

      -- Start a new context
      newCtx = true
      tablex.clear(mainCtxKey)
      tablex.clear(ctxKeys)
      tablex.clear(currentContextLines)

    elseif line == "exit" and ctxKeys[0]:startswith("rpki") then
      self:saveContexts(ctxKeys, currentContextLines)
      logger.debug(
        "LINE %s: exiting old context, %s",
        line,
        ("; "):join(ctxKeys)
      )

      -- Start a new context
      newCtx = true
      tablex.clear(mainCtxKey)
      tablex.clear(ctxKeys)
      tablex.clear(currentContextLines)

    elseif line == "exit-vrf" then
      self:saveContexts(ctxKeys, currentContextLines)
      table.insert(currentContextLines, line)
      logger.debug(
        "LINE %s: append to currentContextLines, %s",
        line,
        ("; "):join(ctxKeys)
      )

      -- Start a new context
      newCtx = true
      tablex.clear(mainCtxKey)
      tablex.clear(ctxKeys)
      tablex.clear(currentContextLines)

    elseif (
      line == "exit"
      and tablex.size(ctxKeys) > 1
      and ctxKeys[1]:startswith("segment-routing")
    ) then
      self:saveContexts(ctxKeys, currentContextLines)

      -- Start a new context
      ctxKeys:remove()
      tablex.clear(currentContextLines)
      logger.debug(
        "LINE %s: popping segment routing sub-context to ctx %s",
        line,
        ("; "):join(ctxKeys)
      )

    elseif (
      line == "exit-address-family"
      or line == "exit"
      or line == "exit-vnc"
    ) then
      self:saveContexts(ctxKeys, currentContextLines)

      -- Start a new context
      ctxKeys = tablex.deepcopy(mainCtxKey)
      tablex.clear(currentContextLines)
      logger.debug(
        "LINE %s: popping from subcontext to ctx %s",
        line,
        ("; "):join(ctxKeys)
      )

    elseif newCtx then
      if type(mainCtxKey) == "table" and tablex.size(mainCtxKey) > 0 then
        ctxKeys = tablex.deepcopy(mainCtxKey)
        tablex.clear(mainCtxKey)
      else
        tablex.clear(ctxKeys)
        table.insert(ctxKeys, line)
      end

      tablex.clear(currentContextLines)
      newCtx = false
      logger.debug(
        "LINE %s: entering new context, %s",
        line,
        ("; "):join(ctxKeys)
      )

    elseif (
      line:startswith("address-family ")
      or line:startswith("vnc defaults")
      or line:startswith("vnc l2-group")
      or line:startswith("vnc nve-group")
      or line:startswith("peer")
      or line:startswith("key ")
      or line:startswith("member pseudowire")
    ) then
      tablex.clear(mainCtxKey)

      -- Save old context first
      self:saveContexts(ctxKeys, currentContextLines)
      tablex.clear(currentContextLines)
      mainCtxKey = tablex.deepcopy(ctxKeys)
      logger.debug(
        "LINE %s: entering sub-context, append to ctxKeys",
        line
      )

      if (
        line == "address-family ipv6"
        and not ctxKeys[0]:startswith("mpls ldp")
      ) then
        table.insert(ctxKeys, "address-family ipv6 unicast")

      elseif (
        line == "address-family ipv4"
        and not ctxKeys[0]:startswith("mpls ldp")
      ) then
        table.insert(ctxKeys, "address-family ipv4 unicast")

      elseif (
        line == "address-family evpn"
        and not ctxKeys[0]:startswith("mpls ldp")
      ) then
        table.insert(ctxKeys, "address-family l2vpn evpn")

      else
        table.insert(ctxKeys, line)
      end

    else
      -- Continuing in an existing context, add non-commented lines to it
      table.insert(currentContextLines, line)
      logger.debug(
        "LINE %s: append to currentContextLines, %s",
        line,
        ("; "):join(ctxKeys)
      )
    end

    ::continue::
  end

  -- Save the last context if not yet saved
  self:saveContexts(ctxKeys, currentContextLines)
end

--- Helper function to add lines to `linesToAdd`/`linesToDel`.
function FrrReloadUtils.putLinesTo(linesToList, newKeys, line)
  table.insert(linesToList, {newKeys, line})
end

--- Helper function to remove lines from `linesToAdd`/`linesToDel`.
function FrrReloadUtils.removeLinesTo(linesToList, delKeys, delLine)
  for i, entry in pairs(linesToList) do
    if tablex.deepcompare(entry, {delKeys, delLine}) then
      table.remove(linesToList, i)
    end
  end
end

--- Remove from `linesToDel` all commands that cannot be removed using vtysh.
function FrrReloadUtils.ignoreUnconfigurableLines(linesToAdd, linesToDel)
  local linesToDelToRemove = {}

  for _, delEntry in pairs(linesToDel) do
    local ctxKeys, line = delEntry[1], delEntry[2]

    local firstCtxKey = ctxKeys[1]
    if (
      firstCtxKey:startswith("frr version")
      or firstCtxKey:startswith("frr defaults")
      or firstCtxKey:startswith("username")
      or firstCtxKey:startswith("password")
      or firstCtxKey:startswith("line vty")
      or firstCtxKey:startswith("service integrated-vtysh-config")
    ) then
      logger.debug('"%s" cannot be removed', ctxKeys[#ctxKeys])
      table.insert(linesToDelToRemove, {ctxKeys, line})
    end
  end

  -- Remove the delete lines to ignore
  for _, removePair in pairs(linesToDelToRemove) do
    local ctxKeys, line = removePair[1], removePair[2]

    FrrReloadUtils.removeLinesTo(linesToDel, ctxKeys, line)
  end

  return linesToAdd, linesToDel
end

--- Helper function to check all keys in a list against a matching string.
function FrrReloadUtils.anyCtxKeyStartsWith(ctxKeys, str)
  if (
    type(ctxKeys) == "table"
    and tablex.size(ctxKeys) > 0
    and type(str) == "string"
    and str ~= ""
  ) then
    for _, key in pairs(ctxKeys) do
      if key:startswith(str) then
        return true
      end
    end
  end
  return false
end

--- Helper function to check if line is present in a context.
function FrrReloadUtils.isLineInCtx(context, line)
  return context.dlines[line]
end

--- Create a context diff for the two specified config objects.
--
-- Returns two lists: `linesToAdd` and `linesToDel`, of `{key, {lines}}` pairs.
function FrrReloadUtils.compareContextObjects(newConf, running)
  local linesToAdd = {}
  local linesToDel = {}
  local deleteBgpd = false

  -- Find contexts that are in newConf but not in running and vice-versa
  for _, runningCtxKey in ipairs(running.keyList) do
    local runningCtx = running.contexts[runningCtxKey]

    if not FrrReloadUtils.getContext(newConf.contexts, runningCtx.keys) then
      local firstRunningCtxKey = runningCtx.keys[1]
      if (
        firstRunningCtxKey:startswith("router bgp")
        and tablex.size(runningCtx.keys) == 1
      ) then
        deleteBgpd = true
        FrrReloadUtils.putLinesTo(linesToDel, runningCtx.keys, true)

      -- If this is an address-family under 'router bgp' and we are already
      -- deleting the entire 'router bgp' context then ignore this sub-context
      elseif (
        firstRunningCtxKey:startswith("router bgp")
        and tablex.size(runningCtx.keys) > 1
        and deleteBgpd
      ) then
        goto continue

      elseif (
        firstRunningCtxKey:startswith("router bgp")
        and tablex.size(runningCtx.keys) > 1
        and runningCtx.keys[2]:startswith("address-family")
      ) then
        -- There's no 'no address-family' support and so we have to
        -- delete each line individually again
        for _, line in pairs(runningCtx.lines) do
          FrrReloadUtils.putLinesTo(linesToDel, runningCtx.keys, line)
        end

      -- Some commands can happen at higher counts that make doing vtysh -c
      -- inefficient (and can time out.)  For these commands, instead of adding
      -- them to lines_to_del, add the "no " version to lines_to_add
      elseif (
        firstRunningCtxKey:startswith("ip route")
        or firstRunningCtxKey:startswith("ipv6 route")
      ) then
        local addCmd = {"no " .. firstRunningCtxKey}
        FrrReloadUtils.putLinesTo(linesToAdd, addCmd, true)

      -- Non-global contexts
      elseif (
        runningCtxKey ~= ""
        and not FrrReloadUtils.anyCtxKeyStartsWith(
          runningCtx.keys,
          "address-family"
        )
      ) then
        FrrReloadUtils.putLinesTo(linesToDel, runningCtx.keys, true)

      elseif (
        runningCtxKey ~= ""
        and not FrrReloadUtils.anyCtxKeyStartsWith(runningCtx.keys, "vni")
      ) then
        FrrReloadUtils.putLinesTo(linesToDel, runningCtx.keys, true)

      -- Global context
      else
        for _, line in pairs(runningCtx.lines) do
          FrrReloadUtils.putLinesTo(linesToDel, runningCtx.keys, line)
        end
      end
    end

    ::continue::
  end

  -- Find the lines within each context to add or delete
  for _, newConfCtxKeys in ipairs(newConf.keyList) do
    local newConfCtx = newConf.contexts[newConfCtxKeys]
    local runningCtx = FrrReloadUtils.getContext(
      running.contexts,
      newConfCtx.keys
    )

    if type(runningCtx) == "table" and tablex.size(runningCtx) > 0 then
      for _, line in pairs(newConfCtx.lines) do
        if not FrrReloadUtils.isLineInCtx(runningCtx, line) then
          logger.debug("Line %s must be added to the running config", line)
          FrrReloadUtils.putLinesTo(linesToAdd, newConfCtx.keys, line)
        end
      end
      for _, line in pairs(runningCtx.lines) do
        if not FrrReloadUtils.isLineInCtx(newConfCtx, line) then
          logger.debug("Line %s must be deleted from the running config", line)
          FrrReloadUtils.putLinesTo(linesToDel, newConfCtx.keys, line)
        end
      end
    end
  end

  for _, newConfCtxKeys in ipairs(newConf.keyList) do
    local newConfCtx = newConf.contexts[newConfCtxKeys]
    if not FrrReloadUtils.getContext(running.contexts, newConfCtx.keys) then
      -- If the entire context is missing, add each line
      FrrReloadUtils.putLinesTo(linesToAdd, newConfCtx.keys, true)
      for _, line in pairs(newConfCtx.lines) do
        FrrReloadUtils.putLinesTo(linesToAdd, newConfCtx.keys, line)
      end
    end
  end

  linesToAdd, linesToDel = FrrReloadUtils.ignoreUnconfigurableLines(
    linesToAdd,
    linesToDel
  )

  return linesToAdd, linesToDel
end

--- Convert from config lines and context to a vtysh `-c` command.
function FrrReloadUtils.linesToFrrConfig(ctxKeys, line, delete)
  local cmd = {}

  if type(line) == "string" and line ~= "" then
    for i, ctxKey in pairs(ctxKeys) do
      table.insert(cmd, string.rep(" ", i) .. ctxKey)
    end

    line = line:lstrip()
    local indent = string.rep(" ", tablex.size(ctxKeys))

    -- There are some commands that are on by default so their "no" form will be
    -- displayed in the config.  "no bgp default ipv4-unicast" is one of these.
    -- If we need to remove this line we do so by adding
    -- "bgp default ipv4-unicast", not by doing a "no no ..."
    if delete then
      if line:startswith("no ") then
        table.insert(cmd, string.format("%s%s", indent, line:sub(4)))
      else
        table.insert(cmd, string.format("%sno %s", indent, line))
      end
    else
      table.insert(cmd, indent .. line)
    end

  -- If line is "true" then we are typically deleting an entire
  -- context ('no router ospf' for example)
  elseif type(line) ~= "string" then
    local lastKey = { ctxKeys[#ctxKeys] }
    local otherKeys = tablex.difference(ctxKeys, lastKey)

    for i, ctxKey in pairs(otherKeys) do
      table.insert(cmd, string.rep(" ", i) .. ctxKey)
    end

    -- If delete is true, only put/remove the 'no' on the last sub-context
    if delete then
      if lastKey[1]:startswith("no ") then
        table.insert(
          cmd,
          string.rep(" ", tablex.size(otherKeys)) .. lastKey[1]:sub(4)
        )
      else
        table.insert(
          cmd,
          string.format(
            "%sno %s",
            string.rep(" ", tablex.size(otherKeys)),
            lastKey[1]
          )
        )
      end
    else
      table.insert(
        cmd,
        string.rep(" ", tablex.size(otherKeys)) .. lastKey[1]
      )
    end
  end

  return cmd
end

local function createParser()
  local parser = argparse("frr_reload", "Dynamically apply diff in frr configs")
  parser:help_max_width(80)
  parser:argument("filename", "Location of new frr config file"):args(1)
  parser:mutex(
    parser:flag("-r --reload", "Apply the deltas", false),
    parser:flag("-t --test", "Show the deltas", false)
  )
  parser:option(
    "--input",
    "Read running config from file instead of vtysh 'show running'"
  )
  parser:option("--bindir", "Path to the vtysh executable", "/usr/bin")
  parser:option("--confdir", "Path to the frr config files", "/etc/frrouting")
  parser:option("--daemon", "Daemon for which want to replace the config", "")
  parser:option(
    "--vty_socket",
    "Socket to be used by vtysh to connect to the daemons",
    ""
  )
  parser:option("-N --pathspace", "Reload specific path/namespace")
  parser:option(
    "--log-level",
    "Log level {critical, error, warning, info, debug}",
    "info"
  )
  parser:flag(
    "-d --debug",
    "Enable debugs (synonym for --log-level=debug)",
    false
  )
  parser:flag("-o --stdout", "Log to STDOUT", false)
  parser:flag(
    "-w --overwrite",
    "Overwrite frr.conf with running config output",
    false
  )
  return parser
end

local function main()
  local parser = createParser()
  local args = parser:parse()

  -- Setup logging system based on the flags and options
  if args.debug then
    logger.level = "debug"
  else
    logger.level = args.log_level
  end

  if (not args.test) and (not args.reload) then
    logger.error("Must specify --reload or --test")
    os.exit(1)
  end

  -- Verify that the new config file is a file
  if not path.isfile(args.filename) then
    logger.error("Filename %s does not exist", args.filename)
    os.exit(1)
  end
  if not path.getsize(args.filename) then
    logger.error("Filename %s is an empty file", args.filename)
    os.exit(1)
  end

  -- Verify that confdir is correct
  -- NOTE: We don't use this, so commenting out "confdir" code.
  --[[
  if not path.isdir(args.confdir) then
    logger.error("Confdir %s is not a valid path", args.confdir)
    os.exit(1)
  end
  --]]

  -- Verify that bindir is correct
  if (
    not path.isdir(args.bindir)
    or not path.isfile(path.join(args.bindir, "vtysh"))
  ) then
    logger.error("Bindir %s is not a valid path to vtysh", args.bindir)
    os.exit(1)
  end

  -- Verify that the vty_socket, if specified, is valid
  if args.vty_socket ~= "" and not path.isdir(args.vty_socket) then
    logger.error("vty_socket %s is not a valid path", args.vty_socket)
    os.exit(1)
  end

  local vtysh = Vtysh.new(args)

  -- Create a Config object from the config generated by newConf
  local newConf = Config.new(vtysh)
  local reloadOk = pcall(newConf.loadFromFile, newConf, args.filename)
  if not reloadOk then
    logger.error("vtysh failed to process new configuration")
    os.exit(1)
  end

  if args.test then
    -- Create a Config object from the running config
    local running = Config.new(vtysh)

    if args.input then
      reloadOk = pcall(running.loadFromFile, running, args.input)
      if not reloadOk then
        logger.error("vtysh failed to process new configuration")
        os.exit(1)
      end
    else
      running:loadFromShowRunning(args.daemon)
    end

    local linesToAdd, linesToDel = FrrReloadUtils.compareContextObjects(
      newConf,
      running
    )

    if type(linesToDel) == "table" and tablex.size(linesToDel) > 0 then
      logger.info("\nLines To Delete")
      logger.info("===============")
      for _, delEntry in pairs(linesToDel) do
        local ctxKeys, line = delEntry[1], delEntry[2]

        if line ~= "!" then
          local cmd = FrrReloadUtils.linesToFrrConfig(ctxKeys, line, true)
          logger.info(("\n"):join(cmd))
        end
      end
    end

    if type(linesToAdd) == "table" and tablex.size(linesToAdd) > 0 then
      logger.info("\nLines To Add")
      logger.info("============")
      for _, addPair in pairs(linesToAdd) do
        local ctxKeys, line = addPair[1], addPair[2]

        if line ~= "!" then
          local cmd = FrrReloadUtils.linesToFrrConfig(ctxKeys, line, false)
          logger.info(("\n"):join(cmd))
        end
      end
    end

  elseif args.reload then
    -- Check if vtysh config is available
    if not vtysh:isConfigAvailable() then
      os.exit(1)
    end

    logger.debug("\nNew Frr config\n%s", newConf:getLines())

    -- We do two passes of diff generation and application of configs:
    -- In the first pass we add and remove lines
    -- In the second pass we only add, to make sure no extra line was removed
    local linesToAddFirstPass = {}

    for pass = 1, 2 do
      local running = Config.new(vtysh)
      running:loadFromShowRunning(args.daemon)
      logger.debug(
        "\nRunning Frr config (Pass #%d)\n%s",
        pass,
        running:getLines()
      )

      local linesToAdd, linesToDel = FrrReloadUtils.compareContextObjects(
        newConf,
        running
      )

      -- We intentionally redo all "adds" from the first pass in the second pass
      -- (See frr-reload.py for explanation/example)
      if pass == 1 then
        linesToAddFirstPass = tablex.deepcopy(linesToAdd)
      else
        tablex.move(linesToAdd, linesToAddFirstPass, #linesToAdd + 1)
      end

      -- Only do deletes on the first pass
      if tablex.size(linesToDel) > 0 and pass == 1 then
        for _, delEntry in pairs(linesToDel) do
          local ctxKeys, line = delEntry[1], delEntry[2]

          if line ~= "!" then
            local cmd = FrrReloadUtils.linesToFrrConfig(ctxKeys, line, true)
            table.insert(cmd, 1, "configure")
            vtysh:call(cmd)
            logger.debug('Executed "%s"', (" "):join(cmd))
          end
        end
      end

      if tablex.size(linesToAdd) > 0 then
        local linesToConfigure = {}
        for _, addEntry in pairs(linesToAdd) do
          local ctxKeys, line = addEntry[1], addEntry[2]

          if line ~= "!" then
            -- Don't run "no" commands twice
            if pass == 2 and ctxKeys[1]:startswith("no ") then
              goto continue
            end

            local cmd = ("\n"):join(
              FrrReloadUtils.linesToFrrConfig(ctxKeys, line, false)
            ) .. "\n"
            table.insert(linesToConfigure, cmd)
          end
          ::continue::
        end

        if tablex.size(linesToConfigure) > 0 then
          local filename = os.tmpname()
          logger.info(
            "%s content (Pass #%d):\n%s\n%s\n%s",
            filename,
            pass,
            ("="):rep(40),
            ("\n"):join(linesToConfigure),
            ("="):rep(40)
          )

          -- Open file and write all lines from linesToConfigure
          local tmpFile = io.open(filename, "w+")
          for _, line in ipairs(linesToConfigure) do
            tmpFile:write(tostring(line))
          end
          tmpFile:close()

          -- Execute the vtysh call on the file
          vtysh:execFile(filename)

          -- Clear temporary file
          os.remove(filename)
        end
      end
    end

    -- Make these changes persistent by overwriting frr.conf
    --[[
    local target = path.join(args.confdir, "frr.conf")
    if args.overwrite or args.filename ~= target then
    --]]
    if args.overwrite then
      vtysh:call({"write"})
    end
  end

  if not reloadOk then
    os.exit(1)
  end
end

if tg_utils.isMain() then
  main()
else
  return Vtysh, Context, Config, FrrReloadUtils
end
