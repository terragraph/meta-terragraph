/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

import React from "react";
import { Divider, ListItem, TextField } from "react95";
import AppWindow from "./AppWindow";
import ButtonMenu from "./ButtonMenu";

const Notepad = props => {
  const TITLE_SUFFIX = " - Notepad";
  const title = props.title || `Untitled${TITLE_SUFFIX}`;

  // Menu button handlers
  const onClickFileNew = () => props.onChange("");
  const onClickFileSave = () => {
    const downloadLink = document.createElement("a");
    const data =
      "data:text/plain;charset=utf-8," + encodeURIComponent(props.value);
    const filename = props.title.endsWith(TITLE_SUFFIX)
      ? props.title.substring(0, props.title.length - TITLE_SUFFIX.length)
      : props.title;
    downloadLink.href = data;
    downloadLink.download = filename;
    downloadLink.target = "_blank";
    try {
      document.body.appendChild(downloadLink);
      downloadLink.click();
    } catch (error) {
      console.error(error);
    } finally {
      document.body.removeChild(downloadLink);
    }
  };

  return (
    <AppWindow
      title={title}
      width={720}
      contentStyle={{ padding: 0 }}
      toolbar={
        <>
          <ButtonMenu
            buttonText="File"
            buttonProps={{
              size: "sm",
              variant: "menu",
              disabled: props.disabled
            }}
          >
            <ListItem size="sm" onClick={onClickFileNew}>
              New
            </ListItem>
            <ListItem size="sm" onClick={onClickFileSave}>
              Save
            </ListItem>
            <Divider size="unset" />
            <ListItem size="sm" onClick={props.onClose}>
              Exit
            </ListItem>
          </ButtonMenu>
          {props.extraToolbarButtons}
        </>
      }
      {...props}
    >
      <TextField
        className="notepad-textfield"
        multiline
        value={props.value}
        disabled={props.disabled}
        fullWidth
        spellCheck="false"
        autoComplete="false"
        autoCorrect="false"
        autoCapitalize="false"
        onChange={ev => props.onChange(ev.target.value)}
      />
    </AppWindow>
  );
};

export default Notepad;
