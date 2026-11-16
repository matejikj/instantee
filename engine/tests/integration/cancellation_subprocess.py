"""Exercise signal interruption and crash visibility using bounded children."""

import json
from pathlib import Path
import selectors
import signal
import subprocess
import sys
import tempfile
import unittest

CHILD, SUPERVISOR = sys.argv[1:3]
del sys.argv[1:]


class CancellationTests(unittest.TestCase):
    def start(self, command):
        child = subprocess.Popen(command, stdout=subprocess.PIPE,
                                 stderr=subprocess.PIPE, text=True)
        self.addCleanup(self.reap, child)
        with selectors.DefaultSelector() as selector:
            selector.register(child.stdout, selectors.EVENT_READ)
            self.assertTrue(selector.select(30), "child readiness deadline")
        self.assertEqual(child.stdout.readline().strip(), "READY")
        return child

    @staticmethod
    def reap(child):
        if child.poll() is None:
            child.kill()
        child.communicate(timeout=10)
        child.stdout.close()
        child.stderr.close()

    def target(self, root, exists):
        target = Path(root) / "target.mp4"
        if exists:
            target.write_bytes(b"previous output")
        return target

    def verify_target(self, target, exists, published):
        if published:
            self.assertEqual(target.read_bytes()[4:8], b"ftyp")
        elif exists:
            self.assertEqual(target.read_bytes(), b"previous output")
        else:
            self.assertFalse(target.exists())

    def test_cooperative_matrix(self):
        for mode in ("full", "empty", "credits", "callback", "pool", "gpu",
                     "writer", "finish", "published"):
            for signum in (signal.SIGINT, signal.SIGTERM):
                for exists in (False, True):
                    with self.subTest(mode=mode, signal=signum, existing=exists):
                        with tempfile.TemporaryDirectory(prefix="instantee-stop-") as root:
                            target = self.target(root, exists)
                            child = self.start([CHILD, mode, str(target)])
                            child.send_signal(signum)
                            out, _ = child.communicate(timeout=30)
                            self.assertEqual(child.returncode, 0)
                            report = json.loads(out.strip())
                            self.assertEqual(report, {"published": mode == "published",
                                                      "cancelled": True})
                            self.verify_target(target, exists, mode == "published")
                            self.assertEqual(list(Path(root).iterdir()),
                                             [target] if target.exists() else [])

    def test_kill_leaves_only_private_artifacts(self):
        for mode in ("full", "gpu", "writer", "published"):
            for exists in (False, True):
                with self.subTest(mode=mode, existing=exists):
                    with tempfile.TemporaryDirectory(prefix="instantee-kill-") as root:
                        target = self.target(root, exists)
                        child = self.start([CHILD, mode, str(target)])
                        child.kill()
                        child.communicate(timeout=10)
                        self.assertEqual(child.returncode, -signal.SIGKILL)
                        self.verify_target(target, exists, mode == "published")
                        for artifact in Path(root).iterdir():
                            if artifact != target:
                                self.assertTrue(artifact.is_dir())
                                self.assertEqual(artifact.stat().st_mode & 0o777, 0o700)
                                self.assertTrue((artifact / ".instantee-output-txn").is_file())
                                self.assertTrue({p.name for p in artifact.iterdir()} <=
                                                {".instantee-output-txn", "output.mp4"})

    def test_supervisor_kills_hung_native_call(self):
        with tempfile.TemporaryDirectory(prefix="instantee-watchdog-") as root:
            target = self.target(root, True)
            result = subprocess.run(
                [sys.executable, SUPERVISOR, "--timeout", "2", "--grace", "0.2",
                 "--", CHILD, "hung", str(target)], capture_output=True,
                text=True, timeout=15)
            self.assertEqual(result.returncode, 12)
            reports = [json.loads(line) for line in result.stderr.splitlines()
                       if line.startswith('{')]
            self.assertEqual(reports[-1]["code"], "watchdog_killed")
            self.assertIsNone(reports[-1]["published"])
            self.verify_target(target, True, False)

    def test_supervisor_forwards_interrupt(self):
        with tempfile.TemporaryDirectory(prefix="instantee-forward-") as root:
            target = self.target(root, False)
            supervisor = self.start(
                [sys.executable, SUPERVISOR, "--timeout", "30", "--grace", "5",
                 "--", CHILD, "full", str(target)])
            supervisor.send_signal(signal.SIGINT)
            out, _ = supervisor.communicate(timeout=10)
            self.assertEqual(supervisor.returncode, 0)
            self.assertFalse(json.loads(out)["published"])
            self.assertEqual(list(Path(root).iterdir()), [])

    def test_invalid_watchdog_duration(self):
        for duration in ("0", "-1", "nan", "inf"):
            with self.subTest(duration=duration):
                result = subprocess.run(
                    [sys.executable, SUPERVISOR, "--timeout", duration,
                     "--grace", "1", "--", CHILD], capture_output=True, timeout=5)
                self.assertEqual(result.returncode, 2)

    def test_supervisor_launch_failure(self):
        with tempfile.TemporaryDirectory(prefix="instantee-missing-child-") as root:
            result = subprocess.run(
                [sys.executable, SUPERVISOR, "--timeout", "1", "--grace", "1",
                 "--", str(Path(root) / "absent")], capture_output=True,
                text=True, timeout=5)
            self.assertEqual(result.returncode, 6)
            self.assertEqual(json.loads(result.stderr), {
                "code": "child_launch_failed", "phase": "supervisor",
                "published": False, "durability": "not_published"})


if __name__ == "__main__":
    unittest.main()
