#!/usr/bin/env python3
# genspec.py <device.manifest.json> <out.spec>: the kernel-argument layouts of
# AMReX LOOM's compute_dSdt kernels, for cns_loom --amrex-kernels. Each pointer
# binding is named after the Array4 whose fields follow it; a trailing binding
# is the Parm pointer.
import json, re, sys
roles = {"0": "ctoprim", "3": "slope_x", "4": "riemann_x", "6": "slope_y",
         "7": "riemann_y", "9": "slope_z", "10": "riemann_z", "12": "flux_to_dudt"}
m = json.load(open(sys.argv[1]))
out = []
for f in m["functions"]:
    n = f["name"]
    if "compute_dSdt" not in n: continue
    lam = re.search(r"E\d?\$_(\d+)", n).group(1)
    if lam not in roles: continue
    it = f["interface"]; ps = it["parameters"]
    out.append(f"kernel {roles[lam]} {it['argument_byte_length']} {n}")
    for i, p in enumerate(ps):
        field = p["name"]
        if p["kind"] == "binding":
            nxt = ps[i + 1]["name"] if i + 1 < len(ps) else ""
            cap = re.match(r"tag_f_f_(\w+?)_(stride_a|stride_n|begin_vect|end_vect)", nxt)
            field = "ptr:" + (cap.group(1) if cap else "lparm")
        out.append(f"leaf {p['byte_offset']} {p['byte_length']} {field}")
open(sys.argv[2], "w").write("\n".join(out) + "\n")
print(f"{sum(1 for l in out if l.startswith('kernel'))} kernels")
