/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

import React from "react";

const TextPair = props => {
  return (
    <div className="justify">
      <span>{props.left}</span>
      <span>{props.right}</span>
    </div>
  );
};

export default TextPair;
