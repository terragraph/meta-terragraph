/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

import React from "react";
import {
  Button,
  Panel,
  Toolbar,
  Window,
  WindowContent,
  WindowHeader
} from "react95";
import Draggable from "react-draggable";

// CSS z-index values - actual value subtracts "stackIndex" prop
const Z_INDEX_BASE = 500;

// Default window dimensions
const DEFAULT_WIDTH = 512;
const DEFAULT_HEIGHT = 200;

const AppWindow = props => {
  const nodeRef = React.useRef(null);

  return (
    <Draggable
      nodeRef={nodeRef}
      handle=".window-header-title"
      onMouseDown={props.onFocus}
      bounds="parent"
    >
      <Window
        ref={nodeRef}
        className="window"
        style={{
          zIndex: Z_INDEX_BASE - props.stackIndex,
          width: props.width || DEFAULT_WIDTH,
          minHeight: props.height || DEFAULT_HEIGHT
        }}
      >
        <WindowHeader active={props.stackIndex === 0} className="window-header">
          <span className="window-header-title ellipsis">{props.title}</span>
          {props.closeButton !== false && (
            <Button onClick={props.onClose}>
              <span className="close-icon" />
            </Button>
          )}
        </WindowHeader>
        {props.toolbar && <Toolbar noPadding>{props.toolbar}</Toolbar>}
        <WindowContent className="window-content" style={props.contentStyle}>
          {props.children}
        </WindowContent>
        {props.footer && (
          <Panel
            className="window-footer ellipsis"
            variant="well"
            style={props.footerStyles}
          >
            {props.footer}
          </Panel>
        )}
      </Window>
    </Draggable>
  );
};

export default AppWindow;
