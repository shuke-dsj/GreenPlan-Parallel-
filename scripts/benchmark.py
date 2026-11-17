"""Run repeated wall-clock experiments; write raw timing CSV, no invented speedup."""
import argparse
import csv
import io
import pathlib
import subprocess
import tempfile

p = argparse.ArgumentParser()
p.add_argument("binary")
p.add_argument("--output", default="results/timings.csv")
p.add_argument("--population", type=int, default=200)
p.add_argument("--generations", type=int, default=80)
p.add_argument("--months", type=int, default=60)
p.add_argument("--repeats", type=int, default=5)
a = p.parse_args()
rows = []
with tempfile.TemporaryDirectory() as td:
    for backend, threads in [("serial", 1), ("openmp", 1), ("openmp", 2), ("openmp", 4)]:
        for repeat in range(a.repeats):
            cmd = [str(pathlib.Path(a.binary).resolve()), "--backend", backend,
                   "--threads", str(threads), "--population", str(a.population),
                   "--generations", str(a.generations), "--months", str(a.months),
                   "--seed", str(42 + repeat), "--output", str(pathlib.Path(td) / "front.csv")]
            result = subprocess.run(cmd, check=True, capture_output=True, text=True)
            row = next(csv.DictReader(io.StringIO(result.stdout)))
            row["repeat"] = repeat
            rows.append(row)
out = pathlib.Path(a.output)
out.parent.mkdir(parents=True, exist_ok=True)
with out.open("w", newline="", encoding="utf-8") as f:
    writer = csv.DictWriter(f, fieldnames=rows[0].keys())
    writer.writeheader()
    writer.writerows(rows)
print(out)
