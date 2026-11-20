#!/usr/bin/env python3
"""
Embeds web/index.html (gzip-compressed) and web/softap/{index.html,rsa-utils/*.js}
into C++ header files under src/generated/ with zero external dependencies.
"""
import gzip
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_DIR = os.path.join(ROOT, "src", "generated")
os.makedirs(OUT_DIR, exist_ok=True)


def minify_html_light(text: str) -> str:
    # Conservative whitespace cleanup safe for inline JS/CSS
    lines = [line.strip() for line in text.splitlines() if line.strip()]
    return "\n".join(lines)


def format_c_array(name: str, data: bytes) -> str:
    chunks = []
    for i in range(0, len(data), 16):
        row = ", ".join(f"0x{b:02x}" for b in data[i : i + 16])
        chunks.append(f"    {row},")
    body = "\n".join(chunks)
    return (
        f"inline constexpr unsigned char {name}[] = {{\n"
        f"{body}\n"
        f"}};\n"
        f"inline constexpr size_t {name}Len = {len(data)};\n"
    )


def generate_lan_ui():
    src_path = os.path.join(ROOT, "web", "index.html")
    with open(src_path, "r", encoding="utf-8") as f:
        raw = minify_html_light(f.read()).encode("utf-8")
    gz = gzip.compress(raw, compresslevel=9, mtime=0)

    out_path = os.path.join(OUT_DIR, "web_ui_gz.h")
    with open(out_path, "w", encoding="utf-8") as f:
        f.write("#pragma once\n#include <cstddef>\n\nnamespace garadget {\n\n")
        f.write(f"// Generated from web/index.html ({len(raw)} bytes raw -> {len(gz)} bytes gzip)\n")
        f.write(format_c_array("kWebUiIndexGz", gz))
        f.write("\n} // namespace garadget\n")
    print(f"Generated {os.path.relpath(out_path, ROOT)}: {len(raw)} -> {len(gz)} bytes gzip")


def generate_softap_ui():
    softap_html_path = os.path.join(ROOT, "web", "softap", "index.html")
    with open(softap_html_path, "r", encoding="utf-8") as f:
        html_bytes = minify_html_light(f.read()).encode("utf-8") + b"\x00"

    rsa_dir = os.path.join(ROOT, "web", "softap", "rsa-utils")
    rsa_order = ["jsbn_1.js", "jsbn_2.js", "prng4.js", "rng.js", "rsa.js"]
    rsa_parts = []
    for fname in rsa_order:
        fpath = os.path.join(rsa_dir, fname)
        with open(fpath, "r", encoding="utf-8") as f:
            txt = f.read()
        # Strip block and line comments for compactness
        txt = re.sub(r"/\*[\s\S]*?\*/", "", txt)
        txt = re.sub(r"(?m)^\s*//.*$", "", txt)
        lines = [ln.strip() for ln in txt.splitlines() if ln.strip()]
        rsa_parts.append("\n".join(lines))
    rsa_bytes = "\n".join(rsa_parts).encode("utf-8") + b"\x00"

    out_path = os.path.join(OUT_DIR, "softap_ui.h")
    with open(out_path, "w", encoding="utf-8") as f:
        f.write("#pragma once\n#include <cstddef>\n\nnamespace garadget {\n\n")
        f.write(f"// Generated from web/softap/index.html ({len(html_bytes) - 1} bytes)\n")
        f.write(format_c_array("kSoftApIndexHtml", html_bytes))
        f.write("\n")
        f.write(f"// Generated from web/softap/rsa-utils/*.js ({len(rsa_bytes) - 1} bytes)\n")
        f.write(format_c_array("kSoftApRsaJs", rsa_bytes))
        f.write("\n} // namespace garadget\n")
    print(
        f"Generated {os.path.relpath(out_path, ROOT)}: "
        f"html={len(html_bytes) - 1}B, rsa.js={len(rsa_bytes) - 1}B"
    )


if __name__ == "__main__":
    generate_lan_ui()
    generate_softap_ui()
