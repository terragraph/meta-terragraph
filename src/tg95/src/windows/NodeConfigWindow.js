/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

import React from "react";
import { Divider, ListItem } from "react95";
import ButtonMenu from "../components/ButtonMenu";
import Notepad from "../components/Notepad";
import { getNodeConfig, setNodeConfig } from "../api/api";

const NodeConfigWindow = props => {
  const [draftConfig, setDraftConfig] = React.useState(null);
  const [footerText, setFooterText] = React.useState(null);

  React.useEffect(() => {
    getNodeConfig(setDraftConfig, setFooterText);
  }, []);

  // Menu button handlers
  const onClickActionValidate = () => {
    try {
      JSON.parse(draftConfig);
      setFooterText("Node config is valid JSON.");
    } catch (e) {
      setFooterText(e.message);
    }
  };
  const onClickActionReset = () => {
    setDraftConfig(null);
    setFooterText(null);
    getNodeConfig(setDraftConfig, setFooterText);
  };
  const onClickActionSendToNode = () => {
    try {
      JSON.parse(draftConfig);
      setNodeConfig(
        draftConfig,
        () => setFooterText("Successfully sent configuration to node."),
        err => setFooterText("Error: " + err)
      );
    } catch (e) {
      setFooterText(e.message);
    }
  };

  return (
    <Notepad
      value={draftConfig === null ? "Loading..." : draftConfig}
      onChange={setDraftConfig}
      disabled={draftConfig === null}
      extraToolbarButtons={
        <>
          <ButtonMenu
            buttonText="Actions"
            buttonProps={{
              size: "sm",
              variant: "menu",
              disabled: draftConfig === null
            }}
          >
            <ListItem size="sm" onClick={onClickActionValidate}>
              Validate
            </ListItem>
            <ListItem size="sm" onClick={onClickActionReset}>
              Reset
            </ListItem>
            <Divider size="unset" />
            <ListItem size="sm" onClick={onClickActionSendToNode}>
              Send To Node
            </ListItem>
          </ButtonMenu>
        </>
      }
      footer={footerText}
      {...props}
    />
  );
};

export default NodeConfigWindow;
