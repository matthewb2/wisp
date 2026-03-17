# LibCSS Parser-Cascade Architecture

## Overview

LibCSS uses a three-layer architecture for processing CSS properties:

```
Parser → Bytecode → Cascade → CSS Units → Layout
```

## Layer Responsibilities

### 1. Parser Layer
**Location**: `src/parse/properties/*.c`

**Responsibility**: Parse CSS text and emit bytecode

**Unit Handling**: 
- Uses `css__parse_unit_specifier()` to identify unit keywords (e.g., "fr", "px")
- Emits **raw bytecode constants** from `bytecode/bytecode.h`
- **MUST NOT** convert to CSS_UNIT_* enums

**Example Bytecode Constants**:
```c
// From bytecode/bytecode.h
UNIT_PX = (1u << 8) + 0  = 256
UNIT_EM = (1u << 8) + 1  = 257
UNIT_FR = (1u << 8) + 23 = 279
```

### 2. Cascade Layer
**Location**: `src/select/properties/*.c`

**Responsibility**: Read bytecode and populate computed style

**Unit Handling**:
- Reads raw bytecode values from style bytecode
- Calls `css__to_css_unit(bytecode_value)` to convert
- Stores CSS_UNIT_* enum values in computed style structures

**Conversion Function** (`src/select/helpers.h`):
```c
static inline css_unit css__to_css_unit(uint32_t u)
{
    switch (u) {
    case UNIT_PX:  // 256
        return CSS_UNIT_PX;  // 0
    case UNIT_EM:  // 257
        return CSS_UNIT_EM;  // 1
    case UNIT_FR:  // 279
        return CSS_UNIT_FR;  // 23
    // ...
    }
}
```

### 3. Layout Layer
**Location**: `src/content/handlers/html/layout*.c`

**Responsibility**: Use computed styles for rendering

**Unit Handling**:
- Reads CSS_UNIT_* enum values from computed style
- Compares against CSS_UNIT_* constants (e.g., `if (unit == CSS_UNIT_FR)`)
- Performs layout calculations based on unit type

## Common Pitfall: Premature Conversion

**WRONG** - Converting in parser:
```c
// In parser (grid_template_columns.c)
track->unit = CSS_UNIT_FR;  // ❌ Wrong layer!
track->unit = track->unit & 0xFF;  // ❌ Truncates bytecode!
```

**CORRECT** - Emitting bytecode:
```c
// In parser (grid_template_columns.c)
track->unit = UNIT_FR;  // ✅ Raw bytecode (279)

// In cascade (select/properties/grid_template_columns.c)
tracks[i].unit = css__to_css_unit(raw_unit);  // ✅ Convert here (279 → 23)
```

## Unit Value Ranges

**Bytecode Units** (parser layer):
- Range: 256-511 (high byte set)
- Format: `(1u << 8) + offset`
- Examples: UNIT_PX=256, UNIT_FR=279

**CSS Units** (cascade/layout layers):
- Range: 0-255 (low byte only)
- Format: Simple enum values
- Examples: CSS_UNIT_PX=0, CSS_UNIT_FR=23

## Debugging Tips

When debugging unit handling issues:

1. **Add logging in parser** to verify bytecode emission:
   ```c
   fprintf(stderr, "Parser emitting: raw_unit=%u\n", raw_unit);
   ```

2. **Add logging in cascade** to verify conversion:
   ```c
   fprintf(stderr, "Cascade converting: %u -> %d\n", 
           raw_unit, css__to_css_unit(raw_unit));
   ```

3. **Add logging in layout** to verify final values:
   ```c
   NSLOG(netsurf, INFO, "Layout using: unit=%d (FR=%d PX=%d)",
         track->unit, CSS_UNIT_FR, CSS_UNIT_PX);
   ```

## Related Files

- `src/bytecode/bytecode.h` - Bytecode unit constants
- `src/select/helpers.h` - `css__to_css_unit()` conversion
- `src/parse/properties/utils.c` - `css__parse_unit_specifier()`
- `include/libcss/types.h` - CSS_UNIT_* enum definitions
