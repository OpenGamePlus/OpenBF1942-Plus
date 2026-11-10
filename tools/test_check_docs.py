import os
import tempfile
import unittest

from check_docs import check


def make_repo(files):
    d = tempfile.mkdtemp()
    for path, text in files.items():
        full = os.path.join(d, path)
        os.makedirs(os.path.dirname(full), exist_ok=True)
        with open(full, "w", encoding="utf-8") as f:
            f.write(text)
    return d


class CheckDocsTest(unittest.TestCase):
    def test_clean_repo_passes(self):
        d = make_repo({
            "README.md": "See [docs](docs/a.md) and `tools/x.py`, data at `<BF1942_DIR>/Mods`.",
            "docs/a.md": "[back](../README.md) [web](https://example.com) [anchor](#top)",
            "tools/x.py": "",
        })
        self.assertEqual(check(d), [])

    def test_broken_link(self):
        d = make_repo({"README.md": "[x](docs/missing.md)"})
        self.assertIn("broken link", check(d)[0])

    def test_missing_backtick_path(self):
        d = make_repo({"README.md": "`tools/gone.py`", "tools/x.py": ""})
        self.assertIn("missing path", check(d)[0])

    def test_personal_data(self):
        for text in ["C:\\Games\\X", "/home/someone/x", "/mnt/c/x", "host 192.0.2.1", "-----BEGIN PRIVATE KEY"]:
            d = make_repo({"README.md": text})
            self.assertTrue(check(d), text)

    def test_memory_stage_bullets(self):
        mem = "# M\n\n## 2026-01-01 — x\n- a\n- b\n- c\n- d\n"
        d = make_repo({"docs/ai/PROJECT_MEMORY.md": mem})
        self.assertIn("more than 3 bullets", check(d)[0])


if __name__ == "__main__":
    unittest.main()
