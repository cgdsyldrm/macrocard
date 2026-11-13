# PlatformIO pre-build script: turns source files into C headers.
#   sd/macropad.json -> src/default_config.generated.h  (built-in default config)
#   web/index.html   -> src/web_page.generated.h        (web configurator page)
from pathlib import Path

Import("env")  # noqa: F821  (provided by PlatformIO/SCons)

project = Path(env.subst("$PROJECT_DIR"))  # noqa: F821

import gzip

# (source, header, symbol, length symbol, gzip?)
EMBEDS = [
    ("sd/macropad.json", "src/default_config.generated.h", "kDefaultConfig", "kDefaultConfigLen", False),
    # Served with Content-Encoding: gzip; mtime=0 keeps the output reproducible.
    ("web/index.html", "src/web_page.generated.h", "kWebPageGz", "kWebPageGzLen", True),
]

for src_rel, dst_rel, sym, len_sym, compress in EMBEDS:
    src = project / src_rel
    dst = project / dst_rel
    data = src.read_bytes()
    if compress:
        data = gzip.compress(data, compresslevel=9, mtime=0)
    lines = []
    for i in range(0, len(data), 24):
        lines.append("    " + ", ".join(f"0x{b:02x}" for b in data[i:i + 24]) + ",")
    content = (
        f"// Generated from {src_rel} by tools/embed_config.py - do not edit.\n"
        "#pragma once\n"
        "#include <stddef.h>\n\n"
        f"static const char {sym}[] = {{\n"
        + "\n".join(lines)
        + "\n    0x00};\n"
        f"static const size_t {len_sym} = {len(data)};\n"
    )
    if not dst.exists() or dst.read_text() != content:
        dst.write_text(content)
        print("embed_config: regenerated", dst.name)
