# Terragraph CLI
This document describes Terragraph command-line utilities for interacting with
Terragraph software.

## "tg2" CLI
`tg2` is a Lua-based command-line utility for interacting with various software
components including the E2E controller, E2E minion, driver interface, and stats
agent. Most CLI commands are translated into ZMQ messages and sent to the
respective services.

The CLI is intended to serve as a tool for basic debugging, *not* to provide a
comprehensive API. Documentation for individual CLI commands is available via
the `--help` flag. Some top-level commands are shown in the table below.

| Category         | "tg2" commands                      |
| ---------------- | ----------------------------------- |
| Stats            | `stats`, `event`                    |
| Driver interface | `fw`                                |
| E2E minion       | `minion`                            |
| E2E controller   | `controller`, `topology`            |
| Information      | `version`, `tech-support`, `whoami` |

The `tg2` source code is largely contained within
`src/terragraph-e2e/lua/tg2.lua`. It parses arguments using [Argparse], uses
[lzmq] for ZMQ bindings, and uses Apache Thrift client libraries and code
generators (*not* fbthrift, which lacks Lua support).

Unit tests for `tg2` reside in `src/terragraph-e2e/lua/tests/tg2_test.lua`. The
tests are written using the [LuaUnit] testing framework.

<a id="terragraph-cli-tg-cli"></a>

## "tg" CLI
Terragraph also provides `tg`, a Python-based CLI. `tg` is similar to `tg2`
(though usage is not identical) and implements a slightly broader feature set,
but runs significantly slower and has largely been deprecated.

A rough categorization of the top-level commands is shown in the table below.

| Category | "tg" commands          |
| -------- | ---------------------- |
| E2E      | `config`, `event`, `ignition`, `link`, `node`, `scan`, `site`, `status`, `topology`, `traffic`, `upgrade`, `version` |
| Firmware | `fw`                   |
| Stats    | `counters`             |
| Debug    | `minion`, `scp`, `ssh` |

The `tg` source code resides in `src/tg/`. The main class is `tg.py`, which
parses arguments using [Click] and invokes the appropriate command submodule.
All command submodules reside in the `commands/` subdirectory, and inherit the
base class `BaseCmd` (in `commands/base.py`).

The CLI behaves like a standard Click program, with the following exceptions:
* The program provides an explicit `help` command (in `commands/help.py`) which
  recursively prints Click's `--help` output for every available command.
* The main class does some preliminary parsing or arguments before passing them
  into Click. This is an optimization so that it can only import submodules
  which are actually needed (i.e. for the command being called).

## Resources
* [Argparse] - Lua package for command-line interfaces (based on Python)
* [lzmq] - Lua ZMQ bindings
* [LuaUnit] - Lua unit testing framework
* [Click] - Python package for command-line interfaces

[Argparse]: https://github.com/luarocks/argparse
[lzmq]: https://github.com/zeromq/lzmq
[LuaUnit]: https://github.com/bluebird75/luaunit
[Click]: http://click.pocoo.org
