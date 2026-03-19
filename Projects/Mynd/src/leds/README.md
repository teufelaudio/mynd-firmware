# LED Pattern Guide (Create + Play)

This guide is for the RPi firmware LED path (`leds_rpi.cpp`).

## Files You Will Edit

- API and public enum: `Projects/Mynd/src/leds/leds.h`
- Pattern definitions + engine wiring: `Projects/Mynd/src/leds/leds_rpi.cpp`
- Optional daemon trigger bridge: `Projects/Mynd/src/tasks/rpi/task_rpi.cpp`
- Optional daemon sender: `Projects/Mynd/src/tasks/rpi/daemon_install/mynd_rpi_link.py`
- Optional protocol enum for daemon-triggered patterns: `Projects/Mynd/external/teufel/libs/actionslink/proto/rpi/leds.proto`

## How Patterns Are Built

In `leds_rpi.cpp`, patterns are composed in layers:

1. **Time base**
   - `PATTERN_TICK_MS` (currently `25 ms`)
   - `PATTERN_MS_TO_STEPS(ms)` to convert duration -> table length
2. **Primitive segments**
   - `PatternConst` (flat brightness over N steps)
   - `PatternFn` (table-based curve/ramp)
3. **Sequence**
   - `IndicationEngine::Pattern` (ordered list of segments)
4. **LED/color binding**
   - `PatternGeneric<...>` (single-channel or fixed mask)
   - `PatternGenericRgb<...>` (RGB curves; used for breathing color variants)
5. **Runtime selection**
   - `set_source_pattern(SourcePattern pattern)` dispatches to engine calls:
     - `run_once...`
     - `run_few(...)`
     - `run_inf...`

## Fast Path: Build a New Pattern from Existing Components

Use this when you want a new behavior but can reuse existing building blocks.

### Worked example: `CustomNotify` = triple blue pulse + smooth fade-out

### 1) Add enum entry (`leds.h`)

```cpp
CustomNotify,
```

### 2) Define pattern pieces (`leds_rpi.cpp`)

This creates a distinctive 3-pulse one-shot pattern:

```cpp
// Short pulse timing for this custom pattern
static IndicationEngine::PatternConst s_custom_on(255, PATTERN_MS_TO_STEPS(120));
static IndicationEngine::PatternConst s_custom_off(0,   PATTERN_MS_TO_STEPS(80));

// 3x pulse sequence
static IndicationEngine::Pattern s_custom_triple_pulse {
    &s_custom_on, &s_custom_off,
    &s_custom_on, &s_custom_off,
    &s_custom_on, &s_custom_off,
};

// Bind sequence to source LED color/mask
static PatternGeneric<6, RGB_LED, BLUE_LED> s_custom_notify(s_custom_triple_pulse);
static PatternGeneric<1, RGB_LED, BLUE_LED> s_custom_notify_ramp_down(s_fast_ramp_to_off);
```

Note: `PatternGeneric<N, ...>` must use the exact number of segments in the underlying
`IndicationEngine::Pattern`. The `s_custom_triple_pulse` example has 6 segments (`on/off` x3), so `N=6`.
In `PatternGeneric<N, RGB_LED, BLUE_LED>`:
- `RGB_LED` is the LED channel count (`3`, for R/G/B).
- `BLUE_LED` is the channel mask, so only blue is driven by this pattern.
- Combine masks for mixed colors, e.g. `RED_LED | GREEN_LED` (yellow) or
  `RED_LED | GREEN_LED | BLUE_LED` (white/full RGB).

### 3) Wire in dispatcher (`set_source_pattern(...)`)

```cpp
case SourcePattern::CustomNotify:
    s_source_led_engine.run_once_with_postload(s_custom_notify, s_custom_notify_ramp_down);
    break;
```

### 4) Play it

```cpp
Teufel::Task::Leds::set_source_pattern(Teufel::Task::Leds::SourcePattern::CustomNotify);
```

## Full Path: Create a Completely New Pattern (New Waveform)

Use this when existing segments are not enough and you need a custom pulse/ramp shape.

### Step 1: Add a new brightness table

In `leds_rpi.cpp`, add table storage with your duration:

```cpp
static std::array<uint8_t, PATTERN_MS_TO_STEPS(1200)> s_custom_wave_table{0};
```

### Step 2: Fill table values

Populate in the same init area where other tables are prepared (follow the existing table-fill style used for ramps/breathing):

```cpp
for (size_t i = 0; i < s_custom_wave_table.size(); ++i)
{
    const float x = static_cast<float>(i) / static_cast<float>(s_custom_wave_table.size() - 1);

    // "Comet" shape:
    // - fast bright attack in first 20%
    // - slow decay for the rest
    if (x < 0.2f)
    {
        const float attack = x / 0.2f;
        s_custom_wave_table[i] = static_cast<uint8_t>(255.0f * linear_up(attack));
    }
    else
    {
        const float decay = (x - 0.2f) / 0.8f;
        s_custom_wave_table[i] = static_cast<uint8_t>(255.0f * cubic_curve_down(decay));
    }
}
```

### Step 3: Turn table into a segment and pattern

```cpp
static IndicationEngine::PatternFn s_custom_wave_fn(s_custom_wave_table);

static IndicationEngine::Pattern s_custom_wave {
    &s_custom_wave_fn,
    &s_half_second_off,   // optional tail segment
};
```

### Step 4: Bind to LED mask/color

```cpp
static PatternGeneric<2, RGB_LED, PURPLE_LED> s_custom_wave_pattern(s_custom_wave);
```

### Step 5: Add enum + dispatch case + trigger

Same as Fast Path:
- add `SourcePattern::CustomWave`
- handle in `set_source_pattern(...)`
- call `set_source_pattern(...)` from your task/event logic

## Which Engine Method to Use

- `run_once_with_preload(...)`: one-shot feedback blink with clean pre-off
- `run_once_transient(...)`: one-shot that should not permanently replace long-running state
- `run_few(pattern, n)`: repeat finite feedback count
- `run_inf(...)`: continuous state indication
- `run_inf_with_preload_and_postload(...)`: continuous state with smooth entry/exit

## Play From Daemon (Current Existing Path)

Current daemon-triggered protocol supports:
- `POSITIVE_FEEDBACK`
- `NEGATIVE_FEEDBACK`

Daemon sender (`mynd_rpi_link.py`) already does:

```python
event = message_pb.ToMcuEvent()
event.notify_play_led_pattern.pattern = leds_pb.PlayLedPattern.Pattern.POSITIVE_FEEDBACK
await client.send_event(event)
```

MCU receives in `task_rpi.cpp` at `.on_notify_play_led_pattern` and maps to:
- `SourcePattern::PositiveFeedback`
- `SourcePattern::NegativeFeedback`

## Add a New Daemon-Triggered Pattern

1. Add enum value to `rpi/leds.proto` (`PlayLedPattern.Pattern`).
2. Regenerate:

```bash
bash Projects/Mynd/external/teufel/libs/actionslink/scripts/generate_proto.sh
```

3. Update MCU `.on_notify_play_led_pattern` switch mapping.
4. Update daemon sender to emit new enum value.
5. Build/flash/test.

Note: after proto regen, use the exact generated symbol names from generated headers/modules.

## Minimal Validation

1. Build + flash MCU.
2. Trigger pattern from firmware (and daemon path if relevant).
3. Verify entry/exit transitions look correct (no abrupt clipping unless intended).
4. Verify it does not unintentionally override higher-priority long-running indications.
