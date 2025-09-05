#pragma once
#include "esphome.h"

// Timer state
static uint32_t timer_start = 0;
static bool timer_running = false;
static uint32_t last_flash = 0;
static bool flash_state = false;

// Timer control functions
void reset_timer() {
    timer_start = millis();
    timer_running = false;
    flash_state = false;
}

void start_timer() {
    if (!timer_running) {
        timer_start = millis();
        timer_running = true;
        // Reset flash state when starting
        flash_state = false;
        last_flash = 0;
    }
}

void stop_timer() {
    timer_running = false;
}

// Update timer display with optimized string formatting
void update_timer_display(lv_obj_t* timer_label) {
    if (!timer_running) {
        return;
    }
    
    uint32_t now = millis();
    uint32_t elapsed_ms = now - timer_start;
    int minutes = elapsed_ms / 60000;
    int seconds = (elapsed_ms % 60000) / 1000;
    int deciseconds = (elapsed_ms % 1000) / 100;
    
    // Cache formatted strings to reduce string operations
    static char timer_text[16];
    static uint32_t last_update_ms = 0;
    
    // Only update string if deciseconds changed (reduce sprintf calls)
    if (elapsed_ms / 100 != last_update_ms / 100) {
        sprintf(timer_text, "%02d:%02d.%01d", minutes, seconds, deciseconds);
        lv_label_set_text(timer_label, timer_text);
        last_update_ms = elapsed_ms;
    }
    
    // Handle flash mode (30-33s window)
    bool flash_mode = (elapsed_ms >= 30000 && elapsed_ms < 33000);
    
    if (flash_mode) {
        // Flash every 300ms for 3 seconds only
        if (now - last_flash > 300) {
            flash_state = !flash_state;
            last_flash = now;
        }
    } else {
        flash_state = false;
    }
    
    // Update color only when state changes to reduce LVGL calls
    static bool last_flash_state = false;
    static bool last_flash_mode = false;
    static bool color_initialized = false;
    
    // Ensure timer starts green on first run
    if (!color_initialized) {
        lv_obj_set_style_text_color(timer_label, lv_color_hex(0x00FF00), 0);  // Green initial
        color_initialized = true;
        last_flash_state = flash_state;
        last_flash_mode = flash_mode;
        return;
    }
    
    if (flash_state != last_flash_state || flash_mode != last_flash_mode) {
        if (flash_mode && flash_state) {
            lv_obj_set_style_text_color(timer_label, lv_color_hex(0xFF0000), 0);  // Red flash
        } else {
            lv_obj_set_style_text_color(timer_label, lv_color_hex(0x00FF00), 0);  // Green normal
        }
        last_flash_state = flash_state;
        last_flash_mode = flash_mode;
    }
}

// Check if timer reset was requested and handle it
void handle_timer_reset_request(bool reset_requested) {
    if (reset_requested) {
        reset_timer();
    }
}