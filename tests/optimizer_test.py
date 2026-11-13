"""Compile and execute both emission modes at both backend optimisation levels."""
import pathlib
import subprocess
import sys
import tempfile

fixture, compiler = sys.argv[1:]
with tempfile.TemporaryDirectory(prefix="jaqr-optimizer-") as directory:
    root = pathlib.Path(directory)
    for mode, arguments in (("optimized", []), ("plain", ["--no-optimize"])):
        source = root / (mode + ".cpp")
        source.write_text(subprocess.check_output([fixture, *arguments], text=True))
        for level in ("-O0", "-O2"):
            binary = root / (mode + level)
            subprocess.run([compiler, "-std=c++20", level, str(source), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
