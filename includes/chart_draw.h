// Chart draw helpers for LVGL 9.
//
// LVGL 8's per-part draw events (LV_EVENT_DRAW_PART_BEGIN/END with
// lv_obj_draw_part_dsc_t) were removed in v9. We replace them with two
// callbacks on the chart's main-draw lifecycle:
//
//   chart_shading_cb     LV_EVENT_DRAW_MAIN_BEGIN — paints the brew /
//                         recovery shading bands behind the series.
//   chart_tick_label_cb  LV_EVENT_DRAW_MAIN_END   — paints the X and Y
//                         axis tick labels (replaces lv_chart_set_axis_tick
//                         which is gone in v9).
//
// Both callbacks pull the draw layer via lv_event_get_layer(e) and draw
// directly into it with the standard LVGL draw API.
#pragma once

// Y-axis range as last applied by update_chart_display; needed by the tick
// label callback to know what numeric labels to print. Defined in
// chart_helpers.h.
extern int32_t last_y_min;
extern int32_t last_y_max;

static void chart_shading_cb(lv_event_t* e) {
    lv_layer_t* layer = lv_event_get_layer(e);
    if (!layer) return;
    lv_obj_t* obj = lv_event_get_target_obj(e);
    if (!obj) return;

    uint32_t now_ms = millis();
    bool high = is_high_res_mode();
    uint32_t window_ms = (high ? HIGH_RES_WINDOW_SEC : LOW_RES_WINDOW_SEC) * 1000u;
    if (window_ms == 0) return;

    lv_area_t content;
    lv_obj_get_content_coords(obj, &content);
    int32_t plot_left = content.x1;
    int32_t plot_top = content.y1;
    int32_t plot_bottom = content.y2;
    int32_t plot_w = lv_area_get_width(&content);
    if (plot_w <= 0) return;

    // Clamp visible window start to 0 to avoid underflow early after boot
    uint32_t vis_start = (now_ms >= window_ms) ? (now_ms - window_ms) : 0u;

    const BrewEvent* ev = get_brew_events();
    int n = get_brew_event_count();

    // Pass 1: brew windows (blue)
    for (int i = 0; i < n; ++i) {
        uint32_t t0 = ev[i].start_ms;
        uint32_t t1 = ev[i].stop_ms > 0 ? ev[i].stop_ms : now_ms;
        if (t1 <= vis_start || t0 >= now_ms) continue;

        uint32_t sx_ms = t0 < vis_start ? vis_start : t0;
        uint32_t ex_ms = t1 > now_ms ? now_ms : t1;
        if (ex_ms <= sx_ms) continue;

        float sx_ratio = (float)(sx_ms - vis_start) / (float)window_ms;
        float ex_ratio = (float)(ex_ms - vis_start) / (float)window_ms;
        int32_t x1 = plot_left + (int32_t)(sx_ratio * plot_w);
        int32_t x2 = plot_left + (int32_t)(ex_ratio * plot_w);
        if (x2 <= x1) continue;
        lv_area_t sect = { .x1 = x1, .y1 = plot_top, .x2 = x2, .y2 = plot_bottom };
        lv_draw_rect_dsc_t rd; lv_draw_rect_dsc_init(&rd);
        // Pre-dimmed colour with full opacity — avoids alpha accumulation
        // when the chart repaints partially.
        rd.bg_color = lv_color_hex(0x0C264C); // ~30% of 0x2A7FFF over black
        rd.bg_opa = LV_OPA_COVER;
        rd.border_opa = LV_OPA_TRANSP;
        lv_draw_rect(layer, &rd, &sect);
    }

    // Pass 2: recovery windows (green). End on data-driven recovered_ms if
    // present, otherwise the nominal recovery duration. Clamp to next brew
    // start to avoid overlap.
    for (int i = 0; i < n; ++i) {
        if (ev[i].stop_ms == 0) continue;
        uint32_t nominal_end = ev[i].stop_ms + get_recovery_ms();
        uint32_t rec_end = (ev[i].recovered_ms > ev[i].stop_ms) ? ev[i].recovered_ms : nominal_end;
        if (rec_end <= vis_start) continue;
        uint32_t rx0 = ev[i].stop_ms < vis_start ? vis_start : ev[i].stop_ms;
        uint32_t rx1 = rec_end > now_ms ? now_ms : rec_end;
        if (i + 1 < n) {
            uint32_t next_start = ev[i+1].start_ms;
            if (next_start > 0 && next_start < rx1) rx1 = next_start;
        }
        if (rx1 <= rx0) continue;

        float rs = (float)(rx0 - vis_start) / (float)window_ms;
        float re = (float)(rx1 - vis_start) / (float)window_ms;
        int32_t rx1c = plot_left + (int32_t)(rs * plot_w);
        int32_t rx2c = plot_left + (int32_t)(re * plot_w);
        if (rx2c <= rx1c) continue;
        lv_area_t rsect = { .x1 = rx1c, .y1 = plot_top, .x2 = rx2c, .y2 = plot_bottom };
        lv_draw_rect_dsc_t rrd; lv_draw_rect_dsc_init(&rrd);
        rrd.bg_color = lv_color_hex(0x143015); // ~30% of 0x43A047 over black
        rrd.bg_opa = LV_OPA_COVER;
        rrd.border_opa = LV_OPA_TRANSP;
        lv_draw_rect(layer, &rrd, &rsect);
    }
}

// Custom-drawn X/Y axis tick labels. Replaces v8's per-part LV_PART_TICKS
// path (and lv_chart_set_axis_tick, both removed in v9). Registered on the
// chart's *parent*, not on the chart itself, so it can draw to the left of
// and below the chart — drawing from the chart's own event would be clipped
// to the chart's outer bounds.
// 6 X-axis labels (-50s..0s in high-res mode, or -15m..0m in low-res),
// 5 Y-axis labels stepped between the chart's current Y range.
static void chart_tick_label_cb(lv_event_t* e) {
    if (!temp_chart) return;
    lv_layer_t* layer = lv_event_get_layer(e);
    if (!layer) return;

    // Chart's absolute screen-space outer bounds.
    lv_area_t chart_area;
    lv_obj_get_coords(temp_chart, &chart_area);
    int32_t chart_left = chart_area.x1;
    int32_t chart_top = chart_area.y1;
    int32_t chart_right = chart_area.x2;
    int32_t chart_bottom = chart_area.y2;
    int32_t chart_w = chart_right - chart_left;
    int32_t chart_h = chart_bottom - chart_top;
    if (chart_w <= 0 || chart_h <= 0) return;

    lv_draw_label_dsc_t lbl;
    lv_draw_label_dsc_init(&lbl);
    lbl.color = lv_color_hex(0x888888);
    lbl.font = LV_FONT_DEFAULT;

    char text[16];

    // X axis: 6 labels evenly spaced along the chart's bottom edge, painted
    // just below the chart in the parent's gutter.
    const int n_x = 6;
    bool high = is_high_res_mode();
    for (int idx = 0; idx < n_x; idx++) {
        int32_t x_center = chart_left + idx * chart_w / (n_x - 1);
        if (high) {
            int sec = -((n_x - 1 - idx) * (HIGH_RES_WINDOW_SEC / (n_x - 1)));
            if (idx == n_x - 1) sec = 0;
            lv_snprintf(text, sizeof(text), "%ds", sec);
        } else {
            int minutes = -((n_x - 1 - idx) * ((LOW_RES_WINDOW_SEC / (n_x - 1)) / 60));
            if (idx == n_x - 1) minutes = 0;
            lv_snprintf(text, sizeof(text), "%dm", minutes);
        }
        lbl.text = text;
        lv_area_t area = {
            .x1 = (int32_t)(x_center - 18),
            .y1 = (int32_t)(chart_bottom + 4),
            .x2 = (int32_t)(x_center + 18),
            .y2 = (int32_t)(chart_bottom + 20),
        };
        lv_draw_label(layer, &lbl, &area);
    }

    // Y axis: 5 labels stepped from last_y_max (top) to last_y_min (bottom),
    // painted just to the left of the chart in the parent's gutter.
    if (last_y_max > last_y_min) {
        const int n_y = 5;
        for (int idx = 0; idx < n_y; idx++) {
            int32_t value = last_y_max - idx * (last_y_max - last_y_min) / (n_y - 1);
            int32_t y_center = chart_top + idx * chart_h / (n_y - 1);
            lv_snprintf(text, sizeof(text), "%d", (int)value);
            lbl.text = text;
            lv_area_t area = {
                .x1 = (int32_t)(chart_left - 30),
                .y1 = (int32_t)(y_center - 8),
                .x2 = (int32_t)(chart_left - 4),
                .y2 = (int32_t)(y_center + 8),
            };
            lv_draw_label(layer, &lbl, &area);
        }
    }
}
