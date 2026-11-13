"""End-to-end tests: JAQR source in, program behaviour out.

`jaqr file.jaqr` compiles the program (through generated C++) and runs it. These
tests call the real jaqr executable on every case under tests/e2e/cases/:

  run/<name>.jaqr     must compile and run, exiting 0. Its stdout must equal
                      <name>.out exactly. Optional <name>.in is piped to stdin.
  reject/<name>.jaqr  must be rejected. The first line names the expected
                      diagnostic code, e.g. `# expect: SEM017`. jaqr must exit
                      non-zero without crashing, report that code on stderr, and
                      print nothing on stdout (the program never runs).

Usage (after `just build`):
  python tests/e2e/run_e2e.py                 # every case
  python tests/e2e/run_e2e.py -k step         # only cases whose name contains "step"
  python tests/e2e/run_e2e.py --cxx clang++   # C++ compiler jaqr uses (sets JAQR_CXX)
  python tests/e2e/run_e2e.py --jaqr path/to/jaqr
"""
import argparse
import concurrent.futures
import dataclasses
import os
import pathlib
import re
import shutil
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parent.parent
CASES = HERE / "cases"
EXPECT_RE = re.compile(r"^#\s*expect:\s*((?:LEX|SYN|SEM)\d{3})\s*$")
TIMEOUT_SECONDS = 60
# jaqr compiles to a temporary program.cpp; compiler errors mention that file
GENERATED_CPP_RE = re.compile(r"program\.cpp")


@dataclasses.dataclass
class Case:
    kind: str  # "run" or "reject"
    source: pathlib.Path

    @property
    def name(self):
        return f"{self.kind}/{self.source.stem}"

    def sidecar(self, suffix):
        path = self.source.with_suffix(suffix)
        return path.read_text(encoding="utf-8") if path.exists() else None


class Failure(Exception):
    pass


def normalise(text):
    """Compare output independent of the platform's newline convention."""
    return text.replace("\r\n", "\n")


def crashed(result):
    """A diagnostic must be a clean exit, not a signal or an access violation."""
    return result.returncode < 0 or result.returncode > 255


def run_jaqr(args, case, stdin):
    command = [args.jaqr, str(case.source)]
    try:
        return subprocess.run(command, input=stdin, capture_output=True, text=True,
                              timeout=TIMEOUT_SECONDS, env=args.env)
    except subprocess.TimeoutExpired as error:
        raise Failure(f"timed out after {TIMEOUT_SECONDS}s") from error


def compiler_dir(compiler):
    """Directory of a compiler given as a path or a bare name on PATH."""
    found = shutil.which(compiler)
    return str(pathlib.Path(found).resolve().parent) if found else None


def build_compiler(jaqr):
    """The C++ compiler CMake built jaqr with, read from the CMakeCache.txt beside it."""
    cache = pathlib.Path(jaqr).resolve().parent / "CMakeCache.txt"
    if cache.is_file():
        match = re.search(r"^CMAKE_CXX_COMPILER:\w+=(.+)$", cache.read_text(encoding="utf-8"), re.MULTILINE)
        if match:
            return match.group(1).strip()
    return None


def test_environment(args):
    env = dict(os.environ)
    if args.cxx:
        # a full path: jaqr's own PATH search does not add .exe on Windows
        env["JAQR_CXX"] = shutil.which(args.cxx) or args.cxx
    if os.name == "nt":
        # MinGW builds of jaqr, and the programs jaqr builds, load libstdc++/libc++ DLLs from
        # PATH. Another toolchain's copy earlier on PATH (Git Bash ships one) makes them exit
        # with 0xC0000139 before main, so put the matching toolchains first.
        directories = [compiler_dir(c) for c in (env.get("JAQR_CXX"), build_compiler(args.jaqr)) if c]
        env["PATH"] = os.pathsep.join([d for d in directories if d] + [env.get("PATH", "")])
    return env


def check_run(args, case):
    expected = case.sidecar(".out")
    if expected is None:
        raise Failure(f"missing expected output file {case.source.with_suffix('.out').name}")
    result = run_jaqr(args, case, case.sidecar(".in") or "")
    if result.returncode != 0:
        if GENERATED_CPP_RE.search(result.stderr):
            stage = "generated C++ does not compile"
        elif re.search(r"\b(LEX|SYN|SEM)\d{3}\b|^Error:", result.stderr, re.MULTILINE):
            stage = "jaqr rejected a valid program"
        else:
            stage = "program failed"
        raise Failure(f"{stage} (exit {result.returncode}):\n{result.stderr.strip()}\n"
                      f"--- stdout so far\n{result.stdout!r}")
    actual = normalise(result.stdout)
    if actual != normalise(expected):
        raise Failure(f"output mismatch\n--- expected\n{expected!r}\n--- actual\n{actual!r}")


def check_reject(args, case):
    first_line = case.source.read_text(encoding="utf-8").splitlines()[0]
    match = EXPECT_RE.match(first_line)
    if not match:
        raise Failure("first line must be `# expect: <CODE>`, e.g. `# expect: SEM017`")
    code = match.group(1)
    result = run_jaqr(args, case, "")
    if crashed(result):
        raise Failure(f"jaqr crashed (exit {result.returncode}) instead of reporting {code}:\n{result.stderr}")
    if result.returncode == 0:
        raise Failure(f"jaqr accepted and ran a program that should fail with {code}")
    if result.stdout.strip():
        raise Failure(f"the program produced output although it should be rejected:\n{result.stdout[:500]}")
    if GENERATED_CPP_RE.search(result.stderr):
        raise Failure(f"jaqr let an invalid program through to the C++ compiler instead of reporting {code}:\n"
                      f"{result.stderr.strip()[:1500]}")
    if code not in result.stderr:
        raise Failure(f"expected diagnostic {code}, got:\n{result.stderr.strip() or '<empty stderr>'}")


def discover(name_filter):
    cases = []
    for kind in ("run", "reject"):
        for source in sorted((CASES / kind).glob("*.jaqr")):
            case = Case(kind, source)
            if name_filter is None or name_filter in case.name:
                cases.append(case)
    return cases


def check(args, case):
    try:
        (check_run if case.kind == "run" else check_reject)(args, case)
        return case, None
    except Failure as failure:
        return case, str(failure)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    default_jaqr = REPO / "build" / "debug" / ("jaqr.exe" if os.name == "nt" else "jaqr")
    parser.add_argument("--jaqr", default=str(default_jaqr), help="jaqr executable (default: the debug build)")
    parser.add_argument("--cxx", help="C++20 compiler for jaqr to use (default: JAQR_CXX, else jaqr's own search)")
    parser.add_argument("-k", dest="name_filter", help="only run cases whose name contains this text")
    parser.add_argument("-j", "--jobs", type=int, default=os.cpu_count() or 1)
    args = parser.parse_args()

    if not pathlib.Path(args.jaqr).is_file():
        print(f"jaqr executable not found at {args.jaqr}; run `just build` or pass --jaqr", file=sys.stderr)
        return 1
    args.env = test_environment(args)

    cases = discover(args.name_filter)
    if not cases:
        print("no cases matched", file=sys.stderr)
        return 1

    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        results = list(pool.map(lambda case: check(args, case), cases))

    failures = [(case, message) for case, message in results if message]
    for case, message in results:
        print(f"{'FAIL' if message else 'ok  '} {case.name}")
    for case, message in failures:
        print(f"\n=== FAIL {case.name} ({case.source.relative_to(REPO)})\n{message}")
    print(f"\n{len(results) - len(failures)}/{len(results)} passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
