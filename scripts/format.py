#!/usr/bin/env python3


def bytes(num):
    for unit in ['B', 'KiB', 'MiB', 'GiB', 'TiB', 'PiB', 'EiB']:
        if abs(num) < 1024.0:
            break
        num /= 1024.0

    return "{:.2f} {}".format(float(num), unit)
