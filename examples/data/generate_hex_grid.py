#!/usr/bin/env python3
"""
生成 NxNyNz 个 HEX8 单元的 Nastran BDF 文件，用于压力/性能测试。

用法:
    python generate_hex_grid.py --nx 50 --ny 50 --nz 50 -o hex_50x50x50.bdf

规模参考 (Nx, Ny, Nz → 节点数 / 单元数 / 文件大小):
    20×20×20  →   9,261 /     8,000 /  ~  600 KB
    50×50×50  → 132,651 /   125,000 /  ~  10 MB
    80×80×80  → 531,441 /   512,000 /  ~  40 MB
   100×100×100 → 1,030,301 / 1,000,000 / ~ 80 MB
"""

import argparse
import sys


def gen(nx, ny, nz, out_path, dx=1.0, dy=1.0, dz=1.0):
    nnx, nny, nnz = nx + 1, ny + 1, nz + 1
    n_nodes = nnx * nny * nnz
    n_elems = nx * ny * nz

    def nid(ix, iy, iz):
        return iz * nny * nnx + iy * nnx + ix + 1   # 1-based

    with open(out_path, "w", encoding="ascii") as f:
        f.write("$ Generated hex grid: {0}x{1}x{2} = {3} nodes / {4} HEX8 elements\n"
                .format(nx, ny, nz, n_nodes, n_elems))
        f.write("BEGIN BULK\n")
        f.write("MAT1    1       2.1E5           .3      7.85E-9\n")
        f.write("PSOLID  1       1\n")

        # GRID cards (small field: 8 chars per col)
        # GRID    id  cp  x       y       z
        for iz in range(nnz):
            z = iz * dz
            for iy in range(nny):
                y = iy * dy
                for ix in range(nnx):
                    x = ix * dx
                    i = nid(ix, iy, iz)
                    f.write("GRID    {:<8d}        {:<8.3f}{:<8.3f}{:<8.3f}\n"
                            .format(i, x, y, z))

        # CHEXA cards (8 nodes spread across 2 lines)
        # CHEXA   eid pid  n1  n2  n3  n4  n5  n6
        # +       n7  n8
        eid = 1
        for iz in range(nz):
            for iy in range(ny):
                for ix in range(nx):
                    # 标准 HEX8 顶点顺序：底面 (z) 逆时针 → 顶面 (z+1) 逆时针
                    n1 = nid(ix,     iy,     iz)
                    n2 = nid(ix + 1, iy,     iz)
                    n3 = nid(ix + 1, iy + 1, iz)
                    n4 = nid(ix,     iy + 1, iz)
                    n5 = nid(ix,     iy,     iz + 1)
                    n6 = nid(ix + 1, iy,     iz + 1)
                    n7 = nid(ix + 1, iy + 1, iz + 1)
                    n8 = nid(ix,     iy + 1, iz + 1)
                    f.write("CHEXA   {:<8d}{:<8d}{:<8d}{:<8d}{:<8d}{:<8d}{:<8d}{:<8d}\n"
                            .format(eid, 1, n1, n2, n3, n4, n5, n6))
                    f.write("+       {:<8d}{:<8d}\n".format(n7, n8))
                    eid += 1

        f.write("ENDDATA\n")

    return n_nodes, n_elems


def main():
    ap = argparse.ArgumentParser(description="Generate NxNyNz HEX8 grid BDF for stress testing.")
    ap.add_argument("--nx", type=int, default=50, help="elements along X (default 50)")
    ap.add_argument("--ny", type=int, default=50, help="elements along Y (default 50)")
    ap.add_argument("--nz", type=int, default=50, help="elements along Z (default 50)")
    ap.add_argument("-o", "--output", default=None,
                    help="output .bdf path (default hex_{NX}x{NY}x{NZ}.bdf)")
    args = ap.parse_args()

    out = args.output or "hex_{}x{}x{}.bdf".format(args.nx, args.ny, args.nz)
    print("Generating {}x{}x{} hex grid → {}".format(args.nx, args.ny, args.nz, out))
    n_nodes, n_elems = gen(args.nx, args.ny, args.nz, out)
    print("Wrote {} nodes, {} elements".format(n_nodes, n_elems))


if __name__ == "__main__":
    sys.exit(main())
