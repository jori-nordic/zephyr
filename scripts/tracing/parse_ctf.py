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
    if tid not in g_thread_names.keys():
        g_thread_names[tid] = {'name': str(name), 'active': active}
        return False

    prev = g_thread_names[tid]['active']
    g_thread_names[tid]['active'] = active

    return prev == active

def spit_json(path, trace_events):
    trace_events.append(add_metadata("thread_name", 0, 0, {'name': 'general'}))
    trace_events.append(add_metadata("thread_name", 0, 1, {'name': 'ISR context'}))
    trace_events.append(add_metadata("thread_name", 0, 2, {'name': 'Mutex'}))
    trace_events.append(add_metadata("thread_name", 0, 3, {'name': 'Semaphore'}))
    trace_events.append(add_metadata("thread_name", 0, 4, {'name': 'Timer'}))
    trace_events.append(add_metadata("thread_name", 0, 5, {'name': 'Network buffers'}))

    for k in g_thread_names.keys():
        trace_events.append(add_metadata("thread_name", 0, int(k), {'name': g_thread_names[k]['name']}))

    content = json.dumps({
        "traceEvents": trace_events,
        "displayTimeUnit": "ns"
    })

    with open(path, "w") as f:
        f.write(content)

g_events = []
g_isr_active = False

def format_json(name, ts, ph, tid=0, obj=0, cnt=0):
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
        evt['dur'] = 10

    if ph == 'C':
        evt['args'] = {'count': int(cnt)}
        return evt

    if obj != 0:
        evt['args'] = {'obj': f'{hex(obj)}'}

    return evt

prev_ts = 0
def main():
    args = parse_args()

    msg_it = bt2.TraceCollectionMessageIterator(args.trace)
    last_event_ns_from_origin = None
    timeline = []

    def do_trace(msg):
        ns_from_origin = msg.default_clock_snapshot.ns_from_origin // 1000
        event = msg.event
        ph = 'X'
        name = event.name
        tid = 0
        global g_isr_active
        global prev_ts

        # print(f'{event.name} - {ns_from_origin}')
        # workaround for UI getting confused when two events appear
        # at the same reported time
        while prev_ts >= ns_from_origin:
            ns_from_origin += 1
            # print('workaround')

        prev_ts = ns_from_origin

        if 'thread' in event.name:
            name = 'thread_active'

            if 'thread_switched_in' in event.name:
                ph = 'B'
            elif 'thread_switched_out' in event.name:
                ph = 'E'
            elif 'thread_create' in event.name:
                tid = event.payload_field['thread_id']
                g_events.append(format_json(name, ns_from_origin, ph, tid))
                return
            elif 'thread_info' in event.name:
                tid = event.payload_field['thread_id']
                g_events.append(format_json(name, ns_from_origin, ph, tid))
                return
            elif 'thread_name_set' in event.name:
                tid = event.payload_field['thread_id']
                g_events.append(format_json(name, ns_from_origin, ph, tid))
                return
            else:
                print(f'THREAD OTHER: {event.name}')
                raise(Exception)

            tid = event.payload_field['thread_id']

            # Means that this event tries to mark the thread as active
            already = add_thread(tid, event.payload_field['name'], ph == 'B')

            if already and ph == 'B':
                # Means the thread is already switched in/out,
                # adding another event will confuse the UI
                # It probably means that we are coming from an ISR
                # g_events.append(format_json(event.name, ns_from_origin, 'X', 0))
                return

            if ph == 'B' and g_isr_active:
                g_isr_active = False
                g_events.append(format_json('isr_active', ns_from_origin, 'E', 1))

        elif 'idle' in name:
            # Means no thread is switched in.
            # Also a valid way of exiting the ISR
            if g_isr_active:
                g_isr_active = False
                g_events.append(format_json('isr_active', ns_from_origin - 1, 'E', 1))

        elif 'isr' in name:
            # debug
            # g_events.append(format_json(event.name, ns_from_origin, 'X', 0))

            if 'isr_enter' in name:
                if g_isr_active:
                    return

                ph = 'B'
                g_isr_active = True
            elif 'isr_exit' in name:
                if not g_isr_active:
                    return

                ph = 'E'
                g_isr_active = False
            else:
                raise(Exception)

            name = 'isr_active'
            tid = 1

        elif 'mutex' in name:
            tid = 2

        elif 'semaphore' in name:
            tid = 3

        elif 'timer' in name:
            tid = 4

        elif 'net_buf' in name:
            tid = 5

            # TODO: use counter events to graph usage
            if 'net_buf_allocated' in name:
                buf = event.payload_field['buf']

                # TODO: one TID per buffer pool
                if buf != 0:
                    ph = 'B'
                    pool = event.payload_field['pool']
                    g_events.append(format_json(f"buffer [{hex(buf)}]", ns_from_origin, ph, tid, buf))
                    return

            elif 'net_buf_destroyed' in name:
                ph = 'E'
                pool = event.payload_field['pool']
                buf = event.payload_field['buf']
                g_events.append(format_json(f"buffer [{hex(buf)}]", ns_from_origin, ph, tid, buf))
                return

            elif 'ref' in name:
                ph = 'C'
                buf = event.payload_field['buf']
                cnt = event.payload_field['count']
                g_events.append(format_json(f"[{hex(buf)}] reference", ns_from_origin, ph, tid, buf, cnt))
                return

        else:
            print(f'Unknown event: {event.name} payload {event.payload_field}')
            raise(Exception)

        g_events.append(format_json(name, ns_from_origin, ph, tid))

    try:
        for msg in msg_it:
            if not isinstance(msg, bt2._EventMessageConst):
                continue

            do_trace(msg)
    except bt2._Error as e:
        if e._msg != 'graph object could not run once':
            raise(e)
        print(f'Trace does not terminate cleanly')
    finally:
        spit_json('./out.json', g_events)
        print(f'Done')

if __name__=="__main__":
    main()
