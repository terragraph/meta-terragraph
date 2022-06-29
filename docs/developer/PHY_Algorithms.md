# PHY Algorithms
This document describes the configuration of some physical (PHY) layer
algorithms.

## Maximum AGC & Minimum RSSI
To limit early-weak interference, Terragraph includes a feature to either limit
the maximum automatic gain control (AGC) gain or the minimum received signal
strength indicator (RSSI). An interfering signal that is below the minimum RSSI
threshold or requires a gain above the maximum AGC limit will not be detected.
An algorithm running in the firmware tracks the gain and RSSI of the desired
signal, and limits the maximum AGC and minimum RSSI at some margin from the
desired signal.

The AGC has an IF and RF gain component. The overall gain (in dB) is calculated
as follows, using configurable vendor-specific coefficients `a` (default 1.0)
and `b` (default 7.0):
```
gain = a*IF + b*RF
```

For example, if the AGC of the desired signal is `IF = 3, RF = 2` and the
corresponding `RSSI = -40dBm`, the maximum AGC could be set to `IF = 4, RF = 3`
and `minRSSI = -48dBm`. Any signal requiring a gain greater than
`IF = 4, RF = 3` or having `RSSI < -48dBm` would not be detected.

### Algorithm Description
The hardware provides the measured RSSI value, and optionally the IF and RF
gains and the *raw* analog-to-digital converter (ADC) RSSI, i.e. the RSSI
measured after gains are applied. The algorithm can either calculate a relative
RSSI based on the gains and raw RSSI (if `useMinRssi` is not set) or use the
measured RSSI (if `useMinRssi` is set).

When `useMinRssi` is not set, the relative RSSI is calculated as follows, using
a configurable coefficient `c` (default 0.5):
```
relativeRSSI = c*rawAdcRssi - a*IF - b*RF
```

The algorithm filters the RSSI using a "fast drop slow rise" infinite impulse
response (IIR) filter, and then computes the maximum AGC and minimum RSSI using
the filtered output and a configured margin.

In summary:
1. RSSI is reported by the PHY.
2. Filter the RSSI using "fast drop slow rise" IIR filter.
3. Subtract the configurable margin from the filtered RSSI (this is the minimum
   RSSI).
4. Optionally, calculate the corresponding IF and RF gains.
5. Configure the hardware with either the minimum RSSI or the maximum AGC IF and
   RF gains.

### Calculating Gains
The IF and RF gains are calculated given the RSSI target, and are configured
using vendor-proprietary indices. By default, this assumes 1.0dB per IF index
and 7.0dB per RF index (as mentioned above). There is also a vendor-proprietary
and configurable range for each index. By default, the IF gain index can be
[0:31] and the RF gain index can be [0:5].

When `useMinRssi` is not set and the raw ADC RSSI is reported, the actual raw
ADC RSSI might differ from the desired raw ADC RSSI target. The IF and RF gains
are normally set such that the raw ADC RSSI will hit a desired target, ensuring
the signal does not clip and thus minimizing quantization noise. If the reported
raw ADC RSSI is not equal to the desired level, the gains are adjusted
accordingly.

The mapping from RSSI to IF and RF gains is not unique given the ranges above;
for example, `IF = 3, RF = 2` and `IF = 10, RF = 1` both have the same gain. To
deal with this, a "sweet" IF gain range can be configured such that RF gain will
be chosen to keep the IF gain within the "sweet" range. By default, this is
between 7 and 17. By keeping the sweet range larger than the RF gain step,
thrashing can be prevented where the IF and RF gains keep switching back and
forth, e.g. between `IF = 3, RF = 2` and `IF = 9, RF = 1`.

#### Details
```
target RSSI = filtered RSSI - margin = -a*XIF -b*XRF + c*targetRawAdc
```
`XIF` and `XRF` are the desired gains to calculate. `targetRawAdc` is a
configurable input (default -14), and `margin` is the configurable margin (in
dB).

Let target gain be defined as follows:
```
targetGain := filtered RSSI - margin - c*targetRawAdc
```

Given that `XRF` is the current RF gain, `XIF` is calculated as follows:
```
XRF = current RF
XIF = -(targetGain + b*XRF) / a
```
Now, if `XIF` is outside the sweet range, `XRF` is incremented or decremented to
try and move it into the sweet range, or until hitting the RF range limit.

### P2MP
In a point-to-multipoint scenario, a node receives signals from multiple sources
and the RSSI from each source can differ. There is a configurable option to set
the maximum gain and minimum RSSI to the smallest amongst all signals, or to set
each maximum gain and minimum RSSI separately for each station (i.e. link).

### RF Gain Hi/Lo
This is a vendor-proprietary option where RF gain selection (1 or 0) is selected
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
The firmware configuration options for AGC are listed in the table below.

| Parameter                   | Description |
| --------------------------- | ----------- |
| `maxAgcIfGaindBperIndexQ8`  | The `a` coefficient above (default 1.0dB) |
| `maxAgcMaxRfGainIndex`      | The maximum allowed RF gain index (default 5) |
| `maxAgcMinRfGainIndex`      | The minimum allowed RF gain index (default 0) |
| `maxAgcMaxIfGainIndex`      | The maximum allowed IF gain index (default 31) |
| `maxAgcMinIfGainIndex`      | The minimum allowed IF gain index (default 0) |
| `maxAgcMaxIfSweetGainRange` | See discussion on sweet IF range above (default 17) |
| `maxAgcMinIfSweetGainRange` | See discussion on sweet IF range above (default 7) |
| `maxAgcMinRssi`             | If maximum AGC tracking is disabled, a fixed value can be used for minRSSI or maximum AGC (based on `useMinRssi`) (default -40) |
| `maxAgcRawAdcScaleFactorQ8` | The `c` coefficient above (default 0.5dB) |
| `maxAgcRfGaindBperIndexQ8`  | The `b` coefficient above (default 7.0dB) |
| `maxAgcTargetRawAdc`        | The target raw ADC RSSI described above (default -14) |
| `maxAgcTrackingEnabled`     | Enables run-time tracking (default 1) |
| `maxAgcTrackingMargindB`    | The margin described above (default 7dB) |
| `maxAgcUseMinRssi`          | Determines whether to use minRSSI or maximum AGC (default 0) |
| `maxAgcUseSameForAllSta`    | See discussion on P2MP above (default 1) |
| `maxAgcRfGainHiLo`          | Setting bit 0 to `1` enables the feature; bits 15:8 represent the threshold (dB) (default 0) |

### Dynamic AGC Configuration
There are two mechanisms for changing AGC parameters on-the-fly while a link
is up without interrupting service. These mechanisms are described below.

#### Using FW_CONFIG_REQ
The `FW_CONFIG_REQ` command can be issued via the TG CLI or r2d2, instructing
the E2E minion to apply new firmware parameters on-the-fly.

This method supports only a subset of all firmware parameters. The available
parameters can be listed using the TG CLI:
```
$ tg fw node -n <any dummy value> set_fw_params -s
```

Example usage:
```
# Set a link parameter via TG CLI
$ tg fw node -n <node name> set_fw_params -r <peer node name> <parameter> <value>

# Set a link parameter via r2d2
$ r2d2 fw_set_params -m <peer MAC address> <parameter> <value>

# Get the current value of a link parameter via TG CLI
$ tg fw node -n <node name> get_fw_params linkParams -r <peer node name>
```

#### Using PhyAgcParams
The entire `PhyAgcParams` Thrift structure can be passed via r2d2 only, which
issues the same `FW_CONFIG_REQ` command from above to the driver interface:
```
$ r2d2 phyagc_config -m <peer mac> -f <configuration file>
```

An example configuration file is included in the node image at
`/etc/e2e_config/fw_phyagc_cfg.json`.

## Link Adaptation and Transmit Power Control (LA/TPC)
The modulation and code rate (MCS) and transmit power are both adaptive values,
and are set at the transmitter independently for every link and for both
directions. The adaptive MCS selection procedure is referred to as *link
adaptation (LA)*, and the transmit power procedure as *transmit power control
(TPC)*. Both procedures can be enabled or disabled independently at
initialization time or runtime.

The algorithm is driven by an *offset*. The offset increases as feedback
indicates good behavior and reduces as feedback indicates negative behavior.
When the offset crosses a fixed threshold (+1dB), MCS generally increases or
power generally decreases. The opposite is true if the offset crosses a negative
fixed threshold (-0.5dB). After crossing a threshold, the offset returns to
zero. The offset can never go outside the range +/- 2dB. The algorithm tries to
maximize throughput first by maximizing the MCS, then it will reduce power.

When there is data traffic, the offset is driven by the LDPC-based block error
rate (BLER) reported every SF (1.6ms). A lower BLER causes the algorithm to
increase the offset. When there is no data traffic, the algorithm is driven by
the STF SNR as reported each mgmt packet (every BWGD). The SNR is compared to
the MCS table and if the SNR > (<) table value, the offset will
increase (decrease).

### Traffic-Driven Mode
Every superframe (SF), a new set of LDPC statistics is received.  A `deltaOffset`
is calculated:
- `deltaOffset = (1-PER)*convergenceFactor/nackWeightFactor - PER*convergenceFactor`
  - default convergenceFactor = 1.0dB
  - default nackWeightFactor is 200 (5e-3 target PER)
  - PER is calculated from the syndrome error rate
    - PER = nSyn/nCW*bler2perFactor
      - nSyn = syndrome count over the last SF
      - nCW = number of LDPC codewords over the last SF
      - bler2perFactor: see BLER to PER below
- `offset = offset + deltaOffset`

The speed of the algorithm is determined by `convergenceFactor` and the
target PER is by `nackWeightFactor`.  Both are configurable as a FW
parameter.

### No-Traffic Mode
The traffic-driven mode uses LDPC statistics to drive the MCS/txPower. If no (or
very few) data packets are sent, there are no LDPC statistics. In this case, we
use the no-traffic mode which uses an MCS table lookup based on SNR.
- Detection of traffic:
    - if, in any one SF, there are no transmitted MPDUs, then
      `noTrafficDuration++`
    - if there is no traffic for 125 superframes (200ms total, not
      configurable), then move to the no-traffic mode
    - as soon as >=1 MDPUs are transmitted, algorithm sets noTrafficDuration = 0
      and moves back to traffic mode

In no-traffic mode:
- max MCS = min(configured max MCS, noTrafficMaxMcsFallback)
- default noTrafficMaxMcsFallback is 9 (configurable)
- offset = `effective SNR - MCStable[curMCS]` (note: this is not the
  deltaOffset, it's the offset)

### Core Algorithm
The core algorithm takes the offset and current MCS and txPower and returns the
new MCS and txPower.  When the offset crosses the negative threshold (-0.5dB),
MCS and power follow the "Bad" Path and follow the "Good" Path when the offset
crosses the positive threshold (+1.0dB).
- if offset < -0.5dB
    - if not at max power (see *note 1*); `txPower++`
    - else if not at min MCS; MCS to next lower value (see *note 2*)
    - if MCS or power changed, `offset = 0`
- else if offset > +1.0dB
    - if there is enough headroom to increase power and MCS, then do both
    - else if there is room to lower power, then lower power
    - if MCS or power changed, `offset = 0`

Notes:
1. The maximum power is the minimum of the global txMaxPower and the
   max power per MCS (both configurable).
2. LA/TPC skips MCS5 because MCS6 is a higher rate and lower SNR.

<p align="center">
  <img src="../media/figures/latpc_waterfill.svg" width="620" />
</p>

When the offset is good, the algorithm increases both power and MCS at the same
time. The reason is that a transition from `MCS(n)` to `MCS(n+1)` requires a
power increase that depends on `n`. The amount
of power increase is determined by the difference in SNR in the MCS table
between `MCS(n)` and `MCS(n+1)` and depends on the transmit power table.
If the calculated power increase pushes the power above the
configured maximum, then LA/TPC will reduce power by one index instead of
increasing MCS and power. It will continue to reduce power until there is enough
headroom to increase both power and MCS.

In this case where the offset is good, Δ = `MCS(n+1) - MCS(n)`.  The power index
continues to increase while the change in power is less than Δ and while
it is less than the maximum allowed power.  For example,
assuming that every transmit power index change corresponds to 0.5dB and if
Δ = 1.5dB, then the power will increase 2 indices.
If Δ = 1.75dB, the power would increase by 3 indices.

During a transition from no-traffic mode to traffic mode, MCS can increase
without increasing power. This prevents a ramp in power every time we transition
from no-traffic to traffic modes. MCS will continue to increase until the offset
crosses the negative threshold at which point the algorithm returns to the
normal mode described above.

When the offset is bad, the algorithm is simpler - it will reduce the power
until it hits the limit then it will reduce the MCS.

### LA/TPC Configuration
LA is enabled if the `mcs` configuration value is 35, with lower and upper
bounds set to `laMinMcs` and `laMaxMcs` respectively. Setting any other valid
`mcs` value disables LA and freezes the MCS at the given value.

TPC is enabled if the `tpcEnable` configuration value is 3, with lower and upper
bounds set at `minTxPower` and `maxTxPower` respectively. Initializing
`tpcEnable` to 0 disables TPC and freezes the transmit power at the `txPower`
value. If `tpcEnable` is disabled during runtime, the power will be fixed at the
current value instead.

The `txPower` value is an index between 0 and 31. The TPC algorithm expects that
the actual transmit power will monotonically increase as the index increases,
and also expects approximately 1dB of increase per index (but this need not be
precise). The actual power will depend on the number of antenna arrays and is
vendor-specific.

The MCS table is configurable at both initialization time and during runtime
(all links use the same MCS table in P2MP). At initialization time, it is
configurable using the 4 parameters `mcsLqmQ3_1_4`, `mcsLqmQ3_5_8`,
`mcsLqmQ3_9_12`, and `mcsLqmQ3_13_16` corresponding to MCS 1-4, 5-8, 9-12, and
13-16 (EDMG only). Each parameter has 4 MCS SNR values packed in Q3 format
(times 2^3), 8 bits per value. For example, the SNR corresponding to MCS2 is
`(mcsLqmQ3_1_4 >> 8) & 0xff`. There is a MATLAB script in the `utils` directory
(`fw_cfg_mcs_table.m`) that will convert the MCS table into this packed
notation. The MATLAB also contains code to convert the packed notation back into
unpacked.

To configure the MCS table at runtime, issue the r2d2 command below:
```bash
$ r2d2 phyla_config -m <peer MAC addr> -f /path/to/fw_phyla_cfg.json
```
There is an example of this file in `/etc/e2e_config/fw_phyla_cfg.json`. Runtime
configuration is only for debugging (it is not supported from the E2E).

### BLER to PER
When there is data traffic, LA/TPC is driven by the LDPC block error rate (BLER)
as reported in the block acks. The LDPC BLER is:
```
nSyn/nCW
```
where nSyn is the number of syndromes and nCW the number of LDPC codewords.
The packet error rate (PER) is estimated from the BLER as:
```
PER ~= BLER2PER_factor * nSyn/nCW
```
We have found through experimentation that BLER2PER_factor is approximately 30.
Occasionally, there can be short (~10ms) periods of PER that are not caused by
the channel. To reduce sensitivity, BLER2PER_factor includes an exponential ramp
that increases by a factor of 2 for every SF with BLER > 0 up to an upper limit.
When the BLER is 0, BLER2PER_factor is set to a lower limit. An exception is
immediately after a change in power or MCS - in this case, BLER2PER_factor is
set to the upper limit to react quickly to errors caused by MCS/power changes.

The lower and upper limits are configurable using `latpcBlerToPer` as follows:

| Bit Mask | Description                        | Default              |
| -------- | ---------------------------------- | -------------------- |
| bits 3:0 | Lower limit = 2^n for n = bits 3:0 | 1 (lower limit = 2)  |
| bits 7:4 | Upper limit = 2^n for n = bits 7:4 | 5 (upper limit = 32) |

The overall default for `latpcBlerToPer` is therefore 81 or 0x51.

### Setting Maximum Transmit Power Per MCS
To handle non-linear distortion at high power, it is possible to limit the
maximum transmit power per MCS at high transmit powers. Without this capability,
the algorithm would always assume that higher power is better and increase the
transmit power when there are errors. Setting the maximum power per MCS informs
the algorithm that when it increases the transmit power, it may also need to
lower the MCS.

The maximum transmit power per MCS is determined by the configuration values
`maxTxPowerPerMcs` and `maxTxPowerPerMcsEdmg` (for EDMG only). MCS values are
divided into the following ranges/values:
```
[1-9, 10, 11, 12, 13, 14, 15, 16]
```
The maximum power can be set independently for each of these ranges/values, with
a hard upper limit of `maxTxPower` (the global limit).

`maxTxPowerPerMcs` is a 4-byte number, where each byte represents the maximum
power for an MCS range/value. The least significant byte (LSB) corresponds to
the first range/value (i.e. MCS 1-9). For example:
```
maxTxPowerPerMcs = 0x1115181c (hex) or 286595100 (dec)
- max power for MCS  [12] = 0x11 (hex) or 17 (dec)
- max power for MCS  [11] = 0x15 (hex) or 21 (dec)
- max power for MCS  [10] = 0x18 (hex) or 24 (dec)
- max power for MCS [1-9] = 0x1c (hex) or 28 (dec)
```

`maxTxPowerPerMcsEdmg` extends the configuration to MCS 13, 14, 15, and 16. The
LSB corresponds to MCS 13. This value only takes effect when `cb2Enable` is set,
which enables the EDMG MCS levels.

### Data Traffic vs. No Data Traffic
LA/TPC is normally driven by the packet error rate (PER). When there is very
little or no data traffic, the PER cannot be measured, and SNR is used instead
until data traffic is detected again.

If there is no data traffic for 125 consecutive superframes (200ms), the
algorithm switches to the "no data traffic" mode. In this mode, the maximum MCS
temporarily changes from `laMaxMcs` to `noTrafficMaxMcsFallback` (default 9 for
DMG and 10 for EDMG). Note that it can take a couple of seconds for the MCS to
recover (e.g. to MCS 12 if the fallback is 9).

### PER Target
As mentioned above, LA/TPC is normally driven by PER. The PER is the receiver
LDPC-derived PER, which is fed back to the transmitter.

The algorithm attempts to stabilize at a PER target, defined as the inverse of
`laInvPERTarget`. The default value of `laInvPERTarget` is 200, making the PER
target 0.005 (or 0.5%). This is a rough, long-term target.

If the current PER is above the target, the algorithm will generally increase
the power or lower the MCS. If the PER is below the target, the algorithm will
generally, and more slowly, increase the MCS or lower the power.

The speed at which the algorithm reacts is governed by
`laConvergenceFactordBperSFQ8`. Changing this value is not recommended.

### 100% PER Condition
LA/TPC is driven by LDPC feedback when there is traffic.  When there is 100% PER
for an entire PPDU, there might be no LDPC feedback and the transmit counters
(txOk/txFail) indicate 100% PER. When this happens, the LA/TPC offset is reduced
by 0.4dB (configurable, see below).

Reasons for 100% can include early-weak interference causing a missed PPDU or a
missed block-ack, a calibration event, or can mean that none of the MDPUs were
decoded because of low SNR.  In the case of calibration, we don't want to adapt
the MCS or power.  The case of early-weak interference is more complicated;
lowering MCS will not help but increasing the power can. In the low SNR case, we
do want to adapt the MCS and/or power.

There is no way to know what causes 100% PER.  We assume that calibration
superframes are always isolated meaning that there are always several
non-calibration superframes between calibration superframes. Therefore, for each
link, LA/TPC will wait for two (configurable) or more data-carrying superframes
with 100% PER in a row before reacting.  In that case, we want the offset
reduction to be 0.8dB when two superframes of 100% are first detected. After
then, the offset should return to 0.4dB.

The 100% PER algorithm can also disable TPC to prevent a power ramp when
`peer SNR > mcsTable[MCS]` where `mcsTable[]` is a static table of SNR values to
support a given MCS. But as mentioned earlier, this could affect early-weak
interference performance.

The LA/TPC offset reduction is now configurable (0.4dB is the default). The
number of consecutive superframes of 100% PER is configurable (default is 2; set
to 1 for no filtering). The feature that disables TPC can also be disabled.
Configuration uses bits in `latpc100PercentPERDrop` according to the table
below.

| Bit Mask  | Description                                                        | Default |
| --------- | ------------------------------------------------------------------ | ------- |
| bits 3:0  | Offset reduction = 0.4dB * (value) / 4                             | 4       |
| bit 4     | TPC disable feature (1 to allow TPC disable, 0 to not disable TPC) | 1       |
| bits 10:8 | Number of consecutive superframes with data                        | 2       |

## Fast Link Impairment Detection
To enable fast re-routing, the firmware monitors link conditions and signals
link impairment to routing protocols when a link is impaired. The goal is to
report link impairment in under 50ms. When a link is impaired, the routing
protocol will not use the link, and will re-route traffic if another path is
available.

The Terragraph wireless link remains up as long as MCS 0 management messages are
successfully exchanged. Data traffic uses MCS >= 1, which requires around
12dB higher signal-to-noise ratio (SNR) than the management messages. This
means that the link can be up but unable to pass traffic. When this happens,
the link state changes from `LINK_UP` to `LINK_UP_DATADOWN`.

When `numOfHbLossToFail` (default 10) management messages are missed, the link
will go down (`LINK_DOWN`). Fast link impairment is designed to indicate a
failure more quickly than `LINK_DOWN`.

The `LINK_UP_DATADOWN` state persists for at least 200 superframes (320ms), or
longer if the impairment condition continues.

It is possible that link impairment is detected at only one side of a link (e.g.
if interference is present). A management message is exchanged such that if one
side enters `LINK_UP_DATADOWN`, both sides will enter `LINK_UP_DATADOWN`. This
prevents routing traffic over the link in a single direction.

### Conditions for Link Impairment
At a high level, the algorithm monitors link conditions, PER, and management
messages, switching to `LINK_UP_DATADOWN` upon seeing any of the following
conditions:
* A small number of management messages are missed and PER is at 100%.
* LA/TPC wants to reduce MCS or increase power, but MCS/power are already at the
  minimum/maximum (respectively).
* Many management messages are missed (but fewer than `numOfHbLossToFail`).

Specifically, link impairment is detected using the following condition:
```
(100%PER && (missedHB || SNRlow || farEndSNRlow))
  || MCS@limit
  || missedManyHB
```

Each sub-condition is described below:
* `100%PER`: 100% `txPER` (use ACK/NACK count) as measured at the transmitter.
   The condition is that there is a time window with 4 (default, configurable)
   superframes with 100% PER and traffic.  The time window is reset
   if there is a superframe with traffic and PER < 100%.
* `missedHB`: 3 (default, configurable) missed received HB/KA/ULBWREQs in a row.
* `SNRlow`: SNR measured on HB/KA/ULBWREQ < 2dB (if received). Previous studies
  showed that even with a bus or truck, sometimes heartbeats can get through.
* `farEndSNRlow`: Peer SNR reported in HB/KA/ULBWREQ < 2dB (as reported through
   HB/KA/ULBWREQ), for the same reason as `SNRlow`.
* `MCS@limit`: If, in any one superframe, the LA/TPC algorithm wants to
   increase power or decrease MCS but is at the limit, `MCS@limitSF` is set.
   The condition for `MCS@limit` is that there is a time window with 4
   (default, configurable) superframes with `MCS@limitSF` and traffic.
   A superframe in which `MCS@limitSF` is not set resets the window.
* `missedManyHB`: Missed 5 (default, configurable) HB/KA/ULBWREQ messages in a
  row. This covers the case of no traffic and truck blockage.

#### Configuration Parameters
There are four configurable threshold parameters for detecting link impairment.
They are encoded in the 32-bit latpcLinkImpairConfig as part of the link
configuration. These parameters can be set per link.

The condition for all thresholds is that the condition **>=** threshold;
if the threshold is zero, the condition will always be true.

| Parameter                   | Description                                                    |
| --------------------------- | -------------------------------------------------------------- |
| `100PER` (bits 3:0)         | See `100%PER` above. Set to 0xf to disable `100%PER`           |
| `missedCnt` (bits 7:4)      | See `missedHB` above. Set to 0xf to disable `missedHB`         |
| `missedManyCnt` (bits 11:8) | See `missedManyHB` above. Set to 0xf to disable `missedManyHB` |
| `MCSlimit` (bits 15:12)     | See `MCS@limit` above. Set to 0xf to disable `MCS@limit`       |

### Debouncing
Debouncing is implemented to prevent rapid transitions between `LINK_UP` and
`LINK_UP_DATADOWN`. This uses a state machine with states, transition events,
and times shown in the figure below.

<p align="center">
  <img src="../media/figures/fast_link_impairment.svg" width="620" />
</p>

The normal state is `TG_LINK_IMPAIRMENT_LINK_UP`. If the firmware detects link
impairment, it transitions to `TG_LINK_IMPAIRMENT_LINK_DOWN` and sends an event
to the TG state machine to transition to `LINK_UP_DATADOWN`. If there is an
`LSM_EVE_LINK_DATAUP` event, the TG state machine will transition back to
`LINK_UP`. All timeouts are in units of superframes (1.6ms). As mentioned
earlier, "peer link impaired" means that the other end of the link detected link
impairment and reported it via a management message.

### Monitoring Link Impairment
Two relevant counters (stats) are reported by the firmware every second:
* `staPkt.mgmtLinkUp` - Increments every BWGD when the link is in `LINK_UP` or
  `LINK_UP_DATADOWN`.
* `staPkt.linkAvailable` - Increments every BWGD when the link is in `LINK_UP`
  only.

If the difference between these two stats increases, then the link entered
`LINK_UP_DATADOWN`. The change in the difference between the two stats indicates
how long it was in the `LINK_UP_DATADOWN` state, in units of BWGDs (25.6ms).
