#!/usr/bin/env python3
#
# Copyright (c) 2020 Intel Corporation.
#
# SPDX-License-Identifier: Apache-2.0
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
g_buf_names = {}
active_buffers = {}

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

    for k in g_buf_names.keys():
        name = g_buf_names[k]['name']
        trace_events.append(add_metadata("thread_name", 2, int(k), {'name': f'Refs on {name}'}))
        trace_events.append(add_metadata("thread_name", 1, int(k), {'name': f'Buf lifetime for {name}'}))

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

def format_json(name, ts, ph, tid=0, meta=None, pid=0):
    # Chrome trace format
    # `args` has to have at least one arg, is
    # shown when clicking the event
    evt = {
        'pid': int(pid),
        'tid': int(tid),
        'name': name,
        'ph': ph,
        'ts': ts,
        'args': {
            'dummy': 0,
        },
    }

    if ph == 'X':
        # fake duration for now
        evt['dur'] = 10

    if meta is not None:
        evt['args'] = meta

    return evt

prev_ts = 0
def main():
    args = parse_args()

    msg_it = bt2.TraceCollectionMessageIterator(args.trace)
    last_event_ns_from_origin = None
    timeline = []

    def do_trace(msg):
        ns_from_origin = msg.default_clock_snapshot.ns_from_origin / 1000
        event = msg.event
        ph = 'i'
        name = event.name
        tid = 0
        meta = None
        global g_isr_active
        global prev_ts

        # print(f'{event.name} - {ns_from_origin}')
        # workaround for UI getting confused when two events appear
        # at the same reported time
        while prev_ts >= ns_from_origin:
            ns_from_origin += .001
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

            if 'net_buf_allocated' in name or 'net_buf_destroyed' in name:
                buf = event.payload_field['buf']
                poolname = event.payload_field['name']
                pool = event.payload_field['pool']

                if buf == 0:
                    ph = 'i'
                    free = event.payload_field['free']
                    meta = {'pool_name': str(poolname), 'pool_addr': f'{hex(pool)}'}
                    g_events.append(format_json(f"net_buf_alloc_failed", ns_from_origin, ph, tid, meta))
                    return
                else:
                    # Record pool free count
                    ph = 'C'
                    free = event.payload_field['free']
                    meta = {f'{poolname} ({hex(pool)})': int(free)}
                    g_events.append(format_json(f"free bufs", ns_from_origin, ph, tid, meta))

                    # Record buffer lifetime as duration event
                    if 'allocated' in name:
                        ph = 'B'
                        if buf in active_buffers.keys():
                            raise Exception(f"Missing destroy for buf {hex(buf)}")
                        active_buffers[buf] = 1
                        g_events.append(format_json(f"ref", ns_from_origin, ph, buf, meta, 2))
                    else:
                        ph = 'E'
                        if buf not in active_buffers.keys():
                            raise Exception(f"Missing alloc for buf {hex(buf)}")
                        del active_buffers[buf]
                        g_events.append(format_json(f"ref", ns_from_origin, ph, buf, meta, 2))

                    meta = {'pool_name': str(poolname), 'pool_addr': f'{hex(pool)}'}
                    g_events.append(format_json(f"buf [{hex(buf)}]", ns_from_origin, ph, buf, meta, 1))
                    return

                    ph = 'i'    # debug
                    meta = {'name': str(poolname), 'pool': f'{hex(pool)}', 'buf': f'{hex(buf)}'}

            elif 'net_buf_alloc' in name:
                poolname = event.payload_field['name']
                pool = event.payload_field['pool']
                free = event.payload_field['free']
                meta = {'name': str(poolname), 'pool': f'{hex(pool)}', 'count': int(free)}

            elif 'ref' in name:
                buf = event.payload_field['buf']
                cnt = event.payload_field['count']
                tid = buf

                # TODO: keep track of origin pool and mention it in the name

                if tid not in g_buf_names.keys():
                    g_buf_names[tid] = {'name': hex(buf), 'active': 0}

                if buf not in active_buffers.keys():
                    raise Exception(f"Refcounting a buf that hasn't been allocated: {hex(buf)}")

                if 'unref' in name:
                    ph = 'E'
                    active_buffers[buf] -= 1
                else:
                    ph = 'B'
                    active_buffers[buf] += 1

                if cnt != active_buffers[buf]:
                    print(f"Something doesn't add up: {hex(buf)}")

                g_events.append(format_json(f"ref", ns_from_origin, ph, tid, meta, 2))
                return

        else:
            print(f'Unknown event: {event.name} payload {event.payload_field}')
            raise(Exception)

        g_events.append(format_json(name, ns_from_origin, ph, tid, meta))

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
