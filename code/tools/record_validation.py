"""Record real make output for the report; terminate only the QEMU group we start."""
import os
import signal
import subprocess
from pathlib import Path

root = Path(__file__).resolve().parent.parent
logs = root.parent / "report" / "validation"
logs.mkdir(parents=True, exist_ok=True)
for name, command in [("build.log", ["make", "-B", "-j2"]),
                      ("qemu.log", ["make", "qemu"]),
                      ("grade.log", ["make", "grade"])]:
    if name == "qemu.log":
        process = subprocess.Popen(command, cwd=root, start_new_session=True,
                                   stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                                   stderr=subprocess.STDOUT)
        try:
            output, _ = process.communicate(timeout=3)
            raise RuntimeError("make qemu exited before the wait loop")
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGTERM)
            try:
                output, _ = process.communicate(timeout=2)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                output, _ = process.communicate()
        finally:
            if process.poll() is None:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait()
        if b"(THU.CST) os is loading ..." not in output:
            raise RuntimeError("make qemu did not print the boot message")
    else:
        result = subprocess.run(command, cwd=root, capture_output=True, timeout=60)
        output = result.stdout + result.stderr
        (logs / name).write_bytes(output)
        if result.returncode:
            raise RuntimeError(f"{' '.join(command)} failed; see ../report/validation/{name}")
    (logs / name).write_bytes(output)
    print(f"Recorded {' '.join(command)} -> ../report/validation/{name}")
print("PASS: final framework build, make qemu and local make grade recorded")
