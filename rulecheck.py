"""rulecheck.py <repo> <base-rev> [--all] [--tests]: CLAUDE.md rules a script can see, on lines changed since <base-rev>.
--tests also checks octtest.cpp and the test sources next to it.

Checks C++ under native/di1compat (not octtest.cpp, minhook, third_party, ports10.hpp, renderdoc_app.h):
N0 comment over 20 words (a run of consecutive // lines is one comment), T6 line over 120 chars,
N7 function over 60 lines, N15 #include below code, N24 TODO without an audit ID, N26 #define,
N21 sprintf_s/strcpy_s, N19 new/delete, M4 header comment over 6 lines.
Without --all only findings touching changed lines are listed. Exit 1 when anything is found.
"""
import re
import subprocess
import sys
from pathlib import Path

SKIP = ("octtest.cpp", "ports10.hpp", "renderdoc_app.h")
SKIP_DIRS = ("minhook", "third_party")


def git(repo, *args):
    return subprocess.run(["git", "-C", repo, *args], capture_output=True, text=True, encoding="utf-8",
                          errors="replace", check=True).stdout


def changed_lines(repo, base):
    out, cur, res = git(repo, "diff", "-U0", base, "--", "native/di1compat"), None, {}
    for line in out.splitlines():
        if line.startswith("+++ "):
            cur = line[6:] if line.startswith("+++ b/") else None
        elif line.startswith("@@") and cur:
            m = re.search(r"\+(\d+)(?:,(\d+))?", line)
            start, n = int(m.group(1)), int(m.group(2) or 1)
            res.setdefault(cur, set()).update(range(start, start + n))
    for rel in git(repo, "ls-files", "--others", "--exclude-standard", "--", "native/di1compat").splitlines():
        res[rel] = set(range(1, 1 << 20))   # untracked: every line is new
    return res


def wanted(rel):
    p = Path(rel)
    skip = SKIP[1:] if "--tests" in sys.argv else SKIP
    return p.suffix in (".hpp", ".cpp", ".h") and p.name not in skip and not any(d in p.parts for d in SKIP_DIRS)


def comments(lines):
    """Yields (first line, last line, words) per comment; consecutive whole-line // comments are one."""
    i = 0
    while i < len(lines):
        s = lines[i].strip()
        if s.startswith("//"):
            j = i
            while j + 1 < len(lines) and lines[j + 1].strip().startswith("//"):
                j += 1
            text = " ".join(lines[k].strip()[2:] for k in range(i, j + 1))
            yield i + 1, j + 1, len(text.split())
            i = j + 1
            continue
        m = re.search(r"//(.*)$", lines[i])
        if m and lines[i].count('"', 0, m.start()) % 2 == 0:
            yield i + 1, i + 1, len(m.group(1).split())
        i += 1


def functions(lines):
    """Yields (first line, last line) of brace bodies opened at namespace level by a function header."""
    depth, start, ns = 0, None, []
    for i, raw in enumerate(lines):
        s = re.sub(r'"([^"\\]|\\.)*"|\'([^\'\\]|\\.)*\'|//.*$', "", raw)
        for ch in s:
            if ch == "{":
                if start is None and depth == len(ns):
                    head = " ".join(lines[max(0, i - 3):i + 1])
                    if re.search(r"\bnamespace\b|\b(struct|class|enum|union)\b[^;(]*$|=\s*\{", head.split(";")[-1]):
                        ns.append(depth)
                    elif ")" in head.split(";")[-1]:
                        start = i
                depth += 1
            elif ch == "}":
                depth -= 1
                if ns and depth == ns[-1] and start is None:
                    ns.pop()
                elif start is not None and depth == len(ns):
                    yield start + 1, i + 1
                    start = None


def check_file(path, rel, touched, out):
    lines = path.read_text(encoding="utf-8", errors="replace").splitlines()

    def hit(a, b, rule, msg):
        if touched is None or any(n in touched for n in range(a, b + 1)):
            out.append(f"{rel}:{a}: {rule} {msg}")

    for a, b, words in comments(lines):
        if words > 20:
            hit(a, b, "N0", f"comment of {words} words")
    code_seen = False
    for n, raw in enumerate(lines, 1):
        s = raw.strip()
        if len(raw) > 120:
            hit(n, n, "T6", f"line of {len(raw)} chars")
        if s.startswith("#include") and code_seen:
            hit(n, n, "N15", "#include below code")
        if s and not s.startswith(("#", "//", "/*", "*")):
            code_seen = True
        if re.search(r"\bTODO\b(?!\([A-Z]+-\w+\))", s):
            hit(n, n, "N24", "TODO without an audit ID")
        if s.startswith("#define"):
            hit(n, n, "N26", "macro")
        code = re.sub(r'"([^"\\]|\\.)*"|//.*$', '""', s)   # string literals and comments are not code
        if re.search(r"\b(sprintf_s|strcpy_s)\s*\(", code):
            hit(n, n, "N21", "sprintf_s/strcpy_s")
        if re.search(r"(?<![\w.>])new\s+[A-Za-z_]|\bdelete\s*(\[\])?\s*[A-Za-z_(]", code):
            hit(n, n, "N19", "new/delete")
    for a, b in functions(lines):
        if b - a + 1 > 60:
            hit(a, b, "N7", f"function of {b - a + 1} lines")
    if path.suffix == ".hpp":
        head = 0
        for raw in lines:
            if raw.strip().startswith("//"):
                head += 1
            elif raw.strip() and not raw.startswith("#pragma"):
                break
        if head > 6:
            hit(1, head, "M4", f"header comment of {head} lines")


def main():
    if len(sys.argv) < 3 or "--help" in sys.argv:
        print(__doc__)
        return 0
    repo, base, every = sys.argv[1], sys.argv[2], "--all" in sys.argv
    touched = changed_lines(repo, base)
    out = []
    for rel in sorted(touched):
        path = Path(repo) / rel
        if wanted(rel) and path.exists():
            check_file(path, rel, None if every else touched[rel], out)
    print("\n".join(out))
    counts = {}
    for line in out:
        counts[line.split(": ")[1].split()[0]] = counts.get(line.split(": ")[1].split()[0], 0) + 1
    print(f"{len(out)} findings " + " ".join(f"{k}={v}" for k, v in sorted(counts.items())))
    return 1 if out else 0


if __name__ == "__main__":
    sys.exit(main())
