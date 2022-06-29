/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

import React from "react";
import {
  Button,
  Fieldset,
  Hourglass,
  Table,
  TableBody,
  TableHead,
  TableRow,
  TableHeadCell,
  TableDataCell
} from "react95";
import AppWindow from "../components/AppWindow";
import Loading from "../components/Loading";
import NodeRadio from "../components/NodeRadio";
import { getStatusReport, topoScan } from "../api/api";
import { findBestBeam, formatPositionString } from "../utils/network";

// From Controller.thrift
const ScanFwStatus = {
  COMPLETE: 0,
  INVALID_TYPE: 1,
  INVALID_START_TSF: 2,
  INVALID_STA: 3,
  AWV_IN_PROG: 4,
  STA_NOT_ASSOC: 5,
  REQ_BUFFER_FULL: 6,
  LINK_SHUT_DOWN: 7,
  UNSPECIFIED_ERROR: 8,
  UNEXPECTED_ERROR: 9,
  EXPIRED_TSF: 10,
  INCOMPL_RTCAL_BEAMS_FOR_VBS: 11
};

const TopoScanWindow = props => {
  const [statusReport, setStatusReport] = React.useState(null);
  const [topoScanResults, setTopoScanResults] = React.useState(null);
  const [selectedRadio, setSelectedRadio] = React.useState(null);
  const [isTopoScanRunning, setIsTopoScanRunning] = React.useState(false);

  React.useEffect(() => {
    getStatusReport(setStatusReport, console.error);
  }, []);

  const doTopoScan = () => {
    setIsTopoScanRunning(true);
    topoScan(
      selectedRadio,
      data => {
        setTopoScanResults(data);
        setIsTopoScanRunning(false);
      },
      console.error
    );
  };

  const renderRadioSelection = statusReport => {
    const WIDTH = 300;

    if (!statusReport) {
      return (
        <AppWindow width={WIDTH} {...props}>
          <Loading />
        </AppWindow>
      );
    }
    if (
      !statusReport?.radioStatus ||
      Object.keys(statusReport.radioStatus).length === 0
    ) {
      return (
        <AppWindow width={WIDTH} {...props}>
          <span>Node has no radios?</span>
        </AppWindow>
      );
    }

    return (
      <AppWindow width={WIDTH} {...props}>
        <div>
          <Fieldset label="Select Radio">
            {Object.entries(statusReport.radioStatus).map(([mac, status]) => (
              <div key={mac}>
                <NodeRadio
                  macAddr={mac}
                  status={status}
                  checked={selectedRadio === mac}
                  onChange={e => setSelectedRadio(e.target.value)}
                  name="radio"
                  isDisabled={isTopoScanRunning}
                />
              </div>
            ))}
          </Fieldset>
        </div>
        <div className="justify" style={{ paddingTop: 8 }}>
          <span>{isTopoScanRunning && <Hourglass />}</span>
          <span>
            <Button
              className="choice-button"
              primary
              onClick={doTopoScan}
              disabled={!selectedRadio || isTopoScanRunning}
            >
              OK
            </Button>
            <Button className="choice-button" onClick={props.onClose}>
              Cancel
            </Button>
          </span>
        </div>
      </AppWindow>
    );
  };

  const renderTopoScanResults = topoScanResults => {
    // Check completion status
    if (topoScanResults.status !== ScanFwStatus.COMPLETE) {
      return (
        <AppWindow {...props}>
          <div>
            Scan failed:{" "}
            <tt>
              {Object.keys(ScanFwStatus).find(
                k => ScanFwStatus[k] === topoScanResults.status
              ) || `code ${topoScanResults.status}`}
            </tt>
          </div>
        </AppWindow>
      );
    }

    // No responses?
    if (!topoScanResults.topoResps || topoScanResults.topoResps.length === 0) {
      return (
        <AppWindow {...props}>
          <div>No responses received.</div>
        </AppWindow>
      );
    }

    // Process results from each responder
    const results = Object.values(topoScanResults.topoResps)
      .map(info => {
        const { bestSnr, bestTxBeamAngle, bestRxBeamAngle } =
          findBestBeam(info.itorLqmMat);
        return {
          addr: info.addr,
          bestSnr,
          bestTxBeamAngle,
          bestRxBeamAngle,
          location: info.pos
            ? formatPositionString(info.pos.latitude, info.pos.longitude)
            : null,
          adjs: info.adjs || []
        };
      })
      .sort((a, b) => b.bestSnr - a.bestSnr);

    // Group by node using adjacency information
    const resultsByNode = []; // each entry is an array of results
    const macToResult = {};
    for (const result of results) {
      let nodeIdx;
      for (const adj of result.adjs) {
        if (macToResult.hasOwnProperty(adj)) {
          nodeIdx = macToResult[adj];
          break; // found existing node entry
        }
      }
      if (nodeIdx === undefined) {
        // New node
        nodeIdx = resultsByNode.length;
        resultsByNode.push([]);
      }
      resultsByNode[nodeIdx].push(result);
      macToResult[result.addr] = nodeIdx;
    }

    const headers = [
      "Node",
      "MAC Addrs",
      "SNR (dB)",
      "Tx Angle",
      "Rx Angle",
      "Location",
      "Adjacencies"
    ];
    const getFields = (objs, idx) => {
      // Look for any other adjacencies not captured by node grouping
      const adjs = new Set();
      objs.forEach(o =>
        o.adjs.forEach(adj => {
          if (!macToResult.hasOwnProperty(adj)) {
            adjs.add(adj);
          }
        })
      );
      return [
        idx + 1,
        objs.map(o => o.addr).join("\n"),
        objs.map(o => o.bestSnr).join("\n"),
        objs.map(o => o.bestTxBeamAngle + "\xB0").join("\n"),
        objs.map(o => o.bestRxBeamAngle + "\xB0").join("\n"),
        objs.map(o => o.location).join("\n"),
        [...adjs].join("\n")
      ];
    };
    return (
      <AppWindow
        width={850}
        footer={
          <span>
            Scan from {topoScanResults.radioMac} - {results.length}{" "}
            responder(s), {resultsByNode.length} node(s)
          </span>
        }
        {...props}
      >
        <div className="scrollX">
          <Table>
            <TableHead>
              <TableRow head>
                {headers.map((header, idx) => (
                  <TableHeadCell key={idx}>{header}</TableHeadCell>
                ))}
              </TableRow>
            </TableHead>
            <TableBody>
              {resultsByNode.map((objs, idx) => (
                <TableRow key={idx}>
                  {getFields(objs, idx).map((cell, cellIdx) => (
                    <TableDataCell key={cellIdx} className="window-text pre">
                      {cell}
                    </TableDataCell>
                  ))}
                </TableRow>
              ))}
            </TableBody>
          </Table>
        </div>
      </AppWindow>
    );
  };

  return !topoScanResults
    ? renderRadioSelection(statusReport)
    : renderTopoScanResults(topoScanResults);
};

export default TopoScanWindow;
