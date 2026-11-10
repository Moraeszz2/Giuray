import json
import sys

data = json.load(open(sys.argv[1]))
cands = [(int(c["address"], 16), c["value"]) for c in data["candidates"]]
# Keep plausible world coordinates: not denormal, not huge.
cands = [(a, v) for a, v in cands if 1e-2 < abs(v) < 1e6]
print("plausible", len(cands))
# Group values whose addresses are within 16 bytes of each other.
groups = []
for a, v in sorted(cands):
    if groups and a - groups[-1][-1][0] <= 16:
        groups[-1].append((a, v))
    else:
        groups.append([(a, v)])
groups.sort(key=len, reverse=True)
for g in groups[: int(sys.argv[2]) if len(sys.argv) > 2 else 40]:
    print(" ".join(f"{a:#x}={v:.3f}" for a, v in g))
