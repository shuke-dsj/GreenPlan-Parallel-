"""Backend parity, reproducibility, feasibility proxies, invalid-input checks."""
import argparse
import csv
import pathlib
import subprocess
import tempfile

p = argparse.ArgumentParser()
p.add_argument("binary")
p.add_argument("--backend", default="openmp", choices=["openmp", "ppl", "mpi"])
args = p.parse_args()
binary = str(pathlib.Path(args.binary).resolve())
with tempfile.TemporaryDirectory() as td:
    root = pathlib.Path(td)
    def run(name, backend, prefix=()):
        output = root / (name + ".csv")
        subprocess.run([*prefix, binary, "--backend", backend, "--population", "19",
                        "--generations", "7", "--months", "3", "--seed", "73",
                        "--output", str(output)], check=True, capture_output=True)
        return output.read_bytes()
    reference = run("serial", "serial")
    assert reference == run("repeat", "serial"), "seed reproducibility failed"
    prefix = ["mpiexec", "-n", "3"] if args.backend == "mpi" else []
    assert reference == run("parallel", args.backend, prefix), "backend parity failed"
    rows = list(csv.DictReader(reference.decode().splitlines()))
    assert rows
    for row in rows:
        assert 0 <= float(row["utilization"]) <= 1 + 1e-10
        assert float(row["carbon"]) >= 0
    invalid = subprocess.run([binary, "--population", "0"], capture_output=True)
    assert invalid.returncode != 0
print("PASS: reproducibility, backend parity (odd population), bounds, invalid input")
