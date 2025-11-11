#!/bin/bash

mkdir -p ~/warp_logs
LOG_FILE=~/warp_logs/session-$(date +%F-%H%M%S).log
tmux new-session -s warplog "script -q -c bash $LOG_FILE"
