/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

import React from "react";
import { Panel } from "react95";

const Dock = props => {
  const [date, setDate] = React.useState(new Date());

  React.useEffect(() => {
    const id = setInterval(() => setDate(new Date()), 1000);
    return () => clearInterval(id);
  });

  const dateStr = date.toLocaleTimeString([], {
    hour: "2-digit",
    minute: "2-digit"
  });

  return (
    <Panel id="appbar-dock" variant="well">
      <span>{dateStr}</span>
    </Panel>
  );
};

export default Dock;
