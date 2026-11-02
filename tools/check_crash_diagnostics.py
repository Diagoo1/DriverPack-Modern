"""Run intentionally crashing *test* subprocesses; never starts a driver operation."""
import json
import pathlib
import struct
import subprocess
import sys

exe = pathlib.Path(sys.argv[1]).resolve()
root = pathlib.Path(sys.argv[2]).resolve()
results = []
for mode in ("normal", "seh", "worker", "abort", "terminate", "qt-fatal", "full"):
    folder = root / mode
    folder.mkdir(parents=True, exist_ok=True)
    if list(folder.iterdir()):
        raise RuntimeError(f"Use a fresh output directory: {folder}")
    process = subprocess.run([str(exe), str(folder), mode], timeout=20, capture_output=True)
    logs = list(folder.rglob("session.txt"))
    assert len(logs) == 1, (mode, process.returncode, process.stderr)
    text = logs[0].read_text(encoding="utf-8")
    assert "probe-before-action" in text, mode
    if mode == "normal":
        assert process.returncode == 0 and "session-clean-exit" in text
        assert not list(folder.rglob("*.dmp"))
        results.append({"mode": mode, "cleanExit": True})
        continue
    assert process.returncode != 0 and "FATAL exception=" in text, (mode, text)
    if mode == "full":
        assert "DETAIL LIMIT" in text and logs[0].stat().st_size < 4*1024*1024+8192
    assert "session-clean-exit" not in text
    dumps = list(folder.rglob("*.dmp"))
    assert len(dumps) == 1 and "crash-dump saved=1 win32Error=0" in text, (mode, text)
    data = dumps[0].read_bytes()
    assert data[:4] == b"MDMP", mode
    count, table = struct.unpack_from("<II", data, 8)
    exception = None
    for i in range(count):
        kind, size, offset = struct.unpack_from("<III", data, table + 12 * i)
        if kind == 6:  # ExceptionStream must preserve the original faulting thread.
            thread, _, code = struct.unpack_from("<III", data, offset)
            exception = {"thread": thread, "code": f"0x{code:08X}"}
    assert exception and exception["thread"] > 0, mode
    if mode in ("seh", "worker"):
        assert exception["code"] == "0xC0000005", (mode, exception)
    if mode == "worker":
        assert "worker-before-crash" in text
    results.append({"mode": mode, "exit": process.returncode, "dumpBytes": len(data), **exception})
(root / "proof.json").write_text(json.dumps(results, indent=2), encoding="utf-8")
print(json.dumps(results, indent=2))
