#pragma once
#include "esphome.h"
#include <math.h>
#include "timer_helpers.h"

// Optional: silence verbose logs for production builds
#ifdef MARAX_PRODUCTION
#  undef ESP_LOGD
#  define ESP_LOGD(tag, fmt, ...) ((void)0)
#  undef ESP_LOGV
#  define ESP_LOGV(tag, fmt, ...) ((void)0)
#endif

// Professional instrument design - Time-first approach
// UART provides data every 400ms (2.5 Hz), we need to handle this properly

// High resolution: 50 second window, 1s intervals = 50 points
#define HIGH_RES_WINDOW_SEC  50
#define HIGH_RES_INTERVAL_MS 1000
#define HIGH_RES_POINTS      50

// Low resolution: 15 minute window, 15s intervals = 60 points
#define LOW_RES_WINDOW_SEC   900
#define LOW_RES_INTERVAL_MS  15000
#define LOW_RES_POINTS       60

// Raw data buffer to handle 400ms UART data (2.5 Hz)
#define RAW_DATA_BUFFER_SIZE 200  // ~80 seconds of raw data at 400ms intervals 

// Adaptive recovery detection parameters
#define RECOVERY_WINDOW_SEC          20    // analyze last N seconds at 1s resolution
#define RECOVERY_MIN_SEC             20    // must wait at least this long after pump stop
#define RECOVERY_MAX_DELTA_C         1.0f  // avg |HX-Target| within this
#define RECOVERY_MAX_SLOPE_C_PER_SEC 0.04f // |d(HX)/dt| below this

// Raw data buffer - captures all 400ms UART data
struct RawTempData {
    uint32_t timestamp;
    float steam, hx, target;
    int heat_status;
    int pump_status;
};

static RawTempData raw_data[RAW_DATA_BUFFER_SIZE];
static int raw_data_index = 0;
static bool raw_data_filled = false;

// Brew shading configuration and storage (keep small for 15m window)
struct BrewEvent { uint32_t start_ms; uint32_t stop_ms; uint32_t recovered_ms; };
static BrewEvent brew_events[10];
static int brew_event_count = 0;

inline void brew_event_start(uint32_t t) {
    if (brew_event_count >= (int)(sizeof(brew_events)/sizeof(brew_events[0]))) {
        for (int i = 1; i < brew_event_count; ++i) brew_events[i-1] = brew_events[i];
        brew_event_count--;
    }
    brew_events[brew_event_count++] = {t, 0u, 0u};
}

inline void brew_event_stop(uint32_t t) {
    if (brew_event_count == 0) return;
    for (int i = brew_event_count - 1; i >= 0; --i) {
        if (brew_events[i].stop_ms == 0 && brew_events[i].start_ms <= t) {
            brew_events[i].stop_ms = t;
            break;
        }
    }
}

inline void brew_event_recovered(uint32_t t) {
    if (brew_event_count == 0) return;
    for (int i = brew_event_count - 1; i >= 0; --i) {
        if (brew_events[i].stop_ms > 0 && brew_events[i].recovered_ms == 0 && brew_events[i].start_ms <= t) {
            brew_events[i].recovered_ms = t;
            break;
        }
    }
}

inline int get_brew_event_count() { return brew_event_count; }
inline const BrewEvent* get_brew_events() { return brew_events; }
inline uint32_t get_recovery_ms() { return 90000u; } // 90s recovery
inline uint32_t get_recovery_buffer_ms() { return 10000u; } // 10s buffer after recovery

// Get most recent brew stop timestamp (0 if none)
inline uint32_t get_last_brew_stop_ms() {
    for (int i = brew_event_count - 1; i >= 0; --i) {
        if (brew_events[i].stop_ms > 0) return brew_events[i].stop_ms;
    }
    return 0;
}

// High-resolution chart data (1s intervals, 60 points = 1 minute)
static float steam_1s[HIGH_RES_POINTS];
static float hx_1s[HIGH_RES_POINTS];
static float target_1s[HIGH_RES_POINTS];
static uint32_t timestamps_1s[HIGH_RES_POINTS];
static int data_index_1s = 0;
static bool data_filled_1s = false;

// Low-resolution chart data (15s intervals, 60 points = 15 minutes)
static float steam_15s[LOW_RES_POINTS];
static float hx_15s[LOW_RES_POINTS];
static float target_15s[LOW_RES_POINTS];
static uint32_t timestamps_15s[LOW_RES_POINTS];
static int data_index_15s = 0;
static bool data_filled_15s = false;

// Remove unsafe macro - use helper functions instead

// Chart objects
static lv_obj_t* temp_chart = nullptr;
static lv_chart_series_t* steam_series = nullptr;
static lv_chart_series_t* hx_series = nullptr;
static lv_chart_series_t* target_series = nullptr;

// Last Y-axis range applied to the chart. File-scope (not function-local
// static) so the LVGL 9 tick label callback in chart_draw.h can read it
// without scanning data on every paint.
int32_t last_y_min = -9999;
int32_t last_y_max = -9999;

// Forward declarations - Professional instrument interface
void add_raw_uart_data(float steam, float hx, float target, int heat, int pump);
void update_temperature_displays(float steam, float hx, float target, int heat, int pump);
void process_chart_data();  // Convert raw data to chart resolution
void update_chart_display();
void get_temp_range(float& min_temp, float& max_temp);
bool is_high_res_mode();
void switch_chart_resolution();
void clear_chart_data();
void reset_temperature_displays_cache();
float get_averaged_temp_at_time(uint32_t target_time, uint32_t window_ms, int temp_type);

// Determine if system has recovered based on recent high-res data
bool has_recovered() {
    // Only meaningful if we have some 1s data
    int available = data_filled_1s ? HIGH_RES_POINTS : data_index_1s;
    if (available < 5) return false;

    uint32_t now = millis();
    uint32_t window_ms = RECOVERY_WINDOW_SEC * 1000u;

    // Adaptive thresholds: relax in demo mode so the state machine progresses
    bool demo = id(demo_mode_enabled);
    uint32_t min_sec = demo ? 12u : (uint32_t)RECOVERY_MIN_SEC;
    float max_delta = demo ? 2.2f : RECOVERY_MAX_DELTA_C;
    float max_slope = demo ? 0.08f : RECOVERY_MAX_SLOPE_C_PER_SEC;

    // Enforce a minimum time since last brew stop
    uint32_t last_stop = get_last_brew_stop_ms();
    if (last_stop == 0 || (now - last_stop) < (min_sec * 1000u)) return false;

    int considered = 0;
    float sum_abs_delta = 0.0f;
    uint32_t t_first = 0, t_last = 0;
    float hx_first = 0.0f, hx_last = 0.0f;

    for (int i = 0; i < available; i++) {
        int idx = (data_index_1s - 1 - i + HIGH_RES_POINTS) % HIGH_RES_POINTS;
        uint32_t ts = timestamps_1s[idx];
        if (ts == 0 || (now - ts) > window_ms) break;
        float hxv = hx_1s[idx];
        float tgt = target_1s[idx];
        if (hxv <= 0 || hxv > 200 || tgt <= 0 || tgt > 200) continue;
        sum_abs_delta += fabsf(hxv - tgt);
        if (considered == 0) {
            t_last = ts; hx_last = hxv;
        }
        t_first = ts; hx_first = hxv;
        considered++;
    }

    if (considered < RECOVERY_WINDOW_SEC - 2) return false;  // need enough points
    uint32_t dur_ms = (t_last >= t_first) ? (t_last - t_first) : 0;
    if (dur_ms < (RECOVERY_WINDOW_SEC - 2) * 1000u) return false;

    float avg_abs = sum_abs_delta / (float)considered;
    float slope = (dur_ms > 0) ? (hx_last - hx_first) / ((float)dur_ms / 1000.0f) : 0.0f;
    if (avg_abs <= max_delta && fabsf(slope) <= max_slope) {
        return true;
    }
    return false;
}

// Draw helpers (axis labels + shaded bands) — needs forward decls
#include "chart_draw.h"

// Demo mode state machine
enum DemoState {
    DEMO_STARTUP,      // Machine starting up - cold temperatures
    DEMO_HEATING_UP,   // Machine heating to target temperatures  
    DEMO_READY,        // Machine at target temperatures, ready to brew
    DEMO_BREWING       // Actively brewing - pump running, temps dropping
};

static DemoState demo_state = DEMO_STARTUP;
static uint32_t demo_state_start_time = 0;
static uint32_t demo_brew_start_time = 0;
static uint32_t demo_brew_stop_time = 0;
static uint32_t demo_next_brew_at = 0;  // Schedule for next brew in READY state
static int demo_last_pump_status = -1;
static bool demo_state_initialized = false;
static uint32_t demo_lowres_since = 0;   // When we actually returned to low-res
static const uint32_t DEMO_IDLE_AFTER_LOWRES_MS = 4000; // visible low-res dwell

// State transition functions
void demo_transition_to_state(DemoState new_state) {
    if (demo_state != new_state) {
        ESP_LOGI("demo", "State transition: %d -> %d", demo_state, new_state);
        demo_state = new_state;
        demo_state_start_time = millis();
        // Reset next brew schedule on state transitions
        if (new_state != DEMO_READY) {
            demo_next_brew_at = 0;
        }
    }
}

void demo_start_brew_cycle() {
    demo_transition_to_state(DEMO_BREWING);
    demo_brew_start_time = millis();
    demo_lowres_since = 0; // reset low-res dwell tracker
}

void demo_stop_brew_cycle() {
    demo_transition_to_state(DEMO_READY);
    demo_brew_stop_time = millis();
}

bool demo_is_brewing() {
    return demo_state == DEMO_BREWING;
}

DemoState demo_get_state() {
    return demo_state;
}

void demo_reset_state_machine() {
    demo_state = DEMO_STARTUP;
    demo_state_start_time = millis();
    demo_state_initialized = true;
    demo_last_pump_status = -1;  // Reset pump status tracking
    demo_next_brew_at = 0;
    
    // Stop timer when state machine resets
    stop_timer();
    
    ESP_LOGI("demo", "Demo state machine reset to STARTUP - timer stopped");
}

// Professional instrument data generation - simulates real 400ms Mara X UART
void generate_test_data(bool uart_connected) {
    // Only generate test data if demo mode is enabled and no real UART connection
    if (uart_connected || !id(demo_mode_enabled)) return;
    
    static uint32_t last_data_generation = 0;
    static uint32_t sequence_counter = 560;  // Countdown from Mara X description
    
    // Current temperature values (persist between calls)
    static float current_steam = 25, current_target = 25, current_hx = 25;
    static int current_heat = 0, current_pump = 0;
    
    uint32_t now = millis();

    // Initialize state machine on first run
    if (!demo_state_initialized) {
        demo_reset_state_machine();
    }

    // Generate data every 400ms to match real Mara X UART frequency
    if (now - last_data_generation < 400) return;
    
    last_data_generation = now;
    
    // Generate realistic temperature data based on state machine
    uint32_t time_in_state = now - demo_state_start_time;
    float steam_temp, target_temp, hx_temp;
    int heat_status = 0, pump_active = 0;
        
    // State machine logic
    switch (demo_state) {
            case DEMO_STARTUP:
                // Machine starting up - temperatures slowly rising from room temperature
                {
                    // Slightly longer initial warmup for realism (~60s)
                    float progress = min(1.0f, time_in_state / 60000.0f);  // 60 seconds startup
                    steam_temp = 25 + (90 - 25) * progress;   // 25°C to 90°C
                    target_temp = 25 + (85 - 25) * progress;  // 25°C to 85°C  
                    hx_temp = 25 + (75 - 25) * progress;      // 25°C to 75°C
                    heat_status = 1;  // Heating
                    pump_active = 0;
                    
                    // Transition to heating up when startup temps reached
                    if (progress >= 1.0f) {
                        demo_transition_to_state(DEMO_HEATING_UP);
                    }
                }
                break;
                
            case DEMO_HEATING_UP:
                // Machine heating to operating temperature
                {
                    // Take ~5 minutes to ramp to operating temperature
                    float progress = min(1.0f, time_in_state / 300000.0f);  // 300000 ms = 5 minutes
                    steam_temp = 90 + (118 - 90) * progress;   // 90°C to 118°C
                    target_temp = 85 + (96 - 85) * progress;   // 85°C to 96°C
                    hx_temp = 75 + (94 - 75) * progress;       // 75°C to 94°C
                    heat_status = 1;  // Heating
                    pump_active = 0;
                    
                    // Add some temperature variation during heating
                    float time_offset = time_in_state / 10000.0f;
                    steam_temp += 2 * sin(time_offset);
                    target_temp += 1 * sin(time_offset + 1);
                    hx_temp += 1.5 * sin(time_offset + 2);
                    
                    // Transition to ready when target temps reached
                    if (progress >= 1.0f) {
                        demo_transition_to_state(DEMO_READY);
                    }
                }
                break;
                
            case DEMO_READY:
                // Machine ready - stable operating temperatures
                {
                    float time_offset = time_in_state / 15000.0f;  // Slow variation
                    steam_temp = 118 + 2 * sin(time_offset);       // Stable around 118°C
                    target_temp = 96 + 1 * sin(time_offset + 1);   // Stable around 96°C  
                    hx_temp = 94 + 1.5 * sin(time_offset + 2);     // Stable around 94°C
                    heat_status = (sin(time_offset * 3) > 0.7f) ? 1 : 0;  // Occasional heating to maintain temp
                    pump_active = 0;
                    
                    // Schedule next brew only after we returned to low-res and idled visibly
                    if (demo_next_brew_at == 0) {
                        if (demo_brew_stop_time > 0) {
                            bool rec = has_recovered();
                            uint32_t last_stop = get_last_brew_stop_ms();
                            uint32_t deadline = last_stop > 0 ? (last_stop + get_recovery_ms() + get_recovery_buffer_ms()) : 0;
                            bool ready_to_consider = rec || (deadline > 0 && now >= deadline);
                            bool lowres_active = !is_high_res_mode();
                            if (ready_to_consider && lowres_active) {
                                // Start dwell timer on first observation of low-res after recovery
                                if (demo_lowres_since == 0) {
                                    demo_lowres_since = now;
                                    ESP_LOGD("demo", "Low-res active; starting dwell timer");
                                }
                                if (now - demo_lowres_since >= DEMO_IDLE_AFTER_LOWRES_MS) {
                                    demo_next_brew_at = now; // start immediately after dwell
                                    ESP_LOGD("demo", "Dwell complete; scheduling next brew now");
                                }
                            }
                        } else {
                            // First brew: small delay to let system warm/stabilize
                            uint32_t jitter = 30000 + ((now * 2654435761u) % 30000);
                            demo_next_brew_at = now + jitter;
                            ESP_LOGD("demo", "Initial brew scheduled in %u ms", jitter);
                        }
                    } else if (now >= demo_next_brew_at) {
                        demo_next_brew_at = 0;  // Clear before starting
                        demo_start_brew_cycle();
                    }
                }
                break;
                
            case DEMO_BREWING:
                // Actively brewing - pump running, temperatures dropping
                {
                    float brew_progress = min(1.0f, time_in_state / 30000.0f);  // Max 30s brew
                    float time_offset = time_in_state / 5000.0f;  // Faster variation during brewing
                    
                    // Temperatures drop during brewing due to water extraction
                    steam_temp = 118 - 8 * brew_progress + 3 * sin(time_offset * 2);
                    target_temp = 96 - 3 * brew_progress + 2 * sin(time_offset * 2 + 1);
                    hx_temp = 94 - 6 * brew_progress + 2 * sin(time_offset * 2 + 2);
                    heat_status = 1;  // Heating to compensate for heat loss
                    pump_active = 1;  // Pump active
                    
                    // Stop brewing after ~25-40 seconds (random duration)
                    uint32_t brew_duration = 25000 + ((now + demo_brew_start_time) % 15000);
                    if (time_in_state > brew_duration) {
                        demo_stop_brew_cycle();
                    }
                }
                break;
    }
    
    // Update sequence counter (countdown in real Mara X)
    if (sequence_counter > 0) sequence_counter--;
    else sequence_counter = 1500;  // Reset cycle
    
    // Store current values
    current_steam = steam_temp;
    current_target = target_temp; 
    current_hx = hx_temp;
    current_heat = heat_status;
    current_pump = pump_active;
    
    // Add to raw data buffer - this simulates UART data arrival
    add_raw_uart_data(steam_temp, hx_temp, target_temp, heat_status, pump_active);
    
    // Update machine status display
    static uint32_t last_status_update = 0;
    if (now - last_status_update > 2000) {  // Update status every 2s
        std::string status_text;
        uint32_t status_color;
        
        switch (demo_state) {
            case DEMO_STARTUP:
                status_text = "STARTING UP";
                status_color = 0xFFC107;  // Amber
                break;
            case DEMO_HEATING_UP:
                status_text = "HEATING UP";
                status_color = 0xFF9800;  // Orange
                break;
            case DEMO_READY:
                status_text = "READY";
                status_color = 0x4CAF50;  // Green
                break;
            case DEMO_BREWING:
                status_text = "BREWING";
                status_color = 0x2196F3;  // Blue
                break;
            default:
                status_text = "DEMO MODE";
                status_color = 0xFFC107;
                break;
        }
        
        lv_label_set_text(&id(machine_status), status_text.c_str());
        lv_obj_set_style_text_color(&id(machine_status), lv_color_hex(status_color), 0);
        
        // Version display
        lv_label_set_text(&id(version_display), "V1.06");
        
        last_status_update = now;
    }
    
    ESP_LOGV("demo", "Generated: S=%.0f T=%.0f H=%.0f Heat=%d Pump=%d State=%d", 
             steam_temp, target_temp, hx_temp, heat_status, pump_active, demo_state);
}
            
// PROFESSIONAL DATA COLLECTION - handles 400ms UART data
void add_raw_uart_data(float steam, float hx, float target, int heat, int pump) {
    uint32_t now = millis();
    
    // Add to raw data buffer
    raw_data[raw_data_index] = {now, steam, hx, target, heat, pump};
    raw_data_index = (raw_data_index + 1) % RAW_DATA_BUFFER_SIZE;
    if (raw_data_index == 0) raw_data_filled = true;
    
    // IMMEDIATE UI UPDATE - Professional instrument feel
    update_temperature_displays(steam, hx, target, heat, pump);
    
    // Process chart data at appropriate intervals
    process_chart_data();
    
    ESP_LOGV("uart", "Raw data: S=%.0f H=%.0f T=%.0f Heat=%d Pump=%d", steam, hx, target, heat, pump);
}

// IMMEDIATE UI FEEDBACK - Always responsive
// Each LVGL setter is gated on a cached value so we only touch the widget when
// the displayed value or color band actually changed. Every set_text / set_style
// call invalidates the widget regardless of whether the value differs, so the
// previous unconditional updates were causing ~12 redraws/sec on the left panel
// for steady-state data.
//
// Caches are at file scope (not function-local statics) so reset_temperature_
// displays_cache() can invalidate them when something else has blanked the
// labels (UART disconnect, demo toggle).
static int td_last_steam_int = -999;
static int td_last_target_int = -999;
static int td_last_hx_int = -999;
static int td_last_steam_band = -1;
static int td_last_hx_band = -1;
static bool td_target_color_set = false;
static int td_last_heat_state = -1;
static int td_last_pump_display = -1;

void reset_temperature_displays_cache() {
    td_last_steam_int = -999;
    td_last_target_int = -999;
    td_last_hx_int = -999;
    td_last_steam_band = -1;
    td_last_hx_band = -1;
    td_target_color_set = false;
    td_last_heat_state = -1;
    td_last_pump_display = -1;
}

void update_temperature_displays(float steam, float hx, float target, int heat, int pump) {
    int steam_int = (int)lroundf(steam);
    int target_int = (int)lroundf(target);
    int hx_int = (int)lroundf(hx);

    char buf[16];
    if (steam_int != td_last_steam_int) {
        snprintf(buf, sizeof(buf), "%d°C", steam_int);
        lv_label_set_text(&id(steam_temp_display), buf);
        td_last_steam_int = steam_int;
    }
    if (target_int != td_last_target_int) {
        snprintf(buf, sizeof(buf), "%d°C", target_int);
        lv_label_set_text(&id(target_temp_display), buf);
        td_last_target_int = target_int;
    }
    if (hx_int != td_last_hx_int) {
        snprintf(buf, sizeof(buf), "%d°C", hx_int);
        lv_label_set_text(&id(hx_temp_display), buf);
        td_last_hx_int = hx_int;
    }

    int steam_band = (steam > 115.0f) ? 2 : (steam > 90.0f ? 1 : 0);
    if (steam_band != td_last_steam_band) {
        uint32_t c = (steam_band == 2) ? 0xFF5722 : (steam_band == 1 ? 0xFF9800 : 0x555555);
        lv_obj_set_style_text_color(&id(steam_temp_display), lv_color_hex(c), 0);
        td_last_steam_band = steam_band;
    }

    if (!td_target_color_set) {
        // Target color is constant; set once.
        lv_obj_set_style_text_color(&id(target_temp_display), lv_color_hex(0x4CAF50), 0);
        td_target_color_set = true;
    }

    int hx_band;
    if (hx >= 88.0f && hx <= 96.0f) hx_band = 0;
    else if ((hx >= 85.0f && hx < 88.0f) || (hx > 96.0f && hx <= 100.0f)) hx_band = 1;
    else hx_band = 2;
    if (hx_band != td_last_hx_band) {
        uint32_t c = (hx_band == 0) ? 0x4CAF50 : (hx_band == 1 ? 0xFF9800 : 0xFF5722);
        lv_obj_set_style_text_color(&id(hx_temp_display), lv_color_hex(c), 0);
        td_last_hx_band = hx_band;
    }

    if (heat != td_last_heat_state) {
        // Heating label text is constant; only seed it on the first call.
        if (td_last_heat_state == -1) {
            lv_label_set_text(&id(heating_status), "\U000F1A45 HEAT");
        }
        uint32_t c = (heat == 1) ? 0xFF5722 : 0x555555;
        lv_obj_set_style_text_color(&id(heating_status), lv_color_hex(c), 0);
        td_last_heat_state = heat;
    }

    if (pump != td_last_pump_display) {
        if (pump == 1) {
            lv_label_set_text(&id(pump_status), "\U000F1402 PUMP");
            lv_obj_set_style_text_color(&id(pump_status), lv_color_hex(0x2196F3), 0);
        } else {
            lv_label_set_text(&id(pump_status), "\U000F1B22 PUMP");
            lv_obj_set_style_text_color(&id(pump_status), lv_color_hex(0x555555), 0);
        }
        td_last_pump_display = pump;
    }

    // Edge-triggered pump status handling (timer + high-res switch). Kept
    // separate from the display cache above because it must observe the same
    // edge exactly once even if the display cache is reset.
    static int last_pump_status = -1;
    if (last_pump_status != pump) {
        if (pump == 1 && last_pump_status == 0) {
            // Pump started - start timer and switch to high-res mode
            start_timer();
            id(high_res_chart) = true;
            ESP_LOGI("brew", "Brew started - timer started, switched to high-res mode");
            // Track brew window for shading
            brew_event_start(millis());
            switch_chart_resolution();
        } else if (pump == 0 && last_pump_status == 1) {
            // Pump stopped - stop timer and schedule return to normal resolution
            stop_timer();
            // Reset timer color to neutral on stop
            set_timer_neutral((lv_obj_t*) &id(brew_timer));
            ESP_LOGI("brew", "Brew stopped - timer stopped, will return to normal resolution after recovery");
            // Close brew window for shading
            brew_event_stop(millis());
            // No timer-based switch here; handled by recovery window logic
        }
        last_pump_status = pump;
    }
}
// CHART DATA PROCESSING - Convert raw 400ms data to chart resolution
void process_chart_data() {
    static uint32_t last_1s_update = 0;
    static uint32_t last_lowres_update = 0;
    uint32_t now = millis();
    
    // Update 1s chart data every 1000ms
    if (now - last_1s_update >= HIGH_RES_INTERVAL_MS) {
        float avg_steam = get_averaged_temp_at_time(now, HIGH_RES_INTERVAL_MS, 0);
        float avg_hx = get_averaged_temp_at_time(now, HIGH_RES_INTERVAL_MS, 1);
        float avg_target = get_averaged_temp_at_time(now, HIGH_RES_INTERVAL_MS, 2);
        
        if (avg_steam > 0) {  // Valid data
            steam_1s[data_index_1s] = avg_steam;
            hx_1s[data_index_1s] = avg_hx;
            target_1s[data_index_1s] = avg_target;
            timestamps_1s[data_index_1s] = now;
            
            data_index_1s = (data_index_1s + 1) % HIGH_RES_POINTS;
            if (data_index_1s == 0) data_filled_1s = true;
            
            // Update high-res chart if active
            if (is_high_res_mode()) {
                update_chart_display();
            }
            
            last_1s_update = now;
            ESP_LOGD("chart_1s", "Added: S=%.1f H=%.1f T=%.1f", avg_steam, avg_hx, avg_target);
        }
    }
    
    // Update low-res chart data every 15000ms
    if (now - last_lowres_update >= LOW_RES_INTERVAL_MS) {
        float avg_steam = get_averaged_temp_at_time(now, LOW_RES_INTERVAL_MS, 0);
        float avg_hx = get_averaged_temp_at_time(now, LOW_RES_INTERVAL_MS, 1);
        float avg_target = get_averaged_temp_at_time(now, LOW_RES_INTERVAL_MS, 2);
        
        if (avg_steam > 0) {  // Valid data
            steam_15s[data_index_15s] = avg_steam;
            hx_15s[data_index_15s] = avg_hx;
            target_15s[data_index_15s] = avg_target;
            timestamps_15s[data_index_15s] = now;
            
            data_index_15s = (data_index_15s + 1) % LOW_RES_POINTS;
            if (data_index_15s == 0) data_filled_15s = true;
            
            // Update low-res chart if active
            if (!is_high_res_mode()) {
                update_chart_display();
            }
            
            last_lowres_update = now;
            ESP_LOGD("chart_15s", "Added: S=%.1f H=%.1f T=%.1f", avg_steam, avg_hx, avg_target);
        }
    }
}
            
// HELPER FUNCTION - Get averaged temperature from raw data buffer.
// Walks backward from the most recent insertion. Because timestamps in the
// raw buffer are monotonically increasing, the first entry older than
// start_time means everything earlier is also out-of-window and we can stop.
// This keeps the inner loop O(k) where k is the number of points in the
// window (~3 for a 1s window, ~38 for a 15s window) instead of always
// scanning all 200 buffer slots.
float get_averaged_temp_at_time(uint32_t target_time, uint32_t window_ms, int temp_type) {
    uint32_t start_time = target_time - window_ms;
    uint32_t end_time = target_time;

    int available = raw_data_filled ? RAW_DATA_BUFFER_SIZE : raw_data_index;
    if (available == 0) return 0.0f;

    float sum = 0.0f;
    int count = 0;
    for (int i = 0; i < available; i++) {
        int idx = (raw_data_index - 1 - i + RAW_DATA_BUFFER_SIZE) % RAW_DATA_BUFFER_SIZE;
        uint32_t ts = raw_data[idx].timestamp;
        if (ts == 0) break;            // uninitialized slot beyond valid data
        if (ts < start_time) break;    // older than window; earlier slots are older still
        if (ts > end_time) continue;   // skip future-dated entries (rare)

        float v = 0.0f;
        switch (temp_type) {
            case 0: v = raw_data[idx].steam; break;
            case 1: v = raw_data[idx].hx; break;
            case 2: v = raw_data[idx].target; break;
        }
        if (v > 0.0f && v < 200.0f) {
            sum += v;
            count++;
        }
    }

    return count > 0 ? sum / count : 0.0f;
}

// CHART DISPLAY UPDATE - Professional instrument performance
void update_chart_display() {
    if (temp_chart == nullptr) return;
    
    // Safety: switch back to low-res after recovery window + buffer, only if not brewing
    auto pump_active_now = []() -> bool {
        if (id(demo_mode_enabled)) {
            return demo_is_brewing();
        }
        if (!raw_data_filled && raw_data_index == 0) return false;
        int last = (raw_data_index - 1 + RAW_DATA_BUFFER_SIZE) % RAW_DATA_BUFFER_SIZE;
        return raw_data[last].pump_status == 1;
    };

    if (is_high_res_mode() && !pump_active_now()) {
        if (has_recovered()) {
            // Record data-driven recovery moment for shading
            brew_event_recovered(millis());
            id(high_res_chart) = false;
            ESP_LOGI("chart", "Auto-switching back to low-res (recovered)");
            switch_chart_resolution();
            return; // Avoid further work in this update cycle after mode change
        }
    }

    bool high_res = is_high_res_mode();
    
    // Get current data arrays
    float* steam_data = high_res ? steam_1s : steam_15s;
    float* hx_data = high_res ? hx_1s : hx_15s;
    float* target_data = high_res ? target_1s : target_15s;
    
    // Add latest data point to chart
    int latest_idx = high_res ? 
        (data_index_1s - 1 + HIGH_RES_POINTS) % HIGH_RES_POINTS :
        (data_index_15s - 1 + LOW_RES_POINTS) % LOW_RES_POINTS;
        
    lv_chart_set_next_value(temp_chart, steam_series, (int32_t)steam_data[latest_idx]);
    lv_chart_set_next_value(temp_chart, hx_series, (int32_t)hx_data[latest_idx]);
    lv_chart_set_next_value(temp_chart, target_series, (int32_t)target_data[latest_idx]);

    // Hysteresis on Y range: lv_chart_set_range always refreshes the chart
    // internally, so calling it on every update — even when the bounds barely
    // moved — forced a full repaint at 1 Hz. Only commit a new range when it
    // drifts past a threshold; otherwise just refresh so the shading bands
    // continue to slide left with time.
    float min_temp, max_temp;
    get_temp_range(min_temp, max_temp);
    int32_t y_min = (int32_t)min_temp;
    int32_t y_max = (int32_t)max_temp;
    int32_t dmin = y_min - last_y_min; if (dmin < 0) dmin = -dmin;
    int32_t dmax = y_max - last_y_max; if (dmax < 0) dmax = -dmax;
    bool range_changed = (last_y_min == -9999) || dmin > 2 || dmax > 2;
    if (range_changed) {
        lv_chart_set_range(temp_chart, LV_CHART_AXIS_PRIMARY_Y, y_min, y_max);
        last_y_min = y_min;
        last_y_max = y_max;
    } else {
        lv_chart_refresh(temp_chart);
    }
    ESP_LOGD("chart", "Y-range %s: %.0f-%.0f°C (mode: %s)",
             range_changed ? "updated" : "stable",
             min_temp, max_temp, high_res ? "1s" : "15s");
}

// Legacy function - now redirects to professional system
void add_temp_data(float steam, float hx, float target) {
    // This function is kept for compatibility but now uses the professional system
    add_raw_uart_data(steam, hx, target, 0, 0);  // Default heat=0, pump=0 for legacy calls
}

// Safe resolution check function
bool is_high_res_mode() {
    // Use a static flag to track initialization state
    static bool globals_initialized = false;
    static bool cached_mode = false;
    
    // Check if we can safely access globals (after 5 seconds boot time)
    if (!globals_initialized && millis() > 5000) {
        globals_initialized = true;
    }
    
    if (globals_initialized) {
        cached_mode = id(high_res_chart);
        return cached_mode;
    } else {
        return false;  // Default to low-res during early initialization
    }
}

// Update chart point count when resolution changes - both use 60 points now
void update_chart_point_count() {
    if (temp_chart != nullptr) {
        // Match point count to resolution: 50 (1s) or 60 (15s)
        if (is_high_res_mode()) {
            lv_chart_set_point_count(temp_chart, HIGH_RES_POINTS);
        } else {
            lv_chart_set_point_count(temp_chart, LOW_RES_POINTS);
        }
    }
}

// Helper functions for professional chart system
float* get_current_steam_data() {
    return is_high_res_mode() ? steam_1s : steam_15s;
}

float* get_current_hx_data() {
    return is_high_res_mode() ? hx_1s : hx_15s;
}

float* get_current_target_data() {
    return is_high_res_mode() ? target_1s : target_15s;
}

int get_current_data_count() {
    if (is_high_res_mode()) {
        return data_filled_1s ? HIGH_RES_POINTS : data_index_1s;
    } else {
        return data_filled_15s ? LOW_RES_POINTS : data_index_15s;
    }
}

int get_current_data_points() {
    return is_high_res_mode() ? HIGH_RES_POINTS : LOW_RES_POINTS;
}

// OPTIMIZED min/max calculation using current resolution data
void get_temp_range(float& min_temp, float& max_temp) {
    min_temp = 999.0f;
    max_temp = 0.0f;
    bool found_valid_data = false;

    float* steam_data = get_current_steam_data();
    float* hx_data = get_current_hx_data();
    float* target_data = get_current_target_data();
    int points = get_current_data_count();
    
    // Process all temperature values in current chart buffer
    for (int i = 0; i < points; i++) {
        float steam_val = steam_data[i];
        float hx_val = hx_data[i];
        float target_val = target_data[i];
        
        // Skip invalid data points (uninitialized or clearly invalid)
        if (steam_val <= 0 || hx_val <= 0 || target_val <= 0 || 
            steam_val > 200 || hx_val > 200 || target_val > 200) {
            continue;
        }
        
        // Find min/max for each temperature series
        if (!found_valid_data) {
            min_temp = steam_val;
            max_temp = steam_val;
            found_valid_data = true;
        }
        
        // Check all three temperatures
        min_temp = min({min_temp, steam_val, hx_val, target_val});
        max_temp = max({max_temp, steam_val, hx_val, target_val});
    }
    
    // Fallback to reasonable defaults if no valid data found
    if (!found_valid_data) {
        min_temp = 60.0f;
        max_temp = 140.0f;
        ESP_LOGW("chart", "No valid temperature data found, using default range 60-140°C");
        return;
    }

    // Add padding for better visibility (15% on each side)
    float range = max_temp - min_temp;
    if (range < 20.0f) range = 20.0f;  // Minimum 20°C range for better readability
    float padding = range * 0.15f;  // 15% padding
    min_temp -= padding;
    max_temp += padding;

    // Sensible bounds for espresso machine and round nicely
    if (min_temp < 0) min_temp = 0;
    if (max_temp > 200) max_temp = 200;
    // Round to whole degrees to avoid clipping due to truncation
    min_temp = floorf(min_temp);
    max_temp = ceilf(max_temp);
    
    ESP_LOGD("chart", "Temperature range calculated: %.1f - %.1f°C (range: %.1f°C)", min_temp, max_temp, max_temp - min_temp);
}

// Full chart rebuild - ensures proper time-based display for resolution switching
void update_temp_chart() {
    if (temp_chart == nullptr) return;

    // Clear existing data
    lv_chart_set_all_value(temp_chart, steam_series, LV_CHART_POINT_NONE);
    lv_chart_set_all_value(temp_chart, hx_series, LV_CHART_POINT_NONE);
    lv_chart_set_all_value(temp_chart, target_series, LV_CHART_POINT_NONE);

    // Get current time-based parameters
    bool high_res = is_high_res_mode();
    uint32_t now = millis();
    // Use configured windows and intervals
    uint32_t time_span = (high_res ? HIGH_RES_WINDOW_SEC : LOW_RES_WINDOW_SEC) * 1000; // 50s or 900s
    uint32_t interval = high_res ? HIGH_RES_INTERVAL_MS : LOW_RES_INTERVAL_MS;         // 1s or 15s intervals
    
    // Calculate how many data points we need to fill the timeframe
    int target_points = time_span / interval;
    int chart_points = high_res ? HIGH_RES_POINTS : LOW_RES_POINTS;
    target_points = min(target_points, chart_points);
    
    bool used_local_range = false;
    float used_min = 0.0f, used_max = 0.0f;
    if (high_res) {
        // Build high-res chart directly from raw buffer to avoid gaps
        static float last_steam = 0, last_hx = 0, last_target = 0;
        float local_min = 999.0f, local_max = 0.0f;
        for (int i = target_points; i > 0; --i) {
            uint32_t bucket_end = now - (uint32_t)(i - 1) * interval;
            float avg_steam = get_averaged_temp_at_time(bucket_end, interval, 0);
            float avg_hx = get_averaged_temp_at_time(bucket_end, interval, 1);
            float avg_target = get_averaged_temp_at_time(bucket_end, interval, 2);

            bool steam_ok = avg_steam > 0 && avg_steam < 200;
            bool hx_ok = avg_hx > 0 && avg_hx < 200;
            bool target_ok = avg_target > 0 && avg_target < 200;
            if (!steam_ok) avg_steam = last_steam;
            if (!hx_ok) avg_hx = last_hx;
            if (!target_ok) avg_target = last_target;

            lv_chart_set_next_value(temp_chart, steam_series, (int32_t)avg_steam);
            lv_chart_set_next_value(temp_chart, hx_series, (int32_t)avg_hx);
            lv_chart_set_next_value(temp_chart, target_series, (int32_t)avg_target);

            last_steam = avg_steam;
            last_hx = avg_hx;
            last_target = avg_target;

            // Track min/max for Y range in this rebuild
            if (steam_ok || hx_ok || target_ok) {
                float tmin = avg_steam;
                float tmax = avg_steam;
                if (hx_ok) { tmin = min(tmin, avg_hx); tmax = max(tmax, avg_hx); }
                if (target_ok) { tmin = min(tmin, avg_target); tmax = max(tmax, avg_target); }
                local_min = min(local_min, tmin);
                local_max = max(local_max, tmax);
            }
        }

        // If we collected valid min/max, set a padded Y range now
        if (local_min < local_max && local_min < 900.0f) {
            float range = local_max - local_min;
            if (range < 20.0f) range = 20.0f;
            float padding = range * 0.15f;
            float y_min = floorf(max(0.0f, local_min - padding));
            float y_max = ceilf(min(200.0f, local_max + padding));
            lv_chart_set_range(temp_chart, LV_CHART_AXIS_PRIMARY_Y, (int32_t)y_min, (int32_t)y_max);
            used_local_range = true;
            used_min = y_min;
            used_max = y_max;
        }
    } else {
        // Use low-res 15s buffers (covers 15m)
        float* steam_data = steam_15s;
        float* hx_data = hx_15s;
        float* target_data = target_15s;
        uint32_t* timestamps = timestamps_15s;
        int data_idx = data_index_15s;
        bool data_filled = data_filled_15s;
        int data_size = LOW_RES_POINTS;

        int available_points = data_filled ? data_size : data_idx;
        int start_idx = 0;
        if (data_filled) {
            uint32_t oldest_time = now - time_span;
            start_idx = data_idx;
            for (int i = 0; i < data_size; i++) {
                int idx = (data_idx + i) % data_size;
                if (timestamps[idx] >= oldest_time) { start_idx = idx; break; }
            }
        }
        int points_added = 0;
        for (int i = 0; i < available_points && points_added < target_points; i++) {
            int idx = (start_idx + i) % data_size;
            if (timestamps[idx] == 0 || (now - timestamps[idx]) > time_span) continue;
            lv_chart_set_next_value(temp_chart, steam_series, (int32_t)steam_data[idx]);
            lv_chart_set_next_value(temp_chart, hx_series, (int32_t)hx_data[idx]);
            lv_chart_set_next_value(temp_chart, target_series, (int32_t)target_data[idx]);
            points_added++;
        }
    }

    // Update chart range with dynamic scaling
    float min_temp = 0, max_temp = 0;
    if (!used_local_range) {
        get_temp_range(min_temp, max_temp);
        lv_chart_set_range(temp_chart, LV_CHART_AXIS_PRIMARY_Y, (int32_t)min_temp, (int32_t)max_temp);
    } else {
        min_temp = used_min;
        max_temp = used_max;
    }
    
    // Dynamic grid lines based on temperature range
    float temp_range = max_temp - min_temp;
    int y_divs = 4;  // Default
    if (temp_range > 50) {
        y_divs = 8;  // More divisions for large ranges
    } else if (temp_range > 30) {
        y_divs = 6;  // Medium divisions
    } else if (temp_range > 15) {
        y_divs = 4;  // Fewer divisions for small ranges
    } else {
        y_divs = 3;  // Minimal for very small ranges
    }
    lv_chart_set_div_line_count(temp_chart, y_divs, 6);  // 6 time divisions

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

// draw event moved to chart_draw.h

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

    // Chart configuration - point count will be adjusted per resolution
    lv_chart_set_type(temp_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(temp_chart, 60);  // Will be updated by update_chart_point_count()
    lv_chart_set_range(temp_chart, LV_CHART_AXIS_PRIMARY_Y, 60, 140);  // Default range

    // Disable all interaction and scrollbars
    lv_obj_clear_flag(temp_chart, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(temp_chart, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_clear_flag(temp_chart, LV_OBJ_FLAG_SCROLL_CHAIN_HOR);
    lv_obj_clear_flag(temp_chart, LV_OBJ_FLAG_SCROLL_CHAIN_VER);
    // No padding — tick labels are now painted in the parent (graph_area)
    // outside the chart's outer bounds, not in a padding gutter inside it.
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

    // Tick labels are now drawn manually by chart_tick_label_cb during the
    // chart's MAIN_END draw event — v9 removed lv_chart_set_axis_tick and the
    // LV_PART_TICKS draw path entirely.

    // Series styling - lines only, no dots
    lv_obj_set_style_line_width(temp_chart, 3, LV_PART_ITEMS);
    // 4-arg form in v9 (was 3-arg in v8): width, height, selector.
    lv_obj_set_style_size(temp_chart, 0, 0, LV_PART_INDICATOR);

    // Add axis unit label (°C) - positioned below the highest Y value
    lv_obj_t* y_label = lv_label_create(parent);
    lv_label_set_text(y_label, "°C");
    lv_obj_set_style_text_color(y_label, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(y_label, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(y_label, 5, 25);  // Below first Y-axis value

    // Shading paints behind the series on the chart's own MAIN_BEGIN.
    // Tick labels live on the *parent* (graph_area) so they can render in
    // the area to the left of and below the chart — the chart's draw event
    // can only paint inside the chart's own outer bounds. POST_END on the
    // parent fires after the chart has been drawn, so labels sit on top.
    lv_obj_add_event_cb(temp_chart, chart_shading_cb, LV_EVENT_DRAW_MAIN_BEGIN, nullptr);
    lv_obj_add_event_cb(parent, chart_tick_label_cb, LV_EVENT_DRAW_POST_END, nullptr);

    // Chart will be populated by existing data when resolution changes
    // No need to reset indices - data persists across resolution switches
}

// Switch the chart between high-res and low-res without tearing it down.
// Tick labels are computed dynamically in chart_tick_label_cb from
// is_high_res_mode(), so a full rebuild is unnecessary — just resize the
// point buffer and re-fill it from the appropriate data source.
void switch_chart_resolution() {
    if (temp_chart == nullptr) return;
    update_chart_point_count();
    update_temp_chart();
}

// Clear all chart data when demo mode is disabled
void clear_chart_data() {
    // Clear raw data buffer
    for (int i = 0; i < RAW_DATA_BUFFER_SIZE; i++) {
        raw_data[i] = {0, 0, 0, 0, 0, 0};
    }
    raw_data_index = 0;
    raw_data_filled = false;
    
    // Reset high-resolution (1s) arrays
    for (int i = 0; i < HIGH_RES_POINTS; i++) {
        steam_1s[i] = 0.0f;
        hx_1s[i] = 0.0f;
        target_1s[i] = 0.0f;
        timestamps_1s[i] = 0;
    }
    
    // Reset low-resolution (5s) arrays  
    for (int i = 0; i < LOW_RES_POINTS; i++) {
        steam_15s[i] = 0.0f;
        hx_15s[i] = 0.0f;
        target_15s[i] = 0.0f;
        timestamps_15s[i] = 0;
    }
    
    // Reset data indices and filled flags
    data_index_1s = 0;
    data_filled_1s = false;
    data_index_15s = 0;
    data_filled_15s = false;
    
    // Clear chart display if it exists
    if (temp_chart != nullptr) {
        lv_chart_set_all_value(temp_chart, steam_series, LV_CHART_POINT_NONE);
        lv_chart_set_all_value(temp_chart, hx_series, LV_CHART_POINT_NONE);
        lv_chart_set_all_value(temp_chart, target_series, LV_CHART_POINT_NONE);
        lv_chart_refresh(temp_chart);
    }

    // Display labels are blanked externally (display_ui.yaml demo-off branch
    // or the UART disconnect handler), so the cache must forget what was on
    // screen — otherwise the next data tick would skip the re-render.
    reset_temperature_displays_cache();
}
