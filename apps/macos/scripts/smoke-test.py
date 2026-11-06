#!/usr/bin/env python3
"""Exercise the packaged CLI against an original silent WAV and upstream QMC vectors."""
from pathlib import Path
import io
import subprocess
import tempfile
import wave

ROOT = Path(__file__).resolve().parent.parent


def main() -> None:
    with tempfile.TemporaryDirectory(prefix="smoke-test-", dir=ROOT / "build") as directory:
        exercise(Path(directory))


def exercise(work: Path) -> None:
    SOURCE = work / "input"
    OUTPUT = work / "output"
    SOURCE.mkdir(parents=True, exist_ok=True)
    OUTPUT.mkdir(parents=True, exist_ok=True)
    stream = io.BytesIO()
    with wave.open(stream, "wb") as audio:
        audio.setnchannels(1)
        audio.setsampwidth(2)
        audio.setframerate(8000)
        audio.writeframes(bytes(1600))
    original = stream.getvalue()
    encrypted = bytes(value ^ 0xA3 for value in original)
    source = SOURCE / "原生导出测试.uc"
    source.write_bytes(encrypted)
    subprocess.run([str(ROOT / "build/musicunlock"), str(OUTPUT), str(source)], check=True)
    assert (OUTPUT / "原生导出测试.wav").read_bytes() == original
    assert source.read_bytes() == encrypted
    with wave.open(str(OUTPUT / "原生导出测试.wav")) as result:
        assert result.getnframes() == 800

    vectors = ROOT.parent.parent / "tests/fixtures/qmc"
    for name, extension in [("mflac0_rc4", "mflac0"), ("mflac_rc4", "mflac"),
                            ("mflac_map", "mflac"), ("mgg_map", "mgg"), ("qmc0_static", "qmc0")]:
        ciphertext = (vectors / f"{name}_raw.bin").read_bytes() + (vectors / f"{name}_suffix.bin").read_bytes()
        expected = (vectors / f"{name}_target.bin").read_bytes()
        file = SOURCE / f"{name}.{extension}"
        file.write_bytes(ciphertext)
        subprocess.run([str(ROOT / "build/musicunlock"), str(OUTPUT), str(file)], check=True)
        candidates = list(OUTPUT.glob(f"{name}.*"))
        assert len(candidates) == 1 and candidates[0].read_bytes() == expected, name
    print("Packaged CLI smoke test passed: 1 generated UC/WAV + 5 upstream QMC vectors.")


if __name__ == "__main__":
    main()
