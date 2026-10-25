#!/usr/bin/env python3
"""Decode the onboard flash backup (FLASHn.BIN from the SD card) to CSV on stdout.
Usage: python3 decode_flash.py FLASH0.BIN > flash.csv
Record layout must match src/flashrec.h."""
import math
import struct
import sys

FMT = "<IHBB4ff3f3f4hhH"  # t_soe_ms (ms since SOE), boot, state, flags, chP[4], barP, acc[3], gyr[3], chT[4], barT, crc
STATES = ["BOOT", "STANDBY", "TEST", "ACTIVE", "SHUTDOWN", "SAFE"]
assert struct.calcsize(FMT) == 64


def crc16(data):  # CCITT-FALSE, same as firmware
    c = 0xFFFF
    for b in data:
        c ^= b << 8
        for _ in range(8):
            c = ((c << 1) ^ 0x1021) & 0xFFFF if c & 0x8000 else (c << 1) & 0xFFFF
    return c


assert crc16(b"123456789") == 0x29B1


def f(v):
    return "" if math.isnan(v) else f"{v:.3f}"


def t(v):
    return "" if v == -32768 else f"{v / 100:.2f}"


def main(path):
    data = open(path, "rb").read()
    print("boot,t_soe_s,state,soe,lo,cam,sd_ok,ch1_p,ch1_t,ch2_p,ch2_t,ch3_p,ch3_t,ch4_p,ch4_t,bar_p,bar_t,ax,ay,az,gx,gy,gz")
    bad = 0
    for i in range(0, len(data) - 63, 64):
        rec = data[i:i + 64]
        if rec == b"\xff" * 64:  # padding at the end of a page
            continue
        r = struct.unpack(FMT, rec)
        if crc16(rec[:62]) != r[-1]:
            bad += 1
            continue
        ms, boot, st, fl = r[0:4]
        tsoe = f"{(ms - (1 << 32) if ms >= 1 << 31 else ms) / 1000:.1f}"  # signed: negative before a re-armed SOE
        chp, barp, acc, gyr, cht, bart = r[4:8], r[8], r[9:12], r[12:15], r[15:19], r[19]
        row = [boot, tsoe, STATES[st] if st < len(STATES) else st, fl & 1, fl >> 1 & 1, fl >> 2 & 1, fl >> 3 & 1]
        for k in range(4):
            row += [f(chp[k]), t(cht[k])]
        row += [f(barp), t(bart)] + [f(v) for v in acc + gyr]
        print(",".join(map(str, row)))
    if bad:
        print(f"{bad} records failed CRC (power cut mid-write?)", file=sys.stderr)


if __name__ == "__main__":
    main(sys.argv[1])
