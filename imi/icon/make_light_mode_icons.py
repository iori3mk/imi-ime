"""タスクバーが明るいとき（Windows モードが「ライト」）用の、入力モードのアイコンを作る。

  python imi/icon/make_light_mode_icons.py   # src/data/images/win/ に *_light.ico を書き出す

Mozc の入力モードのアイコン（ms_*_a.ico）は白い文字に細い縁を付けた絵だけで、明るいタスクバーでは
文字が背景に溶けて見えにくい。色（青・緑・赤）を反転して黒い文字にし、透明度はそのままにする。
元のアイコンはどれも 32 ビットの BMP なので、標準の機能だけで読み書きできる。
"""

from __future__ import annotations

import pathlib
import struct

WIN = pathlib.Path(__file__).resolve().parents[2] / "src" / "data" / "images" / "win"
NAMES = [
    "ms_hiragana_a",
    "ms_katakana_a",
    "ms_katakana_half_a",
    "ms_alpha_a",
    "ms_alpha_half_a",
    "ms_direct_input_a",
    "ms_disabled_a",
]


def invert_icon(data: bytes) -> bytes:
    out = bytearray(data)
    _, kind, count = struct.unpack_from("<HHH", data, 0)
    assert kind == 1, "ICO ではない"
    for i in range(count):
        w, h, _, _, _, bpp, size, offset = struct.unpack_from("<BBBBHHII", data, 6 + 16 * i)
        w = w or 256
        h = h or 256
        assert data[offset : offset + 4] != b"\x89PNG", "PNG の項目には対応していない"
        header_size, _, _, _, dib_bpp = struct.unpack_from("<IiiHH", data, offset)
        assert header_size == 40 and dib_bpp == 32, "32 ビットの BMP だけに対応"
        pixels = offset + header_size
        for p in range(pixels, pixels + w * h * 4, 4):
            if out[p + 3] == 0:
                continue  # 透明な所はそのまま
            out[p] = 255 - out[p]  # 青
            out[p + 1] = 255 - out[p + 1]  # 緑
            out[p + 2] = 255 - out[p + 2]  # 赤
    return bytes(out)


def main() -> None:
    for name in NAMES:
        src = WIN / f"{name}.ico"
        dst = WIN / f"{name.removesuffix('_a')}_light.ico"
        dst.write_bytes(invert_icon(src.read_bytes()))
        print(dst.name)


if __name__ == "__main__":
    main()
