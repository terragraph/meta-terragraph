#!/bin/bash

# Ensure, if avaliable we load the default environment - e.g. LANG, PATH etc.
PROFILE=/etc/profile
if [ -r $PROFILE ]; then
  . $PROFILE
fi

if [ ! -z "$SSH_ORIGINAL_COMMAND" ]; then
  eval ip netns exec default bash -c \""$SSH_ORIGINAL_COMMAND"\"
  exit $?
fi

cur_netns=$(ip netns identify $$)

if [ -z "${cur_netns}" ] || [ "${cur_netns}" != "oob" ]; then
  exit 0
fi

netns=($(ip netns list))

if [[ ! ${netns[@]} =~ "default" ]]; then
  exit 1
fi

ip netns exec default bash
