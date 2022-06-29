#!/usr/bin/env lua

-- Copyright (c) Meta Platforms, Inc. and affiliates.
--
-- This source code is licensed under the MIT license found in the
-- LICENSE file in the root directory of this source tree.

local lu = require "luaunit"
local tabulate = require "tg.tabulate"
require("pl.stringx").import()

TestMain = {}

--- Test that empty tables work.
function TestMain:testEmptyTable()
  lu.assertEquals(tabulate.tabulate({}), "")
  lu.assertEquals(tabulate.tabulate({}, {headers = {}}), "")
end

--- Test different table formats.
function TestMain:testTableFormats()
  local headers = {"fruits", "daily intake", "commentary", "yes/no"}
  local rows = {
    {"Apple",        32, nil,   "yes"},
    {"Orange",     4444, "wow", "yes"},
    {"Watermelon",    2, nil,   "yes"},
    {"Pear",          6, nil,   "no"}
  }

  -- hack around editors deleting trailing whitespace
  local whitespaceCh = "`"

  lu.assertEquals(
    tabulate.tabulate(
      rows, {headers = headers, fmt = tabulate.TableFormat.PLAIN}
    ):gsub(" ", whitespaceCh),
    ([[
fruits````````daily`intake``commentary```yes/no```
Apple```````````````````32```````````````yes``````
Orange````````````````4444``wow``````````yes``````
Watermelon```````````````2```````````````yes``````
Pear`````````````````````6```````````````no```````
    ]]):rstrip():sub(1, -2)
  )

  lu.assertEquals(
    tabulate.tabulate(
      rows, {headers = headers, fmt = tabulate.TableFormat.SIMPLE}
    ):gsub(" ", whitespaceCh),
    ([[
fruits````````daily`intake``commentary```yes/no```
-----------``-------------``-----------``-------``
Apple```````````````````32```````````````yes``````
Orange````````````````4444``wow``````````yes``````
Watermelon```````````````2```````````````yes``````
Pear`````````````````````6```````````````no```````
    ]]):rstrip():sub(1, -2)
  )
end

--- Test tables with multi-line rows.
function TestMain:testMultilineRows()
  local headers = {"1", "2", "3"}
  local rows = {{"one\ntwo\nthree", nil, "one"}, {"four", "one", "two\nthree"}}

  -- hack around editors deleting trailing whitespace
  local whitespaceCh = "`"

  lu.assertEquals(
    tabulate.tabulate(rows, {headers = headers}):gsub(" ", whitespaceCh),
    ([[
1```````2`````3```````
------``----``------``
one```````````one`````
two```````````````````
three`````````````````
four````one```two`````
``````````````three```
    ]]):rstrip()
  )
end

--- Test table options.
function TestMain:testTableOpts()
  local headers = {"a", "s", "d", "f"}
  local rows = {{"12", "34", "56", "78"}, {"a", "bb", "ccc", "dddd"}}
  local pad = 5
  local skip_lines = true

  -- hack around editors deleting trailing whitespace
  local whitespaceCh = "`"

  lu.assertEquals(
    tabulate.tabulate(
      rows,
      {headers = headers, pad = pad, skip_lines = skip_lines}
    ):gsub(" ", whitespaceCh),
    ([[
```a````````s````````d`````````f`````
----`````----`````----`````-----`````
``12```````34```````56````````78`````
`````````````````````````````````````
```a```````bb``````ccc``````dddd`````
    ]]):rstrip()
  )
end

os.exit(lu.LuaUnit.run("-v"))
