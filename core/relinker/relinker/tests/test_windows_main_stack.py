import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

from test_optional_plt import fixture


def main():
    relinker = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="anyps5-main-stack-") as directory:
        work = Path(directory)
        image = fixture()
        code = bytes.fromhex("48 81 ec 00 00 1c 00 48 89 04 24 48 81 c4 00 00 1c 00 b8 2a 00 00 00 c3")
        image[0x210:0x210 + len(code)] = code
        source = work / "input.elf"
        output = work / "output.exe"
        source.write_bytes(image)
        result = subprocess.run([str(relinker), "--windows", "--skip-sce-module", str(source), str(output)], capture_output=True, text=True, timeout=20)
        assert result.returncode == 0, (result.returncode, result.stdout, result.stderr)
        data = output.read_bytes()
        optional = struct.unpack_from("<I", data, 0x3c)[0] + 24
        reserve, commit = struct.unpack_from("<QQ", data, optional + 72)
        assert (reserve, commit) == (0x210000, 0x1000), (hex(reserve), hex(commit))
        if os.name == "nt":
            executed = subprocess.run([str(output)], capture_output=True, timeout=20)
            assert executed.returncode == 42, (hex(executed.returncode & 0xffffffff), executed.stdout, executed.stderr)
    print("Windows main stack: threads commit one page by default and a 1.75 MiB guest frame without probes runs on the main thread")


if __name__ == "__main__":
    main()
