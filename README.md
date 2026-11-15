# TXRExtractor2

TXRExtractor2 unpacks a Wangan Midnight or Import Tuner Challenge build package into GLB models and PNG textures. It is a fork of [fatalhalt/TXRExtractor](https://github.com/fatalhalt/TXRExtractor). The original program is GPL-2.0, and so is this one.

## Export

Pick the archive, its TOC, and an output folder.

- Wangan Midnight: `WMN.DAT` and `WMN.TOC`.
- Import Tuner Challenge: `BUILD_CRS.DAT` or `BUILD.DAT`, with the matching `.TOC`.

The format is detected from the TOC. Raw records, models, and textures can be turned off independently. The combined map writes one GLB of every exported mesh and is off by default, because a full course is about a gigabyte.

An optional deprotected ITC image places the checker models. Without it, those two models are still exported, unplaced.

From a terminal:

```
TXRExtractor2 <archive> <toc> <output> [image]
TXRExtractor2 --combine <archive> <toc> <output> [image]
TXRExtractor2 --selftest
```

## Output

ITC records land in `raw/`, `models/rec_<id>.glb`, and `textures/own/<id>/`. The course set lands in `textures/course/` and `courseset/`. A standalone texture package lands in `textures/`.

Wangan Midnight keeps the TOC paths. Each `.GRID` also becomes a `.glb`, and each `.GTF` becomes a PNG beside it. A material's diffuse bitmap is embedded when that PNG was exported.

Material names stay the texture tokens from the package (`wall_hiru03`, `an13_check_00`).
