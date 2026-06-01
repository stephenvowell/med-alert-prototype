# PlatformIO post-hook: esptool v5 draws a Unicode progress bar (█░) that breaks
# PlatformIO's line echo on Windows cp1252. --no-progress must follow write-flash;
# upload_flags in platformio.ini are prepended and end up in the wrong place.

Import("env")

flags = env.get("UPLOADERFLAGS")
if flags and "write-flash" in list(flags):
    lst = list(flags)
    i = lst.index("write-flash") + 1
    if "--no-progress" not in lst:
        lst.insert(i, "--no-progress")
        env.Replace(UPLOADERFLAGS=lst)
