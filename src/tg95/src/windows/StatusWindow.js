/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

import React from "react";
import {
  Anchor,
  Fieldset,
  Tabs,
  Tab,
  TabBody,
  Table,
  TableBody,
  TableHead,
  TableRow,
  TableHeadCell,
  TableDataCell
} from "react95";
import AppWindow from "../components/AppWindow";
import Loading from "../components/Loading";
import TextPair from "../components/TextPair";
import { getStatusReport, getLinkDump } from "../api/api";

// From Controller.thrift & Topology.thrift
const NodeType = { 1: "CN", 2: "DN" };
const NodeStatusType = { 1: "OFFLINE", 2: "ONLINE", 3: "ONLINE_INITIATOR" };
const LinkStatusType = { 1: "LINK_UP", 2: "LINK_DOWN" };

// Tab names
const TABS = ["General", "Radios", "Links", "Versions", "Interfaces", "BGP"];

// Text shown for unknown/missing fields
const UNKNOWN = "unknown";

const StatusWindow = props => {
  const [statusReport, setStatusReport] = React.useState(null);
  const [linkDump, setLinkDump] = React.useState(null);
  const [lastUpdated, setLastUpdated] = React.useState(null);
  const [activeTab, setActiveTab] = React.useState(0);

  const fetchData = () => {
    getStatusReport(data => {
      setStatusReport(data);
      setLastUpdated(new Date());
    }, console.error);
    getLinkDump(data => {
      setLinkDump(data);
      setLastUpdated(new Date());
    }, console.error);
  };
  const resetData = () => {
    setStatusReport(null);
    setLinkDump(null);
    setLastUpdated(null);
  };

  React.useEffect(fetchData, []);

  const footerProps = lastUpdated
    ? {
        footer: (
          <TextPair
            left={
              <Anchor
                onClick={() => {
                  resetData();
                  fetchData();
                }}
              >
                Click to refresh.
              </Anchor>
            }
            right={`Last updated: ${
              lastUpdated ? lastUpdated.toLocaleTimeString() : UNKNOWN
            }`}
          />
        ),
        footerStyles: { textAlign: "right" }
      }
    : {};

  const renderGeneral = statusReport => {
    if (!statusReport) {
      return <Loading />;
    }

    return (
      <div>
        <Fieldset label="Info">
          <TextPair
            left="E2E Status:"
            right={NodeStatusType[statusReport.status] || UNKNOWN}
          />
          <TextPair
            left="IPv6 Addr:"
            right={statusReport.ipv6Address || UNKNOWN}
          />
          <TextPair
            left="Node Type:"
            right={NodeType[statusReport.nodeType] || UNKNOWN}
          />
        </Fieldset>
        <br />
        <Fieldset label="Hardware">
          <TextPair
            left="Board ID:"
            right={statusReport.hardwareBoardId || UNKNOWN}
          />
          <TextPair
            left="Model:"
            right={statusReport.hardwareModel || UNKNOWN}
          />
        </Fieldset>
      </div>
    );
  };

  const renderRadios = statusReport => {
    if (!statusReport) {
      return <Loading />;
    }
    if (
      !statusReport?.radioStatus ||
      Object.keys(statusReport.radioStatus).length === 0
    ) {
      return <span>n/a</span>;
    }

    const headers = ["MAC Addr", "Status", "GPS Sync"];
    return (
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
            {Object.entries(statusReport.radioStatus).map(([mac, status]) => (
              <TableRow key={mac}>
                <TableDataCell className="window-text">
                  {mac || UNKNOWN}
                </TableDataCell>
                <TableDataCell className="window-text">
                  {status.nodeParamsSet
                    ? "configured"
                    : status.initialized
                    ? "initialized"
                    : "n/a"}
                </TableDataCell>
                <TableDataCell className="window-text">
                  {status.gpsSync ? "yes" : "no"}
                </TableDataCell>
              </TableRow>
            ))}
          </TableBody>
        </Table>
      </div>
    );
  };

  const renderLinks = linkDump => {
    if (!linkDump) {
      return <Loading />;
    }
    if (
      !linkDump?.linkStatusDump ||
      Object.keys(linkDump.linkStatusDump).length === 0
    ) {
      return <span>n/a</span>;
    }

    const headers = ["Interface", "Radio MAC", "Responder MAC", "Status"];
    return (
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
            {Object.entries(linkDump.linkStatusDump).map(
              ([responderMac, info]) => (
                <TableRow key={responderMac}>
                  <TableDataCell className="window-text">
                    {info.ifname}
                  </TableDataCell>
                  <TableDataCell className="window-text">
                    {info.radioMac}
                  </TableDataCell>
                  <TableDataCell className="window-text">
                    {responderMac}
                  </TableDataCell>
                  <TableDataCell className="window-text">
                    {LinkStatusType[info.linkStatusType] || UNKNOWN}
                  </TableDataCell>
                </TableRow>
              )
            )}
          </TableBody>
        </Table>
      </div>
    );
  };

  const renderVersions = statusReport => {
    if (!statusReport) {
      return <Loading />;
    }

    const headers = ["Type", "Version"];
    const rows = [
      ["Software", statusReport?.version],
      ["U-Boot", statusReport?.ubootVersion],
      ["Firmware", statusReport?.firmwareVersion]
    ];
    return (
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
            {rows.map((rowData, rowIdx) => (
              <TableRow key={rowIdx}>
                {rowData.map((cell, cellIdx) => (
                  <TableDataCell className="window-text" key={cellIdx}>
                    {cell || UNKNOWN}
                  </TableDataCell>
                ))}
              </TableRow>
            ))}
          </TableBody>
        </Table>
      </div>
    );
  };

  const renderInterfaces = statusReport => {
    if (!statusReport) {
      return <Loading />;
    }
    if (
      !statusReport?.networkInterfaceMacs ||
      Object.keys(statusReport.networkInterfaceMacs).length === 0
    ) {
      return <span>n/a</span>;
    }
    const interfaces = Object.keys(statusReport.networkInterfaceMacs).sort();

    const headers = ["Interface", "MAC Addr"];
    return (
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
            {interfaces.map(ifName => (
              <TableRow key={ifName}>
                <TableDataCell className="window-text">{ifName}</TableDataCell>
                <TableDataCell className="window-text">
                  {statusReport.networkInterfaceMacs[ifName]}
                </TableDataCell>
              </TableRow>
            ))}
          </TableBody>
        </Table>
      </div>
    );
  };

  const renderBgp = statusReport => {
    if (!statusReport) {
      return <Loading />;
    }
    if (
      !statusReport?.bgpStatus ||
      Object.keys(statusReport.bgpStatus).length === 0
    ) {
      return <span>n/a</span>;
    }

    const renderRoutes = routes => (
      <div style={{ paddingLeft: 16 }}>
        {routes.map(route => (
          <div key={route.network}>
            {route.network + " \u2192 " + route.nextHop}
          </div>
        ))}
      </div>
    );
    return statusReport.bgpStatus ? (
      Object.entries(statusReport.bgpStatus).map(([ip, info], idx) => (
        <React.Fragment key={ip}>
          <Fieldset label={ip}>
            <TextPair
              left="Status:"
              right={info.online ? "Established" : "Disconnected"}
            />
            <TextPair left="ASN:" right={info.asn} />
            <TextPair
              left={info.online ? "Uptime:" : "Downtime:"}
              right={info.upDownTime}
            />
            <TextPair
              left={isNaN(info.stateOrPfxRcd) ? "State:" : "Received Prefixes:"}
              right={info.stateOrPfxRcd}
            />
            {info.advertisedRoutes.length ? (
              <>
                <TextPair
                  left="Advertised Routes:"
                  right={info.advertisedRoutes.length}
                />
                {renderRoutes(info.advertisedRoutes)}
              </>
            ) : null}
            {info.receivedRoutes.length ? (
              <>
                <TextPair
                  left="Received Routes:"
                  right={info.receivedRoutes.length}
                />
                {renderRoutes(info.receivedRoutes)}
              </>
            ) : null}
          </Fieldset>
          {idx < Object.keys(statusReport.bgpStatus).length - 1 && <br />}
        </React.Fragment>
      ))
    ) : (
      <span>n/a</span>
    );
  };

  return (
    <AppWindow {...footerProps} {...props}>
      {lastUpdated ? (
        <>
          <Tabs value={activeTab} onChange={(e, value) => setActiveTab(value)}>
            {TABS.map((name, idx) => (
              <Tab key={name} value={idx}>
                {name}
              </Tab>
            ))}
          </Tabs>
          <TabBody className="tab-body">
            {activeTab === 0 && renderGeneral(statusReport)}
            {activeTab === 1 && renderRadios(statusReport)}
            {activeTab === 2 && renderLinks(linkDump)}
            {activeTab === 3 && renderVersions(statusReport)}
            {activeTab === 4 && renderInterfaces(statusReport)}
            {activeTab === 5 && renderBgp(statusReport)}
          </TabBody>
        </>
      ) : (
        <Loading />
      )}
    </AppWindow>
  );
};

export default StatusWindow;
