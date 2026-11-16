#!/usr/bin/env python3
"""Bound a native child lifetime without tearing down live C++ resources."""

import argparse
import json
import math
import os
import signal
import subprocess
import sys
import time


def positive_seconds(value):
    result = float(value)
    if not math.isfinite(result) or result <= 0:
        raise argparse.ArgumentTypeError("duration must be finite and positive")
    return result


def supervise(command, timeout, grace):
    requested = 0

    def remember(signum, _frame):
        nonlocal requested
        requested = signum

    previous = {sig: signal.signal(sig, remember)
                for sig in (signal.SIGINT, signal.SIGTERM)}
    child = None
    try:
        try:
            child = subprocess.Popen(command, start_new_session=True)
        except OSError:
            print('{"code":"child_launch_failed","phase":"supervisor",'
                  '"published":false,"durability":"not_published"}', file=sys.stderr)
            return 6
        deadline = time.monotonic() + timeout
        forwarded = False
        expired = False
        while True:
            result = child.poll()
            if result is not None:
                return 12 if expired else (result if result >= 0 else 128 - result)
            now = time.monotonic()
            if not forwarded and (requested or now >= deadline):
                expired = not requested
                if expired:
                    print(json.dumps({"code": "deadline_exceeded", "phase": "supervisor",
                                      "published": None, "durability": "unknown"}),
                          file=sys.stderr)
                try:
                    os.killpg(child.pid, requested or signal.SIGTERM)
                except ProcessLookupError:
                    pass
                forwarded = True
                deadline = now + grace
            elif forwarded and now >= deadline:
                try:
                    os.killpg(child.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                child.wait()
                # A kill cannot prove whether a rename already happened.
                print(json.dumps({"code": "watchdog_killed", "phase": "supervisor",
                                  "published": None, "durability": "unknown"}),
                      file=sys.stderr)
                return 12
            try:
                child.wait(timeout=min(0.05, max(0.001, deadline - now)))
            except subprocess.TimeoutExpired:
                pass
    finally:
        if child is not None and child.poll() is None:
            try:
                os.killpg(child.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            child.wait()
        for sig, handler in previous.items():
            signal.signal(sig, handler)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--timeout", required=True, type=positive_seconds)
    parser.add_argument("--grace", required=True, type=positive_seconds)
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command
    if command[:1] == ["--"]:
        command = command[1:]
    if not command:
        parser.error("a child command is required")
    try:
        return supervise(command, args.timeout, args.grace)
    except OSError:
        print('{"code":"supervisor_failed","phase":"supervisor",'
              '"published":null,"durability":"unknown"}', file=sys.stderr)
        return 6


if __name__ == "__main__":
    raise SystemExit(main())
