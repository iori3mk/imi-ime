"""IMi のアイコン（案P4）を描いて .ico にする。外部の画像ライブラリは使わず、numpy で塗る。

  uv run --project <imi-dev>/phase0 python imi/icon/make_icon.py   # imi/icon/ に書き出す

形（64×64 の座標）：「i」の点 (32,23)・半径 4.6。棒は (32,33.5) から (32,47) へ下り、下の端で左へ曲がって
円（中心 (32,32)・半径 26）の 110° の位置に滑らかにつながり、時計回りに左・上と回って、右（3時の位置）で止まる。
線の太さ 5.5、端は丸。1色（白の版と黒の版）。

小さい大きさ（16〜24px）では線が細くなりすぎないよう、線の太さと点の大きさに下限を設け、
点と棒のすき間を広げる（点 (32,21)、棒は (32,36) から）。
"""

from __future__ import annotations

import math
import pathlib
import struct

import numpy as np

OUT = pathlib.Path(__file__).resolve().parent
SIZES = [16, 20, 24, 32, 40, 48, 64, 96, 128, 256]
SS = 8  # 1ピクセルあたりの縦横の標本数（ふちをなめらかにする）


def path_points(stem_top: float = 33.5) -> np.ndarray:
    """棒〜曲がり〜円の中心線を細かい点の列にする（64×64 の座標）。"""
    pts = [(32.0, stem_top), (32.0, 47.0)]
    # 3次ベジェ：(32,47) → 制御点 (32,53.5)・(29.69,58.82) → (23.11,56.43)
    p0, p1, p2, p3 = map(np.array, [(32.0, 47.0), (32.0, 53.5), (29.69, 58.82), (23.11, 56.43)])
    for t in np.linspace(0, 1, 60)[1:]:
        q = (1 - t) ** 3 * p0 + 3 * (1 - t) ** 2 * t * p1 + 3 * (1 - t) * t ** 2 * p2 + t ** 3 * p3
        pts.append(tuple(q))
    # 円弧：中心 (32,32)・半径 26、110° から 360° まで（y が下向きの座標で角度が増える向き＝画面で時計回り）
    for a in np.linspace(math.radians(110), math.radians(360), 300)[1:]:
        pts.append((32 + 26 * math.cos(a), 32 + 26 * math.sin(a)))
    return np.array(pts)


def render(size: int) -> np.ndarray:
    """size×size の不透明度（0〜1）。"""
    scale = size / 64.0
    stroke = max(5.5 * scale, 1.7) / scale   # 64 座標での線の太さ（小さいときは太らせる）
    dot_r = max(4.6 * scale, 1.5) / scale
    n = size * SS
    c = (np.arange(n) + 0.5) / SS / scale     # 標本の 64 座標
    x, y = np.meshgrid(c, c)
    # 24px 以下では、点と棒がくっつかないよう、点を上げ棒の書き出しを下げてすき間を広げる
    small = size <= 24
    dot_y, stem_top = (21.0, 36.0) if small else (23.0, 33.5)
    inside = (x - 32) ** 2 + (y - dot_y) ** 2 <= dot_r ** 2
    pts = path_points(stem_top)
    half2 = (stroke / 2) ** 2
    near = np.zeros_like(inside)
    for (ax, ay), (bx, by) in zip(pts[:-1], pts[1:]):
        dx, dy = bx - ax, by - ay
        L2 = dx * dx + dy * dy
        t = np.clip(((x - ax) * dx + (y - ay) * dy) / L2, 0, 1)
        near |= (x - ax - t * dx) ** 2 + (y - ay - t * dy) ** 2 <= half2
    cov = (inside | near).astype(np.float32)
    return cov.reshape(size, SS, size, SS).mean(axis=(1, 3))


def bmp_entry(alpha: np.ndarray, rgb: tuple[int, int, int]) -> bytes:
    """32bit の BMP（ICO の中身）。上下を逆にして BGRA で並べ、AND マスクを付ける。"""
    size = alpha.shape[0]
    a = np.round(alpha * 255).astype(np.uint8)[::-1]
    px = np.zeros((size, size, 4), np.uint8)
    px[..., 0], px[..., 1], px[..., 2], px[..., 3] = rgb[2], rgb[1], rgb[0], a
    header = struct.pack("<IiiHHIIiiII", 40, size, size * 2, 1, 32, 0, size * size * 4, 0, 0, 0, 0)
    row = ((size + 31) // 32) * 4
    mask = bytes(row * size)  # 透明は alpha で表すので AND マスクは 0
    return header + px.tobytes() + mask


def write_ico(path: pathlib.Path, rgb: tuple[int, int, int]) -> None:
    entries = [(s, bmp_entry(render(s), rgb)) for s in SIZES]
    head = struct.pack("<HHH", 0, 1, len(entries))
    off = 6 + 16 * len(entries)
    dirs, blobs = b"", b""
    for s, data in entries:
        dirs += struct.pack("<BBBBHHII", s % 256, s % 256, 0, 0, 1, 32, len(data), off + len(blobs))
        blobs += data
    path.write_bytes(head + dirs + blobs)
    print(f"{path.name}: {', '.join(str(s) for s, _ in entries)}px, {path.stat().st_size // 1024}KB")


def write_preview(path: pathlib.Path) -> None:
    """確かめ用：白の版を暗い地に、黒の版を明るい地に並べた PNG（各大きさ）。"""
    import zlib

    pad, rows = 8, []
    width = sum(SIZES[:-1]) + pad * (len(SIZES))
    height = max(SIZES[:-1]) * 2 + pad * 3
    img = np.zeros((height, width, 3), np.uint8)
    img[: height // 2] = (32, 33, 36)
    img[height // 2:] = (236, 238, 241)
    xo = pad
    for s in SIZES[:-1]:
        a = render(s)[..., None]
        top, bot = pad, height // 2 + pad
        img[top:top + s, xo:xo + s] = (img[top:top + s, xo:xo + s] * (1 - a) + 255 * a).astype(np.uint8)
        img[bot:bot + s, xo:xo + s] = (img[bot:bot + s, xo:xo + s] * (1 - a)).astype(np.uint8)
        xo += s + pad
    raw = b"".join(b"\x00" + img[r].tobytes() for r in range(height))
    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)) \
        + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    path.write_bytes(png)
    print(f"{path.name}: {width}x{height}")


def write_png(path: pathlib.Path, size: int, rgb: tuple[int, int, int], icon: int | None = None, bottom: int = 0) -> None:
    """透明な地に1色で描いた PNG（「IMi について」の画面のロゴなど）。
    icon を渡すと、size の画像の中に icon の大きさで、左右の中央・下から bottom の位置に描く。"""
    import zlib

    icon = icon or size
    a = np.zeros((size, size), np.uint8)
    y0, x0 = size - bottom - icon, (size - icon) // 2
    a[y0:y0 + icon, x0:x0 + icon] = np.round(render(icon) * 255).astype(np.uint8)
    px = np.zeros((size, size, 4), np.uint8)
    px[..., 0], px[..., 1], px[..., 2], px[..., 3] = rgb[0], rgb[1], rgb[2], a
    raw = b"".join(b"\x00" + px[r].tobytes() for r in range(size))

    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0))
                     + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))
    print(f"{path.name}: {size}x{size}")


if __name__ == "__main__":
    write_ico(OUT / "imi_white.ico", (255, 255, 255))
    write_ico(OUT / "imi_black.ico", (17, 17, 17))
    write_preview(OUT / "preview.png")
    # 「IMi について」の画面は、ロゴの下端を色の帯の上端にそろえて描くので、下に少し余白を取る
    write_png(OUT / "imi_black_128.png", 128, (17, 17, 17), icon=84, bottom=14)
