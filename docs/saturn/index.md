# Saturn PAL - Complete Index

**Quick Navigation for Saturn Platform Abstraction Layer**

---

## Start Here

**New to Saturn development?**
→ Start with [QUICKSTART.md](QUICKSTART.md) (5 minutes)

**Ready to integrate?**
→ Read [README.md](README.md) (15 minutes)

**Need code example?**
→ See [example_main.c](example_main.c) (working demo)

---

## Documentation Map

### For Getting Started
```
QUICKSTART.md
  ↓
README.md
  ↓
example_main.c
  ↓
Build & Test!
```

### For Integration
```
README.md (patterns)
  ↓
saturn_pal.h (API)
  ↓
example_main.c (reference)
  ↓
COMPATIBILITY.md (limitations)
```

### For Deep Understanding
```
ARCHITECTURE.md (technical details)
  ↓
COMPATIBILITY.md (feature matrix)
  ↓
saturn_pal.c (implementation)
```

---

## File Reference

### Implementation Files

| File | Purpose | Size | Read When |
|------|---------|------|-----------|
| **saturn_pal.h** | Public API header | 2.3KB | Integrating into project |
| **saturn_pal.c** | Implementation | 9.9KB | Understanding internals |
| **Makefile** | Build config | 3.2KB | Setting up build |
| **.gitignore** | Git exclusions | 300B | Creating repo |

### Documentation Files

| File | Purpose | Size | Read When |
|------|---------|------|-----------|
| **QUICKSTART.md** | 30-second setup | 4.8KB | First time user |
| **README.md** | Main documentation | 6.6KB | General usage |
| **ARCHITECTURE.md** | Technical deep dive | 13KB | Optimizing/debugging |
| **COMPATIBILITY.md** | Feature matrix | 13KB | Checking limitations |
| **FILES.md** | File reference | 8.1KB | Navigating codebase |
| **IMPLEMENTATION_SUMMARY.md** | Completion report | 11KB | Understanding scope |
| **INDEX.md** | This file | 3KB | Navigation |

### Example Files

| File | Purpose | Size | Read When |
|------|---------|------|-----------|
| **example_main.c** | Working demo | 6.1KB | Learning integration |

### Build Scripts

| File | Purpose | Size | Location |
|------|---------|------|----------|
| **test-saturn.sh** | Build automation | 4.5KB | `../../scripts/` |

---

## Quick Reference

### API Functions

```c
// Get platform
const cui_platform_t* cui_saturn_platform(void);

// Set frame callback (60Hz)
void cui_saturn_set_frame_callback(cui_saturn_frame_callback_t callback);

// Start main loop (never returns)
void cui_saturn_run(void);
```

See [saturn_pal.h](saturn_pal.h) for full API.

### Integration Pattern

```c
#include <jo/jo.h>
#include "saturn_pal.h"

void my_frame(void) {
    cui_input_action_t action = CUI_INPUT()->poll();
    /* handle and render */
}

void jo_main(void) {
    jo_core_init(JO_COLOR_Black);
    cui_pal_register(cui_saturn_platform());
    cui_pal_init();

    cui_saturn_set_frame_callback(my_frame);
    cui_saturn_run();  /* never returns */
}
```

See [example_main.c](example_main.c) for complete example.

### Build Commands

```bash
# Build
make

# Build and test
./../../scripts/test-saturn.sh --run

# Clean
make clean
```

See [Makefile](Makefile) for all targets.

---

## Documentation by Task

### I want to...

**...get started quickly**
→ [QUICKSTART.md](QUICKSTART.md) - 5 minute guide

**...understand the architecture**
→ [ARCHITECTURE.md](ARCHITECTURE.md) - Technical details

**...know what features work**
→ [COMPATIBILITY.md](COMPATIBILITY.md) - Feature matrix

**...integrate into my project**
→ [README.md](README.md) + [example_main.c](example_main.c)

**...see what files do**
→ [FILES.md](FILES.md) - Complete file reference

**...know what was implemented**
→ [IMPLEMENTATION_SUMMARY.md](IMPLEMENTATION_SUMMARY.md)

**...find a specific file**
→ [INDEX.md](INDEX.md) - This file

**...build and test**
→ [Makefile](Makefile) + [test-saturn.sh](../../scripts/test-saturn.sh)

---

## Documentation by Role

### New Developer (First Time)
1. [QUICKSTART.md](QUICKSTART.md) - Get running fast
2. [example_main.c](example_main.c) - See it in action
3. [README.md](README.md) - Learn the details

### Integrator (Putting it in a project)
1. [README.md](README.md) - Integration patterns
2. [saturn_pal.h](saturn_pal.h) - API reference
3. [COMPATIBILITY.md](COMPATIBILITY.md) - Limitations
4. [example_main.c](example_main.c) - Template code

### Maintainer (Working on implementation)
1. [ARCHITECTURE.md](ARCHITECTURE.md) - Design decisions
2. [saturn_pal.c](saturn_pal.c) - Implementation
3. [COMPATIBILITY.md](COMPATIBILITY.md) - Current state
4. [IMPLEMENTATION_SUMMARY.md](IMPLEMENTATION_SUMMARY.md) - Scope

### QA/Tester (Verifying functionality)
1. [COMPATIBILITY.md](COMPATIBILITY.md) - What to test
2. [example_main.c](example_main.c) - Test application
3. [test-saturn.sh](../../scripts/test-saturn.sh) - Test script
4. [README.md](README.md) - Expected behavior

---

## Key Concepts

### Control Flow Inversion

cui is poll-based, Saturn/Jo Engine is callback-based.

**Solution**: Saturn PAL inverts control - Jo Engine drives cui through callbacks.

See: [ARCHITECTURE.md § Control Flow Inversion](ARCHITECTURE.md#control-flow-inversion)

### Known Limitations

1. **Text color ignored** - jo_printf is white only
2. **Rectangle accuracy** - ±8 pixels (character grid)
3. **No alpha transparency** - RGB555 format
4. **Cannot exit** - Console limitation

See: [COMPATIBILITY.md § Known Limitations](COMPATIBILITY.md#known-limitations)

### Color Conversion

cui: 32-bit RGBA → Saturn: 15-bit RGB555

Precision loss: 256 levels → 32 levels per channel

See: [ARCHITECTURE.md § Color System](ARCHITECTURE.md#color-system)

### Performance

- **Frame budget**: 16.67ms @ 60Hz
- **Rect limit**: ~50-100 per frame
- **Sprite limit**: ~1200-1300 per frame

See: [COMPATIBILITY.md § Performance Characteristics](COMPATIBILITY.md#performance-characteristics)

---

## Reading Order by Goal

### Goal: Get Running ASAP
1. [QUICKSTART.md](QUICKSTART.md) - Setup
2. [example_main.c](example_main.c) - Copy template
3. Build and test!

### Goal: Integrate into Existing Project
1. [README.md](README.md) - Understand approach
2. [saturn_pal.h](saturn_pal.h) - API reference
3. [example_main.c](example_main.c) - Integration pattern
4. [COMPATIBILITY.md](COMPATIBILITY.md) - Check limitations
5. [Makefile](Makefile) - Adapt build system

### Goal: Optimize Performance
1. [ARCHITECTURE.md](ARCHITECTURE.md) - Technical details
2. [COMPATIBILITY.md](COMPATIBILITY.md) - Performance limits
3. [saturn_pal.c](saturn_pal.c) - Implementation details
4. Profile and optimize!

### Goal: Understand Scope
1. [IMPLEMENTATION_SUMMARY.md](IMPLEMENTATION_SUMMARY.md) - What was built
2. [FILES.md](FILES.md) - What's included
3. [COMPATIBILITY.md](COMPATIBILITY.md) - What works

### Goal: Debug Issues
1. [QUICKSTART.md § Debugging](QUICKSTART.md#debugging) - Common issues
2. [COMPATIBILITY.md](COMPATIBILITY.md) - Known limitations
3. [ARCHITECTURE.md](ARCHITECTURE.md) - How it works
4. [example_main.c](example_main.c) - Known working code

---

## External Resources

### Jo Engine
- **Website**: https://jo-engine.org/
- **Documentation**: https://jo-engine.org/doxygen/
- **GitHub**: https://github.com/johannes-fetz/joengine

### Saturn Development
- **Architecture**: https://www.copetti.org/writings/consoles/sega-saturn/
- **Hardware**: http://antime.kapsi.fi/sega/docs.html

### Emulators
- **Mednafen**: https://mednafen.github.io/ (recommended)
- **Yabause**: https://yabause.org/

See [README.md § Resources](README.md#resources) for complete list.

---

## Statistics

### Implementation
- **Code**: 12.2KB (saturn_pal.{h,c})
- **Build**: 3.2KB (Makefile)
- **Example**: 6.1KB (example_main.c)
- **Scripts**: 4.5KB (test-saturn.sh)

### Documentation
- **Total**: 65KB across 7 files
- **QUICKSTART.md**: 4.8KB (getting started)
- **README.md**: 6.6KB (main docs)
- **ARCHITECTURE.md**: 13KB (technical)
- **COMPATIBILITY.md**: 13KB (features)
- **FILES.md**: 8.1KB (reference)
- **IMPLEMENTATION_SUMMARY.md**: 11KB (completion)
- **INDEX.md**: 3KB (this file)

### Grand Total
- **10 files** in `pal/saturn/`
- **1 script** in `scripts/`
- **~90KB** total (26KB code, 65KB docs)

---

## Version Info

- **Version**: 1.0
- **Date**: 2026-02-01
- **Status**: Production Ready
- **Author**: pal-saturn-agent
- **SDK**: Jo Engine
- **Platform**: Sega Saturn

---

## Quick Links

| What | Where |
|------|-------|
| **Getting Started** | [QUICKSTART.md](QUICKSTART.md) |
| **Main Docs** | [README.md](README.md) |
| **API Reference** | [saturn_pal.h](saturn_pal.h) |
| **Example Code** | [example_main.c](example_main.c) |
| **Technical Details** | [ARCHITECTURE.md](ARCHITECTURE.md) |
| **Feature Matrix** | [COMPATIBILITY.md](COMPATIBILITY.md) |
| **File Reference** | [FILES.md](FILES.md) |
| **Implementation Summary** | [IMPLEMENTATION_SUMMARY.md](IMPLEMENTATION_SUMMARY.md) |
| **Build Config** | [Makefile](Makefile) |
| **Test Script** | [../../scripts/test-saturn.sh](../../scripts/test-saturn.sh) |

---

## License

Part of cui component library. See project root LICENSE.

---

**End of Index - Choose your path above!**
