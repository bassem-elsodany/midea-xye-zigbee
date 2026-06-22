# Research Summary: XYE Protocol Header Analysis

**Date**: 2026-01-30 (updated 2026-06-10)  
**Issue**: Research the xye.h header and its usage to document xye headers more

## Executive Summary

This research analyzed wtahler's esphome-mideaXYE-rs485 implementation and compared it with the current ESPHome-Midea-XYE component to validate protocol definitions and convert unknown fields to known fields.

## Key Findings

### 1. Temperature Sensor Mappings (Now Documented)

Successfully mapped all temperature sensors with their actual purposes:

- **T1 (byte 11)**: Internal/inlet air temperature - room temperature
- **T2A (byte 12)**: Indoor coil inlet temperature - refrigerant entering evaporator
- **T2B (byte 13)**: Indoor coil outlet temperature - refrigerant leaving evaporator  
- **T3 (byte 14)**: Outdoor coil/ambient temperature

**Status**: ✅ Documented in code comments and PROTOCOL.md

### 2. Error Flags Clarification

Error flags at bytes 22-23 (Flags16 error_flags) correspond to E1/E2 error codes mentioned in wtahler's implementation:
- Byte 22: Error code E1 (low byte)
- Byte 23: Error code E2 (high byte)

**Status**: ✅ Documented in comments

### 3. Protocol Variations Identified

#### AUTO Mode Discrepancy
- **Current implementation**: AUTO = 0x80
- **wtahler implementation**: AUTO = 0x91 (0x80 | 0x10 | 0x01)
- **Analysis**: wtahler's value includes OP_MODE_AUTO_FLAG (0x10)
- **Recommendation**: Keep current 0x80 but document the variation

**Status**: ✅ Documented with note about 0x91 variation

#### FAN_LOW Discrepancy
- **Current implementation**: FAN_LOW = 0x04
- **wtahler implementation**: FAN_LOW = 0x03
- **Analysis**: Different units may use different values
- **Recommendation**: Keep current 0x04 but document the 0x03 variation

**Status**: ✅ Documented with note about 0x03 variation

### 4. Temperature Encoding

**Current implementation** uses formula:
```
celsius = (value - 0x28) / 2.0
encoded_value = (celsius * 2.0) + 0x28
```

**wtahler implementation** appears to use raw Fahrenheit values without encoding.

**Analysis**: The encoding may differ based on:
- Unit configuration (Celsius vs Fahrenheit display)
- Regional settings
- Unit model variations

**Status**: ✅ Documented both approaches in PROTOCOL.md

### 5. Message Structure Validation

All message structures validated against wtahler's implementation:
- ✅ 16-byte transmit messages (TX_MESSAGE_LENGTH)
- ✅ 32-byte receive messages (RX_MESSAGE_LENGTH)
- ✅ Header structure (6 bytes: preamble + 5-byte header)
- ✅ Field positions for mode, fan, temperature
- ✅ CRC calculation method

### 6. Unknown Fields — Partially Resolved

Analyzed all "unknown" fields in receive messages:
- Bytes 6 and 16 remain unknown
- **Byte 19** — confirmed as provisional compressor-running flag (`0x01` running, `0x00` idle).
  Implemented via `compressor_aware_action: true` which uses this flag for HA climate action.
  **Status**: ✅ Implemented and enabled by default
- **Byte 27** — hardware-dependent, steady within a given device (`0x00` on PNW ducted HP,
  `0x14` on C&H CH-36AHU). Likely a capability / model-class byte. **Status**: ⚠️ Unknown
- **Bytes 28-29** — two candidate interpretations from MidATRIX (May 2026):
  1. IDU EEV position (16-bit LE) — leading hypothesis; PNW values decode to 430-480 steps
  2. Oil Return Cycle counter (byte 28 only) — alternative; doesn't fit PNW capture cleanly
  Both unconfirmed. See PROTOCOL.md "Byte 27-29 observations" for full analysis.
  **Status**: ⚠️ Hypotheses documented, not yet confirmed

**Status**: ⚠️ Bytes 6, 16, 27 remain unknown; bytes 28-29 have research hypotheses

## Deliverables

### 1. Comprehensive Protocol Documentation
Created `PROTOCOL.md` with:
- Complete message structure tables
- Command reference
- Operation and fan mode tables
- Temperature encoding details
- Timer flags documentation
- Error handling guidelines
- Communication flow examples
- Known variations and issues
- References to all source implementations

**Location**: `esphome/components/midea_xye/PROTOCOL.md`

### 2. Enhanced Code Comments

Updated header files with:
- Protocol variation notes (AUTO/FAN_LOW)
- Temperature sensor purposes (T1-T3, T2A-T2B)
- Absolute byte position comments
- Temperature encoding formula with examples
- Error flag clarifications

**Files Updated**:
- `xye.h`: Operation mode, fan mode, and temperature encoding notes
- `xye_recv.h`: Enhanced QueryResponseData comments

### 3. Updated README Files

Added references to:
- New PROTOCOL.md documentation
- wtahler's implementation in acknowledgments

**Files Updated**:
- `README.md` (root)
- `esphome/components/midea_xye/README.md`

## Recommendations

### Immediate Actions (Completed)
- ✅ Document protocol variations (AUTO: 0x80 vs 0x91, FAN_LOW: 0x04 vs 0x03)
- ✅ Add comprehensive protocol reference
- ✅ Clarify temperature sensor purposes
- ✅ Document error flag meanings

### Resolved Since Initial Research (June 2026)

1. **FAN_LOW_ALT (0x03) bug** — Fixed: `get_climate_fan_mode()` now maps `0x03 → LOW`. Native
   test `test_get_climate_fan_mode.cpp` added to CI.

2. **Byte 15 Current 0xFF** — Resolved: `0xFF` is an IDU sentinel — the IDU does not measure
   compressor current locally. Current is an ODU-side quantity visible only on the S1/S2 bus
   (MidATRIX cross-reference confirms this). `current` sensor now commented out in config.

3. **Byte 19 compressor flag** — Implemented: `compressor_aware_action: true` uses this flag
   to derive the HA climate action from the actual compressor state rather than fan running.

4. **Bytes 28-29** — Two hypotheses documented in PROTOCOL.md v1.2 (MidATRIX, May 2026):
   IDU EEV position (16-bit LE, leading hypothesis) or Oil Return Cycle counter (alternative).
   See [PROTOCOL.md Byte 27-29 observations](PROTOCOL.md#byte-27-29-observations).

5. **Fan mode HA vs bus divergence** — Fixed: Added `sync_fan_mode_from_c0` option that reads
   the physical running speed from C0 byte 9 and updates HA fan_mode. Enabled for units that
   return `0xC5` on C4 extended query (C4 path `sync_fan_mode_from_device` remains `false`).
   Logic: `0x00` → no update (idle); bit-7 set → `AUTO`; else → `LOW/MEDIUM/HIGH`.

### Remaining Future Considerations

1. **Protocol Validation Testing**
   - Test with units that use AUTO = 0x91
   - Verify temperature encoding with both Celsius and Fahrenheit units

2. **Unknown Field Investigation**
   - Bytes 6, 16, 27 still unknown — monitor for patterns across hardware
   - Bytes 28-29: capture simultaneously with S1/S2 frame `0001_53` bytes 11+12 (EXV
     step count) to confirm EEV hypothesis, or log over a full ≥45-min compressor +
     oil-return cycle

3. **Current Reading**
   - If a unit model ever reports real current on byte 15 (not `0xFF`), document the
     scaling factor (S1/S2 reference uses `raw / 3.2` for Compressor_Actual_Amps).

## Conclusion

This research successfully:
- ✅ Validated current protocol implementation against wtahler's work
- ✅ Documented temperature sensor mappings (T1, T2A, T2B, T3)
- ✅ Identified and documented protocol variations (AUTO/FAN_LOW)
- ✅ Created comprehensive protocol documentation (PROTOCOL.md)
- ✅ Enhanced code comments with practical information
- ✅ Clarified error flag structure (E1/E2 codes)

**No code changes were required** - the current implementation is correct. The main contribution is comprehensive documentation that will help:
- Developers understand protocol variations
- Users troubleshoot issues with specific unit models
- Future contributors extend the implementation
- Anyone implementing similar protocols

All changes are documentation-only and compile successfully without issues.
