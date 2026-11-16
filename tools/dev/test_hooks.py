import subprocess
import tempfile
import unittest
from pathlib import Path

HOOK = Path(__file__).resolve().parent / "hooks" / "commit-msg"


def run_hook(message: str) -> str:
    with tempfile.TemporaryDirectory() as tmp:
        path = Path(tmp) / "COMMIT_EDITMSG"
        path.write_text(message, encoding="utf-8")
        subprocess.run(("sh", str(HOOK), str(path)), check=True)
        return path.read_text(encoding="utf-8")


class CommitMsgHookTest(unittest.TestCase):
    def test_removes_tool_attribution_and_trailing_blank_lines(self):
        message = (
            "feat: add thing\n\nBody line.\n\n"
            "Co-authored-by: Cursor <cursoragent@cursor.com>\n"
            "Made with Cursor\n\n"
        )
        self.assertEqual(run_hook(message), "feat: add thing\n\nBody line.\n")

    def test_keeps_human_trailers_and_inner_blank_lines(self):
        message = (
            "fix: keep\n\nFirst paragraph.\n\nSecond paragraph.\n\n"
            "Co-authored-by: Reviewer <reviewer@example.invalid>\n"
        )
        self.assertEqual(run_hook(message), message)

    def test_hook_is_executable(self):
        self.assertTrue(HOOK.stat().st_mode & 0o111)


if __name__ == "__main__":
    unittest.main()
