/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

class FakeWebSocket {
  constructor(generator, interval) {
    this.intervalId = setInterval(generator, interval);
  }
  close() {
    clearInterval(this.intervalId);
  }
}

export function getLinkStats(dataCallback) {
  const keys = [
    { key: "mcs", min: 9, max: 12 },
    { key: "snr", min: -10, max: 30 },
    { key: "rssi", min: -70, max: -50 }
  ];
  const links = [
    { radioMac: "04:ce:14:fe:c8:65", responderMac: "04:ce:14:fe:c6:c9" },
    { radioMac: "04:ce:14:fe:c6:e1", responderMac: "04:ce:14:fe:c6:df" }
  ];
  let i = 0;
  return new FakeWebSocket(() => {
    const timestamp = (1279677232 + i) * 1e6;
    i++;
    keys.forEach(({ key, min, max }) => {
      links.forEach(({ radioMac, responderMac }) =>
        dataCallback({
          key,
          timestamp,
          value: Math.floor(Math.random() * (max - min)) + min,
          radioMac,
          responderMac
        })
      );
    });
  }, 1000);
}
