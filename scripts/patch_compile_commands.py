# Post-link: rewrite bare `riscv32-esp-elf-g++` in compile_commands.json to an absolute
# toolchain path so clangd's --query-driver can execute the compiler for system includes.
Import("env")  # noqa: N816 — SCons / PlatformIO convention

import os
import shutil


def _resolve_cxx_abs(env):
    cxx = env.subst("$CXX")
    if cxx and (os.path.isabs(cxx) or os.path.sep in cxx or (os.altsep and os.altsep in cxx)):
        return cxx
    plat = env.PioPlatform()
    td = plat.get_package_dir("toolchain-riscv32-esp")
    if td:
        name = "riscv32-esp-elf-g++.exe" if os.name == "nt" else "riscv32-esp-elf-g++"
        cand = os.path.join(td, "bin", name)
        if os.path.isfile(cand):
            return cand
    path_env = env.get("ENV", {}).get("PATH", os.environ.get("PATH", ""))
    found = shutil.which(cxx or "riscv32-esp-elf-g++", path=path_env)
    return found or cxx


def _patch_compile_commands(target, source, env):
    import json

    project_dir = env.subst("$PROJECT_DIR")
    cxx = _resolve_cxx_abs(env)
    if not cxx:
        return
    path = os.path.join(project_dir, "compile_commands.json")
    if not os.path.isfile(path):
        return
    try:
        with open(path, "r", encoding="utf-8") as f:
            db = json.load(f)
    except (OSError, ValueError):
        return

    driver = "riscv32-esp-elf-g++"
    changed = False
    for entry in db:
        cmd = entry.get("command")
        if isinstance(cmd, str) and cmd.startswith(driver):
            entry["command"] = cxx + cmd[len(driver) :]
            changed = True

    if changed:
        try:
            with open(path, "w", encoding="utf-8") as f:
                json.dump(db, f, indent=2)
            print("patched compile_commands.json: CXX prefix ->", cxx)
        except OSError as e:
            print("patch_compile_commands: write failed:", e)


env.AddPostAction("$BUILD_DIR/${PROGNAME}.elf", env.Action(_patch_compile_commands))
