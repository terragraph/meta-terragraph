# LED Agent
This document describes the LED controller daemon that runs on Puma hardware.

## Overview
When `envParams.LED_AGENT_ENABLED` is set in the node configuration, the
`led-agent` daemon will run and control the three green LED lights beneath Puma
units using GPIO pins (`/sys/class/gpio`).

The behavior of the LEDs is as follows:

| LED | GPIO | Possible States |
| --- | ---- | --------------- |
| A   | 505  | <ul><li>`ON`: The node is powered on (specifically, `led-agent` is running).</li></ul> |
| B   | 506  | <ul><li>`ON`: At least one link is associated.</li><li>`OFF`: No links are associated.</li></ul> |
| C   | 502  | <ul><li>`ON`: All links are of good RF quality.</li><li>`BLINK`: At least one link exhibits poor RF quality.</li><li>`OFF`: No links are associated, or RF quality could not be determined.</li></ul> |

Link quality is determined by the MCS reported via firmware stats. A "good" link
must be operating at MCS 9 or higher.

`led-agent` subscribes to stats published by `driver-if` to obtain link quality
data.
