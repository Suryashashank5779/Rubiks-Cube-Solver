# Rubik's Cube Solver — IDA* with Pattern Databases

A self-contained C++ command-line application that solves a scrambled 3x3x3 Rubik's Cube using **IDA\* (Iterative Deepening A\*) search** guided by precomputed **pattern databases**. No external libraries — just the C++ standard library.

```
  +-----------------------------------------------------+
  |      RUBIK'S CUBE SOLVER  --  IDA* + Pattern DBs    |
  |  18 moves | Corner DB (85MB) | Edge-4 DB (0.18MB)   |
  +-----------------------------------------------------+
```

## Features

- Interactive console menu: random scramble, custom scramble, solve, display, reset, benchmark, exit.
- Full 18-move cube model (U, D, L, R, F, B and their CW / 180° / CCW variants).
- **Corner pattern database** — 8! × 3⁷ = 88,179,840 entries (~84 MB), tracking position + orientation of all 8 corners.
- **Two complementary edge pattern databases** — P(12,6) × 2⁶ = 42,577,920 entries each (~41 MB each), together covering all 12 edges.
- Admissible combined heuristic `h(n) = max(corner_h, edge6a_h, edge6b_h)` for a tight IDA* lower bound.
- Same-face / opposite-face canonical move pruning to shrink the search tree.
- Built-in benchmark mode (solve N random scrambles, report time / nodes / solution length).
- Zero dependencies — single `.cpp` file, plain `fread`-based database loading.

## Repository Contents

| File | Size | Purpose |
|---|---|---|
| `rubiks_solver_single.cpp` | ~24 KB | Complete solver source (cube model, pattern DBs, IDA*, CLI menu) |
| `corner_db_part1.bin` … `corner_db_part5.bin` | 84 MB total | Corner pattern database, split into 5 chunks |
| `edge6a_db.bin` | 41 MB | Edge pattern DB covering UF, UR, UB, UL, DF, DR |
| `edge6b_db.bin` | 41 MB | Edge pattern DB covering FR, FL, BL, BR, DB, DL |
| `solver.exe` | ~150 KB | Prebuilt Windows binary (optional — can be rebuilt from source) |

> **Note:** the `.bin` pattern database files must sit in the **same directory** as the executable at runtime. If they're missing, the program will rebuild them from scratch via BFS (several minutes) and save them back to disk.

## Build

Requires a C++14-capable compiler (GCC 6.3+, MinGW on Windows, or Clang/GCC on Linux/macOS).

```bash
g++ -O3 -std=c++14 -o solver rubiks_solver_single.cpp
```

## Run

```bash
# Windows
.\solver.exe

# Linux / macOS
./solver
```

On first launch the program loads the three pattern databases (~165 MB total) into memory, then presents an interactive menu:

```
+--------------------------------------+
|  1. Random scramble                  |
|  2. Custom scramble                  |
|  3. Solve current cube               |
|  4. Show current cube                |
|  5. Reset to solved                  |
|  6. Benchmark                        |
|  7. Exit                             |
+--------------------------------------+
```

| Option | Description |
|---|---|
| 1 | Apply N random moves (no two consecutive moves on the same face) |
| 2 | Apply a custom move sequence, e.g. `U R2 F' D B2 L'` |
| 3 | Run IDA* and print the optimal move sequence for the current cube |
| 4 | Print the ASCII net and solved/scrambled status |
| 5 | Reset the cube to solved |
| 6 | Run N random scrambles of a chosen depth and report aggregate stats |
| 7 | Exit |

### Example session

```
Choice: 1
Scramble moves (e.g. 15): 8
Scramble: R' F L' F' D' F2 R U

Choice: 3
IDA* searching (max 25 moves)...
Depth 8...  8 nodes, 0ms

Solution (8 moves):
1.U'  2.R'  3.F2  4.D  5.F  6.L  7.F'  8.R
Time: 0.087685ms   Nodes: 8
Check: CORRECT
```

## How It Works

1. **Cube model** — the cube is stored as `uint8_t s[6][9]` (6 faces × 9 stickers). Each face turn is a small hand-written sticker permutation.
2. **Pattern databases** — built once via breadth-first search backward from the solved state, recording the minimum number of moves needed to solve each corner/edge subset. Loaded from disk on every run after that.
3. **IDA\* search** — starting from `bound = h(root)`, the solver does depth-first search, expanding nodes only while `g(n) + h(n) ≤ bound`, raising the bound and retrying until the goal is found. Same-face and opposite-face move pruning reduces the effective branching factor from 18 to ~15.

## Performance

On the bundled pattern databases, 8–10 move scrambles typically solve in single-digit milliseconds; longer/harder scrambles (closer to the ~20-move "God's Number" diameter) can take several seconds as IDA* searches deeper bounds.

## License

This project is provided as-is for educational and personal use.
