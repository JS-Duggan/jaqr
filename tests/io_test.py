"""Exercise generated I/O at both optimisation levels with piped standard input."""
import pathlib
import subprocess
import sys
import tempfile

fixture, compiler, runtime = sys.argv[1:]
with tempfile.TemporaryDirectory(prefix="jaqr-io-") as directory:
    root = pathlib.Path(directory)
    for mode, arguments in (("optimized", []), ("plain", ["--no-optimize"])):
        source = root / (mode + ".cpp")
        source.write_text(subprocess.check_output([fixture, *arguments], text=True))
        for level in ("-O0", "-O2"):
            binary = root / (mode + level)
            subprocess.run([compiler, "-std=c++20", level, "-I", runtime, str(source), "-o", str(binary)], check=True)
            result = subprocess.run([str(binary)], input="12 3.5 Z 1 8 9 invalid 25\n", text=True, capture_output=True, check=True)
            assert result.stdout == "values:1=12;1=3.5;1=Z;1=1;42\n1=8;1=9;6\n0;0\n", result.stdout
            # EOF must also report failed extraction, with the stream staying failed.
            result = subprocess.run([str(binary)], input="12 3.5 Z 1 8 9", text=True, capture_output=True, check=True)
            assert result.stdout == "values:1=12;1=3.5;1=Z;1=1;42\n1=8;1=9;6\n0;0\n", result.stdout
