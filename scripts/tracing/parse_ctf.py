#!/usr/bin/env python3
#
# Copyright (c) 2020 Intel Corporation.
#
# SPDX-License-Identifier: Apache-2.0
"""
Script to parse CTF data and print to the screen in a custom and colorful
format.

Generate trace using samples/subsys/tracing for example:

    west build -b qemu_x86 samples/subsys/tracing  -t run \
      -- -DCONF_FILE=prj_uart_ctf.conf

    mkdir ctf
    cp build/channel0_0 ctf/
    cp subsys/tracing/ctf/tsdl/metadata ctf/
    ./scripts/tracing/parse_ctf.py -t ctf
"""

import json
import sys
import datetime
import argparse
try:
    import bt2
except ImportError:
    sys.exit("Missing dependency: You need to install python bindings of babeltrace.")

def parse_args():
    parser = argparse.ArgumentParser(
            description=__doc__,
            formatter_class=argparse.RawDescriptionHelpFormatter, allow_abbrev=False)
    parser.add_argument("-t", "--trace",
            required=True,
            help="tracing data (directory with metadata and trace file)")
    args = parser.parse_args()
    return args

def add_metadata(name, pid, tid, args):
    return {
        'pid': pid,
        'tid': tid,
        'name': name,
        'ph': 'M',
        'cat': "__metadata",
        'args': args,
    }

g_thread_names = {}

def add_thread(tid, name, active):
    if f'{tid}' not in g_thread_names.keys():
        g_thread_names[f'{tid}'] = {'name': str(name), 'active': active}
        return False

    prev = g_thread_names[f'{tid}']['active']
    g_thread_names[f'{tid}']['active'] = active

    return prev == active

def spit_json(path, trace_events):
    trace_events.append(add_metadata("thread_name", 0, 0, {'name': 'general'}))
    for k in g_thread_names.keys():
        trace_events.append(add_metadata("thread_name", 0, k, {'name': g_thread_names[k]['name']}))

    content = json.dumps({
        "traceEvents": trace_events,
        "displayTimeUnit": "ns"
    })

    with open(path, "w") as f:
        f.write(content)

g_events = []

def format_json(name, ts, ph, tid=0):
    # Chrome trace format
    # `args` has to have at least one arg, is
    # shown when clicking the event
    evt = {
        'pid': 0,
        'tid': int(tid),
        'name': name,
        'ph': ph,
        'ts': ts,
        'args': {
            'cpu': 0,
        },
    }

    if ph == 'X':
        # fake duration for now
        evt['dur'] = 100

    return evt

def main():
    args = parse_args()

    msg_it = bt2.TraceCollectionMessageIterator(args.trace)
    last_event_ns_from_origin = None
    timeline = []

    def do_trace(msg):
        ns_from_origin = msg.default_clock_snapshot.ns_from_origin
        event = msg.event
        ph = 'X'
        name = event.name
        tid = 0

        if 'thread' in event.name:
            name = 'thread_active'

            if 'thread_switched_in' in event.name:
                ph = 'B'
            elif 'thread_switched_out' in event.name:
                ph = 'E'
            else:
                print(f'THREAD OTHER: {event.name}')
                raise(Exception)

            tid = event.payload_field['thread_id']

            # Means that this event tries to mark the thread as active
            already = add_thread(tid, event.payload_field['name'], ph == 'B')

            if already:
                # Means the thread is already switched in/out,
                # adding another event will confuse the UI
                return

        g_events.append(format_json(name, ns_from_origin, ph, tid))

    try:
        for msg in msg_it:
            if not isinstance(msg, bt2._EventMessageConst):
                continue

            do_trace(msg)
    finally:
        spit_json('./out.json', g_events)

if __name__=="__main__":
    main()
