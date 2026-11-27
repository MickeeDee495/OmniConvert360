# OmniConvert360

**OmniConvert360** is a native, standalone on-console Xbox 360 utility for converting, ripping, extracting, repacking, and repairing Xbox 360 and Original Xbox game packages directly on an RGH/JTAG/BadUpdate Xbox 360 console—no PC required.

---

## Xbox 360 Installation

1. Copy the packaged **`OmniConvert360`** folder (`Release-Assets/OmniConvert360/OmniConvert360/`, containing `default.xex` and `OmniConvert360.xex`) onto your Xbox 360:
   - `Hdd1:\Apps\OmniConvert360\default.xex` (or `Hdd1:\Homebrew\OmniConvert360\default.xex`)
2. Launch **`OmniConvert360`** from **Aurora**, **Freestyle Dash**, or **XeXMenu**.
3. *(Optional)* Copy `AuroraScript/ConvertToGOD/Main.lua` into `Aurora\User\Scripts\Utility\` to launch conversions directly from Aurora's file browser via `game:\target.txt`.

---

## Key Features

### 1. Five On-Console Conversion & Repair Modes (`LB` / `RB`)
- **GOD (`00007000` / `00005000`):**
  - Converts Xbox 360 (`00007000`) and Original Xbox (`00005000`) titles directly into signed-compatible `LIVE` + `SVOD` Games on Demand containers under `<DestDrive>\Content\0000000000000000\<TitleID>\<ContentType>\`.
  - Splits `Data0000..Data00xx` at the exact SVOD boundary (`204 * 203 = 41,412` 4 KB blocks = `169,623,552` bytes per part) with two-level SHA-1 hash tables and master header SHA-1 (`0x32C`).
  - Automatically detects and installs embedded `Content\0000000000000000\` DLC/Title Update packages directly to `Hdd1:\Content\0000000000000000\` without duplicating them inside the GOD container.
  - Automatically strips `$SystemUpdate` / `$$ystemUpdate` folders and routes `XEX_MODULE_PATCH` (`0x00000008`) title-update executables to clean Extracted Folders (`<DestDrive>\<GAMES_DIR>\<CleanTitle>\`).
- **Extracted Folder (`GAMES` / `XGAMES`):**
  - Extracts `.iso`, split `.iso.001`, archives, optical discs, or existing GOD packages into human-readable game folders (`<DestDrive>\GAMES\<Title>\` for Xbox 360 or `<DestDrive>\XGAMES\<Title>\` for Original Xbox).
- **XISO (`.iso` / `.iso.001`):**
  - Rebuilds balanced XISO (`MICROSOFT*XBOX*MEDIA`) disc images from extracted folders, archives, optical discs, or installed GOD packages, splitting automatically at `4,000 MB` (`FATX_SPLIT_LIMIT`) for FATX compatibility.
- **Multi-Part ZIP (`.zip.001`):**
  - Packs game folders, ISOs, or GOD titles into FATX-safe multi-volume ZIP64 archives (`.zip.001`, `.zip.002`, ...).
- **Fix GOD Names & Icons (1-Sec Patch):**
  - Scans `<DestDrive>\Content\0000000000000000\` and lists all installed `00005000` and `00007000` GOD packages by their **human-readable display names** (`[OG Xbox] ...` / `[Xbox 360] ...`).
  - Reads `default.xbe` (`$$XTIMAGE` / `$$XSIMAGE`), `default.xex` (`XDBF`/`XSRC`), or `nxeart` directly out of each game's `.data` folder, replaces placeholder `"Xbox Game"` titles (`0x412` / `0x1692`), embeds crisp NXE Storage PNG icons (`0x1712..0x9719`), and recomputes the `44 KB` `LIVE` header SHA-1 (`0x32C`) in under 1 second per title without re-converting `Data0000..Data00xx`.

### 2. Native Optical Disc Ripping (`\Device\Cdrom0`)
- Opens `\Device\Cdrom0` directly via Xbox 360 kernel `NtOpenFile` and `NtReadFile`.
- Probes and mounts the active Xbox game partition across **XISO (`0x00000000`)**, **Retail Xbox 360 XGD3 (`0x02080000`)**, **Retail Xbox 360 XGD2 (`0x0FD90000`)**, and **Retail Original Xbox XGD1 (`0x18300000`)**.

### 3. Multi-Volume Archive & Recursive ISO Extraction
- **7-Zip:** `.7z`, `.7z.001` (LZMA / LZMA2 / BCJ / BCJ2).
- **ZIP:** `.zip`, `.zip.001` (`.zip.002`, ...), and `.z01` (`.z02`, ..., `.zip`) with **Store (`method 0`)** and **streaming Deflate (`method 8`)** plus Zip64 (`> 4 GB`).
- **RAR:** `.rar`, `.rar.001`, `.part1.rar` / `.part01.rar` / `.part001.rar`, and `.rar` + `.r00` + `.r01` (RAR4 and RAR5 volume sets).
- Recursively discovers `.iso.001`, `.iso`, `default.xex`, or `default.xbe` inside extracted archive subdirectories.

### 4. Original Xbox & Xbox 360 NXE Storage Icon Pipeline
- **Original Xbox (`default.xbe`):** Extracts `$$XTIMAGE` / `$$XSIMAGE` textures (both `XPR0`-wrapped and raw `128x128` / `64x64` `DXT1`), decodes `DXT1` blocks in software to a linear `D3DFMT_LIN_A8R8G8B8` surface, and encodes a valid PNG via `D3DXSaveSurfaceToFileInMemory`.
- **Xbox 360 (`default.xex` / `nxeart`):** Extracts NXE Storage PNG assets directly from `nxeart` or embedded `XDBF` (`SPA`) resources inside `default.xex`.

### 5. Resumable Queue, Text Logs & Custom Directory Settings
- **Queue Persistence (`game:\queue_state.txt`):** Persists queue items, output mode, and destination drive across interruptions; continues past individual item errors.
- **Persistent Text Logs (`game:\omniconvert_log.txt`):** Mirrors all conversion and diagnostic logs to `game:\omniconvert_log.txt`.
- **Custom Folder Settings (`game:\settings.ini`):** Press **`BACK`** to configure `GAMES_DIR`, `XGAMES_DIR`, and `ISOS_DIR`.

---

## Controls

| Button | Action |
| :--- | :--- |
| **D-Pad Up / Down** | Navigate drives, folders, files, or installed GOD titles |
| **A** | Open folder / Queue file, optical disc (`Cdrom0:\`), or GOD title |
| **X** | Queue entire selected folder |
| **LB / RB** | Cycle mode (`GOD`, `Extracted Folder`, `XISO`, `Multi-Part ZIP`, `Fix GOD Names & Icons`) |
| **Y** | Toggle destination drive (`Hdd1:\` / `Usb0:\`) |
| **BACK** | Configure output directory names (`GAMES` / `XGAMES` / `ISOS`) |
| **START** | Execute queued batch |
| **B** | Go back / Exit |

---

## Code & Specification Attributions

Authored by **MickeeDee495** (https://github.com). OmniConvert360 is an original clean-room C++ implementation written specifically for native Xbox 360 execution, incorporating the following public-domain libraries, SDK components, and documented container specifications:

1. **7-Zip LZMA SDK (`lzma/`):** Public-domain decompression routines (`7zDec.c`, `7zArcIn.c`, `LzmaDec.c`, `Lzma2Dec.c`, `Bcj2.c`, etc.) by **Igor Pavlov** (Public Domain).
2. **miniz (`miniz/`):** Public-domain single-file Deflate/zlib inflation library by **Rich Geldreich** (Public Domain / MIT).
3. **Microsoft Xbox 360 XDK & ATG Framework (`Common/`):** `AtgConsole`, `AtgDevice`, `AtgFont`, `AtgResource`, and `AtgUtil` helper classes from the **Microsoft Xbox 360 SDK**, plus `xgraphics` / `d3dx9` PNG encoding and `xboxkrnl` `XeCryptSha*` / `NtOpenFile` / `NtReadFile` kernel APIs.
4. **Xbox 360 & Original Xbox Format Specifications:** Clean-room binary layout implementations based on community-documented specifications (`Free60` / `XboxDevWiki`) for `XISO` (`MICROSOFT*XBOX*MEDIA`), `XEX2` optional headers, `XBE` (`XBEH`) section headers (`$$XTIMAGE`), `XDBF` (`SPA`) title resources, and `LIVE` / `SVOD` container hash tables.

