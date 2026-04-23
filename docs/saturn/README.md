# Saturn Platform Documentation

Saturn platform implementation for the cui library.

## Quick Navigation

| Document | Purpose |
|----------|---------|
| [quickstart.md](quickstart.md) | 5-minute setup guide |
| [architecture.md](architecture.md) | Technical deep dive into PAL implementation |
| [architecture_plan.md](architecture_plan.md) | Strategic roadmap for Saturn capabilities |
| [compatibility.md](compatibility.md) | Feature matrix and known limitations |
| [implementation.md](implementation.md) | Implementation summary and scope |
| [files.md](files.md) | File structure reference |
| [example_main.c](example_main.c) | Working Saturn demo code |
| [visual_safe_area.md](visual_safe_area.md) | Safe area visual test spec |

## Quick Reference

### API

```c
const cui_platform_t* cui_saturn_platform(void);
void cui_saturn_set_frame_callback(cui_saturn_frame_callback_t callback);
void cui_saturn_run(void);  /* never returns */
```

### Build

```bash
./build storybook/saturn        # build storybook ROM
./run storybook/saturn           # run in emulator
./build tests/saturn             # build Saturn tests
```

### Key Constraints

- Resolution: 320x224 (40x28 character grid)
- Color: RGB555 (15-bit, no alpha)
- Static allocation only (no malloc)

## Source Files

Source code lives in [`pal/saturn/`](../../pal/saturn/).
