/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

import React, { Suspense } from "react";
import AppWindow from "../components/AppWindow";
import Loading from "../components/Loading";
import { getLinkStats } from "../api/ws";

// Lazy-load recharts components (bundle size is huge)
const LinkStatsChart = React.lazy(() => import("../components/LinkStatsChart"));

// Maximum size of stats array
const MAX_BUFFER_SIZE = 25 * 3;

const StatisticsWindow = props => {
  const [stats, setStats] = React.useState([]);

  React.useEffect(() => {
    const socket = getLinkStats(data =>
      setStats(s => {
        if (s.length >= MAX_BUFFER_SIZE) {
          s.shift();
        }
        return [...s, data];
      })
    );
    return () => socket.close();
  }, []);

  // Collect all unique stat keys seen
  const statKeys = [...new Set(stats.map(stat => stat.key))];
  statKeys.sort();

  // Recharts data format requires a different data key per line.
  // We'll add a duplicate "value" key named "value-<link_name>".
  const DATA_KEY_PREFIX = "value-";
  const linkSet = new Set();
  const processedStats = stats.map(stat => {
    const link = stat.radioMac + " \u21c6 " + stat.responderMac;
    const valueName = DATA_KEY_PREFIX + link;
    if (!linkSet.has(valueName)) {
      linkSet.add(valueName);
    }
    return { ...stat, [valueName]: stat.value };
  });
  const links = [...linkSet];
  links.sort();

  return (
    <AppWindow contentStyle={{ maxHeight: 600, overflowY: "auto" }} {...props}>
      <Suspense fallback={<Loading message="Loading charts..." />}>
        {processedStats.length ? (
          statKeys.map(key => (
            <LinkStatsChart
              containerProps={{ height: 160, width: "100%" }}
              key={key}
              statKey={key}
              stats={processedStats}
              links={links}
              getLinkName={link => link.substr(DATA_KEY_PREFIX.length)}
            />
          ))
        ) : (
          <Loading message="Fetching data..." />
        )}
      </Suspense>
    </AppWindow>
  );
};

export default StatisticsWindow;
