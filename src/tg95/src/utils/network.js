/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

// Return a formatted GPS position string given a latitude/longitude.
export function formatPositionString(lat, lon) {
  const latStr = lat >= 0 ? lat + "\xB0 N" : -lat + "\xB0 S";
  const lonStr = lon >= 0 ? lon + "\xB0 E" : -lon + "\xB0 W";
  return latStr + " " + lonStr;
}

// Convert a link quality metric (LQM) to signal-to-noise ratio (SNR), in dB.
export function lqmToSnr(lqm) {
  return (lqm - 256) / 8;
}

// Convert a beam index to beam angle, in degrees.
export function beamIndexToAngle(beamIdx) {
  return beamIdx * 1.5 - 45;
}

// Given an initiator-to-responder LQM matrix, return three values:
// best SNR, Tx beam angle, Rx beam angle
//
// This searches for the best beam with the smallest combined angle.
export function findBestBeam(itorLqmMat) {
  let bestLqm = 0;
  let bestTxBeamAngle, bestRxBeamAngle;
  let bestCombinedAngle = 0;
  Object.entries(itorLqmMat).forEach(([txBeamIdx, rxLqmMat]) => {
    const txBeamAngle = beamIndexToAngle(txBeamIdx);
    Object.entries(rxLqmMat).forEach(([rxBeamIdx, lqm]) => {
      const rxBeamAngle = beamIndexToAngle(rxBeamIdx);
      const combinedAngle = Math.abs(txBeamAngle) + Math.abs(rxBeamAngle);
      if (
        lqm > bestLqm ||
        (lqm === bestLqm && combinedAngle < bestCombinedAngle)
      ) {
        bestLqm = lqm;
        bestTxBeamAngle = txBeamAngle;
        bestRxBeamAngle = rxBeamAngle;
        bestCombinedAngle = combinedAngle;
      }
    });
  });
  const bestSnr = bestLqm ? lqmToSnr(bestLqm) : null;
  return { bestSnr, bestTxBeamAngle, bestRxBeamAngle };
}

// Return whether the input is a valid MAC address (in colon-separated format).
export function validateMacAddr(mac) {
  const re = /^([0-9A-Fa-f]{2}:){5}([0-9A-Fa-f]{2})$/;
  return re.test(mac);
}
