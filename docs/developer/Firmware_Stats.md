# Firmware Stats
This document describes all stats exported by Terragraph firmware.

## Overview
Terragraph firmware stats are prefixed with "tgf", and take the following
format:
```
tgf.<peer MAC address>.<stat type>.<member>
```

Stat types are defined in `tgfStatsType` in `fb_tg_fw_pt_if.h`. These can be
divided into two categories: *per-link stats* and *system stats*. For system
stats, the MAC address portion is set to `00:00:00:00:00:00`.

Different types of stats are published at different frequencies, though most
stats are published once per second. Not all stat types are enabled by default,
particularly the high-frequency stats. Stats can be toggled at runtime via the
`FW_STATS_CONFIGURE_REQ` message, or persistently using the node configuration
field `radioParamsBase.fwStatsConfig`. For example, to enable beamforming stats,
set `radioParamsBase.fwStatsConfig.TGF_STATS_BF` to `true`.

Stats are pushed from the firmware (as `tgfStatsSample`) to `driver-if` using
the `TGF_PT_NB_STATS` message. These are then parsed and processed in
`PassThru::getStats()`, which also sets the key names. Lastly, `driver-if`
publishes the stats over a ZMQ port (see
[Stats, Events, Logs](Stats_Events_Logs.md) for details). They can be polled
locally by running the command `tg2 stats driver-if`.

## Per-Link Stats
The table below lists all per-link stats.

| Type | Key                       | Description                    | Frequency |
| ---- | ------------------------- | ------------------------------ | --------- |
| `TGF_STATS_STA_PKT` |            | From `wgc_bh_get_statistics()` | Every 1 sec |
| | `tgf.MAC.staPkt.txOk`          | Number of successfully transmitted frames/MPDUs (counter) | |
| | `tgf.MAC.staPkt.txFail`        | Number of transmission failures | |
| | `tgf.MAC.staPkt.rxOk`          | Number of successfully received frames/MPDUs | |
| | `tgf.MAC.staPkt.rxFail`        | Number of CRC failed frames received (once CRC fail, cannot trust RA as well) | |
| | `tgf.MAC.staPkt.rxPlcpFil`     | Number of received frames with HCS failed PLCP header | |
| | `tgf.MAC.staPkt.perE6`         | Instantaneous TX packet error rate x 10^6 | |
| | `tgf.MAC.staPkt.mcs`           | TX MCS at the time of logging | |
| | `tgf.MAC.staPkt.txBa`          | Number of block acks transmitted | |
| | `tgf.MAC.staPkt.txPpdu`        | Number of PPDUs transmitted | |
| | `tgf.MAC.staPkt.rxBa`          | Number of block acks received | |
| | `tgf.MAC.staPkt.rxPpdu`        | Number of PPDUs received | |
| | `tgf.MAC.staPkt.txPowerIndex`  | Transmit Power Index (values 0-31); this is same value as `tpcStats.txPowerIndex`, but is reported at a higher frequency | |
| | `tgf.MAC.staPkt.txLifetimeExp` | Total number of TX packets dropped due to lifetime expiry | |
| | `tgf.MAC.staPkt.rxDiscBuf`     | Total RX Discard count due to Buffer overflow (per STA) | |
| | `tgf.MAC.staPkt.rxDiscEnc`     | Total RX Discard count due to Encryption failure (per STA) | |
| | `tgf.MAC.staPkt.rxDiscRa`      | Total RX Discard count due to RA mismatch (per STA) | |
| | `tgf.MAC.staPkt.rxDiscUnexp`   | Total RX Discard count due to other unexpected reasons or PER emulator (per STA) | |
| | `tgf.MAC.staPkt.txSlotTime`    | Total TX Data slot time in 256us unit | |
| | `tgf.MAC.staPkt.txAirTime`     | Total TX data air time in 256us unit | |
| | `tgf.MAC.staPkt.linkAvailable` | Counter that increments every BWGD if firmware in `LINK_UP` state | |
| | `tgf.MAC.staPkt.txSlotEff`     | TX slot efficiency in units of 0.01% | |
| | `tgf.MAC.staPkt.mgmtLinkUp`    | Counter that increments every BWGD if link up for mgmt packets | |
| | `tgf.MAC.staPkt.rxPerE6`       | RX packet error rate (analogous to perE6) at the receiver | |
| | `tgf.MAC.staPkt.txMpduCount`   | Number of transmitted MPDUs (since last reported stat epoch) | |
| | `tgf.MAC.staPkt.rxMpduCount`   | Number of received MPDUs (since last reported stat epoch) | |
| | | | |
| `TGF_STATS_PHYSTATUS` |                 | PHY status statistics from management packets | Every 1 sec |
| | `tgf.MAC.phystatus.ssnrEst`           | Short training field (STF) SNR (in dB) measured during management packets (KA, HB, ULBWREQ) | |
| | `tgf.MAC.phystatus.spostSNRdB`        | Post equalizer SNR (in dB) measured during management packets (KA, HB, ULBWREQ) | |
| | `tgf.MAC.phystatus.srssi`             | Receiver signal strength indicator (RSSI) (in dBm) measured during management packets (KA, HB, ULBWREQ) | |
| | `tgf.MAC.phystatus.gainIndexIf`       | See `phystatusdata.gainIndexIf` | |
| | `tgf.MAC.phystatus.gainIndexRf`       | See `phystatusdata.gainIndexRf` | |
| | `tgf.MAC.phystatus.rawAdcRssi`        | See `phystatusdata.rawAdcRssi` | |
| | `tgf.MAC.phystatus.rxStartNormalized` | Normalized rxStart for last management packet | |
| | `tgf.MAC.phystatus.maxGainIndexIf`    | Maximum IF gain set by max AGC tracking | |
| | `tgf.MAC.phystatus.maxGainIndexRf`    | Maximum RF gain set by max AGC tracking | |
| | `tgf.MAC.phystatus.numTotalSyndromes` | Number of failed LDPC codewords | |
| | `tgf.MAC.phystatus.numTotalCodewords` | Total number of codewords (N_CW in 802.11ad) | |
| | `tgf.MAC.phystatus.plcpLength`        | PLCP packet length in bytes | |
| | `tgf.MAC.phystatus.ldpcIterations`    | Total number of LDPC iterations over all N_CW codewords | |
| | `tgf.MAC.phystatus.rxMcs`             | RX MCS calculated from plcp_0 | |
| | | | |
| `TGF_STATS_PHYSTATUS` |                     | PHY status statistics from data packets | Every 1 sec |
| | `tgf.MAC.phystatusdata.ssnrEst`           | Short training field (STF) SNR (in dB) measured during the first data packet in the superframe (if available) | |
| | `tgf.MAC.phystatusdata.spostSNRdB`        | Post equalizer SNR (in dB) measured during the first data packet in the superframe (if available) | |
| | `tgf.MAC.phystatusdata.srssi`             | Receiver signal strength indicator (RSSI) (in dBm) measured during the first data packet in the superframe (if available) | |
| | `tgf.MAC.phystatusdata.gainIndexIf`       | IF gain index (range 0-31) is read when we receive a mgmt or data packet; the gain setting that was used to receive the packet | |
| | `tgf.MAC.phystatusdata.gainIndexRf`       | RF gain index (range 0-15) is read when we receive a mgmt or data packet; the gain setting that was used to receive the packet | |
| | `tgf.MAC.phystatusdata.rawAdcRssi`        | Raw ADC RSSI is the raw calculated RSSI after the ADC output, but before any post-processing to refer the RSSI back to the input (mainly used for debugging - units are related to dB) | |
| | `tgf.MAC.phystatusdata.rxStartNormalized` | Normalized rxStart for the first data packet in the superframe (if available) | |
| | `tgf.MAC.phystatusdata.maxGainIndexIf`    | Maximum IF gain set by max AGC tracking | |
| | `tgf.MAC.phystatusdata.maxGainIndexRf`    | Maximum RF gain set by max AGC tracking | |
| | `tgf.MAC.phystatusdata.numTotalSyndromes` | Number of failed LDPC codewords | |
| | `tgf.MAC.phystatusdata.numTotalCodewords` | Total number of codewords (N_CW in 802.11ad) | |
| | `tgf.MAC.phystatusdata.plcpLength`        | PLCP packet length in bytes | |
| | `tgf.MAC.phystatusdata.ldpcIterations`    | Total number of LDPC iterations over all N_CW codewords | |
| | `tgf.MAC.phystatusdata.rxMcs`             | RX MCS calculated from plcp_0 | |
| | | | |
| `TGF_STATS_MGMT_TX`<br />`TGF_STATS_MGMT_RX` | | Management packet statistics, {Tx/Rx} = Tx or Rx | Every 1 sec |
| | `tgf.MAC.mgmt{Tx/Rx}.bfTrainingReq`        | Counter for `BF_TRAINING_REQ` | |
| | `tgf.MAC.mgmt{Tx/Rx}.bfTrainingRsp`        | Counter for `BF_TRAINING_RSP` | |
| | `tgf.MAC.mgmt{Tx/Rx}.bfTrainingRspAck`     | Counter for `BF_TRAINING_RSP_ACK` | |
| | `tgf.MAC.mgmt{Tx/Rx}.bfTrainingUrx`        | Counter for `BF_TRAINING_URX` | |
| | `tgf.MAC.mgmt{Tx/Rx}.assocReq`             | Counter for `ASSOC_REQ` | |
| | `tgf.MAC.mgmt{Tx/Rx}.assocRsp`             | Counter for `ASSOC_RSP` | |
| | `tgf.MAC.mgmt{Tx/Rx}.assocRspAck`          | Counter for `ASSOC_RSP_ACK` | |
| | `tgf.MAC.mgmt{Tx/Rx}.keepAlive`            | Counter for `KEEP_ALIVE` | |
| | `tgf.MAC.mgmt{Tx/Rx}.heartBeat`            | Counter for `HEART_BEAT` | |
| | `tgf.MAC.mgmt{Tx/Rx}.uplinkBwreq`          | Counter for `UPLINK_BWREQ` | |
| | `tgf.MAC.mgmt{Tx/Rx}.bfRetrainingReq`      | Counter for `BF_RETRAINING_REQ` | |
| | `tgf.MAC.mgmt{Tx/Rx}.bfRetrnUrxChgReq`     | Counter for `BF_RETRN_URX_CHG_REQ` | |
| | `tgf.MAC.mgmt{Tx/Rx}.bfRetrnUrxChgReqAck`  | Counter for `BF_RETRN_URX_CHG_REQ_ACK` | |
| | `tgf.MAC.mgmt{Tx/Rx}.dissocReq`            | Counter for `DISASSOC_REQ` | |
| | | | |
| `TGF_STATS_BWHAN_LINK` |                  | Per-link bandwidth handler statistics | Every 1 sec |
| | `tgf.MAC.bwhanLink.totalTxDataTimeUs`   | Total TX time (in us) | |
| | `tgf.MAC.bwhanLink.totalRxDataTimeUs`   | Total RX time (in us) | |
| | `tgf.MAC.bwhanLink.totalTxDataSlots`    | Total number of TX slots | |
| | `tgf.MAC.bwhanLink.totalRxDataSlots`    | Total number of RX slots | |
| | `tgf.MAC.bwhanLink.currTxTimePercent`   | Current L2 scheduler-proposed TX time percentage | |
| | `tgf.MAC.bwhanLink.currRxTimePercent`   | Current L2 scheduler-proposed RX time percentage | |
| | `tgf.MAC.bwhanLink.currTxSlotPercent`   | Current actual TX slot percentage | |
| | `tgf.MAC.bwhanLink.currRxSlotPercent`   | Current actual RX slot percentage | |
| | `tgf.MAC.bwhanLink.txCtrlFallbackCount` | TX map control-only fallback counter | |
| | `tgf.MAC.bwhanLink.rxCtrlFallbackCount` | RX map control-only fallback counter | |
| | `tgf.MAC.bwhanLink.localBytesPending`   | Queue size (in bytes) | |
| | `tgf.MAC.bwhanLink.localArrivalRate`    | Arrival rate (in bytes/ms) | |
| | `tgf.MAC.bwhanLink.peerBytesPending`    | Queue size at peer (in bytes) | |
| | `tgf.MAC.bwhanLink.peerArrivalRate`     | Arrival rate at peer (in bytes/ms) | |
| | | | |
| `TGF_STATS_TPC` |                        | Transmit Power Control (TPC) statistics | Every 1 sec |
| | `tgf.MAC.tpcStats.effSnr`              | "Effective SNR" is a filtered version of the peer mgmt SNR as reported in mgmt packets from the far end | |
| | `tgf.MAC.tpcStats.tsIirRssi`           | Filtered version of the peer RSSI as reported in mgmt packets from the far end | |
| | `tgf.MAC.tpcStats.tsIirRssiTargetMgmt` | Target RSSI used for open loop power control (when there is no data traffic) | |
| | | | |
| `TGF_STATS_MAX_AGC` |                     | Maximum automatic gain control (AGC) statistics | Every 5 sec |
| | `tgf.MAC.maxAgcStats.maxGainIndexIf`    | Maximum IF gain set by max AGC tracking | |
| | `tgf.MAC.maxAgcStats.maxGainIndexRf`    | Maximum RF gain set by max AGC tracking | |
| | `tgf.MAC.maxAgcStats.numBwgdsInFreeRun` | Number of BWGDs in which the max AGC was free-running | |
| | `tgf.MAC.maxAgcStats.iirAvgRssi`        | Average RSSI at the receiver | |
| | `tgf.MAC.maxAgcStats.minRssi`           | Current minimum RSSI | |
| | | | |
| `TGF_STATS_BF` |           | Beamforming (BF) statistics | 40-5000 per sec |
| | `tgf.MAC.bf.mode`        | SYNC or ASYNC mode | |
| | `tgf.MAC.bf.msgType`     | Message type (REQ / RSP / ACK) | |
| | `tgf.MAC.bf.txBeamIdx`   | Transmit Beam Index | |
| | `tgf.MAC.bf.rxBeamIdx`   | Receive Beam Index | |
| | `tgf.MAC.bf.pktLqm`      | Packet LQM | |
| | `tgf.MAC.bf.pktRssi`     | Packet RSSI | |
| | `tgf.MAC.bf.rxStart`     | RX Start based on TSF | |
| | `tgf.MAC.bf.dblPktIdx`   | Double Packet Index | |
| | `tgf.MAC.bf.frmNumBfWin` | Frame number in BF window | |
| | `tgf.MAC.bf.frmNumInSf`  | Frame number in SF | |
| | | | |
| `TGF_STATS_RECV_MGMT` |           | Received management packet info | 40-5000 per sec |
| | `tgf.MAC.recvMgmt.actionCode`   | Same as `sMgmtPkt.mgmtHdr.actionCode` | |
| | `tgf.MAC.recvMgmt.rxstart`      | Same as `wgc_bh_mgmt_rxdesp_t.rxstart` | |
| | `tgf.MAC.recvMgmt.size`         | Same as `wgc_bh_mgmt_rxdesp_t.size` | |
| | `tgf.MAC.recvMgmt.plcp{n}`      | Same as `wgc_bh_mgmt_rxdesp_t.plcp_{n}` (n=0,1,2) | |
| | `tgf.MAC.recvMgmt.beamRx`       | Same as `wgc_bh_rx_phystatus_t.beamRx` | |
| | `tgf.MAC.recvMgmt.phyStatus{n}` | Same as `wgc_bh_rx_phystatus_t.phy_status_{n}` (n=0,1,2,3) | |
| | | | |
| `TGF_STATS_MGMT_DATA` |   | Received management packet data | 40-5000 per sec |
| | `tgf.MAC.mgmtData.w{n}` | n=0,1,...,19 | |
| | | | |
| `TGF_STATS_MISC_LINK` |                          | Miscellaneous per-link statistics | Every 1 sec |
| | `tgf.MAC.miscLink.dataTxSlotDur`               | Duration in us of `BH_SLOT_TYPE_TX` | |
| | `tgf.MAC.miscLink.dataRxSlotDur`               | Duration in us of `BH_SLOT_TYPE_RX` | |
| | `tgf.MAC.miscLink.bfTxSlotDur`                 | Duration in us of `BH_SLOT_TYPE_BEAMFORMING_TX` | |
| | `tgf.MAC.miscLink.bfRxSlotDur`                 | Duration in us of `BH_SLOT_TYPE_BEAMFORMING_RX` | |
| | `tgf.MAC.miscLink.txstatusFlagAck`             | Counter for `WGC_BH_TXSTATUS_FLAG_ACK` | |
| | `tgf.MAC.miscLink.txstatusLifetimeExp`         | Counter for `WGC_BH_TXSTATUS_LIFETIME_EXPIRED` | |
| | `tgf.MAC.miscLink.txstatusFlushed`             | Counter for `WGC_BH_TXSTATUS_FLUSHED` | |
| | `tgf.MAC.miscLink.currentLinkState`            | Current link state (from fsmState) | |
| | `tgf.MAC.miscLink.mtpoRunCounter`              | Counter that increments when MTPO is triggered (and FB response is OK) at initiator | |
| | `tgf.MAC.miscLink.mtpoSuccessCounter`          | Counter that increments when MTPO runs successfully at initiator | |
| | `tgf.MAC.miscLink.mtpoApplyNewPhaseCounter`    | Counter that increments when MTPO runs successfully at initiator and applies a new phase | |
| | `tgf.MAC.miscLink.mtpoRejectCounter`           | Counter that increments when MTPO requested but rejected | |
| | `tgf.MAC.miscLink.mtpoFailCounter`             | Counter that increments when initiator response indicates failure | |
| | `tgf.MAC.miscLink.mtpoResponderTimeoutCounter` | Counter that increments when response from responder indicates timeout | |
| | `tgf.MAC.miscLink.mtpoCurrentPhases`           | `0xABCD` where A,B,C,D are the phases (D is tile 0); only populated after MTPO succeeds | |
| | | | |
| `TGF_STATS_PHY_PERIODIC` |           | Periodic Beamforming stats | Every 300 sec, or whenever beam changes during scans |
| | `tgf.MAC.phyPeriodic.txBeamIdx`    | Currently selected TX beam index | |
| | `tgf.MAC.phyPeriodic.rxBeamIdx`    | Currently selected RX beam index | |
| | `tgf.MAC.phyPeriodic.txRficBitmap` | Transmit RFIC Bitmap | |
| | `tgf.MAC.phyPeriodic.rxRficBitmap` | Receive RFIC Bitmap | |
| | `tgf.MAC.phyPeriodic.pktLqm`       | Initial beamforming SNR (in dB) for the best beam combination | |
| | `tgf.MAC.phyPeriodic.pktRssi`      | Initial beamforming RSSI (in dBm, not calibrated) for the best beam combination | |
| | | | |
| `TGF_STATS_LA_TPC` |                      | Link Adaptation and Transmit Power Control (LA/TPC) statistics | Every 1 sec |
| | `tgf.MAC.latpcStats.laTpcOffsetdBQ24`   | Offset that drives the LA/TPC algorithm (`offset = laTpcOffsetdBQ24/2^24`); positive offset indicates good link conditions |  |
| | `tgf.MAC.latpcStats.noTrafficCountSF`   | Running counter that increments every SF when that SF has no data traffic | |
| | `tgf.MAC.latpcStats.numSFsAtLowerLimit` | Running counter that increments every SF when the algorithm wants to lower MCS or increase power but can't because it hits the limits | |
| | `tgf.MAC.latpcStats.nCW`                | Running counter of the number of peer LDPC codewords | |
| | `tgf.MAC.latpcStats.nSyn`               | Running counter of the number of peer LDPC syndromes | |
| | `tgf.MAC.latpcStats.nIter`              | Running counter of the number of peer LDPC iterations | |
| | `tgf.MAC.latpcStats.synPERQ16`          | PER as calculated based on LDPC stats | |
| | | | |
| `TGF_STATS_LINK_DOWN` |    | Link down events | Whenever link goes down |
| | `tgf.MAC.linkDown.cause` | Cause of link going down (`tgLinkFailureCause`) | |

## System Stats
The table below lists all system stats.

| Type | Key                       | Description                    | Frequency |
| ---- | ------------------------- | ------------------------------ | --------- |
| `TGF_STATS_GPS` |                | GPS module statistics | Every 1 sec |
| | `tgf.MAC.gps.driverDelay`      | Driver ioctl delay from PPS tsf boundary (in microseconds) | |
| | `tgf.MAC.gps.maxDriverDelay`   | Max of "driverDelay" | |
| | `tgf.MAC.gps.numPpsErr`        | Number of PPS tsf read errors | |
| | `tgf.MAC.gps.numTimelineErr`   | Number of errors due to firmware/driver taking more time | |
| | `tgf.MAC.gps.numMissedSec`     | Number of times driver did not send GPS time (derived from tsf, and can increase by many counts per second) | |
| | `tgf.MAC.gps.ppsJitter`        | Jitter for the last PPS tsf (in microseconds), only recording *valid* samples (i.e. PPS jitter <= 25us) used for GPS drift correction | |
| | `tgf.MAC.gps.maxPpsJitter`     | Max of "ppsJitter", but counting *all* received samples (including samples with high PPS jitter) | |
| | `tgf.MAC.gps.tsfDrift`         | Cumulative drift in tsf | |
| | `tgf.MAC.gps.ppsHwTsf`         | HW TSF at last PPS | |
| | `tgf.MAC.gps.ppsHwTsfNs`       | HW TSF at last PPS ns portion | |
| | `tgf.MAC.gps.ppsSwTsf`         | SW TSF at last PPS | |
| | `tgf.MAC.gps.ppsSwTsfNs`       | SW TSF at last PPS ns portion | |
| | | | |
| `TGF_STATS_SLOT` |                     | Slot programming statistics | Every 1 sec |
| | `tgf.MAC.slot.numOfTxBfSlotsPgmrd`   | Counter for `BH_SLOT_TYPE_BEAMFORMING_TX` (BF TX) slots | |
| | `tgf.MAC.slot.numOfRxBfSlotsPgmrd`   | Counter for `BH_SLOT_TYPE_BEAMFORMING_RX` (BF RX) slots | |
| | `tgf.MAC.slot.numOfTxDataSlotsPgmrd` | Counter for `BH_SLOT_TYPE_TX` (Data TX) slots | |
| | `tgf.MAC.slot.numOfRxDataSlotsPgmrd` | Counter for `BH_SLOT_TYPE_RX` (Data RX) slots | |
| | `tgf.MAC.slot.numOfShortCalibSlots`  | Counter for short calibration slots (duration <= 200us) | |
| | `tgf.MAC.slot.numOfLongCalibSlots`   | Counter for long calibration slots (duration > 200us) | |
| | | | |
| `TGF_STATS_BWHAN_SYS` |               | System bandwidth handler statistics | Every 1 sec |
| | `tgf.MAC.bwhanSys.totalTxAssocTime` | Total TX time used for Association phase (in us) | |
| | `tgf.MAC.bwhanSys.totalRxAssocTime` | Total RX time used for Association phase (in us) | |
| | | | |
| `TGF_STATS_MEM` |          | Memory management statistics | 10000 per sec |
| | `tgf.MAC.mem.mallocSize` | Counter for `wgc_bh_malloc()` | |
| | `tgf.MAC.mem.mfreeSize`  | Counter for `wgc_bh_mfree()` | |
| | | | |
| `TGF_STATS_MISC_SYS` |                         | Miscellaneous system statistics | Every 1 sec |
| | `tgf.MAC.miscSys.numMissedSfm`               | Number of superframes with missed slots programming | |
| | `tgf.MAC.miscSys.malloc`                     | Bytes malloc'ed | |
| | `tgf.MAC.miscSys.free`                       | Bytes free'ed | |
| | `tgf.MAC.miscSys.numFrameTimer`              | Number of times `tgfFrameTimer()` got called | |
| | `tgf.MAC.miscSys.rfToGps`                    | Number of transitions from RF to GPS sync | |
| | `tgf.MAC.miscSys.gpsToRf`                    | Number of transitions from GPS to RF sync | |
| | `tgf.MAC.miscSys.cpuLoadAvg`                 | CPU load average, expressed as an integer percent | |
| | `tgf.MAC.miscSys.rftemperature{n}`           | RFIC-{n} temperature, in degrees C (n=0,1,2,3) | |
| | `tgf.MAC.miscSys.iftemperature`              | Baseband (IF) temperature, in degrees C | |
| | `tgf.MAC.miscSys.getPktBuf`                  | Counter for `wgc_bh_getPktBuf()` call | |
| | `tgf.MAC.miscSys.recvMgmt`                   | Counter for `bh_recv_mgmt()` call | |
| | `tgf.MAC.miscSys.freePktBuf`                 | Counter for `wgc_bh_freePktBuf()` call | |
| | `tgf.MAC.miscSys.sendMgmtCB`                 | Counter for `bh_send_mgmtCB()` call | |
| | `tgf.MAC.miscSys.txstatusNoSta`              | Counter for `WGC_BH_TXSTATUS_NO_STA` | |
| | `tgf.MAC.miscSys.mgmtRxIncorrectHdr`         | Counter for correct `hdr.category` and `hdr.oui` | |
| | `tgf.MAC.miscSys.numBcastImTrnReqSent`       | Counter for Broadcast `BF_RETRAINING_REQ` sent | |
| | `tgf.MAC.miscSys.numBcastImTrnReqRecvd`      | Counter for Broadcast `BF_RETRAINING_REQ` received | |
| | `tgf.MAC.miscSys.numIncorrectBcastPktsRecvd` | Counter for unexpected Broadcast messages | |
| | | | |
| `TGF_STATS_TSF` |           | tsf module statistics | Every 1 sec |
| | `tgf.MAC.tsf.syncModeGps` | 1 if tsf is in "GPS sync" state, otherwise 0 | |
| | `tgf.MAC.tsf.syncModeRf`  | 1 if tsf is in "RF sync" state, otherwise 0 | |
| | `tgf.MAC.tsf.numRfFix`    | Number of times tsf offset fixed on "RF sync" | |
| | `tgf.MAC.tsf.numGpsFix`   | Number of times tsf offset fixed on "GPS sync" | |
| | `tgf.MAC.tsf.rfDrift`     | Largest value of tsf drift over current stats interval with regard to the RF link | |
| | `tgf.MAC.tsf.sumRfFix`    | Sum of tsf fixes for "RF sync" | |
| | `tgf.MAC.tsf.sumGpsFix`   | Sum of tsf fixes for "GPS sync" | |
| | `tgf.MAC.tsf.offsetL`     | Low word of current offset (`sw_tsf - hw_tsf`) | |
| | `tgf.MAC.tsf.offsetH`     | High word of current offset (`sw_tsf - hw_tsf`) | |
| | `tgf.MAC.tsf.driftPerWin` | Average drift per window (e.g. per 10s) | |
| | | | |
| `TGF_STATS_LIFETIME_EXPIRED`<br />`TGF_STATS_LIFETIME_OK` | | Lifetime expiry stats for management packets | Every 1 sec |
| | `tgf.MAC.lifetime{Expired/Ok}.{action}`                 | Counter per action type (action=0,1,...,11, e.g. `ASSOC_REQ`, `KEEP_ALIVE`) | |
| | | | |
| `TGF_STATS_CHN` |                    | Channel module statistics | Every 60 sec |
| | `tgf.MAC.chn.maxTickCodebookFetch` | Max time spent to fetch codebook | |
| | `tgf.MAC.chn.maxTickChannelChange` | Max time spent to change channel | |
| | `tgf.MAC.chn.errInvalidChnIn`      | Count for invalid input error | |
| | `tgf.MAC.chn.errSetChn`            | Count for error in `set_channel()` | |
| | `tgf.MAC.chn.errAssoc`             | Count for errors in assoc | |
| | `tgf.MAC.chn.state`                | Last state | |
| | `tgf.MAC.chn.channel`              | Last operating channel | |
| | `tgf.MAC.chn.configuredChannel`    | Last configured channel | |
| | `tgf.MAC.chn.numSwitches`          | Number of channel switch attempts | |
| | | | |
| `TGF_STATS_SECURITY` |      | Security statistics | Every 300 sec, and also during association and link-down phases |
| | `tgf.MAC.security.status` | Link encryption (wsec) status (`tgWsecAuthType`, i.e. 0=disabled, 1=PSK, 2=EAP) | |
| | | | |
| `TGF_STATS_BF_SCAN` |                        | Beamforming (BF) scan statistics | Every 300 sec, and also after BF scans (when enabled) |
| | `tgf.MAC.bfScanStats.numOfScanReqRecvd`    | Number of scan request commands received | |
| | `tgf.MAC.bfScanStats.numOfScanCompleted`   | Number of scans complete | |
| | `tgf.MAC.bfScanStats.numOfScanDropped`     | Number of scans dropped | |
| | `tgf.MAC.bfScanStats.numOfScanAborted`     | Number of scans aborted | |
| | `tgf.MAC.bfScanStats.numOfScanAsInitiator` | Number of scans as initiator | |
| | `tgf.MAC.bfScanStats.numOfScanAsResponder` | Number of scans as Responder | |
| | `tgf.MAC.bfScanStats.numOfPbfScan`         | Number of PBF scans | |
| | `tgf.MAC.bfScanStats.numOfImScan`          | Number of IM scans | |
| | `tgf.MAC.bfScanStats.numOfRtCalScan`       | Number of RTCAL scans | |
| | `tgf.MAC.bfScanStats.numOfVbsScan`         | Number of VBS scans | |
| | `tgf.MAC.bfScanStats.numOfCbfScan`         | Number of CBF scans | |
