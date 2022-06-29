/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

import React from "react";
import { Button, Fieldset, Hourglass, Radio, Select, TextField } from "react95";
import AppWindow from "../components/AppWindow";
import Loading from "../components/Loading";
import NodeRadio from "../components/NodeRadio";
import {
  assocLink,
  dissocLink,
  getStatusReport,
  getLinkDump
} from "../api/api";
import { validateMacAddr } from "../utils/network";

const ACTIONS = [
  { name: "ASSOC", fn: assocLink, isDisabled: () => false },
  {
    name: "DISSOC",
    fn: dissocLink,
    isDisabled: (statusReport, linkDump) =>
      !linkDump?.linkStatusDump ||
      Object.keys(linkDump.linkStatusDump).length === 0
  }
];

const IgnitionWindow = props => {
  // Node status data
  const [statusReport, setStatusReport] = React.useState(null);
  const [linkDump, setLinkDump] = React.useState(null);

  // Request/error status
  const [message, setMessage] = React.useState(null);
  const [isWaiting, setIsWaiting] = React.useState(false);
  const [isFinished, setIsFinished] = React.useState(false);

  // Form fields
  const [action, setAction] = React.useState(ACTIONS[0].name);
  const [initiator, setInitiator] = React.useState(null);
  const [responder, setResponder] = React.useState(null);

  React.useEffect(() => {
    const onError = e => {
      console.error(e);
      setMessage("Error fetching data.");
      setIsFinished(true);
    };
    getStatusReport(setStatusReport, onError);
    getLinkDump(setLinkDump, onError);
  }, []);

  // Validate and send the ignition request.
  const doIgnitionAction = () => {
    if (!action || !initiator || !responder) {
      setMessage("Please fill out all form fields.");
      return;
    }
    if (!validateMacAddr(initiator) || !validateMacAddr(responder)) {
      setMessage("Invalid MAC address format.");
      return;
    }

    const fn = ACTIONS.find(o => o.name === action)?.fn;
    if (!fn) {
      setMessage("Internal error.");
      return;
    }
    const actionStr =
      action[0].toUpperCase() + action.substring(1).toLowerCase();
    setIsWaiting(true);
    fn(
      initiator,
      responder,
      data => {
        setMessage(`${actionStr} command was sent.`);
        setIsWaiting(false);
        setIsFinished(true);
      },
      e => {
        console.error(e);
        setMessage(`${actionStr} command returned an error.`);
        setIsWaiting(false);
        setIsFinished(true);
      }
    );
  };

  return (
    <AppWindow width={360} {...props}>
      {isFinished ? (
        <div style={{ textAlign: "center" }}>
          {message && <div style={{ paddingBottom: 16 }}>{message}</div>}
          <Button className="choice-button" primary onClick={props.onClose}>
            OK
          </Button>
        </div>
      ) : !statusReport || !linkDump ? (
        <Loading />
      ) : !statusReport?.radioStatus ||
        Object.keys(statusReport.radioStatus).length === 0 ? (
        <span>Node has no radios?</span>
      ) : (
        <>
          <div>
            <div style={{ textAlign: "center" }}>
              {ACTIONS.map(({ name, isDisabled }) => (
                <Radio
                  key={name}
                  className="radio-horizontal capitalize"
                  label={name.toLowerCase()}
                  value={name}
                  checked={action === name}
                  onChange={e => {
                    setAction(e.target.value);
                    setResponder(null);
                    setMessage(null);
                  }}
                  disabled={isWaiting || isDisabled(statusReport, linkDump)}
                  name="action"
                />
              ))}
            </div>
            <Fieldset label="Initiator">
              {Object.entries(statusReport.radioStatus).map(([mac, status]) => (
                <div key={mac}>
                  <NodeRadio
                    macAddr={mac}
                    status={status}
                    checked={initiator === mac}
                    onChange={e => {
                      setInitiator(e.target.value);
                      if (action === "DISSOC") {
                        setResponder(null);
                      }
                    }}
                    name="initiator"
                    isDisabled={isWaiting}
                  />
                </div>
              ))}
            </Fieldset>
            <Fieldset label="Responder">
              {action === "ASSOC" && (
                <TextField
                  value={responder}
                  onChange={e => setResponder(e.target.value)}
                />
              )}
              {action === "DISSOC" && (
                <Select
                  native
                  options={Object.entries(linkDump?.linkStatusDump || [])
                    .filter(
                      ([responderMac, linkStatus]) =>
                        linkStatus.radioMac === initiator
                    )
                    .map(([responderMac, linkStatus]) => ({
                      value: responderMac,
                      label: responderMac
                    }))}
                  onChange={(evt, nextSelection) =>
                    setResponder(nextSelection.value)
                  }
                  value={responder}
                  width="100%"
                  disabled={isWaiting || !initiator}
                />
              )}
            </Fieldset>
          </div>
          {message && (
            <div style={{ textAlign: "center", padding: "4px 0" }}>
              {message}
            </div>
          )}
          <div className="justify" style={{ paddingTop: 8 }}>
            <span>{isWaiting && <Hourglass />}</span>
            <span>
              <Button
                className="choice-button"
                primary
                onClick={doIgnitionAction}
                disabled={!action || !initiator || !responder || isWaiting}
              >
                OK
              </Button>
              <Button className="choice-button" onClick={props.onClose}>
                Cancel
              </Button>
            </span>
          </div>
        </>
      )}
    </AppWindow>
  );
};

export default IgnitionWindow;
