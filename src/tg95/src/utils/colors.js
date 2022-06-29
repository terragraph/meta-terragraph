/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

// Use a seedable PRNG to get deterministic "random colors".
function mulberry32(a) {
  return function() {
    var t = (a += 0x6d2b79f5);
    t = Math.imul(t ^ (t >>> 15), t | 1);
    t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}
const random = mulberry32(100);

// Cached colors
const colors = [];

// Return a color with the given (arbitrary) index.
export function getColor(idx) {
  while (colors.length <= idx) {
    const rand256 = () => Math.floor(random() * 256);
    colors.push(`rgb(${rand256()},${rand256()},${rand256()})`);
  }
  return colors[idx];
}
