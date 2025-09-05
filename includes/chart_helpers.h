#pragma once
#include "esphome.h"
#include "timer_helpers.h"

// Temperature data storage
#define CHART_DATA_POINTS 60  // 5 minutes at 5s intervals OR 1 minute at 1s intervals
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
void get_temp_range(float& min_temp, float& max_temp);
void update_temp_chart();
void add_temp_data(float steam, float hx, float target);
void update_chart_grid(float temp_range);
void recreate_temp_chart(lv_obj_t* parent);
void clear_chart_data();

// Demo mode brew state management
static bool demo_brewing_active = false;
static uint32_t demo_brew_start_time = 0;
static int demo_last_pump_status = -1;

void demo_start_brew_cycle() {
    demo_brewing_active = true;
    demo_brew_start_time = millis();
}

void demo_stop_brew_cycle() {
    demo_brewing_active = false;
}

bool demo_is_brewing() {
    return demo_brewing_active;
}

// Generate realistic Mara X protocol strings (only in demo mode) - OPTIMIZED
void generate_test_data(bool uart_connected) {
    // Only generate test data if demo mode is enabled and no real UART connection
    if (uart_connected || !id(demo_mode_enabled)) return;
    
    static uint32_t last_protocol_update = 0;  // When we last generated new protocol data
    static uint32_t last_chart_update = 0;     // When we last added data to chart
    static uint32_t last_ui_update = 0;        // Separate timing for UI updates
    static uint32_t sequence_counter = 840;    // Start at typical sequence value
    static bool heating_cycle = false;
    static uint32_t cycle_start = 0;
    
    // Cache for last values to avoid unnecessary UI updates
    static float last_steam = -1, last_target = -1, last_hx = -1;
    static int last_heat = -1, last_pump = -1;
    static std::string last_status_text = "";
    
    // Current temperature values (persist between calls)
    static float current_steam = 118, current_target = 94, current_hx = 88;
    static int current_heat = 0, current_pump = 0;
    
    uint32_t now = millis();

    // Generate new protocol data based on chart resolution
    static bool globals_ready = false;
    if (!globals_ready) {
        // Check if globals are ready (avoid crash during startup)
        globals_ready = (millis() > 5000);  // Wait 5 seconds after boot
    }
    
    // Protocol data generation interval (actual sensor data simulation)
    uint32_t protocol_interval = 5000;  // Generate new sensor readings every 5s
    
    // Chart data interval (how often we add points to chart)
    uint32_t chart_interval = 5000;  // Default 5s
    if (globals_ready) {
        chart_interval = id(high_res_chart) ? 1000 : 5000;  // 1s or 5s for chart updates
    }
    
    // STEP 1: Generate new protocol data every 5 seconds (simulate sensor readings)
    if (now - last_protocol_update > protocol_interval) {
        float time_offset = now / 20000.0f;  // Slower variation
        
        // Realistic temperature simulation with heating cycles
        float steam_temp, target_temp, hx_temp;
        int heat_status = 0, pump_active = 0;
        
        // Simulate heating cycle every 30 seconds
        if (now - cycle_start > 30000) {
            heating_cycle = !heating_cycle;
            cycle_start = now;
            
            // Start brewing cycle randomly during non-heating periods
            if (!heating_cycle && !demo_brewing_active && (now % 60000) < 5000) {
                demo_start_brew_cycle();
            }
        }
        
        // Check for manual brew stop (simulate user stopping brew after random time 15-45s)
        if (demo_brewing_active && (now - demo_brew_start_time > (15000 + (now % 30000)))) {
            demo_stop_brew_cycle();
        }
        
        
        if (demo_brewing_active) {
            // Brewing: temperatures drop, pump active
            steam_temp = 110 + 8 * sin(time_offset * 2);
            hx_temp = 88 + 5 * sin(time_offset * 2 + 1);
            target_temp = 93 + 3 * sin(time_offset * 2);
            heat_status = 1;  // Heating to compensate
            pump_active = 1;  // Pump active
            
        } else if (heating_cycle) {
            // Heating up: temperatures rising
            steam_temp = 115 + 12 * sin(time_offset);
            hx_temp = 92 + 8 * sin(time_offset + 0.5);
            target_temp = 98 + 4 * sin(time_offset + 1);
            heat_status = 1;
            pump_active = 0;
        } else {
            // Idle: stable temperatures
            steam_temp = 118 + 3 * sin(time_offset * 0.5);
            hx_temp = 94 + 2 * sin(time_offset * 0.5 + 1);
            target_temp = 98 + 1 * sin(time_offset * 0.5);
            heat_status = 0;
            pump_active = 0;
        }
        
        // Build protocol string: C1.06,steam,target,hx,sequence,heat,pump
        sequence_counter = (sequence_counter + 1) % 10000;
        std::string protocol_data = str_sprintf("C1.06,%03.0f,%03.0f,%03.0f,%04d,%d,%d\n",
                                               steam_temp, target_temp, hx_temp, 
                                               sequence_counter, heat_status, pump_active);
        
        // Inject protocol string into UART buffer for parsing
        // This simulates receiving actual UART data
        ESP_LOGD("test_data", "Injecting protocol: %s", protocol_data.c_str());
        
        // We can't directly inject into UART, so we'll call the parsing logic directly
        // Parse the test data through the same logic used for real UART data
        std::vector<std::string> parts;
        std::string current = "";
        
        for (char c : protocol_data) {
            if (c == ',' || c == '\n') {
                parts.push_back(current);
                current = "";
            } else if (c != 'C' || !parts.empty()) {  // Skip initial 'C'
                current += c;
            }
        }
        
        if (parts.size() >= 7) {
            float steam_val = std::stof(parts[1]);
            float target_val = std::stof(parts[2]);
            float hx_val = std::stof(parts[3]);
            
            // Update current values for chart and UI
            current_steam = steam_val;
            current_target = target_val;
            current_hx = hx_val;
            current_heat = heat_status;
            current_pump = pump_active;
            
            // UI UPDATE OPTIMIZATION: Only update if values changed significantly (>1°C)
            // or enough time has passed (throttle UI updates to every 1s minimum)
            bool should_update_ui = (now - last_ui_update > 1000) || 
                                  (abs(steam_val - last_steam) > 1.0f) ||
                                  (abs(target_val - last_target) > 1.0f) ||
                                  (abs(hx_val - last_hx) > 1.0f) ||
                                  (heat_status != last_heat) ||
                                  (pump_active != last_pump);
                                  
            if (should_update_ui) {
                // Batch UI updates to reduce LVGL blocking time
                
                // Cache string formatting to avoid repeated sprintf calls
                static char steam_text[12], target_text[12], hx_text[12];
                sprintf(steam_text, "%.0f°C", steam_val);
                sprintf(target_text, "%.0f°C", target_val);
                sprintf(hx_text, "%.0f°C", hx_val);
                
                // Update temperature displays
                lv_label_set_text(&id(steam_temp_display), steam_text);
                lv_label_set_text(&id(target_temp_display), target_text);
                lv_label_set_text(&id(hx_temp_display), hx_text);
                
                // Update colors only if temperature value changed significantly
                if (abs(steam_val - last_steam) > 1.0f) {
                    if (steam_val > 115) {
                        lv_obj_set_style_text_color(&id(steam_temp_display), lv_color_hex(0xFF5722), 0);  // Red
                    } else if (steam_val > 90) {
                        lv_obj_set_style_text_color(&id(steam_temp_display), lv_color_hex(0xFF9800), 0);  // Yellow
                    } else {
                        lv_obj_set_style_text_color(&id(steam_temp_display), lv_color_hex(0x555555), 0);  // Gray
                    }
                }
                
                // Target temp always green (only set once per significant change)
                if (abs(target_val - last_target) > 1.0f) {
                    lv_obj_set_style_text_color(&id(target_temp_display), lv_color_hex(0x4CAF50), 0);  // Green
                }
                
                // HX temp colors (only update on significant change)
                if (abs(hx_val - last_hx) > 1.0f) {
                    if (hx_val >= 88 && hx_val <= 96) {
                        lv_obj_set_style_text_color(&id(hx_temp_display), lv_color_hex(0x4CAF50), 0);  // Green
                    } else if ((hx_val >= 85 && hx_val < 88) || (hx_val > 96 && hx_val <= 100)) {
                        lv_obj_set_style_text_color(&id(hx_temp_display), lv_color_hex(0xFF9800), 0);  // Yellow
                    } else {
                        lv_obj_set_style_text_color(&id(hx_temp_display), lv_color_hex(0xFF5722), 0);  // Red
                    }
                }
                
                // Cache values
                last_steam = steam_val;
                last_target = target_val;
                last_hx = hx_val;
                last_ui_update = now;
            }
            
            // Timer control for demo mode based on pump status changes
            if (demo_last_pump_status != pump_active) {
                if (pump_active == 1 && demo_last_pump_status == 0) {
                    // Pump started - start timer
                    start_timer();
                    ESP_LOGI("demo", "Demo brew started - timer started");
                } else if (pump_active == 0 && demo_last_pump_status == 1) {
                    // Pump stopped - stop timer
                    stop_timer();
                    ESP_LOGI("demo", "Demo brew stopped - timer stopped");
                }
                demo_last_pump_status = pump_active;
            }
            
            // OPTIMIZED STATUS UPDATES: Only update when state actually changes
            if (heat_status != last_heat) {
                if (heat_status == 1) {
                    lv_label_set_text(&id(heating_status), "\U000F1A45 HEAT");
                    lv_obj_set_style_text_color(&id(heating_status), lv_color_hex(0xFF5722), 0);
                } else {
                    lv_label_set_text(&id(heating_status), "\U000F1A45 HEAT");
                    lv_obj_set_style_text_color(&id(heating_status), lv_color_hex(0x555555), 0);
                }
                last_heat = heat_status;
            }
            
            if (pump_active != last_pump) {
                if (pump_active == 1) {
                    lv_label_set_text(&id(pump_status), "\U000F1402 PUMP");
                    lv_obj_set_style_text_color(&id(pump_status), lv_color_hex(0x2196F3), 0);
                } else {
                    lv_label_set_text(&id(pump_status), "\U000F1B22 PUMP");
                    lv_obj_set_style_text_color(&id(pump_status), lv_color_hex(0x555555), 0);
                }
                last_pump = pump_active;
            }
            
            // OPTIMIZED STATUS DISPLAY: Reduce blinking frequency and batch updates
            static uint32_t last_blink = 0;
            static bool blink_state = true;
            
            // Reduced blink frequency from 1s to 2s for better performance
            if (now - last_blink > 2000) {
                std::string new_status_text;
                uint32_t new_status_color;
                
                if (blink_state) {
                    // Show actual status (READY/HEATING UP/BREWING) 
                    if (pump_active == 1) {
                        new_status_text = "BREWING";
                        new_status_color = 0x2196F3;
                    } else if (heat_status == 1) {
                        new_status_text = "HEATING UP";
                        new_status_color = 0xFF9800;
                    } else {
                        new_status_text = "READY";
                        new_status_color = 0x4CAF50;
                    }
                } else {
                    // Alternate with "TEST MODE"
                    new_status_text = "TEST MODE";
                    new_status_color = 0xFFC107;  // Amber
                }
                
                // Only update if text changed
                if (new_status_text != last_status_text) {
                    lv_label_set_text(&id(machine_status), new_status_text.c_str());
                    lv_obj_set_style_text_color(&id(machine_status), lv_color_hex(new_status_color), 0);
                    last_status_text = new_status_text;
                }
                
                blink_state = !blink_state;
                last_blink = now;
            }
            
            // Version display - set once only (moved outside frequent update loop)
            static bool version_set = false;
            if (!version_set) {
                lv_label_set_text(&id(version_display), "V1.06");
                version_set = true;
            }
            
            
            ESP_LOGD("test_data", "Generated: Steam=%.0f Target=%.0f HX=%.0f Heat=%d Pump=%d", 
                     steam_val, target_val, hx_val, heat_status, pump_active);
        }
        
        last_protocol_update = now;
    }
    
    // STEP 2: Add data to chart at appropriate intervals (1s for high-res, 5s for normal)
    if (now - last_chart_update > chart_interval) {
        // Add current temperature values to chart
        add_temp_data(current_steam, current_hx, current_target);
        last_chart_update = now;
    }
    
    // Timer is now updated separately via 100ms interval in main config
}

// HEAVILY OPTIMIZED chart updates - minimal LVGL blocking
void add_temp_data(float steam, float hx, float target) {
    // Store data first
    steam_temps[data_index] = steam;
    hx_temps[data_index] = hx;
    target_temps[data_index] = target;
    data_timestamps[data_index] = millis();
    
    // Incremental min/max tracking (avoid expensive range scans)
    static float running_min = 999.0f, running_max = 0.0f;
    static bool min_max_initialized = false;
    
    if (!min_max_initialized) {
        running_min = min(min(steam, hx), target);
        running_max = max(max(steam, hx), target);
        min_max_initialized = true;
    } else {
        // Update running min/max incrementally
        float current_min = min(min(steam, hx), target);
        float current_max = max(max(steam, hx), target);
        running_min = min(running_min, current_min);
        running_max = max(running_max, current_max);
    }

    // CRITICAL OPTIMIZATION: Defer chart updates to reduce stutter
    if (temp_chart != nullptr) {
        static int chart_update_counter = 0;
        static int pending_updates = 0;
        static float pending_steam[3], pending_hx[3], pending_target[3];
        
        // Buffer up to 3 data points before batch updating chart
        pending_steam[pending_updates] = steam;
        pending_hx[pending_updates] = hx;
        pending_target[pending_updates] = target;
        pending_updates++;
        
        chart_update_counter++;
        
        // BATCH UPDATE: Only update chart every 3rd data point to reduce blocking
        if (pending_updates >= 3 || chart_update_counter >= 6) {
            // Apply all pending updates in one batch
            for (int i = 0; i < pending_updates; i++) {
                lv_chart_set_next_value(temp_chart, steam_series, (int32_t)pending_steam[i]);
                lv_chart_set_next_value(temp_chart, hx_series, (int32_t)pending_hx[i]);
                lv_chart_set_next_value(temp_chart, target_series, (int32_t)pending_target[i]);
            }
            
            pending_updates = 0;  // Reset buffer
            
            // RANGE UPDATE: Much less frequent, only every 40 data points
            static float last_min = 0, last_max = 0;
            if (chart_update_counter >= 40) {
                // Use incremental min/max instead of expensive range scan
                float padded_min = running_min - (running_max - running_min) * 0.1f;
                float padded_max = running_max + (running_max - running_min) * 0.1f;
                
                // Clamp to reasonable bounds
                if (padded_min < 0) padded_min = 0;
                if (padded_max > 200) padded_max = 200;
                
                // Only update range if it changed significantly (>10°C for even less updates)
                if (abs((int)(padded_min - last_min)) > 10 || abs((int)(padded_max - last_max)) > 10) {
                    lv_chart_set_range(temp_chart, LV_CHART_AXIS_PRIMARY_Y, (int32_t)padded_min, (int32_t)padded_max);
                    last_min = padded_min;
                    last_max = padded_max;
                    
                    // Only refresh when range actually changes - most expensive operation
                    lv_chart_refresh(temp_chart);
                }
                
                chart_update_counter = 0;
                
                // Reset running min/max periodically to avoid drift
                running_min = 999.0f;
                running_max = 0.0f;
                min_max_initialized = false;
            }
        }
    }

    data_index = (data_index + 1) % CHART_DATA_POINTS;
    if (data_index == 0) data_filled = true;
}

// OPTIMIZED min/max calculation - only used for full rebuilds now
void get_temp_range(float& min_temp, float& max_temp) {
    min_temp = 999.0f;
    max_temp = 0.0f;

    int points = data_filled ? CHART_DATA_POINTS : data_index;
    
    // Unroll loop for better performance - process 3 temps at once
    for (int i = 0; i < points; i++) {
        float steam_val = steam_temps[i];
        float hx_val = hx_temps[i];
        float target_val = target_temps[i];
        
        // Find min/max in this data point
        float point_min = (steam_val < hx_val) ? ((steam_val < target_val) ? steam_val : target_val) : ((hx_val < target_val) ? hx_val : target_val);
        float point_max = (steam_val > hx_val) ? ((steam_val > target_val) ? steam_val : target_val) : ((hx_val > target_val) ? hx_val : target_val);
        
        if (point_min < min_temp) min_temp = point_min;
        if (point_max > max_temp) max_temp = point_max;
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

// Full chart rebuild - only used when needed (resolution change, clear, etc.)
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

    // Update chart range with dynamic scaling
    float min_temp, max_temp;
    get_temp_range(min_temp, max_temp);
    lv_chart_set_range(temp_chart, LV_CHART_AXIS_PRIMARY_Y, (int32_t)min_temp, (int32_t)max_temp);
    
    // Dynamic grid lines based on temperature range
    update_chart_grid(max_temp - min_temp);

    lv_chart_refresh(temp_chart);
}

// Separate function for grid updates to avoid repeated calculations
void update_chart_grid(float temp_range) {
    if (temp_chart == nullptr) return;
    
    int y_divs = 4;  // Default
    int x_divs = 6;  // Time divisions
    
    if (temp_range > 50) {
        y_divs = 8;  // More divisions for large ranges
    } else if (temp_range > 30) {
        y_divs = 6;  // Medium divisions
    } else if (temp_range > 15) {
        y_divs = 4;  // Fewer divisions for small ranges
    } else {
        y_divs = 3;  // Minimal for very small ranges
    }
    
    lv_chart_set_div_line_count(temp_chart, y_divs, x_divs);
}

// Create temperature chart
void create_temp_chart(lv_obj_t* parent) {
    if (temp_chart != nullptr) {
        return;  // Already created
    }

    // Create chart with room for Y-axis labels on left and X-axis below (parent container is 320x285)
    temp_chart = lv_chart_create(parent);
    lv_obj_set_size(temp_chart, 285, 250);  // Adjusted for wider container
    lv_obj_set_pos(temp_chart, 30, 5);      // Same position for Y labels

    ESP_LOGI("chart", "Created chart: %p, size: 285x250", temp_chart);

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

    // Fine-grained dynamic grid
    lv_obj_set_style_line_color(temp_chart, lv_color_hex(0x1A1A1A), LV_PART_MAIN);
    lv_obj_set_style_line_width(temp_chart, 1, LV_PART_MAIN);
    lv_obj_set_style_line_dash_width(temp_chart, 1, LV_PART_MAIN);
    lv_obj_set_style_line_dash_gap(temp_chart, 2, LV_PART_MAIN);
    
    // Dynamic grid lines based on temperature range - will be updated in update_temp_chart()
    lv_chart_set_div_line_count(temp_chart, 6, 10);  // More fine-grained grid

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

    // X-axis time labels - check resolution dynamically if globals are available  
    const char* time_labels_5s[] = {"-5m", "-4m", "-3m", "-2m", "-1m", "0m"};
    const char* time_labels_1s[] = {"-50s", "-40s", "-30s", "-20s", "-10s", "0s"};
    const char** time_labels = time_labels_5s;  // Default to 5s labels
    
    // Try to use current resolution if globals are ready
    if (millis() > 5000) {  // Wait for globals to be ready
        if (id(high_res_chart)) {
            time_labels = time_labels_1s;
        }
    }
    
    int chart_left = 30;  // Updated to match new chart position
    int chart_width = 285; // Updated to match new chart width
    int label_spacing = chart_width / 5;  // 5 intervals for 6 labels

    for (int i = 0; i < 6; i++) {
        lv_obj_t* x_label = lv_label_create(parent);
        lv_label_set_text(x_label, time_labels[i]);
        lv_obj_set_style_text_color(x_label, lv_color_hex(0x888888), 0);
        lv_obj_set_style_text_font(x_label, &lv_font_montserrat_14, 0);
        // Position: start at left edge, end at right edge
        int x_pos = chart_left + (i * label_spacing) - 10;
        if (i == 5) x_pos = chart_left + chart_width - 18;  // Better alignment for "0s/0m"
        lv_obj_set_pos(x_label, x_pos, 260);  // Updated for expanded chart (was 235)
    }

    // Initialize empty - will be filled by animated test data
    data_index = 0;
    data_filled = false;
}

// Recreate chart with updated labels for resolution change
void recreate_temp_chart(lv_obj_t* parent) {
    // Clear existing chart and data
    if (temp_chart != nullptr) {
        lv_obj_del(temp_chart);
        temp_chart = nullptr;
        steam_series = nullptr;
        hx_series = nullptr; 
        target_series = nullptr;
    }
    
    // Clear all child objects (labels) from parent
    lv_obj_clean(parent);
    
    // Recreate chart with current resolution settings
    create_temp_chart(parent);
}

// Clear chart data when demo mode is disabled
void clear_chart_data() {
    // Reset data arrays
    for (int i = 0; i < CHART_DATA_POINTS; i++) {
        steam_temps[i] = 0.0f;
        hx_temps[i] = 0.0f;
        target_temps[i] = 0.0f;
        data_timestamps[i] = 0;
    }
    
    // Reset data index and filled flag
    data_index = 0;
    data_filled = false;
    
    // Clear chart display if it exists
    if (temp_chart != nullptr) {
        lv_chart_set_all_value(temp_chart, steam_series, LV_CHART_POINT_NONE);
        lv_chart_set_all_value(temp_chart, hx_series, LV_CHART_POINT_NONE);
        lv_chart_set_all_value(temp_chart, target_series, LV_CHART_POINT_NONE);
        lv_chart_refresh(temp_chart);
    }
}
