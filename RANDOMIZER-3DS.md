# Randomizer settings on 3DS

Open the randomizer menu from file select, then choose **All settings**.
The categories use the desktop randomizer's setting definitions, including
starting inventory, starting hearts, logic tricks and location exclusions.
Any registered seed setting outside a desktop category appears under
**Other settings**. The **Randomizer enhancements** category also exposes the
separate desktop randomizer quality-of-life controls. **Randomize settings**
on the setup screen applies the desktop randomize-all action.

**Reset to defaults** on the setup screen restores seed settings (including
three starting hearts), clears enabled tricks and location exclusions, and
restores randomizer enhancement defaults. Changes save immediately. Use **Back**
in the header or **B** to leave the menu. Reset is disabled during generation;
existing seeds and saves keep their own settings.

- **D-pad up/down:** select a row, automatically moving between pages.
- **D-pad left/right:** change the selected setting.
- **A:** open a category or read the selected setting's full name, current
  value and description.
- **B:** return to the previous screen.
- **Touch:** select categories; use the arrows on a setting row to change its
  value, or tap its name for details. The header has Back, Prev and Next buttons.

Settings save when changed and apply to the next generated seed. Presets still
work and replace settings according to their definitions. Imported seeds and
existing saves retain their own seed settings.

Unavailable settings remain listed in grey; their details explain the
restriction when the desktop registry supplies one. Changes are blocked while
a seed is generating. The generator still applies its normal validity checks.

Find **Starting Hearts** under **Starting Inventory / Other**. It displays actual heart counts: the default is **3**, while
its stored setting is the zero-based index **2**.

## Generation performance

Reachability filtering uses a compact membership lookup while preserving the
location order used by placement. The 3DS build also compiles `fill.cpp`,
`logic.cpp` and `location_access.cpp` with `-O2`; the wider port retains `-Os`.

Azahar New 3DS benchmarks with seed `3DSTEST`, fresh configuration and the
existing coarse generation timer measured:

| Settings | Before | Optimized | Reduction |
| --- | ---: | ---: | ---: |
| Standard | 26.4 s | 19.1 s | 28% |
| Defaults | 21.3 s | 17.5 s | 18% |

These are emulated generator times, excluding startup. Host load affects elapsed
wall time, and physical-console timings remain to be measured. Complete spoiler
JSON contents matched before and after for both presets, including three starting
hearts. The lookup alone reduced Standard to 20.7 seconds.

Host checks:

```sh
python3 tests/randomizer_boot_settings_3ds_test.py
python3 tests/randomizer_menu_3ds_test.py
python3 tests/randomizer_settings_3ds_test.py
python3 tests/randomizer_reachability_filter_test.py
```

Build with `bash scripts/build-3ds.sh`. Hardware validation should check text
readability, touch input, persistence after restart, and a newly generated save
using a changed starting-heart count.
