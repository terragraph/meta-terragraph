/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

import React from "react";
import { Cutout, Window, WindowContent } from "react95";

const TitleFrame = props => {
  return (
    <Window id="title-frame">
      <WindowContent>
        <Cutout>
          <h1>{props.title}</h1>
        </Cutout>
      </WindowContent>
    </Window>
  );
};

export default TitleFrame;
