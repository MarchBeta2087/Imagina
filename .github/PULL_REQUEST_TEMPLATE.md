<!-- English and Chinese are both fine. / 中英文均可。 -->

## What / 做了什么

## Why / 为什么

Fixes #<!-- issue number, if any -->

## How it was tested / 怎么验证的

- [ ] Builds: `./build.sh` (GCC) and/or `build.ps1` (MSVC, MinGW-w64)
- [ ] `RawMapCodecTest` passes
- [ ] SDL front-end runs (`--frames`/`--screenshot`/`--open`/`--save`)
- [ ] Win32 front-end builds *(Windows only, say so if not tested)*

## Notes for reviewers / 给审查者的说明

- [ ] The shared core in `src/` stays platform independent
- [ ] Both front-ends still build (or: only the SDL one was changed because ...)
- [ ] `Readme.md` and `Readme.zh-CN.md` are updated
- [ ] Targets `dev`, not `master`
