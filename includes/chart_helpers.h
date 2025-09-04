#pragma once
#include "esphome.h"

// Temperature data storage
#define CHART_DATA_POINTS 60  // 5 minutes at 5-second intervals
static float steam_temps[CHART_DATA_POINTS];
static float hx_temps[CHART_DATA_POINTS];
static float target_temps[CHART_DATA_POINTS];
static uint32_t data_timestamps[CHART_DATA_POINTS];
static int data_index = 0;
static bool data_filled = false;

// Chart objects
static lv_obj_t* temp_chart = nullptr;
static lv_chart_series_t* steam_series = nullptr;
static lv_chart_series_t* hx_series = nullptr;
static lv_chart_series_t* target_series = nullptr;

// Forward declarations
void update_temp_chart();
void add_temp_data(float steam, float hx, float target);

// Generate animated test data (only when no UART data)
void generate_test_data(bool uart_connected) {
    // Only generate test data if no real UART connection
    if (uart_connected) return;
    
    static uint32_t last_update = 0;
    uint32_t now = millis();

    // Add new data point every 5 seconds
    if (now - last_update > 5000) {
        float time_offset = now / 10000.0f;  // Slow animation
        float steam = 120 + 15 * sin(time_offset);              // Steam: 105-135°C
        float hx = 95 + 10 * sin(time_offset + 1.57f);          // HX: 85-105°C
        float target = 100 + 5 * sin(time_offset + 3.14f);      // Target: 95-105°C

        add_temp_data(steam, hx, target);
        last_update = now;
    }
}

// Add temperature data point
void add_temp_data(float steam, float hx, float target) {
    steam_temps[data_index] = steam;
    hx_temps[data_index] = hx;
    target_temps[data_index] = target;
    data_timestamps[data_index] = millis();

    data_index = (data_index + 1) % CHART_DATA_POINTS;
    if (data_index == 0) data_filled = true;

    // Update chart if it exists
    if (temp_chart != nullptr) {
        update_temp_chart();
    }
}

// Find min/max values for auto-scaling
void get_temp_range(float& min_temp, float& max_temp) {
    min_temp = 999.0f;
    max_temp = 0.0f;

    int points = data_filled ? CHART_DATA_POINTS : data_index;
    for (int i = 0; i < points; i++) {
        if (steam_temps[i] < min_temp) min_temp = steam_temps[i];
        if (steam_temps[i] > max_temp) max_temp = steam_temps[i];
        if (hx_temps[i] < min_temp) min_temp = hx_temps[i];
        if (hx_temps[i] > max_temp) max_temp = hx_temps[i];
        if (target_temps[i] < min_temp) min_temp = target_temps[i];
        if (target_temps[i] > max_temp) max_temp = target_temps[i];
    }

    // Add some padding
    float range = max_temp - min_temp;
    if (range < 10.0f) range = 10.0f;  // Minimum range
    min_temp -= range * 0.1f;
    max_temp += range * 0.1f;

    // Sensible bounds for espresso machine
    if (min_temp < 0) min_temp = 0;
    if (max_temp > 200) max_temp = 200;
}

// Update chart data
void update_temp_chart() {
    if (temp_chart == nullptr) return;

    // Clear existing data
    lv_chart_set_all_value(temp_chart, steam_series, LV_CHART_POINT_NONE);
    lv_chart_set_all_value(temp_chart, hx_series, LV_CHART_POINT_NONE);
    lv_chart_set_all_value(temp_chart, target_series, LV_CHART_POINT_NONE);

    // Add data points in chronological order
    int points = data_filled ? CHART_DATA_POINTS : data_index;
    int start_idx = data_filled ? data_index : 0;

    for (int i = 0; i < points; i++) {
        int idx = (start_idx + i) % CHART_DATA_POINTS;
        lv_chart_set_next_value(temp_chart, steam_series, (int32_t)steam_temps[idx]);
        lv_chart_set_next_value(temp_chart, hx_series, (int32_t)hx_temps[idx]);
        lv_chart_set_next_value(temp_chart, target_series, (int32_t)target_temps[idx]);
    }

    // Update chart range
    float min_temp, max_temp;
    get_temp_range(min_temp, max_temp);
    lv_chart_set_range(temp_chart, LV_CHART_AXIS_PRIMARY_Y, (int32_t)min_temp, (int32_t)max_temp);

    lv_chart_refresh(temp_chart);
}

// Create temperature chart
void create_temp_chart(lv_obj_t* parent) {
    if (temp_chart != nullptr) {
        return;  // Already created
    }

    // Create chart with room for Y-axis labels on left and X-axis below (parent container is 345x260)
    temp_chart = lv_chart_create(parent);
    lv_obj_set_size(temp_chart, 310, 225);  // Larger size for bigger container
    lv_obj_set_pos(temp_chart, 30, 5);      // Same position for Y labels

    ESP_LOGI("chart", "Created chart: %p, size: 310x225", temp_chart);

    // Chart configuration
    lv_chart_set_type(temp_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(temp_chart, CHART_DATA_POINTS);
    lv_chart_set_range(temp_chart, LV_CHART_AXIS_PRIMARY_Y, 60, 140);  // Default range

    // Disable all interaction and scrollbars
    lv_obj_clear_flag(temp_chart, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(temp_chart, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_clear_flag(temp_chart, LV_OBJ_FLAG_SCROLL_CHAIN_HOR);
    lv_obj_clear_flag(temp_chart, LV_OBJ_FLAG_SCROLL_CHAIN_VER);
    lv_obj_set_style_pad_all(temp_chart, 0, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(temp_chart, LV_SCROLLBAR_MODE_OFF);

    // Subtle grid - much darker, dotted
    lv_obj_set_style_line_color(temp_chart, lv_color_hex(0x222222), LV_PART_MAIN);
    lv_obj_set_style_line_width(temp_chart, 1, LV_PART_MAIN);
    lv_obj_set_style_line_dash_width(temp_chart, 1, LV_PART_MAIN);
    lv_obj_set_style_line_dash_gap(temp_chart, 3, LV_PART_MAIN);
    lv_chart_set_div_line_count(temp_chart, 3, 6);  // Even fewer lines

    // Background
    lv_obj_set_style_bg_color(temp_chart, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(temp_chart, LV_OPA_COVER, LV_PART_MAIN);

    // Border
    lv_obj_set_style_border_color(temp_chart, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_border_width(temp_chart, 1, LV_PART_MAIN);

    // Create data series
    steam_series = lv_chart_add_series(temp_chart, lv_color_hex(0xFF5722), LV_CHART_AXIS_PRIMARY_Y);  // Red
    hx_series = lv_chart_add_series(temp_chart, lv_color_hex(0x2196F3), LV_CHART_AXIS_PRIMARY_Y);     // Blue
    target_series = lv_chart_add_series(temp_chart, lv_color_hex(0x4CAF50), LV_CHART_AXIS_PRIMARY_Y); // Green

    // Show Y-axis ticks and labels
    lv_chart_set_axis_tick(temp_chart, LV_CHART_AXIS_PRIMARY_Y, 5, 2, 5, 2, true, 50);

    // Show X-axis ticks but hide labels (we'll use custom ones)
    lv_chart_set_axis_tick(temp_chart, LV_CHART_AXIS_PRIMARY_X, 10, 2, 6, 2, false, 40);

    // Series styling - lines only, no dots
    lv_obj_set_style_line_width(temp_chart, 3, LV_PART_ITEMS);
    lv_obj_set_style_size(temp_chart, 0, LV_PART_INDICATOR);  // Remove point indicators

    // Add custom labels for axes
    // Y-axis label (°C) - positioned below the highest Y value
    lv_obj_t* y_label = lv_label_create(parent);
    lv_label_set_text(y_label, "°C");
    lv_obj_set_style_text_color(y_label, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(y_label, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(y_label, 5, 25);  // Below first Y-axis value

    // X-axis time labels - properly aligned with adjusted chart position
    const char* time_labels[] = {"-5m", "-4m", "-3m", "-2m", "-1m", "0m"};
    int chart_left = 30;  // Updated to match new chart position
    int chart_width = 310; // Updated to match new chart width
    int label_spacing = chart_width / 5;  // 5 intervals for 6 labels

    for (int i = 0; i < 6; i++) {
        lv_obj_t* x_label = lv_label_create(parent);
        lv_label_set_text(x_label, time_labels[i]);
        lv_obj_set_style_text_color(x_label, lv_color_hex(0x888888), 0);
        lv_obj_set_style_text_font(x_label, &lv_font_montserrat_14, 0);
        // Position: start at left edge, end at right edge
        int x_pos = chart_left + (i * label_spacing) - 10;
        if (i == 5) x_pos = chart_left + chart_width - 18;  // Better alignment for "0m"
        lv_obj_set_pos(x_label, x_pos, 235);  // Lower position for taller chart
    }

    // Initialize empty - will be filled by animated test data
    data_index = 0;
    data_filled = false;
}
