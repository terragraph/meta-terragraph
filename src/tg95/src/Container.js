/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

import React from "react";
import { AppBar, Button, Divider, ListItem, Toolbar } from "react95";
import Dock from "./components/Dock";
import ButtonMenu from "./components/ButtonMenu";
import TitleFrame from "./components/TitleFrame";
import StatusWindow from "./windows/StatusWindow";
import StatisticsWindow from "./windows/StatisticsWindow";
import NodeConfigWindow from "./windows/NodeConfigWindow";
import IgnitionWindow from "./windows/IgnitionWindow";
import RebootWindow from "./windows/RebootWindow";
import TopoScanWindow from "./windows/TopoScanWindow";

const Container = () => {
  // Individual window state
  const [statusWindowOpen, setStatusWindowOpen] = React.useState(false);
  const [statisticsWindowOpen, setStatisticsWindowOpen] = React.useState(false);
  const [nodeConfigWindowOpen, setNodeConfigWindowOpen] = React.useState(false);
  const [rebootWindowOpen, setRebootWindowOpen] = React.useState(false);
  const [topoScanWindowOpen, setTopoScanWindowOpen] = React.useState(false);
  const [ignitionWindowOpen, setIgnitionWindowOpen] = React.useState(false);

  // Window management...
  // - windowStack: order in which windows are stacked, 0 = highest
  // - toolbarWindows: order in which windows appear in the toolbar, 0 = left
  const [windowStack, setWindowStack] = React.useState([]);
  const [toolbarWindows, setToolbarWindows] = React.useState([]);

  const WINDOWS = [
    {
      name: "Node Status",
      component: StatusWindow,
      open: statusWindowOpen,
      setOpen: setStatusWindowOpen,
      showInToolbar: true,
      toolbarIcon: "\uD83D\uDEC8"
    },
    {
      name: "Statistics",
      component: StatisticsWindow,
      open: statisticsWindowOpen,
      setOpen: setStatisticsWindowOpen,
      showInToolbar: true,
      toolbarIcon: "\uD83D\uDCC8"
    },
    {
      name: "Configuration",
      windowName: "node_config.json - Notepad",
      component: NodeConfigWindow,
      open: nodeConfigWindowOpen,
      setOpen: setNodeConfigWindowOpen,
      showInToolbar: true,
      toolbarIcon: "\uD83D\uDCDD"
    },
    {
      name: "Ignition",
      component: IgnitionWindow,
      open: ignitionWindowOpen,
      setOpen: setIgnitionWindowOpen,
      showInToolbar: true,
      toolbarIcon: "\uD83D\uDCE1"
    },
    {
      name: "Topology Scan",
      component: TopoScanWindow,
      open: topoScanWindowOpen,
      setOpen: setTopoScanWindowOpen,
      showInToolbar: true,
      toolbarIcon: "\uD83D\uDCF6"
    },
    null /* divider */,
    {
      name: "Reboot",
      component: RebootWindow,
      open: rebootWindowOpen,
      setOpen: setRebootWindowOpen,
      showInToolbar: false
    }
  ];

  const focusWindow = windowIdx => {
    let newWindowStack;
    if (!windowStack.includes(windowIdx)) {
      newWindowStack = [windowIdx, ...windowStack];
    } else {
      newWindowStack = [
        windowIdx,
        ...windowStack.filter(idx => idx !== windowIdx)
      ];
    }
    setWindowStack(newWindowStack);
    return newWindowStack;
  };

  const openWindow = windowIdx => {
    focusWindow(windowIdx);
    const window = WINDOWS[windowIdx];
    window.setOpen(true);
    if (window.showInToolbar && !toolbarWindows.includes(windowIdx)) {
      setToolbarWindows([...toolbarWindows, windowIdx]);
    }
  };

  const closeWindow = windowIdx => {
    WINDOWS[windowIdx].setOpen(false);
    const newToolbarWindows = toolbarWindows.filter(idx => idx !== windowIdx);
    setToolbarWindows(newToolbarWindows);
    const newWindowStack = windowStack.filter(idx => idx !== windowIdx);
    setWindowStack(newWindowStack);
  };

  return (
    <>
      <div className="container">
        <div className="desktop">
          <TitleFrame title="Terragraph95" />
          {WINDOWS.map(
            (window, idx) =>
              window &&
              window.open && (
                <window.component
                  key={idx}
                  title={window.windowName || window.name}
                  stackIndex={windowStack.indexOf(idx)}
                  onFocus={() => focusWindow(idx)}
                  onClose={() => closeWindow(idx)}
                />
              )
          )}
        </div>
        <AppBar className="appbar">
          <Toolbar>
            <ButtonMenu
              flipped={true}
              buttonClass="start-menu-button"
              buttonText={
                <>
                  <span className="left-emoji" role="img" aria-label={"\u2728"}>
                    {"\u2728"}
                  </span>
                  Start
                </>
              }
            >
              {WINDOWS.map((window, idx) =>
                window ? (
                  <ListItem key={idx} onClick={() => openWindow(idx)}>
                    {window.name}
                  </ListItem>
                ) : (
                  <Divider key={idx} size="unset" />
                )
              )}
            </ButtonMenu>
            <div className="appbar-window-button-container">
              {toolbarWindows.map(windowIdx => (
                <Button
                  key={windowIdx}
                  className="appbar-window-button"
                  onClick={() => focusWindow(windowIdx)}
                  active={windowStack.length && windowStack[0] === windowIdx}
                >
                  <span
                    className="left-emoji"
                    role="img"
                    aria-label={WINDOWS[windowIdx].toolbarIcon}
                  >
                    {WINDOWS[windowIdx].toolbarIcon}
                  </span>
                  <span className="appbar-window-button-title ellipsis">
                    {WINDOWS[windowIdx].windowName || WINDOWS[windowIdx].name}
                  </span>
                </Button>
              ))}
            </div>
            <Dock />
          </Toolbar>
        </AppBar>
      </div>
    </>
  );
};

export default Container;
