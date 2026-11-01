"""Check a RAW-mode capture for the scope's readout artifact.

Real UART edges fall at any sample position. In the artifact, blocks of samples are misplaced, so many
edges land on the same few positions modulo 128. Random edges would put about 3 % on the top four.

    python3 edge_phases.py ../captures/02-rest-and-gpio20-esp-running-raw.json
"""
import json
import sys
from collections import Counter

capture = json.load(open(sys.argv[1]))
for channel, entry in capture.items():
    pre = entry["preamble"]
    yinc, yorig, yref = float(pre[7]), float(pre[8]), float(pre[9])
    logic = [1 if (b - yorig - yref) * yinc > 2.7 else 0 for b in entry["data"]]
    edges = [i for i in range(1, len(logic)) if logic[i] != logic[i - 1]]
    top = Counter(i % 128 for i in edges).most_common(4)
    share = sum(n for _, n in top) / len(edges)
    print(f"{channel}: {len(edges)} edges, top phases mod 128 {top}: {100 * share:.0f} % of edges")
