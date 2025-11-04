#!/bin/sh

CONFIG_FILE=vrxtbl.yaml
PID_FILE=/tmp/control.pid

if [ -f $1/${CONFIG_FILE} ]; then
    diff $1/${CONFIG_FILE} /etc/${CONFIG_FILE} > /dev/null
    if [ $? -ne 0 ]; then 
        cp $1/${CONFIG_FILE} /etc/${CONFIG_FILE}.new
        control_pid=$(cat $PID_FILE)
        if [ -n "${control_pid}" ]; then
            kill -HUP ${control_pid}
        fi
    fi
fi
