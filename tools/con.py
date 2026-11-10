#!/usr/bin/env python3
"""CON script interpreter (BF1942). Stdlib only. Spec: docs/formats/con.md

Usage: con.py <BF1942_DIR> <mod> [level] [gamemode]
  Loads the mod (init.con, game init, all object folders) and optionally a level,
  then prints a summary. Example: con.py <BF1942_DIR> bf1942 Wake conquest
"""
import collections
import glob
import os
import posixpath
import sys

from rfa import read_file, read_index

# Files the engine loads from every objects/** folder by itself (no script runs them).
# shortcut: order is a guess (geometry before templates), verify once something depends on it.
OBJECT_FILES = ("geometries.con", "network.con", "physics.con", "weapons.con", "effects.con", "objects.con", "object.con")


class Vfs:
    """Lower-cased path -> bytes, or a pointer into an archive read on demand."""

    def __init__(self, files=None):
        self.files = dict(files or {})

    def mount(self, mod_dir):
        """Later mounts override earlier ones; within a mod, patches override base archives."""
        # sorted(): "x.rfa" sorts before "x_001.rfa", so patches mount after their base.
        for arc in sorted(glob.glob(os.path.join(mod_dir, "Archives", "**", "*.rfa"), recursive=True), key=str.lower):
            with open(arc, "rb") as f:
                compressed, entries = read_index(f)
            for name, stored, size, off in entries:
                self.files[name.replace("\\", "/").lower()] = (arc, compressed, stored, size, off)
        # shortcut: loose files are assumed to beat archives, unverified.
        for root, dirs, names in os.walk(mod_dir):
            dirs[:] = [d for d in dirs if d.lower() != "archives"]
            for n in names:
                if n.lower().endswith((".con", ".inc")):
                    rel = os.path.relpath(os.path.join(root, n), mod_dir).replace(os.sep, "/").lower()
                    with open(os.path.join(root, n), "rb") as f:
                        self.files[rel] = f.read()

    def read(self, path):
        src = self.files[path]
        if isinstance(src, bytes):
            return src
        arc, compressed, stored, size, off = src
        with open(arc, "rb") as f:
            f.seek(off)
            return read_file(f.read(max(stored, size)), compressed, size, 0)


class Con:
    def __init__(self, vfs):
        self.vfs = vfs
        self.templates = collections.defaultdict(dict)  # subsystem -> lower name -> {type, name, props}
        self.instances = []  # Object.create results: {template, props}
        self.active = {}  # subsystem -> object currently being edited
        self.settings = collections.defaultdict(list)  # commands with no active object
        self.unknown = collections.Counter()
        self.missing = collections.Counter()
        self.mod_paths = []
        self.args = []  # arguments of the script running now
        self.dir = ""  # folder of the script running now: templates record it (relative paths such as loadSoundScript)
        self.level_dir = ""

    def resolve(self, target, cur_dir):
        # Verified on retail data: script folder, then level folder, then mod root.
        t = target.replace("\\", "/").lower()
        for base in (cur_dir, self.level_dir, ""):
            p = posixpath.normpath(posixpath.join(base, t))
            for c in (p, p + ".con"):
                if c in self.vfs.files:
                    return c
        return None

    def run(self, target, args=(), cur_dir=""):
        path = self.resolve(target, cur_dir)
        if path is None:
            self.missing[target.lower()] += 1  # retail scripts reference optional/missing files
            return
        # A run without arguments keeps the caller's (unverified): Conquest/ControlPoints.con is run bare from the game
        # mode script and checks "if v_arg1 == host" again; control points exist in the retail game.
        use = list(args) if args else self.args
        outer, self.args = self.args, use
        try:
            self.execute(self.vfs.read(path).decode("latin-1"), {f"v_arg{i + 1}": a for i, a in enumerate(use)}, posixpath.dirname(path))
        finally:
            self.args = outer

    def execute(self, text, scope, cur_dir=""):
        in_rem = False
        conds = []  # per open if: True taking, False skipping, None parent inactive
        for raw in text.splitlines():
            tok = raw.split()
            if not tok:
                continue
            kw = tok[0].lower()
            if in_rem:
                in_rem = kw != "endrem"
                continue
            if kw == "beginrem":
                in_rem = True
                continue
            if kw.startswith("rem"):  # also "rem****" and commented-out "remObject.x"
                continue
            if kw == "if":
                conds.append(self.test(tok[1:], scope) if all(conds) else None)
                continue
            if kw == "else":
                if conds and conds[-1] is not None:
                    conds[-1] = not conds[-1]
                continue
            if kw == "endif":
                if conds:
                    conds.pop()
                continue
            if not all(conds):
                continue
            dest = None
            if "->" in tok:  # split before substitution: the target is a name, not a value
                i = tok.index("->")
                dest = tok[i + 1].lower() if i + 1 < len(tok) else None
                tok = tok[:i]
            tok = [scope.get(t.lower(), t) for t in tok]
            result = ""
            if kw == "var":
                for v in tok[1:]:
                    scope.setdefault(v.lower(), "")
            elif kw == "echo":
                result = " ".join(tok[1:])
            elif kw in ("run", "include") and len(tok) > 1:
                self.run(tok[1], tok[2:], cur_dir)
            elif "." in kw:
                self.dir = cur_dir
                self.command(kw, tok[1:])
            else:
                self.unknown[kw] += 1
            if dest:
                scope[dest] = result

    def test(self, tok, scope):
        tok = [scope.get(t.lower(), t).lower() for t in tok]
        if len(tok) == 3 and tok[1] in ("==", "!="):
            return (tok[0] == tok[2]) == (tok[1] == "==")
        self.unknown["if " + " ".join(tok)] += 1
        return False

    def command(self, name, args):
        sub, _, method = name.partition(".")
        if (sub, method) == ("game", "addmodpath"):
            self.mod_paths.append(args[0] if args else "")
        elif method == "create":
            if sub == "object":
                obj = {"template": args[0] if args else "", "props": []}
                self.instances.append(obj)
            else:
                typ, nm = (args[0], args[1]) if len(args) >= 2 else ("", args[0] if args else "")
                obj = {"type": typ, "name": nm, "props": [], "dir": self.dir}
                self.templates[sub][nm.lower()] = obj
            self.active[sub] = obj
        elif method == "active":
            self.active[sub] = self.templates[sub].get(args[0].lower()) if args else None
        elif sub == "object" and method == "delete" and self.active.get(sub) in self.instances:
            self.instances.remove(self.active.pop(sub))
        elif self.active.get(sub) is not None:
            self.active[sub]["props"].append((method, args))
        else:
            self.settings[sub].append((method, args))

    def object_folders(self):
        dirs = collections.defaultdict(set)
        for p in self.vfs.files:
            d, f = posixpath.split(p)
            if p.startswith("objects/") and f in OBJECT_FILES:
                dirs[d].add(f)
        for d in sorted(dirs):
            for f in OBJECT_FILES:
                if f in dirs[d]:
                    self.run(f"{d}/{f}")

    def undefined_instances(self):
        known = self.templates["objecttemplate"]
        return collections.Counter(i["template"].lower() for i in self.instances if i["template"].lower() not in known)


def load_mod(game_dir, mod):
    """Mount the mod stack declared by Mods/<mod>/init.con and load game init + object templates."""
    vm = Con(Vfs())
    with open(os.path.join(game_dir, "Mods", mod, "init.con"), "rb") as f:
        vm.execute(f.read().decode("latin-1"), {})
    for mp in reversed(vm.mod_paths or [f"Mods/{mod}/"]):  # first addModPath has highest priority
        vm.vfs.mount(os.path.join(game_dir, *mp.strip("/").split("/")))
    vm.run("bf1942/game/init.con")
    vm.object_folders()
    return vm


def load_level(vm, level, mode="conquest"):
    # shortcut: engine-side level sequence is assumed (Init, StaticObjects, game mode as host), unverified.
    vm.level_dir = f"bf1942/levels/{level}".lower()
    d = vm.level_dir  # full paths: a bare "init.con" would resolve to the mod's own init.con
    vm.run(f"{d}/init.con")
    vm.run(f"{d}/staticobjects.con")
    vm.run(f"{d}/gametypes/{mode}.con" if vm.resolve(f"{d}/gametypes/{mode}", "") else f"{d}/{mode}.con", ["host"])


def main(argv):
    if len(argv) < 3:
        sys.exit(__doc__)
    vm = load_mod(argv[1], argv[2])
    if len(argv) > 3:
        load_level(vm, argv[3], *(argv[4:5]))
    print("mod paths:", vm.mod_paths)
    print("templates:", {k: len(v) for k, v in sorted(vm.templates.items(), key=lambda kv: -len(kv[1]))[:10]})
    print("placed objects:", len(vm.instances))
    und = vm.undefined_instances()
    print("placed objects with undefined template:", sum(und.values()), und.most_common(10))
    print("missing scripts:", sum(vm.missing.values()), vm.missing.most_common(5))
    print("unknown lines:", sum(vm.unknown.values()), vm.unknown.most_common(5))


if __name__ == "__main__":
    main(sys.argv)
