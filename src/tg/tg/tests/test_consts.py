#!/usr/bin/env python3

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

import unittest

from tg.commands import consts


class TestConsts(unittest.TestCase):
    """ Test string encoding coversion """

    def test_bytes_string_encoding(self):
        uni_str = consts.byte_string_decode("test_string")
        self.assertTrue(isinstance(uni_str, str))
        byte2uni_str = consts.byte_string_decode(b"test_string")
        self.assertTrue(isinstance(byte2uni_str, str))


if __name__ == "__main__":
    unittest.main()
