/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

import React from "react";
import {
  LineChart,
  Line,
  XAxis,
  YAxis,
  CartesianGrid,
  ResponsiveContainer,
  Legend
} from "recharts";
import { getColor } from "../utils/colors";

const LinkStatsChart = props => {
  return (
    <ResponsiveContainer {...props.containerProps}>
      <LineChart data={props.stats.filter(stat => stat.key === props.statKey)}>
        <CartesianGrid
          stroke="rgba(6,0,132,0.3)"
          strokeDasharray="3 3"
          vertical={false}
        />
        <XAxis dataKey="timestamp" hide />
        <YAxis
          label={{
            value: props.statKey.toUpperCase(),
            angle: -90,
            position: "insideLeft"
          }}
          domain={["auto", "auto"]}
          allowDecimals={false}
        />
        <Legend />
        {[...props.links].map((link, idx) => (
          <Line
            key={link}
            type="monotone"
            name={props.getLinkName(link)}
            dataKey={link}
            stroke={getColor(idx)}
            strokeWidth={2}
            dot={false}
            isAnimationActive={false}
            connectNulls={true}
          />
        ))}
      </LineChart>
    </ResponsiveContainer>
  );
};

export default LinkStatsChart;
