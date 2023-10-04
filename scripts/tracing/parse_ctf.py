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
import colorama
from colorama import Fore
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

def spit_json(path, trace_events):

    # add_metadata(trace_events, "thread_name", 0, KERNELSPACE_HYPERCALL_NR, {'name': "Kernelspace"})
    # add_metadata(trace_events, "thread_name", 0, USERSPACE_HYPERCALL_NR, {'name': "Userspace"})
    # add_metadata(trace_events, "process_name", 0, USERSPACE_HYPERCALL_NR, {'name': "VM0"})
    # add_metadata(trace_events, "process_labels", 0, USERSPACE_HYPERCALL_NR, {'labels': "Ubuntu 16.04"})
    # add_metadata(trace_events, "thread_sort_index", 0, KERNELSPACE_HYPERCALL_NR, {'sort_index': -5})
    # add_metadata(trace_events, "thread_sort_index", 0, USERSPACE_HYPERCALL_NR, {'sort_index': -10})

    content = json.dumps({
        "traceEvents": trace_events,
        "displayTimeUnit": "ns"
    })

    with open(path, "w") as f:
        f.write(content)

g_events = []

def format_json(name, ts):
    # Chrome trace format
    return {
        'pid': 0,
        'tid': 0,
        'name': name,
        'ph': 'X',
        'dur': 10,
        'ts': ts,
        'args': {
            'cpu': 0,
            'depth': 0
        },
    }

def main():
    colorama.init()

    args = parse_args()

    msg_it = bt2.TraceCollectionMessageIterator(args.trace)
    last_event_ns_from_origin = None
    timeline = []

    def get_thread(name):
        for t in timeline:
            if t.get('name', None) == name and t.get('in', 0 ) != 0 and not t.get('out', None):
                return t
        return {}

    def do_trace(msg):
        ns_from_origin = msg.default_clock_snapshot.ns_from_origin
        event = msg.event
        # Compute the time difference since the last event message.
        diff_s = 0

        dt = datetime.datetime.fromtimestamp(ns_from_origin / 1e9)

        if event.name in [
                'thread_switched_out',
                'thread_switched_in',
                'thread_pending',
                'thread_ready',
                'thread_resume',
                'thread_suspend',
                'thread_create',
                'thread_abort'
                ]:

            cpu = event.payload_field.get("cpu", None)
            thread_id = event.payload_field.get("thread_id", None)
            thread_name = event.payload_field.get("name", None)

            th = {}
            if event.name in ['thread_switched_out', 'thread_switched_in'] and cpu is not None:
                cpu_string = f"(cpu: {cpu})"
            else:
                cpu_string = ""

            # if thread_name:
            # elif thread_id:
            # else:

            if event.name in ['thread_switched_out', 'thread_switched_in']:
                if thread_name:
                    th = get_thread(thread_name)
                    if not th:
                        th['name'] = thread_name
                else:
                    th = get_thread(thread_id)
                    if not th:
                        th['name'] = thread_id

                if event.name in ['thread_switched_out']:
                    th['out'] = ns_from_origin
                    tin = th.get('in', None)
                    tout = th.get('out', None)
                    if tout is not None and tin is not None:
                        diff = tout - tin
                        th['runtime'] = diff
                elif event.name in ['thread_switched_in']:
                    th['in'] = ns_from_origin

                    timeline.append(th)

        elif event.name in ['thread_info']:
            stack_size = event.payload_field['stack_size']
        elif event.name in ['start_call', 'end_call']:
            if event.payload_field['id'] == 39:
                c = Fore.GREEN
            elif event.payload_field['id'] in [37, 38]:
                c = Fore.CYAN
            else:
                c = Fore.YELLOW
        elif event.name in ['semaphore_init', 'semaphore_take', 'semaphore_give']:
            c = Fore.CYAN
        elif event.name in ['mutex_init', 'mutex_take', 'mutex_give']:
            c = Fore.MAGENTA

        g_events.append(format_json(event.name, ns_from_origin))

    try:
        for msg in msg_it:
            if not isinstance(msg, bt2._EventMessageConst):
                continue

            do_trace(msg)
    finally:
        spit_json('./out.json', g_events)

if __name__=="__main__":
    main()
