std = "lua52"
unused_args = false
unused_secondaries = false
self = false
max_code_line_length = 80
max_comment_line_length = 80
max_string_line_length = false
globals = {}
exclude_files = {}

-- We need to expose some globals prefixed with "Test" for testing with luaunit.
files["**/*_test.lua"] = {
  ignore = {"111/Test.*", "112/Test.*", "113/Test.*"}
}

-- All thrift library and generated code lives in the global table.
files["**/tg2*.lua"] = {
  ignore = {"113"}
}
files["**/puff*.lua"] = {
  ignore = {"113"}
}
files["**/get_pop_ip.lua"] = {
  ignore = {"113"}
}
files["**/thrift_utils.lua"] = {
  ignore = {"113/T.*", "113/liblualongnumber"}
}
files["**/tg_thrift_utils_test.lua"] = {
  ignore = {"113"}
}
files["**/TFakeSimpleJSONProtocol.lua"] = {
  ignore = {"111", "112", "113"}
}
files["**/TFakeSimpleJSONProtocol_test.lua"] = {
  ignore = {"113"}
}
files["**/frr_bgp_healthcheck.lua"] = {
  ignore = {"113"}
}
files["**/frr_openr_sync.lua"] = {
  ignore = {"113"}
}
files["**/ZmqMonitorClient.lua"] = {
  ignore = {"113"}
}
