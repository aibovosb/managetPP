# -*- coding: utf-8 -*-
"""生成透明背景的"单眼睛"图标（ICO 多尺寸 + SVG 预览）
设计：透明背景 + 白色杏仁形眼眶描边 + 白色瞳孔圆环，
     整体为极简「单眼睛」符号，深色/浅色任务栏下均清晰。
"""
import struct, os
OUT_DIR = os.path.dirname(os.path.abspath(__file__))
def make_eye_rgba(size):
    """返回 size x size 的 RGBA 字节数组，透明背景 + 白色描边眼睛。"""
    S = size
    px = bytearray(S * S * 4)  # RGBA
    cx, cy = S / 2.0, S / 2.0
    # 眼睛外轮廓：椭圆（宽:高 = 2:1 的杏仁形）
    rx, ry = S * 0.44, S * 0.26
    stroke = max(1.0, S * 0.075)      # 眼眶描边宽度
    iris_r = S * 0.17                 # 瞳孔半径
    iris_stroke = max(1.0, S * 0.065) # 瞳孔描边宽度
    def setpx(x, y, a):
        if 0 <= x < S and 0 <= y < S:
            i = (y * S + x) * 4
            if a > px[i + 3]:
                px[i] = 255
                px[i + 1] = 255
                px[i + 2] = 255
                px[i + 3] = int(a)
    # 超采样抗锯齿
    SS = 3
    for y in range(S):
        for x in range(S):
            cov = 0
            for sy in range(SS):
                for sx in range(SS):
                    fx = x + (sx + 0.5) / SS
                    fy = y + (sy + 0.5) / SS
                    dx, dy = fx - cx, fy - cy
                    # 椭圆归一化距离
                    d_ell = ((dx / rx) ** 2 + (dy / ry) ** 2) ** 0.5
                    in_outer = d_ell <= 1.0
                    in_inner = d_ell <= (1.0 - stroke / min(rx, ry))
                    on_ring = in_outer and not in_inner
                    # 瞳孔圆环
                    d_circ = (dx * dx + dy * dy) ** 0.5
                    in_c_out = d_circ <= iris_r
                    in_c_in = d_circ <= (iris_r - iris_stroke)
                    on_circle = in_c_out and not in_c_in
                    if on_ring or on_circle:
                        cov += 1
            if cov:
                setpx(x, y, int(255 * cov / (SS * SS)))
    return bytes(px)
def bmp_for_ico(size, rgba):
    """构造 ICO 内的 BMP（含 AND 掩码）。"""
    hdr = struct.pack('<IiiHHIIiiII', 40, size, size * 2, 1, 32, 0, 0, 0, 0, 0, 0)
    body = bytearray()
    for y in range(size - 1, -1, -1):
        for x in range(size):
            i = (y * size + x) * 4
            r, g, b, a = rgba[i], rgba[i + 1], rgba[i + 2], rgba[i + 3]
            body += bytes((b, g, r, a))
    row_bytes = ((size + 31) // 32) * 4
    body += bytes(row_bytes * size)
    return hdr + bytes(body)
def main():
    sizes = [16, 24, 32, 48, 64, 128, 256]
    images = []
    for s in sizes:
        rgba = make_eye_rgba(s)
        images.append((s, bmp_for_ico(s, rgba)))
    out = bytearray()
    out += struct.pack('<HHH', 0, 1, len(images))
    offset = 6 + 16 * len(images)
    for s, data in images:
        w = 0 if s >= 256 else s
        h = 0 if s >= 256 else s
        out += struct.pack('<BBBBHHII', w, h, 0, 0, 1, 32, len(data), offset)
        offset += len(data)
    for s, data in images:
        out += data
    ico_path = os.path.join(OUT_DIR, "icon.ico")
    with open(ico_path, "wb") as f:
        f.write(out)
    print("已生成:", ico_path, os.path.getsize(ico_path), "字节")
    svg = '''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
  <path d="M2 12s3.5-6.5 10-6.5S22 12 22 12s-3.5 6.5-10 6.5S2 12 2 12z"/>
  <circle cx="12" cy="12" r="3.2"/>
</svg>
'''
    svg_path = os.path.join(OUT_DIR, "icon.svg")
    with open(svg_path, "w", encoding="utf-8") as f:
        f.write(svg)
    print("已生成:", svg_path)
if __name__ == "__main__":
    main()