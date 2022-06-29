# PHY Algorithms
This addendum to PHY_Algorithms.md is specific for QTI.

## Maximum AGC
Assumptions
- FB FW will get the measured raw ADC RSSI - the RSSI after the ADC -
for the last received packet per link (is this correct?)
- FB FW will set the maximum IF gain index; the maximum IF gain is set to
maintain the raw ADC RSSI at the target plus some margin
- FB FW will set G0/G1 according to the currently measured SNR and
a configurable threshold; G0 is used at lower SNR (more sensitivity); G1
is used at higher SNR (less distortion)
- CRS (carrier sense threshold) is set by configuration and is not
updated/adaptive

#### Algorithm Description
maxAgcMaxRfGainIndex should be set to 0 and then:
```
relativeRSSI = c*rawAdcRssi - a*IF
```
- `c` is set to 1.0 (maxAgcRawAdcScaleFactorQ8 = 256)
- `a` is set to 1.0 (maxAgcIfGaindBperIndexQ8 = 256)

The average RSSI used in the tracking algorithm is a filtered version of
relativeRSSI.  The rawAdcRssi is what is measured after the ADC; the AGC
tries to make rawAdcRssi constant and we assume the optimal constant
is targetRawAdc (configurable).

#### Calculating Gains
The maximum IF gain is calculated using the configurable margin.

##### Details
```
target RSSI = filtered relativeRSSI - margin = -a*XIF + c*targetRawAdc
```
`XIF` is the desired gain to calculate. `targetRawAdc` is a
configurable input (default -14), and `margin` is the configurable margin (in
dB).

Let target gain be defined as follows:
```
targetGain := filtered relativeRSSI - margin - c*targetRawAdc
```

`XIF` is calculated as follows:
```
XIF = -targetGain / a
```

#### P2MP
Assume that gains can be set independently for each P2MP link.

#### RF Gain Hi/Lo
G0/G1 (referred to as "RFgain") is selected
according to the measured SNR and a threshold, including hysteresis
(hysteresis is 1dB - not configurable):
```
if SNR > threshold + hysteresis:
  RFgain = 1

if SNR < threshold - hysteresis:
  RFgain = 0

if threshold - hysteresis < SNR < threshold + hysteresis:
  RFgain remains at the current setting
```

### Configuration Options
The firmware configuration options are listed in the table below.

| Parameter                   | Description |
| --------------------------- | ----------- |
| `maxAgcIfGaindBperIndexQ8`  | The `a` coefficient above (set to 1.0 (default)) |
| `maxAgcMaxRfGainIndex`      | The maximum allowed RF gain index (set to 0) |
| `maxAgcMinRfGainIndex`      | The minimum allowed RF gain index (not used) |
| `maxAgcMaxIfGainIndex`      | The maximum allowed IF gain index (set to 31? (default)) |
| `maxAgcMinIfGainIndex`      | The minimum allowed IF gain index (set to 0? (default)) |
| `maxAgcMaxIfSweetGainRange` | See discussion on sweet IF range above (not used) |
| `maxAgcMinIfSweetGainRange` | See discussion on sweet IF range above (not used) |
| `maxAgcMinRssi`             | If maximum AGC tracking is disabled, a fixed value can be used for minRSSI or maximum AGC (based on `useMinRssi`) (not used)|
| `maxAgcRawAdcScaleFactorQ8` | The `c` coefficient above (set to 1.0?)|
| `maxAgcRfGaindBperIndexQ8`  | The `b` coefficient above (not used)|
| `maxAgcTargetRawAdc`        | The target raw ADC RSSI described above (set according to what you expect)|
| `maxAgcTrackingEnabled`     | Enables run-time tracking (set to 1 (default))|
| `maxAgcTrackingMargindB`    | The margin described above (default 7dB - let's start with that)|
| `maxAgcUseMinRssi`          | Determines whether to use minRSSI or maximum AGC (set to 0 (default))|
| `maxAgcUseSameForAllSta`    | See discussion on P2MP above (set to 0)|
| `maxAgcRfGainHiLo`          | Setting bit 0 to `1` enables the feature; bits 15:8 represent the threshold (dB) (set to SNRthresh << 8 + 1)|
