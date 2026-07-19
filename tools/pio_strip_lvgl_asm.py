"""PlatformIO pre-build hook.

LVGL 9.x ships ARM Helium/NEON blend kernels as `.S` files. Their bodies are
guarded out on non-ARM targets, but the C-preprocessor include chain still
pulls in <stdint.h>, and the Xtensa assembler chokes on the `typedef`s
("unknown opcode ... 'typedef'"). Since the kernels are inert on ESP32, we
simply blank the `.S` files before the build collects sources.

Runs every build; harmless to re-run (it only touches non-empty files).
"""
import glob
import os

Import("env")  # noqa: F821 (injected by PlatformIO)

libdeps = env.subst("$PROJECT_LIBDEPS_DIR")  # noqa: F821
envname = env.subst("$PIOENV")               # noqa: F821
pattern = os.path.join(libdeps, envname, "lvgl", "src", "**", "*.S")

for path in glob.glob(pattern, recursive=True):
    try:
        if os.path.getsize(path) > 0:
            with open(path, "w") as fh:
                fh.write("/* blanked for non-ARM target by pio_strip_lvgl_asm.py */\n")
            print("[lvgl-asm] blanked", os.path.relpath(path, libdeps))
    except OSError as exc:
        print("[lvgl-asm] skip", path, exc)
