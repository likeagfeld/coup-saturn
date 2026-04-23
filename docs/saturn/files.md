# Saturn PAL File Reference

Complete listing of all files in the Saturn platform abstraction layer.

## Core Implementation

### saturn_pal.h
**Purpose**: Public API header for Saturn platform
**Size**: ~2.3KB
**Dependencies**: `cui_pal.h`

**Key Functions**:
- `cui_saturn_platform()` - Get platform implementation
- `cui_saturn_set_frame_callback()` - Register frame callback
- `cui_saturn_run()` - Start main loop (never returns)

**Usage**:
```c
#include "saturn_pal.h"

const cui_platform_t* platform = cui_saturn_platform();
cui_saturn_set_frame_callback(my_callback);
cui_saturn_run();
```

---

### saturn_pal.c
**Purpose**: Saturn PAL implementation
**Size**: ~10KB
**Dependencies**: `jo/jo.h`, `saturn_pal.h`

**Implements**:
- Display functions (init, draw_text, draw_rect, etc.)
- Input polling (Saturn controller mapping)
- Color conversion (RGBA32 -> RGB555)
- Frame callback wrapper

**Key Details**:
- Text rendering uses `jo_printf()` (white only)
- Rectangles drawn with block characters
- Input uses `jo_is_pad1_key_down()` for edge detection
- Fixed 40x28 character grid (320x224 pixels)

---

## Build System

### Makefile
**Purpose**: Jo Engine build configuration
**Size**: ~3.2KB
**Dependencies**: Jo Engine `Makefile.common`

**Targets**:
```bash
make              # Build disc image
make clean        # Remove artifacts
make info         # Show configuration
make test-mednafen # Build and run in Mednafen
make test-yabause  # Build and run in Yabause
```

**Configuration**:
- `JO_ENGINE_SRC_DIR` - Path to Jo Engine (default: `/opt/joengine`)
- `EXTRA_CFLAGS` - Include paths for cui headers
- `JO_COMPILE_USING_SGL=0` - Use Jo Engine renderer

**Output**:
- `cui_saturn.cue` + `cui_saturn.bin` - Disc image (CUE/BIN format)
- `cui_saturn.iso` - Alternative disc image

---

### .gitignore
**Purpose**: Exclude build artifacts from git
**Size**: ~300 bytes

**Excludes**:
- Disc images (*.cue, *.bin, *.iso)
- Object files (*.o, *.obj)
- Debug files (*.map, *.log)
- Emulator saves (*.state, *.srm)

---

## Documentation

### README.md
**Purpose**: Main documentation and usage guide
**Size**: ~6.6KB

**Sections**:
1. Architecture overview (control flow inversion)
2. Integration pattern (example code)
3. Platform capabilities (display, input)
4. Known limitations (4 documented issues)
5. Building instructions
6. Testing with emulators
7. Performance considerations
8. Color conversion details
9. Resources and links

**Audience**: Developers integrating cui with Saturn

---

### QUICKSTART.md
**Purpose**: Minimal getting-started guide
**Size**: ~4.5KB

**Sections**:
- 30-second setup example
- Key differences from PC
- Common mistakes (4 examples)
- Input cheat sheet
- Build & run commands
- Debugging tips

**Audience**: New Saturn developers, quick reference

---

### ARCHITECTURE.md
**Purpose**: Technical deep dive
**Size**: ~13KB

**Sections**:
1. Control flow inversion (diagrams)
2. Memory layout (Saturn RAM map)
3. Display implementation details
4. Input implementation details
5. Color system (conversion algorithm)
6. VDP1 architecture
7. Performance profiling
8. Thread safety (dual SH-2)
9. Future improvements

**Audience**: Advanced developers, optimization work

---

### FILES.md
**Purpose**: This file - complete file reference
**Size**: ~2KB

**Sections**:
- Core implementation files
- Build system files
- Documentation files
- Example code
- Scripts

**Audience**: All developers (navigation aid)

---

## Example Code

### example_main.c
**Purpose**: Demonstration application
**Size**: ~6.1KB
**Dependencies**: `jo/jo.h`, `saturn_pal.h`

**Features**:
- Complete working example
- Shows frame callback pattern
- Demonstrates input polling
- Tests rectangle drawing
- Displays controller info
- Includes usage notes

**Output**:
- Title and frame counter
- Display dimensions (40x28)
- Last button pressed
- Three colored rectangles
- Control instructions

**Usage**:
```bash
# Copy to Jo Engine project
cp example_main.c ~/saturn_project/src/main.c
cd ~/saturn_project
make
mednafen -force_module ss game.cue
```

---

## Scripts

### ../../scripts/test-saturn.sh
**Purpose**: Build and test automation
**Size**: ~4.5KB
**Location**: `/Users/r11/Projects/cui/scripts/test-saturn.sh`

**Features**:
- Auto-detect Jo Engine installation
- Clean and build Saturn PAL
- Find disc image
- Launch in emulator (Mednafen/Yabause/SSF)
- Colored output for status messages

**Usage**:
```bash
# Build only
./scripts/test-saturn.sh

# Build and run
./scripts/test-saturn.sh --run

# With custom Jo Engine path
export JO_ENGINE_SRC_DIR=~/saturn/joengine
./scripts/test-saturn.sh --run
```

**Requirements**:
- Bash shell
- Jo Engine installed
- SH-2 toolchain in PATH
- Emulator installed (optional, for --run)

---

## File Tree

```
pal/saturn/
├── saturn_pal.h        # Public API header
├── saturn_pal.c        # Implementation
├── Makefile            # Build configuration
├── .gitignore          # Git exclusions
├── README.md           # Main documentation
├── QUICKSTART.md       # Quick start guide
├── ARCHITECTURE.md     # Technical deep dive
├── FILES.md            # This file
└── example_main.c      # Example application

scripts/
└── test-saturn.sh      # Build/test script
```

---

## Dependencies

### Required
- **Jo Engine** (https://jo-engine.org/)
  - Provides: `jo/jo.h`, `Makefile.common`
  - Version: Any recent (tested with 2023+)

- **SH-2 Toolchain**
  - Provides: `sh-elf-gcc`, `sh-elf-ld`
  - Typically bundled with Jo Engine

- **cui Core**
  - `core/include/cui_pal.h`
  - `core/include/cui_types.h`

### Optional
- **Mednafen** - Emulator (recommended for testing)
- **Yabause** - Alternative emulator
- **SSF** - Windows-only emulator

---

## Integration Checklist

When integrating Saturn PAL into your project:

- [ ] Copy `saturn_pal.h` and `saturn_pal.c` to project
- [ ] Copy/adapt `Makefile` for your project structure
- [ ] Include cui core headers (`cui_pal.h`, `cui_types.h`)
- [ ] Install Jo Engine (set `JO_ENGINE_SRC_DIR`)
- [ ] Install SH-2 toolchain (or use Jo Engine's)
- [ ] Create `jo_main()` entry point
- [ ] Call `jo_core_init()` first
- [ ] Register platform: `cui_pal_register(cui_saturn_platform())`
- [ ] Initialize: `cui_pal_init()`
- [ ] Set frame callback: `cui_saturn_set_frame_callback()`
- [ ] Start loop: `cui_saturn_run()`
- [ ] Test with `example_main.c` first
- [ ] Install emulator for testing

---

## Testing

### Unit Tests
Not applicable - Saturn code must run on Saturn hardware or emulator.

### Integration Tests
Use `example_main.c` to verify:
- Display initialization
- Input polling (all buttons)
- Text rendering
- Rectangle drawing
- Frame callback system

### Emulator Testing
```bash
# Mednafen (most accurate)
mednafen -force_module ss cui_saturn.cue

# Yabause
yabause -i cui_saturn.cue

# SSF (Windows)
ssf.exe cui_saturn.cue
```

### Real Hardware Testing
- Burn to CD-R (requires mod chip)
- Use Satiator/Rhea/Phoebe ODE
- Load via USB dev cart

---

## Known Issues

See README.md "Known Limitations" section:

1. Text color ignored (jo_printf limitation)
2. No alpha transparency (RGB555)
3. QUIT doesn't exit (console behavior)
4. Rectangle drawing uses characters (not optimal)
5. Fixed display dimensions (40x28)

---

## Future Work

See ARCHITECTURE.md "Future Improvements" section:

1. Sprite-based rectangles (better performance)
2. Custom font system (colored text)
3. VDP2 background integration
4. Palette-based rendering (less VRAM)

---

## Version History

- **v1.0** (2026-02-01) - Initial implementation
  - Jo Engine integration
  - Basic display/input functions
  - Character-based rectangles
  - Complete documentation

---

## License

Part of cui component library. See project root LICENSE.

---

## Contact

For issues or questions:
- Check documentation first (README.md, QUICKSTART.md, ARCHITECTURE.md)
- Review example code (example_main.c)
- Test with example before reporting issues
- Include disc image and emulator version in bug reports
