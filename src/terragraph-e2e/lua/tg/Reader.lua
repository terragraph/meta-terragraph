-- Copyright (c) Meta Platforms, Inc. and affiliates.
--
-- This source code is licensed under the MIT license found in the
-- LICENSE file in the root directory of this source tree.

--- Reader class.
-- @classmod Reader

local Reader = {}
Reader.__index = Reader

--- Initialize a reader on a given data stream.
--
-- - `data` (table): array of bytes (numbers)
-- - `le` (bool): if set, treat data as little-endian (default is big-endian)
function Reader.new(data, le)
  assert(type(data) == "table")

  local self = setmetatable({}, Reader)
  self.data = data
  self.idx = 1
  self.le = le and true or false
  return self
end

--- Return the current reader position (1-indexed), which may be beyond the
-- bounds of the data stream.
function Reader:position()
  return self.idx
end

--- Return the number of bytes remaining in the data stream.
function Reader:available()
  return 1 + #self.data - self.idx
end

--- Reset the current index to a given position, or the start of the stream.
--
-- - `idx` (int): the position (1-indexed)
function Reader:reset(idx)
  self.idx = type(idx) == "number" and idx or 1
end

--- Advance the current reader position by the given number of bytes.
function Reader:skip(n)
  self.idx = self.idx + n
end

--- Read a byte.
function Reader:readByte()
  local b = self.data[self.idx]
  self.idx = self.idx + 1
  return b
end

--- Read a signed word (2 bytes).
function Reader:readWord()
  local w = self:readUWord()
  local mask = 2 ^ 15
  return bit32.bxor(w, mask) - mask
end

--- Read an unsigned word (2 bytes).
function Reader:readUWord()
  local w = self.le
    and bit32.bor(
      self.data[self.idx],
      bit32.lshift(self.data[self.idx+1], 8)
    )
    or bit32.bor(
      bit32.lshift(self.data[self.idx], 8),
      self.data[self.idx+1]
    )
  self.idx = self.idx + 2
  return w
end

--- Read a signed integer (4 bytes).
function Reader:readInt()
  local w = self:readUInt()
  local mask = 2 ^ 31
  return bit32.bxor(w, mask) - mask
end

--- Read an unsigned integer (4 bytes).
function Reader:readUInt()
  local i = self.le
    and bit32.bor(
      self.data[self.idx],
      bit32.lshift(self.data[self.idx+1], 8),
      bit32.lshift(self.data[self.idx+2], 16),
      bit32.lshift(self.data[self.idx+3], 24)
    )
    or bit32.bor(
      bit32.lshift(self.data[self.idx], 24),
      bit32.lshift(self.data[self.idx+1], 16),
      bit32.lshift(self.data[self.idx+2], 8),
      self.data[self.idx+3]
    )
  self.idx = self.idx + 4
  return i
end

--- Read the given number of bytes.
function Reader:readBytes(n)
  local bytes = {}
  for i = 1, n do
    bytes[#bytes+1] = self:readByte()
  end
  return bytes
end

return Reader
