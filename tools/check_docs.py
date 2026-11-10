#!/usr/bin/env python3
"""Documentation checker. Usage: check_docs.py [repo_root]  (exit 1 on problems)

Flags: broken relative links in .md, repo paths in backticks that do not exist,
personal data (drive paths, user home dirs, WSL mounts, IPv4, private keys) in .md/.py,
PROJECT_MEMORY stages with more than 3 bullets.
"""
import os
import re
import sys

SKIP_DIRS = {".git", "__pycache__", "out"}
SELF_TESTS = {"test_check_docs.py"}  # holds fake personal data on purpose
LINK = re.compile(r"\[[^\]]*\]\(([^)\s]+)\)")
TICK = re.compile(r"`([^`\s]+)`")
PERSONAL = [
    (re.compile(r"\b[A-Za-z]:\\"), "drive path"),
    (re.compile(r"/(home|mnt)/\w"), "home/mnt path"),
    (re.compile(r"\b\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3}\b"), "IP address"),
    (re.compile(r"-----BEGIN [A-Z ]*KEY"), "private key"),
]


def _files(root):
    for dirpath, dirs, names in os.walk(root):
        dirs[:] = [d for d in dirs if d not in SKIP_DIRS]
        for n in names:
            if n.endswith((".md", ".py")) and n not in SELF_TESTS:
                yield os.path.join(dirpath, n)


def check(root):
    problems = []
    top = set(os.listdir(root))
    for path in _files(root):
        rel = os.path.relpath(path, root)
        with open(path, encoding="utf-8") as f:
            text = f.read()
        for rx, what in PERSONAL:
            if rx.search(text):
                problems.append(f"{rel}: personal data ({what})")
        if not path.endswith(".md"):
            continue
        for target in LINK.findall(text):
            if re.match(r"[a-z]+:|#", target):
                continue
            t = os.path.join(os.path.dirname(path), target.split("#")[0])
            if not os.path.exists(t):
                problems.append(f"{rel}: broken link {target}")
        for token in TICK.findall(text):
            # Only tokens that start at a real top-level entry are repo paths.
            if "<" in token or "*" in token or token.split("/")[0] not in top or "/" not in token:
                continue
            if not os.path.exists(os.path.join(root, token.rstrip("/"))):
                problems.append(f"{rel}: missing path {token}")
        if rel.endswith("PROJECT_MEMORY.md"):
            for stage in re.split(r"^## ", text, flags=re.M)[1:]:
                if len(re.findall(r"^- ", stage, flags=re.M)) > 3:
                    problems.append(f"{rel}: stage '{stage.splitlines()[0]}' has more than 3 bullets")
    return problems


if __name__ == "__main__":
    found = check(sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), ".."))
    print("\n".join(found) or "docs ok")
    sys.exit(1 if found else 0)
