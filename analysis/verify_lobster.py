"""Rebuild the book by replaying a LOBSTER message file and check it against
the order book file. A consistency test for the simulator's exporter."""
import sys
import numpy as np
import pandas as pd

msg_path, ob_path = sys.argv[1], sys.argv[2]
msg = pd.read_csv(msg_path, header=None, usecols=range(6),
                  names=["time", "type", "id", "size", "price", "dir"])
ob = pd.read_csv(ob_path, header=None).to_numpy()
levels = ob.shape[1] // 4

orders = {}                      # id -> [price, size, dir]
book = {1: {}, -1: {}}           # dir -> {price: qty}

def add(side, price, q):
    d = book[side]; d[price] = d.get(price, 0) + q
    if d[price] == 0: del d[price]

mismatches, checked = 0, 0
for i, (t, typ, oid, size, price, d) in enumerate(msg.itertuples(index=False)):
    if typ == 1:
        orders[oid] = [price, size, d]; add(d, price, size)
    elif typ in (2, 3, 4, 5):
        if oid in orders:
            p, s, dd = orders[oid]
            add(dd, p, -size)
            orders[oid][1] -= size
            if orders[oid][1] == 0: del orders[oid]
    if i % 25 == 0:
        checked += 1
        asks = sorted(book[-1].items())[:levels]
        bids = sorted(book[1].items(), reverse=True)[:levels]
        row = ob[i]
        for l in range(levels):
            ap, asz = asks[l] if l < len(asks) else (9999999999, 0)
            bp, bsz = bids[l] if l < len(bids) else (-9999999999, 0)
            if (row[4*l], row[4*l+1], row[4*l+2], row[4*l+3]) != (ap, asz, bp, bsz):
                mismatches += 1
                if mismatches <= 3:
                    print(f"row {i} level {l+1}: file {tuple(row[4*l:4*l+4])} rebuilt {(ap, asz, bp, bsz)}")
                break
print(f"checked {checked} rows, {mismatches} mismatches")
