/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

import React from "react";
import { Hourglass, LoadingIndicator } from "react95";

const Loading = props => {
  return (
    <div>
      <p className="loading-text">
        <Hourglass />
        <span style={{ marginLeft: 4 }}>{props.message || "Loading..."}</span>
      </p>
      <LoadingIndicator isLoading />
    </div>
  );
};

export default Loading;
