# Vendored libraries

## Seeed Arduino mmWave

Copied from [Love4yzp/Seeed-mmWave-library](https://github.com/Love4yzp/Seeed-mmWave-library) (Seeed Studio) so the project builds without pulling git on every machine.

**Local change:** `src/SEEED_MR60BHA2.h` — replaced invalid C++

`typedef union FirmwareInfo { ... };`

with a proper anonymous union:

`union FirmwareInfo { ... };`

Upstream uses `typedef` with a tagged union name in a way GCC rejects in C++; this removes the compiler warning and matches the same layout.
