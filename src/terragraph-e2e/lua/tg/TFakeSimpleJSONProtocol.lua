-- Copyright (c) Meta Platforms, Inc. and affiliates.
--
-- This source code is licensed under the MIT license found in the
-- LICENSE file in the root directory of this source tree.

--- `TSimpleJSONProtocol` Thrift implementation.
--
-- * Only deserialization is implemented.
-- * The deserializer relies on an external JSON decoder for simplicity, and
--   operates on primitive Lua tables instead. It also relies on a custom
--   `__TObject:read()` function that accepts field names in place of IDs and
--   types.
--
-- @classmod TFakeSimpleJSONProtocol

require "Thrift"
require "TProtocol"
local cjson_safe = require "cjson.safe"

-- `TSimpleJSONProtocol` implementation.
TFakeSimpleJSONProtocol = __TObject.new(TProtocolBase, {
  __type = 'TFakeSimpleJSONProtocol',
  stack = {}
})

-- Stack utilities
local STACK_FIELD_END_MARKER = {} -- compared by address only
function TFakeSimpleJSONProtocol:push(x)
  table.insert(self.stack, 1, x)
end
function TFakeSimpleJSONProtocol:pop()
  local x = nil
  if #self.stack > 0 then
    x = self.stack[1]
    table.remove(self.stack, 1)
  end
  return x
end
function TFakeSimpleJSONProtocol:stackSize() return #self.stack end
function TFakeSimpleJSONProtocol:stackTop() return self.stack[1] end

-- Structs: push each table field on the stack (one at a time)
function TFakeSimpleJSONProtocol:readStructBegin() end
function TFakeSimpleJSONProtocol:readStructEnd() end
function TFakeSimpleJSONProtocol:readFieldBegin()
  local fname, ftype, fid = nil, TType.STOP, 0
  if self:stackSize() > 0 then
    local k, v = next(self:stackTop())
    if k == nil then
      -- No more table fields, pop the struct
      self:pop()
    else
      -- Process the next table field
      fname = k
      ftype = nil  -- HACK!
      self:stackTop()[k] = nil
      self:push(STACK_FIELD_END_MARKER)
      self:push(v)
    end
  end
  return fname, ftype, fid
end
function TFakeSimpleJSONProtocol:readFieldEnd()
  -- If a value wasn't read for whatever reason, pop it
  while self:stackSize() > 0 and self:pop() ~= STACK_FIELD_END_MARKER do end
end

-- Maps: push all key-value pairs on the stack
function TFakeSimpleJSONProtocol:readMapBegin()
  local map = self:pop()
  local kttype, vttype, size = nil, nil, 0
  for k, v in pairs(map) do
    self:push(v)
    self:push(k)
    size = size + 1
  end
  return kttype, vttype, size
end
function TFakeSimpleJSONProtocol:readMapEnd() end

-- Lists & sets: push all values on the stack
function TFakeSimpleJSONProtocol:readListBegin()
  local list = self:pop()
  for i = #list, 1, -1 do
    self:push(list[i])
  end
  return nil, #list
end
function TFakeSimpleJSONProtocol:readListEnd() end
function TFakeSimpleJSONProtocol:readSetBegin()
  -- serialized as a list
  return self:readListBegin()
end
function TFakeSimpleJSONProtocol:readSetEnd() return self:readListEnd() end

-- Primitives: read the next value off the stack
function TFakeSimpleJSONProtocol:readBool() return self:pop() end
function TFakeSimpleJSONProtocol:readByte() return self:pop() end
function TFakeSimpleJSONProtocol:readI16() return self:pop() end
function TFakeSimpleJSONProtocol:readI32() return self:pop() end
function TFakeSimpleJSONProtocol:readI64() return self:pop() end
function TFakeSimpleJSONProtocol:readDouble() return self:pop() end
function TFakeSimpleJSONProtocol:readString() return self:pop() end

-- `TFakeSimpleJSONProtocol` factory.
TFakeSimpleJSONProtocolFactory = TProtocolFactory:new{
  __type = 'TFakeSimpleJSONProtocolFactory',
}

--- Return a `TFakeSimpleJSONProtocol` instance from a JSON string.
function TFakeSimpleJSONProtocolFactory:getProtocol(json)
  local t, err = cjson_safe.decode(json)
  if t == nil then
    terror(TProtocolException:new{message = err})
  end
  return TFakeSimpleJSONProtocol:new{
    stack = {t},
    trans = {} -- unused (but required by TProtocolBase)
  }
end
