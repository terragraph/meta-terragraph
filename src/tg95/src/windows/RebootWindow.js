/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

import React from "react";
import { Button } from "react95";
import AppWindow from "../components/AppWindow";
import Loading from "../components/Loading";
import { getStatusReport, sendReboot } from "../api/api";

// Liveness check intervals after rebooting
const LIVENESS_CHECK_INTERVAL_MS = 2000;
const INITIAL_LIVENESS_CHECK_DELAY_MS = 10000;

const RebootWindow = props => {
  const [isRebooting, setIsRebooting] = React.useState(false);
  const [timerId, setTimerId] = React.useState(null);

  React.useEffect(() => {
    return () => clearInterval(timerId);
  }, [timerId]);

  // After a reboot, keep checking until the API is reachable again,
  // then close this window.
  const livenessCheck = () => {
    getStatusReport(props.onClose, () => {
      setTimerId(setTimeout(livenessCheck, LIVENESS_CHECK_INTERVAL_MS));
    });
  };

  const onReboot = () => {
    sendReboot(() => {
      setIsRebooting(true);
      setTimerId(setTimeout(livenessCheck, INITIAL_LIVENESS_CHECK_DELAY_MS));
    }, console.error);
  };

  return (
    <AppWindow width={360} height={140} closeButton={false} {...props}>
      {isRebooting ? (
        <Loading message="Rebooting..." />
      ) : (
        <>
          <div>Are you sure you want to reboot the node?</div>
          <div className="window-footer-button-container">
            <Button className="prompt-button" primary onClick={onReboot}>
              Yes
            </Button>
            <Button className="prompt-button" onClick={props.onClose}>
              No
            </Button>
          </div>
        </>
      )}
    </AppWindow>
  );
};

export default RebootWindow;
