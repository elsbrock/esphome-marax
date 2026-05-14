# LVGL 9 port for the Mara X chart helpers

## Intent

The chart helpers in `includes/chart_helpers.h` and `includes/chart_draw.h`
target LVGL 8. ESPHome's current `lvgl` component now pulls LVGL 9 from
the ESP-IDF component registry on every fresh build, which makes the
release CI fail across our chart code. Local builds succeed only because
`.piolibdeps/lvgl@8.4.0` is cached from an older ESPHome that used the
PlatformIO library system.

Goal: get the firmware compiling cleanly against LVGL 9 so the CI
release flow works end-to-end, the install button on the Pages site can
actually serve a binary, and we stop holding an unstable LVGL pin.

Non-goals: rewriting the chart UI design, adding features, or changing
the data pipeline.

## Findings from the LVGL 9 audit

Most chart symbols we use survive verbatim in LVGL 9:

- `lv_chart_create`, `lv_chart_set_type`, `LV_CHART_TYPE_LINE`
- `lv_chart_add_series`, `lv_chart_series_t*`
- `LV_CHART_AXIS_PRIMARY_X / Y` (enum unchanged)
- `lv_chart_set_range`, `lv_chart_set_next_value`, `lv_chart_set_all_value`, `lv_chart_set_point_count`, `lv_chart_set_div_line_count`, `lv_chart_refresh`
- `LV_CHART_POINT_NONE`
- `lv_event_get_target`, `lv_obj_get_content_coords`, `lv_area_get_width`
- `lv_draw_rect_dsc_t`, `lv_draw_rect_dsc_init`

The genuine breakage is concentrated in three places:

1. **`LV_USE_CHART` ends up as 0 in the generated `lv_conf.h`.** ESPHome
   only auto-enables a widget when it appears in the YAML. We build the
   chart imperatively from C++ in `on_boot`, so ESPHome doesn't see it,
   generates `#define LV_USE_CHART 0`, and the chart functions get
   compiled out. Our previous fix was `-D LV_USE_CHART=1` in
   `build_flags`, which clashed with the generated define under
   `-Werror`.

2. **The draw-event model was rewritten.** `LV_EVENT_DRAW_PART_BEGIN/END`
   and the `lv_obj_draw_part_dsc_t*` passed via
   `lv_event_get_draw_part_dsc(e)` are gone. The v9 equivalents are
   `LV_EVENT_DRAW_MAIN_BEGIN` / `LV_EVENT_DRAW_MAIN` /
   `LV_EVENT_DRAW_MAIN_END` (drawing around the whole widget, layer
   accessed via `lv_event_get_layer(e)`) or
   `LV_EVENT_DRAW_TASK_ADDED` for per-task hooks. The `lv_draw_ctx_t*`
   that v8 callbacks used is replaced by `lv_layer_t*`.

3. **Chart tick labels are gone from the chart widget.**
   `lv_chart_set_axis_tick` and the `LV_PART_TICKS` draw path were
   removed in v9. The replacement is a separate `lv_scale` widget
   positioned alongside the chart, or custom label drawing during the
   chart's draw events.

Minor:

4. **`lv_obj_set_style_size(obj, value, selector)` is now 4-arg**
   `(obj, w, h, selector)`.

5. **`lv_event_get_target_obj(e)`** is the preferred typed accessor; the
   old `lv_event_get_target(e)` still works through a compat shim.

## Architecture after the port

The visible chart layout and behaviour stay the same:

- A 3-series line chart (steam / hx / target) drawn inside
  `id(graph_area)`.
- Brew window shading (blue) + 90s recovery shading (green) painted
  behind the series.
- Y-axis labels formatted as integer °C.
- X-axis labels formatted as relative seconds / minutes from now.
- Auto-switch between 1 s / 50 s and 15 s / 15 min windows.

What changes internally:

- `LV_USE_CHART` is force-enabled with `-U LV_USE_CHART -D LV_USE_CHART=1`
  in `platformio_options.build_flags`. The `-U` undefines the
  ESPHome-generated `0` so the `-D 1` doesn't trip `-Werror`. Eventually
  the right fix is to express the chart declaratively in the YAML so
  ESPHome enables `LV_USE_CHART` automatically, but that's outside this
  spec's scope.
- The shading callback in `chart_draw.h` switches from
  `LV_EVENT_DRAW_PART_END` + `dsc->draw_ctx` to
  `LV_EVENT_DRAW_MAIN_BEGIN` + `lv_event_get_layer(e)`. Drawing API is
  otherwise identical (`lv_draw_rect_dsc_t`, `lv_draw_rect`).
- The tick label callback is rewritten as a custom paint into the chart's
  layer during `LV_EVENT_DRAW_MAIN_END`. We compute tick positions from
  the chart's content area and current `is_high_res_mode()` setting and
  call `lv_draw_label` directly. This is more code than v8's per-tick
  formatter, but it's contained and keeps the existing visual.
- `lv_chart_set_axis_tick(...)` calls in `create_temp_chart` are
  removed; chart no longer asks LVGL for native ticks.
- `lv_obj_set_style_size(temp_chart, 0, LV_PART_INDICATOR)` becomes
  `lv_obj_set_style_size(temp_chart, 0, 0, LV_PART_INDICATOR)`.
- YAML changes:
  - `rotation: 90` moves from `display:` back into `lvgl:` (the form
    2026.4+ requires).
  - The `lib_deps: lvgl/lvgl@8.4.0` pin is dropped.
  - ESPHome `version:` pin in `.github/workflows/build.yml` is dropped
    (back to `latest`).

## Decisions & trade-offs

**Custom-drawn tick labels vs. `lv_scale` pairing.** Going with custom
draw because:

- `lv_scale` widgets need their geometry aligned by hand to the chart's
  content area; our chart auto-sizes within `id(graph_area)`, and
  duplicating that math in two places is fragile.
- Our tick labels are dynamic (mode-dependent for X, range-dependent
  for Y), which would mean calling `lv_scale_set_text_src` whenever the
  chart's range changes. With custom draw the labels are recomputed
  every paint, automatically staying in sync.

Downside: we're now responsible for label sizing, font selection and
positioning. Mitigation: keep the label style consistent with whatever
the chart's `LV_PART_TICKS` style was (small grey text, default font).

**Drop the LVGL pin entirely vs. keep as a safety net.** Dropping the
pin. Keeping it would mean carrying a v8-only safety net forever; the
goal of this work is to make v9 the supported version. Local rebuilds
will fetch v9 on next clean.

**Migration of `lv_event_get_target` to `_obj` variant.** Doing it
opportunistically while we're touching the file, but not creating
separate commits — it's a trivial rename guarded by compatibility
macros either way.

## Implementation steps

- [ ] **Force-enable LV_USE_CHART.** Replace the (currently-absent)
  `-D LV_USE_CHART=1` in `jc3248w535-marax.yaml`'s build_flags with
  `-U LV_USE_CHART` and `-D LV_USE_CHART=1` (both, in that order).
- [ ] **Port `chart_draw.h` shading.** Change registration from
  `LV_EVENT_DRAW_PART_END` to `LV_EVENT_DRAW_MAIN_BEGIN`. Replace
  `lv_event_get_draw_part_dsc(e)` + `dsc->draw_ctx` with
  `lv_event_get_layer(e)`. The shading-rect computation logic stays.
- [ ] **Port `chart_draw.h` tick labels.** Delete the v8 callback. Add a
  new `LV_EVENT_DRAW_MAIN_END` callback that draws X and Y tick labels
  with `lv_draw_label`, sourcing positions from the chart's content
  coords and the current range / resolution.
- [ ] **Update event registrations in `chart_helpers.h`.** Use the new
  event codes; both callbacks live on the same chart object.
- [ ] **Remove `lv_chart_set_axis_tick` calls** in `create_temp_chart`.
- [ ] **Fix `lv_obj_set_style_size`** to the 4-arg form.
- [ ] **YAML cleanup.** Move `rotation: 90` from `display:` to `lvgl:`;
  drop `lib_deps: lvgl/lvgl@8.4.0` from `platformio_options`.
- [ ] **CI cleanup.** Drop the `version: "2026.2.4"` pin in
  `.github/workflows/build.yml`.
- [ ] **Local verify.** Wipe `.esphome/` and `esphome compile`
  cleanly against current ESPHome.
- [ ] **CI verify.** Tag and watch the run.

## Risk and rollback

The local cached LVGL 8 build is the user's working device — until the
port lands and is flashed, that's the source of truth. If the v9 build
regresses on hardware, we revert the chart_draw.h and chart_helpers.h
changes and reinstate the LVGL pin; `git revert` of the port commit
should be one step.
