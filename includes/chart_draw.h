// Chart draw helpers: tick labels and shaded sections.
// Two narrowly-scoped event handlers, each registered against the single
// LV_EVENT_DRAW_PART_* code it actually needs.
#pragma once

// Shading overlay (brew = blue, recovery = green) drawn after the chart main
// background/grid, before series. Registered on LV_EVENT_DRAW_PART_END only.
static void chart_shading_cb(lv_event_t* e) {
    lv_obj_draw_part_dsc_t* dsc = (lv_obj_draw_part_dsc_t*)lv_event_get_draw_part_dsc(e);
    if (!dsc || dsc->part != LV_PART_MAIN || !dsc->draw_ctx) return;

    uint32_t now_ms = millis();
    bool high = is_high_res_mode();
    uint32_t window_ms = (high ? HIGH_RES_WINDOW_SEC : LOW_RES_WINDOW_SEC) * 1000u;
    if (window_ms == 0) return;

    lv_obj_t* obj = lv_event_get_target(e);
    if (!obj) return;
    lv_area_t content;
    lv_obj_get_content_coords(obj, &content);
    lv_coord_t plot_left = content.x1;
    lv_coord_t plot_top = content.y1;
    lv_coord_t plot_bottom = content.y2;
    lv_coord_t plot_w = lv_area_get_width(&content);
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
        lv_coord_t x1 = plot_left + (lv_coord_t)(sx_ratio * plot_w);
        lv_coord_t x2 = plot_left + (lv_coord_t)(ex_ratio * plot_w);
        if (x2 <= x1) continue;
        lv_area_t sect = { .x1 = x1, .y1 = plot_top, .x2 = x2, .y2 = plot_bottom };
        lv_draw_rect_dsc_t rd; lv_draw_rect_dsc_init(&rd);
        // Use pre-dimmed color with cover blit to avoid alpha accumulation artifacts
        rd.bg_color = lv_color_hex(0x0C264C); // ~30% of 0x2A7FFF over black
        rd.bg_opa = LV_OPA_COVER;
        rd.border_opa = LV_OPA_TRANSP;
        lv_draw_rect(dsc->draw_ctx, &rd, &sect);
    }

    // Pass 2: recovery windows (green). End on data-driven recovered_ms if present,
    // otherwise fall back to nominal recovery duration. Clamp to next brew start to
    // avoid overlap.
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
        lv_coord_t rx1c = plot_left + (lv_coord_t)(rs * plot_w);
        lv_coord_t rx2c = plot_left + (lv_coord_t)(re * plot_w);
        if (rx2c <= rx1c) continue;
        lv_area_t rsect = { .x1 = rx1c, .y1 = plot_top, .x2 = rx2c, .y2 = plot_bottom };
        lv_draw_rect_dsc_t rrd; lv_draw_rect_dsc_init(&rrd);
        rrd.bg_color = lv_color_hex(0x143015); // ~30% of 0x43A047 over black
        rrd.bg_opa = LV_OPA_COVER;
        rrd.border_opa = LV_OPA_TRANSP;
        lv_draw_rect(dsc->draw_ctx, &rrd, &rsect);
    }
}

// Tick label formatter. Registered on LV_EVENT_DRAW_PART_BEGIN only.
static void chart_tick_label_cb(lv_event_t* e) {
    lv_obj_draw_part_dsc_t* dsc = (lv_obj_draw_part_dsc_t*)lv_event_get_draw_part_dsc(e);
    if (!dsc || dsc->part != LV_PART_TICKS || !dsc->text) return;

    if (dsc->id == LV_CHART_AXIS_PRIMARY_X) {
        int idx = (int)dsc->value;  // major tick index 0..5
        if (idx < 0) idx = 0; if (idx > 5) idx = 5;
        if (is_high_res_mode()) {
            int sec = -((5 - idx) * (HIGH_RES_WINDOW_SEC / 5));
            if (idx == 5) sec = 0;
            lv_snprintf(dsc->text, dsc->text_length, "%ds", sec);
        } else {
            int minutes = -((5 - idx) * ((LOW_RES_WINDOW_SEC / 5) / 60));
            if (idx == 5) minutes = 0;
            lv_snprintf(dsc->text, dsc->text_length, "%dm", minutes);
        }
        return;
    }
    if (dsc->id == LV_CHART_AXIS_PRIMARY_Y) {
        int v = (int)dsc->value;
        lv_snprintf(dsc->text, dsc->text_length, "%d", v);
    }
}
