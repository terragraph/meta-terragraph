/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

import React from "react";
import { Radio } from "react95";

const NodeRadio = props => {
  const errors = [];
  if (!props.status.initialized) {
    errors.push("uninitialized");
  } else {
    if (!props.status.nodeParamsSet) {
      errors.push("unconfigured");
    }
    if (!props.status.gpsSync) {
      errors.push("GPS unsynchronized");
    }
  }

  return (
    <Radio
      value={props.macAddr}
      label={
        errors.length > 0 ? (
          <span>
            {props.macAddr}
            <br />[{errors.join(", ")}]
          </span>
        ) : (
          props.macAddr
        )
      }
      disabled={errors.length > 0 || props.isDisabled}
      {...props}
    />
  );
};

export default NodeRadio;
