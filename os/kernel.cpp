typedef unsigned char uint8_t;
typedef signed char int8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;
typedef signed long long int64_t;
typedef unsigned long long uintptr_t;

static void put_pixel(int x, int y, uint32_t color);
static void pixel(uint16_t x, uint16_t y, uint8_t color);
static void draw_start_menu_actions();
static void restore_mouse_cursor();
static void draw_mouse_cursor();
static void handle_mouse_packet();

#include <logo_data.h>
#include <mouse_cursor_data.h>
#include <zex_apps.h>
#include "chinese_font.h"
#include "../HZKX/asc48_map.h"
#include "ascii_font.h"
#include <cstddef>

static volatile uint32_t* framebuffer;
static uint32_t* render_target;
static uint32_t screen_width;
static uint32_t screen_height;
static uint32_t screen_pitch;
static bool start_menu_open;
static int mouse_x;
static int mouse_y;
static uint8_t mouse_packet[3];
static uint8_t mouse_packet_stage;
static uint8_t mouse_buttons;
static bool mouse_cursor_drawn;
static bool keyboard_extended;
static bool keyboard_alt_down;
static uint8_t menu_selection;
static uint32_t mouse_cursor_background[MOUSE_CURSOR_WIDTH * MOUSE_CURSOR_HEIGHT];
static uint8_t mouse_cursor_coverage[MOUSE_CURSOR_WIDTH * MOUSE_CURSOR_HEIGHT];
static uint8_t mouse_cursor_shade[MOUSE_CURSOR_WIDTH * MOUSE_CURSOR_HEIGHT];
static const uintptr_t backbuffer_address = 0x01000000;
// Preserve the clean desktop so overlays only need to redraw their own bounds.
static const uintptr_t desktop_cache_address = 0x02000000;
static const uintptr_t start_menu_cache_address = 0x02800000;
static const uintptr_t zex_window_cache_address = 0x02940000;
static const uint32_t start_menu_width = 570;
static const uint32_t start_menu_height = 530;
static const uint32_t zex_window_cache_width_limit = 600;
static const uint32_t zex_window_cache_height_limit = 420;
static const uint32_t zex_window_cache_slot_size =
    zex_window_cache_width_limit * zex_window_cache_height_limit;
static const uintptr_t wallpaper_address = 0x00020000;
static const uintptr_t zex_load_address = 0x00400000;
static const uint32_t file_manager_visible_entries = 20;
static const uint32_t file_manager_entry_limit = 128;

struct ZexRegisterFrame {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rdi, rsi, rbp, rdx, rcx, rbx, rax;
    uint64_t rip, cs, rflags;
};

struct ZexIdtEntry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t attributes;
    uint16_t offset_middle;
    uint32_t offset_high;
    uint32_t reserved;
} __attribute__((packed));

struct ZexProgram {
    const uint8_t* image;
    uint32_t image_size;
};

struct ZexRuntime {
    bool running;
    bool window_open;
    bool close_requested;
    bool file_manager;
    uint32_t code_end;
    uint32_t data_start;
    uint32_t data_end;
    int window_width;
    int window_height;
    char title[64];
    char message[128];
};

struct FatVolume {
    uint8_t ata_drive;
    uint8_t fat_type;
    uint8_t sectors_per_cluster;
    uint8_t fat_count;
    uint16_t reserved_sectors;
    uint16_t root_entry_count;
    uint32_t partition_start;
    uint32_t partition_sectors;
    uint32_t total_sectors;
    uint32_t fat_sectors;
    uint32_t root_cluster;
    uint32_t root_sector;
    uint32_t root_sectors;
    uint32_t data_sector;
    bool mounted;
};

struct FileManagerEntry {
    char name[64];
    uint32_t cluster;
    uint32_t size;
    bool directory;
};

static ZexIdtEntry zex_idt[256];
static uint8_t zex_idtr[10];
static ZexRuntime zex_runtime;
static FatVolume file_manager_volumes[2];
static FileManagerEntry file_manager_entries[file_manager_entry_limit];
static uint32_t file_manager_entry_count;
static uint32_t file_manager_selected;
static uint32_t file_manager_scroll;
static uint32_t file_manager_directory_cluster;
static uint32_t file_manager_parent_clusters[16];
static char file_manager_parent_names[16][64];
static uint8_t file_manager_parent_depth;
static uint8_t file_manager_volume_index;
static bool file_manager_entries_truncated;
static bool file_manager_read_error;
static char file_manager_status[128];
static uint16_t file_manager_long_name[260];
static uint8_t ata_sector_buffer[512];
static uint8_t fat_sector_buffer[512];
static uint8_t directory_sector_buffer[512];
static bool start_menu_cache_ready;
static bool zex_window_cache_valid[5];
static uint32_t zex_window_cache_width[5];
static uint32_t zex_window_cache_height[5];
static char zex_window_cache_title[5][sizeof(zex_runtime.title)];
static char zex_window_cache_message[5][sizeof(zex_runtime.message)];
static uint32_t active_zex_program;
static const ZexProgram zex_programs[] = {
    { zex_filemgr, zex_filemgr_size },
    { zex_notepad, zex_notepad_size },
    { zex_browser, zex_browser_size },
    { zex_settings, zex_settings_size },
    { zex_terminal, zex_terminal_size }
};

extern "C" void zex_interrupt_handler();
extern "C" void zex_exit_trampoline();
extern "C" void zex_call_entry(uint64_t entry);
extern "C" void zex_syscall_dispatch(ZexRegisterFrame* registers);

static void run_zex_file_manager();
static void render_zex_window();

static inline void copy_to_framebuffer(
    volatile uint32_t* destination,
    const uint32_t* source,
    uint32_t pixel_count
) {
    __asm__ volatile(
        "cld; rep movsl"
        : "+D"(destination), "+S"(source), "+c"(pixel_count)
        :
        : "memory"
    );
}

static void initialize_mouse_cursor() {
    for (int index = 0; index < MOUSE_CURSOR_WIDTH * MOUSE_CURSOR_HEIGHT; ++index) {
        mouse_cursor_background[index] = 0;
        mouse_cursor_coverage[index] = 0;
        mouse_cursor_shade[index] = 0;
    }
    for (int row = 0; row < MOUSE_CURSOR_HEIGHT; ++row) {
        for (int column = 0; column < MOUSE_CURSOR_WIDTH; ++column) {
            const int index = row * MOUSE_CURSOR_WIDTH + column;
            mouse_cursor_coverage[index] = mouse_cursor_alpha[index];
            mouse_cursor_shade[index] = mouse_cursor_luma[index];
        }
    }
}

static const uint32_t logo_colors[16] = {
    0x000000, 0x172554, 0x14532D, 0x155E75,
    0x7F1D1D, 0x701A75, 0x854D0E, 0xCBD5E1,
    0x64748B, 0x60A5FA, 0x4ADE80, 0x67E8F9,
    0xF87171, 0xE879F9, 0x75D8FF, 0xFFFFFF
};

static void pixel(uint16_t x, uint16_t y, uint8_t color) {
    put_pixel(x, y, logo_colors[color & 15]);
}

static uint32_t mix_color(uint32_t a, uint32_t b, uint32_t amount, uint32_t total) {
    const uint32_t inverse = total - amount;
    const uint32_t red = (((a >> 16) & 255) * inverse + ((b >> 16) & 255) * amount) / total;
    const uint32_t green = (((a >> 8) & 255) * inverse + ((b >> 8) & 255) * amount) / total;
    const uint32_t blue = ((a & 255) * inverse + (b & 255) * amount) / total;
    return (red << 16) | (green << 8) | blue;
}

static void put_pixel(int x, int y, uint32_t color) {
    if (x >= 0 && y >= 0 && static_cast<uint32_t>(x) < screen_width &&
        static_cast<uint32_t>(y) < screen_height) {
        framebuffer[static_cast<uint32_t>(y) * (screen_pitch / 4) + static_cast<uint32_t>(x)] = color;
    }
}

static void fill_rect(int x, int y, int width, int height, uint32_t color) {
    if (width <= 0 || height <= 0) return;
    int left = x < 0 ? 0 : x;
    int top = y < 0 ? 0 : y;
    int right = x + width > static_cast<int>(screen_width) ? static_cast<int>(screen_width) : x + width;
    int bottom = y + height > static_cast<int>(screen_height) ? static_cast<int>(screen_height) : y + height;
    for (int row = top; row < bottom; ++row) {
        uint32_t* line = render_target + static_cast<uint32_t>(row) * (screen_pitch / 4) + left;
        for (int column = left; column < right; ++column) *line++ = color;
    }
}

static void draw_triangle(int x1, int y1, int x2, int y2, int x3, int y3, uint32_t color) {
    if (y1 > y2) { int t=x1; x1=x2; x2=t; t=y1; y1=y2; y2=t; }
    if (y2 > y3) { int t=x2; x2=x3; x3=t; t=y2; y2=y3; y3=t; }
    if (y1 > y2) { int t=x1; x1=x2; x2=t; t=y1; y1=y2; y2=t; }
    for (int y = y1; y <= y3; ++y) {
        if (y < 0 || y >= static_cast<int>(screen_height)) continue;
        int long_x = x1 + (y3 == y1 ? 0 : (x3 - x1) * (y - y1) / (y3 - y1));
        int short_x;
        if (y < y2) short_x = x1 + (y2 == y1 ? 0 : (x2 - x1) * (y - y1) / (y2 - y1));
        else short_x = x2 + (y3 == y2 ? 0 : (x3 - x2) * (y - y2) / (y3 - y2));
        if (long_x > short_x) { int t = long_x; long_x = short_x; short_x = t; }
        fill_rect(long_x, y, short_x - long_x + 1, 1, color);
    }
}

static int find_chinese_glyph(uint32_t codepoint) {
    for (int index = 0; index < CHINESE_GLYPH_COUNT; ++index) {
        if (chinese_glyph_codepoints[index] == codepoint) return index;
    }
    return -1;
}

static void draw_chinese_glyph(int glyph_index, int x, int y, int scale, uint32_t color) {
    if (glyph_index < 0) return;
    const uint8_t* bitmap = chinese_glyph_bitmaps[glyph_index];
    for (int row = 0; row < 24; ++row) {
        for (int column = 0; column < 24; ++column) {
            if (bitmap[row * 3 + column / 8] & (0x80 >> (column & 7))) {
                fill_rect(x + column * scale, y + row * scale, scale, scale, color);
            }
        }
    }
}

static bool ascii_source_pixel(const uint8_t* bitmap, int x, int y) {
    return (bitmap[y * 6 + x / 8] & (0x80 >> (x & 7))) != 0;
}

static int draw_ascii_character(char value, int x, int y, int scale, uint32_t color) {
    const uint8_t* bitmap = asc48_get_bitmap(value, ascii_glyph_bitmaps[0]);
    if (bitmap == NULL) {
        const int pixel_scale = scale * 3;
        if (value == '.') {
            fill_rect(x, y + 6 * pixel_scale, pixel_scale, pixel_scale, color);
            return 6 * scale;
        }
        if (value == '-') {
            fill_rect(x, y + 3 * pixel_scale, 4 * pixel_scale, pixel_scale, color);
            return 15 * scale;
        }
        return 15 * scale;
    }

    int first_column = 48;
    int last_column = -1;
    for (int source_y = 0; source_y < 48; ++source_y) {
        for (int source_x = 0; source_x < 48; ++source_x) {
            if (!ascii_source_pixel(bitmap, source_x, source_y)) continue;
            if (source_x < first_column) first_column = source_x;
            if (source_x > last_column) last_column = source_x;
        }
    }
    if (last_column < first_column) return 15 * scale;

    const uint32_t row_stride = screen_pitch / 4;
    const int source_width = last_column - first_column + 1;
    const int output_width = (source_width * scale + 1) / 2;
    const int output_height = 24 * scale;
    for (int output_y = 0; output_y < output_height; ++output_y) {
        const int target_y = y + output_y;
        if (target_y < 0 || target_y >= static_cast<int>(screen_height)) continue;
        uint32_t* line = render_target + static_cast<uint32_t>(target_y) * row_stride;
        for (int output_x = 0; output_x < output_width; ++output_x) {
            int coverage = 0;
            for (int sample_y = 0; sample_y < 2; ++sample_y) {
                const int source_y =
                    (output_y * 2 + sample_y) / scale;
                for (int sample_x = 0; sample_x < 2; ++sample_x) {
                    const int source_x = first_column +
                        (output_x * 2 + sample_x) / scale;
                    if (source_x <= last_column &&
                        ascii_source_pixel(bitmap, source_x, source_y)) {
                        ++coverage;
                    }
                }
            }
            const int target_x = x + output_x;
            if (coverage == 0 || target_x < 0 ||
                target_x >= static_cast<int>(screen_width)) continue;
            line[target_x] = mix_color(line[target_x], color,
                                       static_cast<uint32_t>(coverage), 4);
        }
    }
    return output_width + 2 * scale;
}

static void draw_text(const char* text, int x, int y, int scale, uint32_t color) {
    const uint8_t* current = reinterpret_cast<const uint8_t*>(text);
    while (*current) {
        uint32_t codepoint;
        if (*current < 0x80) {
            codepoint = *current++;
        } else if ((*current & 0xE0) == 0xC0 && current[1] != 0) {
            codepoint = ((*current & 0x1F) << 6) | (current[1] & 0x3F);
            current += 2;
        } else if ((*current & 0xF0) == 0xE0 && current[1] != 0 && current[2] != 0) {
            codepoint = ((*current & 0x0F) << 12) |
                        ((current[1] & 0x3F) << 6) | (current[2] & 0x3F);
            current += 3;
        } else {
            ++current;
            x += 24 * scale;
            continue;
        }

        if (codepoint >= 0x4E00 && codepoint <= 0x9FFF) {
            draw_chinese_glyph(find_chinese_glyph(codepoint), x, y, scale, color);
            x += 24 * scale;
        } else if (codepoint == ' ') {
            x += 7 * scale;
        } else {
            x += draw_ascii_character(static_cast<char>(codepoint), x, y, scale, color);
        }
    }
}

static void decode_logo_row(
    uint8_t* row,
    uint32_t& run_offset,
    uint32_t& run_remaining,
    uint8_t& run_value
) {
    for (int column = 0; column < LOGO_WIDTH; ++column) {
        if (run_remaining == 0) {
            run_remaining = logo_alpha_rle[run_offset++];
            run_value = logo_alpha_rle[run_offset++];
        }
        row[column] = run_value;
        --run_remaining;
    }
}

static void draw_logo(int x, int y, int output_size) {
    if (output_size <= 0) return;
    uint8_t row[LOGO_WIDTH];
    uint32_t run_offset = 0;
    uint32_t run_remaining = 0;
    uint8_t run_value = 0;
    for (int source_y = 0; source_y < LOGO_HEIGHT; ++source_y) {
        decode_logo_row(row, run_offset, run_remaining, run_value);
        const int top = y + source_y * output_size / LOGO_HEIGHT;
        const int bottom = y + (source_y + 1) * output_size / LOGO_HEIGHT;
        for (int source_x = 0; source_x < LOGO_WIDTH; ++source_x) {
            const uint8_t alpha = row[source_x];
            if (alpha == 0) continue;
            const int left = x + source_x * output_size / LOGO_WIDTH;
            const int right = x + (source_x + 1) * output_size / LOGO_WIDTH;
            if (alpha == 15) {
                fill_rect(left, top, right - left, bottom - top, logo_colors[14]);
                continue;
            }

            const int first_y = top < 0 ? 0 : top;
            const int last_y = bottom > static_cast<int>(screen_height)
                ? static_cast<int>(screen_height) : bottom;
            const int first_x = left < 0 ? 0 : left;
            const int last_x = right > static_cast<int>(screen_width)
                ? static_cast<int>(screen_width) : right;
            for (int target_y = first_y; target_y < last_y; ++target_y) {
                uint32_t* line = render_target +
                    static_cast<uint32_t>(target_y) * (screen_pitch / 4);
                for (int target_x = first_x; target_x < last_x; ++target_x) {
                    line[target_x] = mix_color(line[target_x], logo_colors[14],
                        static_cast<uint32_t>(alpha * 17), 255);
                }
            }
        }
    }
}

static void fill_rounded_rect_gradient(
    int x,
    int y,
    int width,
    int height,
    int radius,
    uint32_t top_color,
    uint32_t bottom_color
) {
    if (width <= 0 || height <= 0) return;
    if (radius * 2 > width) radius = width / 2;
    if (radius * 2 > height) radius = height / 2;

    const int radius_squared = radius * radius;
    for (int row = 0; row < height; ++row) {
        int inset = 0;
        int distance_from_corner = 0;
        if (row < radius) distance_from_corner = radius - row;
        else if (row >= height - radius) distance_from_corner = row - (height - radius - 1);

        while (inset < radius &&
               (radius - inset) * (radius - inset) +
                   distance_from_corner * distance_from_corner > radius_squared) {
            ++inset;
        }
        const uint32_t color = mix_color(
            top_color, bottom_color, static_cast<uint32_t>(row),
            static_cast<uint32_t>(height > 1 ? height - 1 : 1));
        fill_rect(x + inset, y + row, width - inset * 2, 1, color);
    }
}

static void draw_app_icon(int x, int y, bool explorer) {
    fill_rounded_rect_gradient(x, y, 96, 96, 20, 0x0A4D83, 0x07365F);
    fill_rounded_rect_gradient(x + 2, y + 2, 92, 92, 18, 0x51C5F5, 0x1672B8);
    fill_rounded_rect_gradient(x + 5, y + 5, 86, 86, 15, 0x35A8E5, 0x125B9C);

    if (explorer) {
        fill_rounded_rect_gradient(x + 14, y + 39, 68, 43, 8, 0x9A5B12, 0x623A13);
        fill_rounded_rect_gradient(x + 19, y + 28, 35, 18, 6, 0xFFE89C, 0xEBAF3B);
        fill_rounded_rect_gradient(x + 15, y + 34, 66, 43, 8, 0xFFE17A, 0xD98A20);
        fill_rounded_rect_gradient(x + 22, y + 39, 51, 31, 5, 0xFFFFFF, 0xDDEFFF);
        fill_rounded_rect_gradient(x + 28, y + 46, 34, 3, 1, 0x91B7D1, 0x6D9AB9);
        fill_rounded_rect_gradient(x + 28, y + 53, 27, 3, 1, 0xA7C7DA, 0x83ACC5);
        fill_rounded_rect_gradient(x + 28, y + 60, 31, 3, 1, 0xA7C7DA, 0x83ACC5);
        fill_rounded_rect_gradient(x + 18, y + 54, 63, 23, 6, 0xFFD66A, 0xE99A2A);
        fill_rounded_rect_gradient(x + 20, y + 54, 57, 4, 2, 0xFFF0A8, 0xF6C74D);
    } else {
        fill_rounded_rect_gradient(x + 18, y + 15, 60, 51, 7, 0xE6F8FF, 0x8AC9EA);
        fill_rounded_rect_gradient(x + 22, y + 19, 52, 41, 4, 0x173E68, 0x0E2D50);
        fill_rounded_rect_gradient(x + 26, y + 23, 44, 33, 2, 0x6ACAF3, 0x227DBB);
        fill_rounded_rect_gradient(x + 27, y + 24, 19, 14, 2, 0xC4F4FF, 0x72D2F5);
        fill_rounded_rect_gradient(x + 50, y + 24, 19, 14, 2, 0x8BE0FF, 0x48ACE4);
        fill_rounded_rect_gradient(x + 27, y + 41, 19, 14, 2, 0x79D8FA, 0x3F9DD8);
        fill_rounded_rect_gradient(x + 50, y + 41, 19, 14, 2, 0xB4EEFF, 0x66BFE8);
        fill_rounded_rect_gradient(x + 42, y + 65, 12, 13, 3, 0xE8F7FF, 0x9ACBE7);
        fill_rounded_rect_gradient(x + 31, y + 77, 34, 6, 3, 0xF2FBFF, 0x9FCBE3);
    }
}

static void draw_desktop_app_icon(int x, int y, uint8_t app) {
    if (app < 2) {
        draw_app_icon(x, y, app == 0);
        return;
    }

    fill_rounded_rect_gradient(x, y, 96, 96, 20, 0x0A4D83, 0x07365F);
    fill_rounded_rect_gradient(x + 2, y + 2, 92, 92, 18, 0x51C5F5, 0x1672B8);
    fill_rounded_rect_gradient(x + 5, y + 5, 86, 86, 15, 0x35A8E5, 0x125B9C);

    if (app == 2) {
        fill_rounded_rect_gradient(x + 25, y + 14, 48, 66, 6, 0xF7FCFF, 0xC9DFEE);
        fill_rounded_rect_gradient(x + 31, y + 22, 36, 4, 2, 0x2495D0, 0x1271B2);
        fill_rounded_rect_gradient(x + 32, y + 34, 33, 3, 1, 0x91ABC0, 0x7290A9);
        fill_rounded_rect_gradient(x + 32, y + 43, 30, 3, 1, 0x91ABC0, 0x7290A9);
        fill_rounded_rect_gradient(x + 32, y + 52, 33, 3, 1, 0x91ABC0, 0x7290A9);
        fill_rounded_rect_gradient(x + 32, y + 61, 24, 3, 1, 0x91ABC0, 0x7290A9);
    } else if (app == 3) {
        fill_rect(x + 16, y + 45, 64, 4, 0xD7F5FF);
        fill_rect(x + 46, y + 16, 4, 64, 0xD7F5FF);
        fill_rounded_rect_gradient(x + 18, y + 18, 60, 60, 30, 0x75D8FF, 0x1268A7);
        fill_rect(x + 20, y + 45, 56, 4, 0xD7F5FF);
        fill_rect(x + 46, y + 20, 4, 56, 0xD7F5FF);
        fill_rounded_rect_gradient(x + 31, y + 22, 34, 6, 3, 0xD7F5FF, 0x83D5F8);
        fill_rounded_rect_gradient(x + 31, y + 68, 34, 6, 3, 0xD7F5FF, 0x83D5F8);
    } else if (app == 4) {
        for (int spoke = 0; spoke < 8; ++spoke) {
            const int offset = (spoke & 1) ? 13 : 5;
            fill_rounded_rect_gradient(x + 43, y + offset, 10, 20, 4,
                                       0xE7F7FF, 0x9CCDE7);
            if (spoke < 4) {
                fill_rounded_rect_gradient(x + offset, y + 43, 20, 10, 4,
                                           0xE7F7FF, 0x9CCDE7);
            }
        }
        fill_rounded_rect_gradient(x + 25, y + 25, 46, 46, 23, 0xE7F7FF, 0x8CBAD8);
        fill_rounded_rect_gradient(x + 36, y + 36, 24, 24, 12, 0x17446C, 0x0B2F51);
    } else {
        fill_rounded_rect_gradient(x + 15, y + 20, 66, 50, 8, 0xE8F7FF, 0x9BCBE7);
        fill_rounded_rect_gradient(x + 20, y + 25, 56, 39, 4, 0x13253B, 0x071421);
        draw_triangle(x + 29, y + 36, x + 37, y + 43, x + 29, y + 50, 0x65D7FF);
        fill_rounded_rect_gradient(x + 41, y + 47, 22, 3, 1, 0xA5F18B, 0x65C75B);
        fill_rounded_rect_gradient(x + 38, y + 71, 20, 8, 3, 0xE8F7FF, 0x9BCBE7);
        fill_rounded_rect_gradient(x + 29, y + 79, 38, 5, 2, 0xE8F7FF, 0x9BCBE7);
    }
}

static void draw_desktop_icon_label(const char* text, int center_x, int y, int characters) {
    const int text_width = characters * 24;
    draw_text(text, center_x - text_width / 2, y, 1, 0xFFFFFF);
}

static void draw_nova_mark(int x, int y) {
    const uint32_t color = 0x75D8FF;
    fill_rect(x, y, 44, 44, 0x103653);
    fill_rect(x + 2, y + 2, 40, 40, 0x15527A);
    fill_rect(x + 9, y + 10, 6, 24, color);
    fill_rect(x + 29, y + 10, 6, 24, color);
    draw_triangle(x + 15, y + 10, x + 21, y + 10, x + 29, y + 34, color);
    draw_triangle(x + 15, y + 10, x + 23, y + 34, x + 29, y + 34, color);
}

static void draw_wallpaper() {
    const uint16_t* wallpaper =
        reinterpret_cast<const uint16_t*>(wallpaper_address);
    const uint32_t row_stride = screen_pitch / 4;
    for (uint32_t y = 0; y < screen_height; ++y) {
        uint32_t* row = render_target + y * row_stride;
        const uint32_t source_y = y * 7 / 20;
        for (uint32_t x = 0; x < screen_width; ++x) {
            const uint32_t source_x = x * 7 / 20;
            const uint16_t color = wallpaper[source_y * 672 + source_x];
            const uint32_t red5 = (color >> 11) & 31;
            const uint32_t green6 = (color >> 5) & 63;
            const uint32_t blue5 = color & 31;
            const uint32_t red = (red5 << 3) | (red5 >> 2);
            const uint32_t green = (green6 << 2) | (green6 >> 4);
            const uint32_t blue = (blue5 << 3) | (blue5 >> 2);
            row[x] = (red << 16) | (green << 8) | blue;
        }
    }
}

static void draw_start_menu() {
    fill_rect(24, static_cast<int>(screen_height) - 618, 570, 530, 0xE8142942);
    fill_rect(24, static_cast<int>(screen_height) - 618, 6, 530, 0x36BDF0);
    draw_text("开始菜单", 62, static_cast<int>(screen_height) - 574, 1, 0xFFFFFF);
    draw_text("Nova 26H2", 62, static_cast<int>(screen_height) - 536, 1, 0x75D8FF);
    draw_app_icon(66, static_cast<int>(screen_height) - 454, false);
    draw_text("此电脑", 174, static_cast<int>(screen_height) - 422, 1, 0xFFFFFF);
    draw_app_icon(66, static_cast<int>(screen_height) - 350, true);
    draw_text("文件管理", 174, static_cast<int>(screen_height) - 318, 1, 0xFFFFFF);

    draw_start_menu_actions();

    fill_rect(52, static_cast<int>(screen_height) - 120, 510, 1, 0x38536F);
    draw_text("按Win键开关菜单", 62, static_cast<int>(screen_height) - 117, 1, 0x9BB1C8);
}

static void present_backbuffer() {
    const uint32_t pixels_per_row = screen_width;
    const uint32_t row_stride = screen_pitch / 4;
    for (uint32_t y = 0; y < screen_height; ++y) {
        volatile uint32_t* destination = framebuffer + y * row_stride;
        const uint32_t* source = render_target + y * row_stride;
        copy_to_framebuffer(destination, source, pixels_per_row);
    }
}

static void present_rect(int x, int y, int width, int height) {
    const int left = x < 0 ? 0 : x;
    const int top = y < 0 ? 0 : y;
    const int right = x + width > static_cast<int>(screen_width)
        ? static_cast<int>(screen_width) : x + width;
    const int bottom = y + height > static_cast<int>(screen_height)
        ? static_cast<int>(screen_height) : y + height;
    const uint32_t row_stride = screen_pitch / 4;
    for (int row = top; row < bottom; ++row) {
        volatile uint32_t* destination = framebuffer + static_cast<uint32_t>(row) * row_stride + left;
        const uint32_t* source = render_target + static_cast<uint32_t>(row) * row_stride + left;
        copy_to_framebuffer(destination, source, static_cast<uint32_t>(right - left));
    }
}

static void copy_cached_rect(
    uintptr_t source_address,
    uint32_t source_stride,
    int x,
    int y,
    int width,
    int height
) {
    const int left = x < 0 ? 0 : x;
    const int top = y < 0 ? 0 : y;
    const int right = x + width > static_cast<int>(screen_width)
        ? static_cast<int>(screen_width) : x + width;
    const int bottom = y + height > static_cast<int>(screen_height)
        ? static_cast<int>(screen_height) : y + height;
    const uint32_t row_stride = screen_pitch / 4;
    const uint32_t* source = reinterpret_cast<const uint32_t*>(source_address);
    render_target = reinterpret_cast<uint32_t*>(backbuffer_address);
    for (int row = top; row < bottom; ++row) {
        volatile uint32_t* destination = reinterpret_cast<volatile uint32_t*>(
            render_target + static_cast<uint32_t>(row) * row_stride + left
        );
        const uint32_t* source_row = source +
            static_cast<uint32_t>(row - y) * source_stride + (left - x);
        copy_to_framebuffer(
            destination,
            source_row,
            static_cast<uint32_t>(right - left)
        );
    }
}

static void restore_desktop_rect(int x, int y, int width, int height) {
    const uintptr_t desktop_rect = desktop_cache_address +
        (static_cast<uintptr_t>(y) * screen_width + static_cast<uint32_t>(x)) *
            sizeof(uint32_t);
    copy_cached_rect(
        desktop_rect,
        screen_width,
        x,
        y,
        width,
        height
    );
}

static void cache_rect(
    uintptr_t destination_address,
    uint32_t destination_stride,
    int x,
    int y,
    int width,
    int height
) {
    const uint32_t row_stride = screen_pitch / 4;
    const uint32_t* source = render_target;
    uint32_t* destination = reinterpret_cast<uint32_t*>(destination_address);
    for (int row = 0; row < height; ++row) {
        const uint32_t* source_row =
            source + static_cast<uint32_t>(y + row) * row_stride + x;
        uint32_t* destination_row =
            destination + static_cast<uint32_t>(row) * destination_stride;
        for (int column = 0; column < width; ++column) {
            destination_row[column] = source_row[column];
        }
    }
}

static void draw_start_menu_actions() {
    const int restart_y = static_cast<int>(screen_height) - 264;
    const int shutdown_y = static_cast<int>(screen_height) - 204;
    fill_rect(52, restart_y, 510, 52, menu_selection == 0 ? 0x24506F : 0x182F4A);
    fill_rect(68, restart_y + 8, 36, 36, 0x14577A);
    draw_text("重启", 120, restart_y + 14, 1, 0xFFFFFF);
    fill_rect(52, shutdown_y, 510, 52, menu_selection == 1 ? 0x24506F : 0x182F4A);
    fill_rect(68, shutdown_y + 8, 36, 36, 0x673844);
    draw_text("关机", 120, shutdown_y + 14, 1, 0xFFFFFF);
}

static void refresh_start_menu_actions() {
    render_target = reinterpret_cast<uint32_t*>(backbuffer_address);
    if (start_menu_cache_ready) {
        copy_cached_rect(
            start_menu_cache_address,
            start_menu_width,
            24,
            static_cast<int>(screen_height) - 618,
            start_menu_width,
            start_menu_height
        );
    }
    draw_start_menu_actions();
    present_rect(52, static_cast<int>(screen_height) - 264, 510, 112);
}

static void draw_desktop();

static void refresh_start_menu() {
    render_target = reinterpret_cast<uint32_t*>(backbuffer_address);
    const int top = static_cast<int>(screen_height) - 618;
    if (start_menu_open) {
        if (start_menu_cache_ready) {
            copy_cached_rect(
                start_menu_cache_address,
                start_menu_width,
                24,
                top,
                start_menu_width,
                start_menu_height
            );
            draw_start_menu_actions();
        } else {
            draw_start_menu();
        }
        present_rect(24, top, 570, 530);
        return;
    }

    restore_desktop_rect(24, top, 570, 530);
    present_rect(24, top, 570, 530);
}

static void draw_desktop() {
    render_target = reinterpret_cast<uint32_t*>(backbuffer_address);
    draw_wallpaper();

    draw_desktop_app_icon(54, 62, 0);
    draw_desktop_icon_label("文件管理器", 102, 166, 5);
    draw_desktop_app_icon(54, 218, 2);
    draw_desktop_icon_label("记事本", 102, 322, 3);
    draw_desktop_app_icon(54, 374, 3);
    draw_desktop_icon_label("浏览器", 102, 478, 3);
    draw_desktop_app_icon(54, 530, 4);
    draw_desktop_icon_label("设置", 102, 634, 2);
    draw_desktop_app_icon(54, 686, 5);
    draw_desktop_icon_label("终端", 102, 790, 2);

    fill_rect(0, static_cast<int>(screen_height) - 76, static_cast<int>(screen_width), 76, 0xE8102033);
    fill_rect(0, static_cast<int>(screen_height) - 76, static_cast<int>(screen_width), 1, 0x31506C);
    fill_rect(22, static_cast<int>(screen_height) - 64, 52, 52, start_menu_open ? 0x15527A : 0x102B45);
    draw_nova_mark(26, static_cast<int>(screen_height) - 60);
    fill_rect(92, static_cast<int>(screen_height) - 58, 1, 40, 0x38516A);
    draw_text("Nova", 116, static_cast<int>(screen_height) - 50, 1, 0xFFFFFF);
    draw_text("64位", static_cast<int>(screen_width) - 220, static_cast<int>(screen_height) - 50, 1, 0xA8C4DB);
    fill_rect(static_cast<int>(screen_width) - 120, static_cast<int>(screen_height) - 52, 1, 30, 0x38516A);
    draw_text("26H2版", static_cast<int>(screen_width) - 130, static_cast<int>(screen_height) - 50, 1, 0x75D8FF);

    const uint32_t row_stride = screen_pitch / 4;
    const uint32_t* source = render_target;
    volatile uint32_t* destination =
        reinterpret_cast<volatile uint32_t*>(desktop_cache_address);
    for (uint32_t y = 0; y < screen_height; ++y) {
        copy_to_framebuffer(
            destination + y * row_stride,
            source + y * row_stride,
            screen_width
        );
    }
    if (start_menu_open) {
        draw_start_menu();
    }
    if (!start_menu_cache_ready && !start_menu_open) {
        const int menu_x = 24;
        const int menu_y = static_cast<int>(screen_height) - 618;
        draw_start_menu();
        cache_rect(
            start_menu_cache_address,
            start_menu_width,
            menu_x,
            menu_y,
            start_menu_width,
            start_menu_height
        );
        restore_desktop_rect(
            menu_x,
            menu_y,
            start_menu_width,
            start_menu_height
        );
        start_menu_cache_ready = true;
    }
    present_backbuffer();
}

static void draw_zex_window() {
    if (!zex_runtime.window_open) return;

    const int x = (static_cast<int>(screen_width) - zex_runtime.window_width) / 2;
    const int y = (static_cast<int>(screen_height) - zex_runtime.window_height) / 2;
    fill_rounded_rect_gradient(
        x, y, zex_runtime.window_width, zex_runtime.window_height, 12,
        0x123451, 0x09192A
    );
    fill_rounded_rect_gradient(
        x + 2, y + 2, zex_runtime.window_width - 4, zex_runtime.window_height - 4, 10,
        0x203D5A, 0x10253B
    );
    fill_rounded_rect_gradient(
        x + 4, y + 4, zex_runtime.window_width - 8, 42, 8,
        0x28618C, 0x17466D
    );
    fill_rect(x + 4, y + 34, zex_runtime.window_width - 8, 12, 0x17466D);
    draw_text(zex_runtime.title, x + 22, y + 15, 1, 0xFFFFFF);
    fill_rounded_rect_gradient(
        x + zex_runtime.window_width - 42, y + 10, 26, 24, 6,
        0xA84453, 0x782B3C
    );
    const int close_center_x = x + zex_runtime.window_width - 29;
    const int close_center_y = y + 22;
    for (int offset = 0; offset < 14; ++offset) {
        fill_rect(close_center_x - 7 + offset, close_center_y - 7 + offset,
                  2, 2, 0xFFFFFF);
        fill_rect(close_center_x + 6 - offset, close_center_y - 7 + offset,
                  2, 2, 0xFFFFFF);
    }
    fill_rounded_rect_gradient(
        x + 20, y + 64, zex_runtime.window_width - 40,
        zex_runtime.window_height - 84, 8, 0x152C43, 0x0D1C2C
    );
    if (zex_runtime.file_manager) {
        draw_text(file_manager_status, x + 38, y + 76, 1, 0xE3EDF7);
        fill_rounded_rect_gradient(
            x + 38, y + 106, 140, 32, 6, 0x28618C, 0x17466D
        );
        draw_text("数据盘 A:", x + 48, y + 110, 1,
                  file_manager_volume_index == 0 ? 0xFFFFFF : 0xA9C3D8);
        fill_rounded_rect_gradient(
            x + 190, y + 106, 188, 32, 6, 0x28618C, 0x17466D
        );
        draw_text("共享目录 B:", x + 200, y + 110, 1,
                  file_manager_volume_index == 1 ? 0xFFFFFF : 0xA9C3D8);
        fill_rounded_rect_gradient(
            x + zex_runtime.window_width - 154, y + 106, 112, 32, 6,
            0x315A7D, 0x1B3D5B
        );
        draw_text("上一级", x + zex_runtime.window_width - 136, y + 110,
                  1, 0xFFFFFF);

        const int first_row_y = y + 148;
        uint32_t visible_count = 0;
        while (visible_count < file_manager_visible_entries &&
               file_manager_scroll + visible_count < file_manager_entry_count) {
            const uint32_t index = file_manager_scroll + visible_count;
            const int row_y = first_row_y + static_cast<int>(visible_count) * 25;
            if (index == file_manager_selected) {
                fill_rounded_rect_gradient(
                    x + 32, row_y, zex_runtime.window_width - 64, 24, 5,
                    0x28618C, 0x17466D
                );
            }
            draw_text(file_manager_entries[index].directory ? "文件夹" : "文件",
                      x + 42, row_y + 2, 1,
                      file_manager_entries[index].directory ? 0x75D8FF : 0xA9C3D8);
            draw_text(file_manager_entries[index].name, x + 142, row_y + 2,
                      1, 0xFFFFFF);
            if (!file_manager_entries[index].directory) {
                char size_text[16];
                uint32_t value = file_manager_entries[index].size;
                uint32_t length = 0;
                do {
                    size_text[length++] = static_cast<char>('0' + value % 10);
                    value /= 10;
                } while (value != 0 && length < sizeof(size_text) - 1);
                for (uint32_t digit = 0; digit < length / 2; ++digit) {
                    const char temporary = size_text[digit];
                    size_text[digit] = size_text[length - digit - 1];
                    size_text[length - digit - 1] = temporary;
                }
                size_text[length] = '\0';
                draw_text(size_text, x + zex_runtime.window_width - 150,
                          row_y + 2, 1, 0x9BB1C8);
            }
            ++visible_count;
        }
        if (file_manager_entry_count == 0) {
            draw_text(!file_manager_volumes[file_manager_volume_index].mounted
                          ? "卷未挂载或文件系统无效"
                          : (file_manager_read_error ? "读取目录失败" : "此文件夹为空"),
                      x + 48, first_row_y + 4, 1, 0xA9C3D8);
        }
        if (file_manager_entries_truncated) {
            draw_text("列表已达到128项上限", x + 42,
                      first_row_y + static_cast<int>(visible_count) * 25 + 2,
                      1, 0xFFD479);
        }
        draw_text("单击选择  Enter打开  Backspace返回  Tab切换卷  Alt+F4关闭",
                  x + 24, y + zex_runtime.window_height - 42, 1, 0x9BB1C8);
    } else {
        draw_text(zex_runtime.message, x + 38, y + 90, 1, 0xE3EDF7);
        draw_text("点击右上角按钮或按 Alt+F4 关闭", x + 24,
                  y + zex_runtime.window_height - 42, 1, 0x9BB1C8);
    }
    present_rect(x, y, zex_runtime.window_width, zex_runtime.window_height);
}

static uint8_t in8(uint16_t port) {
    uint8_t value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static void out8(uint16_t port, uint8_t value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static void out16(uint16_t port, uint16_t value) {
    __asm__ volatile("outw %0, %1" : : "a"(value), "Nd"(port));
}

static void out32(uint16_t port, uint32_t value) {
    __asm__ volatile("outl %0, %1" : : "a"(value), "Nd"(port));
}

static uint16_t in16(uint16_t port) {
    uint16_t value;
    __asm__ volatile("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static uint16_t read_le16(const uint8_t* bytes, uint32_t offset) {
    return static_cast<uint16_t>(bytes[offset]) |
           (static_cast<uint16_t>(bytes[offset + 1]) << 8);
}

static uint32_t read_le32(const uint8_t* bytes, uint32_t offset) {
    return static_cast<uint32_t>(bytes[offset]) |
           (static_cast<uint32_t>(bytes[offset + 1]) << 8) |
           (static_cast<uint32_t>(bytes[offset + 2]) << 16) |
           (static_cast<uint32_t>(bytes[offset + 3]) << 24);
}

static bool ata_wait(uint16_t base, bool require_data) {
    for (uint32_t attempt = 0; attempt < 1000000; ++attempt) {
        const uint8_t status = in8(base + 7);
        if (status == 0 || status == 0xFF) return false;
        if ((status & 0x80) != 0) continue;
        if ((status & 0x21) != 0) return false;
        if (!require_data || (status & 0x08) != 0) return true;
    }
    return false;
}

static bool ata_read_sector(uint8_t drive, uint32_t lba, uint8_t* destination) {
    const uint16_t base = drive < 2 ? 0x1F0 : 0x170;
    const uint8_t unit = drive & 1;
    if (lba >= 0x10000000) return false;

    out8(base + 6, static_cast<uint8_t>(
        0xE0 | (unit << 4) | ((lba >> 24) & 0x0F)
    ));
    for (uint32_t delay = 0; delay < 4; ++delay) {
        (void)in8(base + 0x206);
    }
    if (!ata_wait(base, false)) return false;
    out8(base + 2, 1);
    out8(base + 3, static_cast<uint8_t>(lba));
    out8(base + 4, static_cast<uint8_t>(lba >> 8));
    out8(base + 5, static_cast<uint8_t>(lba >> 16));
    out8(base + 7, 0x20);
    if (!ata_wait(base, true)) return false;

    uint16_t* words = reinterpret_cast<uint16_t*>(destination);
    for (uint32_t index = 0; index < 256; ++index) {
        words[index] = in16(base);
    }
    return ata_wait(base, false);
}

static bool mount_fat_volume(FatVolume& volume) {
    volume.mounted = false;
    volume.partition_start = 0;
    volume.partition_sectors = 0;
    if (!ata_read_sector(volume.ata_drive, 0, ata_sector_buffer) ||
        ata_sector_buffer[510] != 0x55 || ata_sector_buffer[511] != 0xAA) {
        return false;
    }

    if (read_le16(ata_sector_buffer, 11) != 512) {
        bool found_partition = false;
        for (uint32_t index = 0; index < 4; ++index) {
            const uint32_t offset = 446 + index * 16;
            const uint8_t type = ata_sector_buffer[offset + 4];
            const bool fat_partition = type == 0x01 || type == 0x04 ||
                type == 0x06 || type == 0x0B || type == 0x0C ||
                type == 0x0E;
            const uint32_t start = read_le32(ata_sector_buffer, offset + 8);
            const uint32_t sectors = read_le32(ata_sector_buffer, offset + 12);
            if (!fat_partition || start == 0 || sectors == 0) continue;
            if (!ata_read_sector(volume.ata_drive, start, ata_sector_buffer) ||
                read_le16(ata_sector_buffer, 11) != 512 ||
                ata_sector_buffer[510] != 0x55 ||
                ata_sector_buffer[511] != 0xAA) {
                return false;
            }
            volume.partition_start = start;
            volume.partition_sectors = sectors;
            found_partition = true;
            break;
        }
        if (!found_partition) return false;
    }

    volume.sectors_per_cluster = ata_sector_buffer[13];
    volume.reserved_sectors = read_le16(ata_sector_buffer, 14);
    volume.fat_count = ata_sector_buffer[16];
    volume.root_entry_count = read_le16(ata_sector_buffer, 17);
    volume.total_sectors = read_le16(ata_sector_buffer, 19);
    if (volume.total_sectors == 0) {
        volume.total_sectors = read_le32(ata_sector_buffer, 32);
    }
    volume.fat_sectors = read_le16(ata_sector_buffer, 22);
    if (volume.fat_sectors == 0) {
        volume.fat_sectors = read_le32(ata_sector_buffer, 36);
    }
    volume.root_cluster = read_le32(ata_sector_buffer, 44) & 0x0FFFFFFF;

    if (volume.sectors_per_cluster == 0 ||
        volume.sectors_per_cluster > 128 ||
        (volume.sectors_per_cluster & (volume.sectors_per_cluster - 1)) != 0 ||
        volume.reserved_sectors == 0 || volume.fat_count == 0 ||
        volume.fat_sectors == 0 || volume.total_sectors == 0 ||
        volume.partition_start >= 0x10000000U ||
        volume.total_sectors > 0x10000000U - volume.partition_start) {
        return false;
    }

    volume.root_sectors =
        (static_cast<uint32_t>(volume.root_entry_count) * 32 + 511) / 512;
    const uint64_t root_sector =
        static_cast<uint64_t>(volume.reserved_sectors) +
        static_cast<uint64_t>(volume.fat_count) * volume.fat_sectors;
    const uint64_t data_sector = root_sector + volume.root_sectors;
    if (root_sector > volume.total_sectors ||
        data_sector > volume.total_sectors) return false;
    volume.root_sector = static_cast<uint32_t>(root_sector);
    volume.data_sector = static_cast<uint32_t>(data_sector);
    if (volume.data_sector >= volume.total_sectors ||
        (volume.partition_sectors != 0 &&
         volume.total_sectors > volume.partition_sectors)) return false;

    const uint32_t cluster_count =
        (volume.total_sectors - volume.data_sector) /
        volume.sectors_per_cluster;
    if (cluster_count == 0) return false;
    volume.fat_type = cluster_count < 4085 ? 12 :
                      (cluster_count < 65525 ? 16 : 32);
    if (volume.fat_type == 32 &&
        (volume.root_entry_count != 0 || volume.root_cluster < 2 ||
         volume.root_cluster - 2 >= cluster_count)) return false;
    volume.mounted = true;
    return true;
}

static bool read_fat_byte(const FatVolume& volume, uint32_t offset, uint8_t& value) {
    const uint32_t sector = offset / 512;
    if (sector >= volume.fat_sectors ||
        !ata_read_sector(volume.ata_drive,
                         volume.partition_start + volume.reserved_sectors + sector,
                         fat_sector_buffer)) {
        return false;
    }
    value = fat_sector_buffer[offset & 511];
    return true;
}

static bool read_fat_entry(
    const FatVolume& volume,
    uint32_t cluster,
    uint32_t& next_cluster
) {
    uint32_t offset;
    if (volume.fat_type == 12) {
        offset = cluster + cluster / 2;
        uint8_t low;
        uint8_t high;
        if (!read_fat_byte(volume, offset, low) ||
            !read_fat_byte(volume, offset + 1, high)) return false;
        next_cluster = static_cast<uint32_t>(low) |
                       (static_cast<uint32_t>(high) << 8);
        next_cluster = (cluster & 1) != 0
            ? next_cluster >> 4 : next_cluster & 0x0FFF;
    } else if (volume.fat_type == 16) {
        offset = cluster * 2;
        uint8_t low;
        uint8_t high;
        if (!read_fat_byte(volume, offset, low) ||
            !read_fat_byte(volume, offset + 1, high)) return false;
        next_cluster = static_cast<uint32_t>(low) |
                       (static_cast<uint32_t>(high) << 8);
    } else {
        offset = cluster * 4;
        uint8_t bytes[4];
        for (uint32_t index = 0; index < 4; ++index) {
            if (!read_fat_byte(volume, offset + index, bytes[index])) return false;
        }
        next_cluster = read_le32(bytes, 0) & 0x0FFFFFFF;
    }
    return true;
}

static void append_utf8(char* destination, uint32_t capacity, uint32_t& length,
                        uint32_t codepoint) {
    if (codepoint < 0x80) {
        if (length + 1 < capacity) destination[length++] = static_cast<char>(codepoint);
    } else if (codepoint < 0x800) {
        if (length + 2 < capacity) {
            destination[length++] = static_cast<char>(0xC0 | (codepoint >> 6));
            destination[length++] = static_cast<char>(0x80 | (codepoint & 0x3F));
        }
    } else {
        if (length + 3 < capacity) {
            destination[length++] = static_cast<char>(0xE0 | (codepoint >> 12));
            destination[length++] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
            destination[length++] = static_cast<char>(0x80 | (codepoint & 0x3F));
        }
    }
}

static void copy_fat_name(
    const uint8_t* entry,
    const uint16_t* long_name,
    char* destination,
    uint32_t capacity
) {
    uint32_t output = 0;
    if (long_name[0] != 0xFFFF && long_name[0] != 0) {
        for (uint32_t index = 0; index < 260; ++index) {
            uint32_t codepoint = long_name[index];
            if (codepoint == 0 || codepoint == 0xFFFF) break;
            if (codepoint >= 0xD800 && codepoint <= 0xDBFF &&
                index + 1 < 260 && long_name[index + 1] >= 0xDC00 &&
                long_name[index + 1] <= 0xDFFF) {
                codepoint = 0x10000 + ((codepoint - 0xD800) << 10) +
                            (long_name[++index] - 0xDC00);
            }
            append_utf8(destination, capacity, output, codepoint);
        }
    } else {
        for (uint32_t index = 0; index < 8 && entry[index] != ' '; ++index) {
            if (entry[index] == 0x05) destination[output++] = static_cast<char>(0xE5);
            else if (output + 1 < capacity) destination[output++] = entry[index];
        }
        uint32_t extension_end = 11;
        while (extension_end > 8 && entry[extension_end - 1] == ' ') --extension_end;
        if (extension_end > 8 && output + 1 < capacity) {
            destination[output++] = '.';
            for (uint32_t index = 8; index < extension_end && output + 1 < capacity; ++index) {
                destination[output++] = entry[index];
            }
        }
    }
    destination[output < capacity ? output : capacity - 1] = '\0';
}

static void store_long_name_part(
    const uint8_t* entry,
    uint16_t* long_name
) {
    static const uint8_t offsets[13] = {
        1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30
    };
    const uint32_t ordinal = entry[0] & 0x1F;
    if (ordinal == 0 || ordinal > 20) return;
    const uint32_t start = (ordinal - 1) * 13;
    for (uint32_t index = 0; index < 13; ++index) {
        long_name[start + index] = read_le16(entry, offsets[index]);
    }
}

static bool add_file_manager_entry(
    const uint8_t* raw,
    const uint16_t* long_name
) {
    if (raw[0] == 0) return false;
    if (raw[0] == 0xE5 || raw[11] == 0x0F ||
        (raw[11] & 0x08) != 0 || raw[0] == '.') return true;
    if (file_manager_entry_count >= file_manager_entry_limit) {
        file_manager_entries_truncated = true;
        return false;
    }

    FileManagerEntry& entry = file_manager_entries[file_manager_entry_count++];
    copy_fat_name(raw, long_name, entry.name, sizeof(entry.name));
    entry.directory = (raw[11] & 0x10) != 0;
    entry.cluster = (static_cast<uint32_t>(read_le16(raw, 20)) << 16) |
                    read_le16(raw, 26);
    if (file_manager_volumes[file_manager_volume_index].fat_type != 32) {
        entry.cluster &= 0xFFFF;
    }
    entry.size = read_le32(raw, 28);
    return true;
}

static bool scan_directory_sector(
    const uint8_t* sector,
    uint16_t* long_name
) {
    for (uint32_t offset = 0; offset < 512; offset += 32) {
        const uint8_t* raw = sector + offset;
        if (raw[0] == 0) return false;
        if (raw[11] == 0x0F) {
            if ((raw[0] & 0x40) != 0) {
                for (uint32_t index = 0; index < 260; ++index) {
                    long_name[index] = 0xFFFF;
                }
            }
            store_long_name_part(raw, long_name);
        } else {
            if (!add_file_manager_entry(raw, long_name)) return false;
            for (uint32_t index = 0; index < 260; ++index) {
                long_name[index] = 0xFFFF;
            }
        }
    }
    return true;
}

static bool load_file_manager_directory() {
    const FatVolume& volume = file_manager_volumes[file_manager_volume_index];
    file_manager_entry_count = 0;
    file_manager_selected = 0;
    file_manager_scroll = 0;
    file_manager_entries_truncated = false;
    for (uint32_t index = 0; index < 260; ++index) {
        file_manager_long_name[index] = 0xFFFF;
    }
    if (!volume.mounted) return false;

    if (file_manager_directory_cluster == 0 && volume.fat_type != 32) {
        for (uint32_t sector = 0; sector < volume.root_sectors; ++sector) {
            if (!ata_read_sector(volume.ata_drive,
                                 volume.partition_start + volume.root_sector + sector,
                                 directory_sector_buffer)) return false;
            if (!scan_directory_sector(directory_sector_buffer,
                                       file_manager_long_name)) break;
        }
        return true;
    }

    uint32_t cluster = file_manager_directory_cluster == 0
        ? volume.root_cluster : file_manager_directory_cluster;
    const uint32_t cluster_count =
        (volume.total_sectors - volume.data_sector) /
        volume.sectors_per_cluster;
    for (uint32_t chain = 0; chain < cluster_count; ++chain) {
        if (cluster < 2) return false;
        if (cluster - 2 >= cluster_count) return false;
        const uint32_t relative_sector = volume.data_sector +
            (cluster - 2) * volume.sectors_per_cluster;
        if (relative_sector >= volume.total_sectors ||
            volume.sectors_per_cluster >
                volume.total_sectors - relative_sector) return false;
        const uint32_t first_sector = volume.partition_start + relative_sector;
        for (uint32_t sector = 0; sector < volume.sectors_per_cluster; ++sector) {
            if (!ata_read_sector(volume.ata_drive, first_sector + sector,
                                 directory_sector_buffer)) return false;
            if (!scan_directory_sector(directory_sector_buffer,
                                       file_manager_long_name)) return true;
        }
        uint32_t next;
        if (!read_fat_entry(volume, cluster, next)) return false;
        const uint32_t end_marker = volume.fat_type == 12 ? 0xFF8 :
                                    (volume.fat_type == 16 ? 0xFFF8 : 0x0FFFFFF8);
        if (next >= end_marker) return true;
        cluster = next;
    }
    return false;
}

static void install_zex_syscall_gate() {
    const uintptr_t handler = reinterpret_cast<uintptr_t>(&zex_interrupt_handler);
    ZexIdtEntry& gate = zex_idt[0x80];
    gate.offset_low = static_cast<uint16_t>(handler);
    gate.selector = 0x18;
    gate.ist = 0;
    gate.attributes = 0x8E;
    gate.offset_middle = static_cast<uint16_t>(handler >> 16);
    gate.offset_high = static_cast<uint32_t>(handler >> 32);
    gate.reserved = 0;

    const uint16_t limit = static_cast<uint16_t>(sizeof(zex_idt) - 1);
    const uintptr_t base = reinterpret_cast<uintptr_t>(zex_idt);
    zex_idtr[0] = static_cast<uint8_t>(limit);
    zex_idtr[1] = static_cast<uint8_t>(limit >> 8);
    for (uint32_t byte = 0; byte < 8; ++byte) {
        zex_idtr[byte + 2] = static_cast<uint8_t>(base >> (byte * 8));
    }
    __asm__ volatile("lidt %0" : : "m"(zex_idtr));
}

static uint16_t read_zex_u16(const uint8_t* image, uint32_t offset) {
    return static_cast<uint16_t>(image[offset]) |
           (static_cast<uint16_t>(image[offset + 1]) << 8);
}

static uint64_t read_zex_u64(const uint8_t* image, uint32_t offset) {
    uint64_t value = 0;
    for (uint32_t byte = 0; byte < 8; ++byte) {
        value |= static_cast<uint64_t>(image[offset + byte]) << (byte * 8);
    }
    return value;
}

static bool copy_zex_string(uint64_t address, char* destination, uint32_t capacity) {
    if (capacity == 0 || address < zex_runtime.data_start ||
        address >= zex_runtime.data_end) {
        return false;
    }

    const char* source = reinterpret_cast<const char*>(
        static_cast<uintptr_t>(address)
    );
    const uint32_t available = zex_runtime.data_end - static_cast<uint32_t>(address);
    const uint32_t limit = available < capacity ? available : capacity;
    for (uint32_t index = 0; index < limit; ++index) {
        destination[index] = source[index];
        if (source[index] == '\0') return true;
    }
    destination[capacity - 1] = '\0';
    return false;
}

static bool load_zex_program(const ZexProgram& program, uint64_t& entry) {
    const uint32_t header_size = 36;
    if (program.image == 0 || program.image_size < header_size) return false;
    if (read_zex_u16(program.image, 0) != 0x575A ||
        read_zex_u16(program.image, 2) != 2) {
        return false;
    }

    entry = read_zex_u64(program.image, 4);
    const uint64_t code_size = read_zex_u64(program.image, 12);
    const uint64_t data_size = read_zex_u64(program.image, 20);
    const uint64_t bss_size = read_zex_u64(program.image, 28);
    const uint64_t image_size = header_size + code_size + data_size;
    const uint64_t total_size = image_size + bss_size;
    if (code_size == 0 || image_size != program.image_size ||
        total_size > 256 * 1024 ||
        entry < zex_load_address + header_size ||
        entry >= zex_load_address + header_size + code_size) {
        return false;
    }

    uint8_t* const image = reinterpret_cast<uint8_t*>(zex_load_address);
    for (uint32_t offset = 0; offset < program.image_size; ++offset) {
        image[offset] = program.image[offset];
    }
    zex_runtime.code_end = static_cast<uint32_t>(
        zex_load_address + header_size + code_size
    );
    zex_runtime.data_start = zex_runtime.code_end;
    zex_runtime.data_end = static_cast<uint32_t>(
        zex_runtime.data_start + data_size
    );
    for (uint32_t offset = header_size; offset + 1 < zex_runtime.code_end -
         zex_load_address; ++offset) {
        if (image[offset] == 0x0F && image[offset + 1] == 0x05) {
            image[offset] = 0xCD;
            image[offset + 1] = 0x80;
            ++offset;
        }
    }
    for (uint32_t offset = static_cast<uint32_t>(image_size);
         offset < static_cast<uint32_t>(total_size); ++offset) {
        image[offset] = 0;
    }
    return true;
}

static void wait_for_zex_key() {
    while (!zex_runtime.close_requested) {
        const uint8_t status = in8(0x64);
        if ((status & 1) == 0) {
            __asm__ volatile("pause");
            continue;
        }
        const uint8_t data = in8(0x60);
        if ((status & 0x20) != 0) {
            if (mouse_packet_stage == 0) {
                if ((data & 0x08) != 0) {
                    mouse_packet[0] = data;
                    mouse_packet_stage = 1;
                }
            } else {
                mouse_packet[mouse_packet_stage++] = data;
                if (mouse_packet_stage == 3) {
                    mouse_packet_stage = 0;
                    handle_mouse_packet();
                }
            }
        } else if (data == 0xE0) {
            keyboard_extended = true;
        } else {
            const bool released = (data & 0x80) != 0;
            const uint8_t key = data & 0x7F;
            const bool extended = keyboard_extended;
            keyboard_extended = false;
            if (!extended && key == 0x38) {
                keyboard_alt_down = !released;
            } else if (!released && !extended && key == 0x3E &&
                       keyboard_alt_down) {
                zex_runtime.close_requested = true;
            } else if (!released && !extended && key != 0x38) {
                return;
            }
        }
    }
}

static void copy_zex_literal(char* destination, uint32_t capacity, const char* source) {
    uint32_t index = 0;
    while (index + 1 < capacity && source[index] != '\0') {
        destination[index] = source[index];
        ++index;
    }
    destination[index] = '\0';
}

static void append_text(char* destination, uint32_t capacity, const char* source) {
    uint32_t length = 0;
    while (length + 1 < capacity && destination[length] != '\0') ++length;
    uint32_t index = 0;
    while (length + 1 < capacity && source[index] != '\0') {
        destination[length++] = source[index++];
    }
    destination[length] = '\0';
}

static void update_file_manager_path() {
    copy_zex_literal(file_manager_status, sizeof(file_manager_status),
                     file_manager_volume_index == 0 ? "A:\\" : "B:\\");
    for (uint32_t index = 0; index < file_manager_parent_depth; ++index) {
        append_text(file_manager_status, sizeof(file_manager_status),
                    file_manager_parent_names[index]);
        append_text(file_manager_status, sizeof(file_manager_status), "\\");
    }
}

static void redraw_file_manager() {
    update_file_manager_path();
    render_zex_window();
}

static void reload_file_manager_directory() {
    file_manager_read_error = !load_file_manager_directory();
    if (file_manager_read_error) {
        file_manager_entry_count = 0;
        file_manager_scroll = 0;
        file_manager_entries_truncated = false;
    }
    redraw_file_manager();
}

static void switch_file_manager_volume(uint8_t volume_index) {
    if (volume_index >= 2 || volume_index == file_manager_volume_index) return;
    file_manager_volume_index = volume_index;
    file_manager_directory_cluster = 0;
    file_manager_parent_depth = 0;
    reload_file_manager_directory();
}

static void file_manager_go_parent() {
    if (file_manager_parent_depth == 0) return;
    --file_manager_parent_depth;
    file_manager_directory_cluster =
        file_manager_parent_clusters[file_manager_parent_depth];
    reload_file_manager_directory();
}

static void file_manager_open_selected() {
    if (file_manager_selected >= file_manager_entry_count) return;
    const FileManagerEntry& entry = file_manager_entries[file_manager_selected];
    if (entry.directory) {
        if (file_manager_parent_depth >= 16) {
            copy_zex_literal(file_manager_status, sizeof(file_manager_status),
                             "目录层级达到上限");
            return;
        }
        file_manager_parent_clusters[file_manager_parent_depth] =
            file_manager_directory_cluster;
        copy_zex_literal(file_manager_parent_names[file_manager_parent_depth],
                         sizeof(file_manager_parent_names[0]), entry.name);
        ++file_manager_parent_depth;
        file_manager_directory_cluster = entry.cluster;
        reload_file_manager_directory();
    }
}

static void file_manager_select_mouse_row() {
    const int window_x =
        (static_cast<int>(screen_width) - zex_runtime.window_width) / 2;
    const int window_y =
        (static_cast<int>(screen_height) - zex_runtime.window_height) / 2;
    if (mouse_y >= window_y + 106 && mouse_y < window_y + 138) {
        if (mouse_x >= window_x + 38 && mouse_x < window_x + 178) {
            switch_file_manager_volume(0);
        } else if (mouse_x >= window_x + 190 && mouse_x < window_x + 378) {
            switch_file_manager_volume(1);
        } else if (mouse_x >= window_x + zex_runtime.window_width - 154 &&
                   mouse_x < window_x + zex_runtime.window_width - 42) {
            file_manager_go_parent();
        }
        return;
    }

    const int first_row_y = window_y + 148;
    if (mouse_y < first_row_y) return;
    const uint32_t row =
        static_cast<uint32_t>((mouse_y - first_row_y) / 25);
    const uint32_t index = file_manager_scroll + row;
    if (row >= file_manager_visible_entries ||
        index >= file_manager_entry_count) return;
    if (file_manager_selected != index) {
        file_manager_selected = index;
        redraw_file_manager();
    }
}

static void handle_file_manager_key(uint8_t scan_code, bool extended) {
    if (extended) {
        if (scan_code == 0x48 && file_manager_entry_count != 0) {
            if (file_manager_selected != 0) --file_manager_selected;
            if (file_manager_selected < file_manager_scroll) {
                file_manager_scroll = file_manager_selected;
            }
            redraw_file_manager();
        } else if (scan_code == 0x50 && file_manager_entry_count != 0) {
            if (file_manager_selected + 1 < file_manager_entry_count) {
                ++file_manager_selected;
            }
            if (file_manager_selected >=
                file_manager_scroll + file_manager_visible_entries) {
                file_manager_scroll =
                    file_manager_selected - file_manager_visible_entries + 1;
            }
            redraw_file_manager();
        } else if (scan_code == 0x49 && file_manager_entry_count != 0) {
            file_manager_selected = file_manager_selected > file_manager_visible_entries
                ? file_manager_selected - file_manager_visible_entries : 0;
            file_manager_scroll = file_manager_selected;
            redraw_file_manager();
        } else if (scan_code == 0x51 && file_manager_entry_count != 0) {
            const uint32_t last_entry = file_manager_entry_count - 1;
            file_manager_selected = file_manager_selected + file_manager_visible_entries
                < last_entry ? file_manager_selected + file_manager_visible_entries
                             : last_entry;
            file_manager_scroll =
                file_manager_selected >= file_manager_visible_entries
                    ? file_manager_selected - file_manager_visible_entries + 1 : 0;
            redraw_file_manager();
        }
        return;
    }

    switch (scan_code) {
    case 0x01:
        zex_runtime.close_requested = true;
        break;
    case 0x0E:
        file_manager_go_parent();
        break;
    case 0x0F:
        switch_file_manager_volume(
            static_cast<uint8_t>(1 - file_manager_volume_index)
        );
        break;
    case 0x1C:
        file_manager_open_selected();
        break;
    default:
        break;
    }
}

static void run_zex_file_manager() {
    zex_runtime.file_manager = true;
    zex_runtime.window_open = true;
    zex_runtime.close_requested = false;
    zex_runtime.window_width = 1120;
    zex_runtime.window_height = 760;
    copy_zex_literal(zex_runtime.title, sizeof(zex_runtime.title), "文件管理器");
    file_manager_volumes[0].ata_drive = 2;
    file_manager_volumes[1].ata_drive = 1;
    mount_fat_volume(file_manager_volumes[0]);
    mount_fat_volume(file_manager_volumes[1]);
    file_manager_volume_index = 0;
    file_manager_directory_cluster = 0;
    file_manager_parent_depth = 0;
    keyboard_extended = false;
    keyboard_alt_down = false;
    reload_file_manager_directory();

    while (!zex_runtime.close_requested) {
        const uint8_t status = in8(0x64);
        if ((status & 1) == 0) {
            __asm__ volatile("pause");
            continue;
        }

        const uint8_t data = in8(0x60);
        if ((status & 0x20) != 0) {
            if (mouse_packet_stage == 0) {
                if ((data & 0x08) != 0) {
                    mouse_packet[0] = data;
                    mouse_packet_stage = 1;
                }
            } else {
                mouse_packet[mouse_packet_stage++] = data;
                if (mouse_packet_stage == 3) {
                    mouse_packet_stage = 0;
                    handle_mouse_packet();
                }
            }
        } else if (data == 0xE0) {
            keyboard_extended = true;
        } else {
            const bool released = (data & 0x80) != 0;
            const uint8_t scan_code = data & 0x7F;
            const bool extended = keyboard_extended;
            keyboard_extended = false;
            if (!extended && scan_code == 0x38) {
                keyboard_alt_down = !released;
            } else if (!released && !extended && scan_code == 0x3E &&
                       keyboard_alt_down) {
                zex_runtime.close_requested = true;
            } else if (!released) {
                handle_file_manager_key(scan_code, extended);
            }
        }
    }

    restore_mouse_cursor();
    zex_runtime.window_open = false;
    zex_runtime.file_manager = false;
}

static void show_zex_load_error() {
    zex_runtime.window_open = true;
    zex_runtime.close_requested = false;
    zex_runtime.window_width = 520;
    zex_runtime.window_height = 240;
    copy_zex_literal(zex_runtime.title, sizeof(zex_runtime.title), "程序加载失败");
    copy_zex_literal(zex_runtime.message, sizeof(zex_runtime.message), "ZEX程序数据无效");
    render_zex_window();
    wait_for_zex_key();
    zex_runtime.window_open = false;
    const int x = (static_cast<int>(screen_width) - zex_runtime.window_width) / 2;
    const int y = (static_cast<int>(screen_height) - zex_runtime.window_height) / 2;
    restore_desktop_rect(x, y, zex_runtime.window_width, zex_runtime.window_height);
    present_rect(x, y, zex_runtime.window_width, zex_runtime.window_height);
    draw_mouse_cursor();
}

static void run_zex_program(uint32_t program_index) {
    if (program_index >= sizeof(zex_programs) / sizeof(zex_programs[0])) return;

    active_zex_program = program_index;
    uint64_t entry = 0;
    if (!load_zex_program(zex_programs[program_index], entry)) {
        show_zex_load_error();
        return;
    }

    zex_runtime.running = true;
    zex_runtime.window_open = false;
    zex_runtime.close_requested = false;
    keyboard_alt_down = false;
    zex_runtime.title[0] = '\0';
    zex_runtime.message[0] = '\0';
    mouse_cursor_drawn = false;
    zex_call_entry(entry);
    zex_runtime.running = false;
    zex_runtime.window_open = false;
    zex_runtime.close_requested = false;
    restore_mouse_cursor();
    const int x = (static_cast<int>(screen_width) - zex_runtime.window_width) / 2;
    const int y = (static_cast<int>(screen_height) - zex_runtime.window_height) / 2;
    restore_desktop_rect(x, y, zex_runtime.window_width, zex_runtime.window_height);
    present_rect(x, y, zex_runtime.window_width, zex_runtime.window_height);
    draw_mouse_cursor();
}

static void render_zex_window() {
    if (mouse_cursor_drawn) restore_mouse_cursor();
    if (!zex_runtime.window_open) return;
    const int x = (static_cast<int>(screen_width) - zex_runtime.window_width) / 2;
    const int y = (static_cast<int>(screen_height) - zex_runtime.window_height) / 2;
    restore_desktop_rect(x, y, zex_runtime.window_width, zex_runtime.window_height);

    const uint32_t program = active_zex_program;
    const bool cacheable = !zex_runtime.file_manager &&
        program < sizeof(zex_window_cache_valid) / sizeof(zex_window_cache_valid[0]) &&
        zex_runtime.window_width <= static_cast<int>(zex_window_cache_width_limit) &&
        zex_runtime.window_height <= static_cast<int>(zex_window_cache_height_limit);
    bool cache_matches = cacheable && zex_window_cache_valid[program] &&
        zex_window_cache_width[program] == static_cast<uint32_t>(zex_runtime.window_width) &&
        zex_window_cache_height[program] == static_cast<uint32_t>(zex_runtime.window_height);
    for (uint32_t index = 0; cache_matches && index < sizeof(zex_runtime.title); ++index) {
        if (zex_window_cache_title[program][index] != zex_runtime.title[index]) {
            cache_matches = false;
        }
    }
    for (uint32_t index = 0; cache_matches && index < sizeof(zex_runtime.message); ++index) {
        if (zex_window_cache_message[program][index] != zex_runtime.message[index]) {
            cache_matches = false;
        }
    }
    const uintptr_t cache_address = zex_window_cache_address +
        static_cast<uintptr_t>(program) * zex_window_cache_slot_size * sizeof(uint32_t);
    if (cache_matches) {
        copy_cached_rect(
            cache_address,
            zex_window_cache_width_limit,
            x,
            y,
            zex_runtime.window_width,
            zex_runtime.window_height
        );
        present_rect(x, y, zex_runtime.window_width, zex_runtime.window_height);
    } else {
        draw_zex_window();
        if (cacheable) {
            cache_rect(
                cache_address,
                zex_window_cache_width_limit,
                x,
                y,
                zex_runtime.window_width,
                zex_runtime.window_height
            );
            zex_window_cache_width[program] =
                static_cast<uint32_t>(zex_runtime.window_width);
            zex_window_cache_height[program] =
                static_cast<uint32_t>(zex_runtime.window_height);
            copy_zex_literal(
                zex_window_cache_title[program],
                sizeof(zex_window_cache_title[program]),
                zex_runtime.title
            );
            copy_zex_literal(
                zex_window_cache_message[program],
                sizeof(zex_window_cache_message[program]),
                zex_runtime.message
            );
            zex_window_cache_valid[program] = true;
        }
    }
    draw_mouse_cursor();
}

extern "C" void zex_syscall_dispatch(ZexRegisterFrame* registers) {
    if (registers == 0 || !zex_runtime.running) return;

    switch (registers->rax) {
    case 0:
        zex_runtime.running = false;
        registers->rip = reinterpret_cast<uintptr_t>(&zex_exit_trampoline);
        registers->rax = 0;
        break;
    case 1:
        if (copy_zex_string(
                registers->rdi, zex_runtime.message, sizeof(zex_runtime.message)
            )) {
            render_zex_window();
            registers->rax = 0;
        } else {
            registers->rax = static_cast<uint64_t>(-1);
        }
        break;
    case 2:
        wait_for_zex_key();
        registers->rax = 0;
        break;
    case 5:
        if (!copy_zex_string(
                registers->rdi, zex_runtime.title, sizeof(zex_runtime.title)
            )) {
            registers->rax = static_cast<uint64_t>(-1);
            break;
        }
        zex_runtime.window_width = registers->rsi < 320
            ? 320 : static_cast<int>(registers->rsi);
        zex_runtime.window_height = registers->rdx < 220
            ? 220 : static_cast<int>(registers->rdx);
        if (zex_runtime.window_width > static_cast<int>(screen_width) - 80) {
            zex_runtime.window_width = static_cast<int>(screen_width) - 80;
        }
        if (zex_runtime.window_height > static_cast<int>(screen_height) - 100) {
            zex_runtime.window_height = static_cast<int>(screen_height) - 100;
        }
        zex_runtime.window_open = true;
        render_zex_window();
        registers->rax = 0;
        break;
    case 6:
        run_zex_file_manager();
        registers->rax = 0;
        break;
    default:
        registers->rax = static_cast<uint64_t>(-1);
        break;
    }
}

static uint16_t pit_read_count() {
    out8(0x43, 0);
    const uint16_t low = in8(0x40);
    const uint16_t high = in8(0x40);
    return low | (high << 8);
}

static void wait_pit_ticks(uint32_t ticks) {
    uint16_t previous = pit_read_count();
    while (ticks != 0) {
        const uint16_t current = pit_read_count();
        if (current > previous) --ticks;
        previous = current;
        __asm__ volatile("pause");
    }
}

static void draw_loading_screen() {
    render_target = reinterpret_cast<uint32_t*>(backbuffer_address);
    fill_rect(0, 0, static_cast<int>(screen_width), static_cast<int>(screen_height), 0x07172A);

    const int center_x = static_cast<int>(screen_width / 2);
    const int center_y = static_cast<int>(screen_height / 2);
    draw_logo(center_x - 160, center_y - 190, 320);

    const int progress_x = center_x - 180;
    const int progress_y = center_y + 150;
    fill_rect(progress_x, progress_y, 360, 8, 0x31506C);
    fill_rect(progress_x + 2, progress_y + 2, 356, 4, 0x102B45);
    present_backbuffer();

    for (int progress = 0; progress <= 100; progress += 5) {
        const int progress_width = progress * 356 / 100;
        fill_rect(progress_x + 2, progress_y + 2, progress_width, 4, 0x36BDF0);
        present_rect(progress_x + 2, progress_y + 2, progress_width, 4);
        wait_pit_ticks(1);
    }
}

static void draw_shutdown_screen() {
    render_target = reinterpret_cast<uint32_t*>(backbuffer_address);
    fill_rect(0, 0, static_cast<int>(screen_width), static_cast<int>(screen_height), 0x07172A);

    const int center_x = static_cast<int>(screen_width / 2);
    const int center_y = static_cast<int>(screen_height / 2);
    draw_logo(center_x - 120, center_y - 190, 240);
    draw_text("正在关机", center_x - 48, center_y + 76, 1, 0xCBD5E1);

    const int progress_x = center_x - 180;
    const int progress_y = center_y + 128;
    fill_rect(progress_x, progress_y, 360, 8, 0x31506C);
    fill_rect(progress_x + 2, progress_y + 2, 356, 4, 0x102B45);
    present_backbuffer();

    for (int progress = 0; progress <= 100; progress += 5) {
        const int progress_width = progress * 356 / 100;
        fill_rect(progress_x + 2, progress_y + 2, progress_width, 4, 0x36BDF0);
        present_rect(progress_x + 2, progress_y + 2, progress_width, 4);
        wait_pit_ticks(1);
    }
}

static __attribute__((noreturn)) void restart_system() {
    while ((in8(0x64) & 2) != 0) __asm__ volatile("pause");
    out8(0x64, 0xFE);
    for (;;) __asm__ volatile("cli; hlt");
}

static __attribute__((noreturn)) void shutdown_system() {
    draw_shutdown_screen();
    fill_rect(0, 0, static_cast<int>(screen_width), static_cast<int>(screen_height), 0);
    present_backbuffer();
    wait_pit_ticks(2);

    out32(0x00F4, 0x10);
    out16(0x0604, 0x3400);
    out16(0xB004, 0x2000);
    for (;;) __asm__ volatile("cli; hlt");
}

static bool wait_mouse_input_ready() {
    for (uint32_t timeout = 0; timeout < 1000000; ++timeout) {
        if ((in8(0x64) & 2) == 0) return true;
    }
    return false;
}

static bool wait_mouse_output_ready() {
    for (uint32_t timeout = 0; timeout < 1000000; ++timeout) {
        if ((in8(0x64) & 1) != 0) return true;
    }
    return false;
}

static bool send_mouse_command(uint8_t command) {
    if (!wait_mouse_input_ready()) return false;
    out8(0x64, 0xD4);
    if (!wait_mouse_input_ready()) return false;
    out8(0x60, command);
    if (!wait_mouse_output_ready()) return false;
    return in8(0x60) == 0xFA;
}

static bool initialize_mouse() {
    while ((in8(0x64) & 1) != 0) in8(0x60);
    if (!wait_mouse_input_ready()) return false;
    out8(0x64, 0xA8);
    if (!send_mouse_command(0xF6)) return false;
    if (!send_mouse_command(0xF3)) return false;
    if (!send_mouse_command(200)) return false;
    return send_mouse_command(0xF4);
}

static void restore_mouse_cursor() {
    if (!mouse_cursor_drawn) return;

    for (int row = 0; row < MOUSE_CURSOR_HEIGHT; ++row) {
        const int y = mouse_y + row;
        if (y < 0 || y >= static_cast<int>(screen_height)) continue;
        for (int column = 0; column < MOUSE_CURSOR_WIDTH; ++column) {
            const int x = mouse_x + column;
            if (x < 0 || x >= static_cast<int>(screen_width)) continue;
            framebuffer[static_cast<uint32_t>(y) * (screen_pitch / 4) +
                        static_cast<uint32_t>(x)] = mouse_cursor_background[row * MOUSE_CURSOR_WIDTH + column];
        }
    }
    mouse_cursor_drawn = false;
}

static void draw_mouse_cursor() {
    for (int row = 0; row < MOUSE_CURSOR_HEIGHT; ++row) {
        const int y = mouse_y + row;
        if (y < 0 || y >= static_cast<int>(screen_height)) continue;
        for (int column = 0; column < MOUSE_CURSOR_WIDTH; ++column) {
            const int x = mouse_x + column;
            if (x < 0 || x >= static_cast<int>(screen_width)) continue;
            const int pixel_index = row * MOUSE_CURSOR_WIDTH + column;
            const uint32_t offset = static_cast<uint32_t>(y) * (screen_pitch / 4) +
                                    static_cast<uint32_t>(x);
            const uint32_t background = framebuffer[offset];
            mouse_cursor_background[pixel_index] = background;
            const uint8_t alpha = mouse_cursor_coverage[pixel_index];
            if (alpha != 0) {
                const uint32_t shade = mouse_cursor_shade[pixel_index];
                const uint32_t color = (shade << 16) | (shade << 8) | shade;
                framebuffer[offset] = mix_color(background, color, alpha, 255);
            }
        }
    }
    mouse_cursor_drawn = true;
}

static void move_mouse(int delta_x, int delta_y) {
    const int max_x = static_cast<int>(screen_width) - 1;
    const int max_y = static_cast<int>(screen_height) - 1;
    mouse_x += delta_x;
    mouse_y += delta_y;
    if (mouse_x < 0) mouse_x = 0;
    if (mouse_x > max_x) mouse_x = max_x;
    if (mouse_y < 0) mouse_y = 0;
    if (mouse_y > max_y) mouse_y = max_y;
}

static void handle_mouse_packet() {
    const uint8_t flags = mouse_packet[0];
    if ((flags & 0xC0) != 0) return;

    restore_mouse_cursor();
    move_mouse(static_cast<int8_t>(mouse_packet[1]),
               -static_cast<int8_t>(mouse_packet[2]));

    const uint8_t buttons = flags & 7;
    if ((buttons & 1) != 0 && (mouse_buttons & 1) == 0) {
        if (zex_runtime.window_open) {
            const int window_x =
                (static_cast<int>(screen_width) - zex_runtime.window_width) / 2;
            const int window_y =
                (static_cast<int>(screen_height) - zex_runtime.window_height) / 2;
            if (mouse_x >= window_x + zex_runtime.window_width - 42 &&
                mouse_x < window_x + zex_runtime.window_width - 16 &&
                mouse_y >= window_y + 10 && mouse_y < window_y + 34) {
                zex_runtime.close_requested = true;
            } else if (zex_runtime.file_manager) {
                file_manager_select_mouse_row();
            }
        } else {
            const int screen_bottom = static_cast<int>(screen_height);
            if (start_menu_open && mouse_x >= 52 && mouse_x < 562 &&
                mouse_y >= screen_bottom - 264 && mouse_y < screen_bottom - 212) {
                restart_system();
            } else if (start_menu_open && mouse_x >= 52 && mouse_x < 562 &&
                       mouse_y >= screen_bottom - 204 && mouse_y < screen_bottom - 152) {
                shutdown_system();
            } else if (!start_menu_open && mouse_x >= 38 && mouse_x < 166 &&
                       mouse_y >= 50 && mouse_y < 830) {
                run_zex_program(static_cast<uint32_t>((mouse_y - 50) / 156));
            } else if (mouse_x < 84 && mouse_y >= screen_bottom - 76) {
                start_menu_open = !start_menu_open;
                refresh_start_menu();
            } else if (start_menu_open &&
                       (mouse_x < 24 || mouse_x >= 594 ||
                        mouse_y < screen_bottom - 618 || mouse_y >= screen_bottom - 88)) {
                start_menu_open = false;
                refresh_start_menu();
            }
        }
    }
    mouse_buttons = buttons;
    if (!mouse_cursor_drawn) draw_mouse_cursor();
}

extern "C" __attribute__((noreturn)) void kernel_main(
    uint32_t framebuffer_base,
    uint32_t width,
    uint32_t height,
    uint32_t pitch,
    uint32_t bpp
) {
    if (framebuffer_base == 0 || width == 0 || height == 0 || pitch < width * 4 || bpp != 32) {
        for (;;) __asm__ volatile("cli; hlt");
    }

    framebuffer = reinterpret_cast<volatile uint32_t*>(
        static_cast<unsigned long long>(framebuffer_base));
    screen_width = width;
    screen_height = height;
    screen_pitch = pitch;
    render_target = reinterpret_cast<uint32_t*>(backbuffer_address);
    start_menu_open = false;
    mouse_x = static_cast<int>(screen_width / 2);
    mouse_y = static_cast<int>(screen_height / 2);
    mouse_packet_stage = 0;
    mouse_buttons = 0;
    mouse_cursor_drawn = false;
    keyboard_extended = false;
    keyboard_alt_down = false;
    menu_selection = 0;
    install_zex_syscall_gate();
    initialize_mouse_cursor();
    draw_loading_screen();
    draw_desktop();
    initialize_mouse();
    draw_mouse_cursor();

    for (;;) {
        const uint8_t status = in8(0x64);
        if ((status & 1) != 0) {
            const uint8_t scan_code = in8(0x60);
            if ((status & 0x20) != 0) {
                if (mouse_packet_stage == 0) {
                    if ((scan_code & 0x08) != 0) {
                        mouse_packet[0] = scan_code;
                        mouse_packet_stage = 1;
                    }
                } else {
                    mouse_packet[mouse_packet_stage++] = scan_code;
                    if (mouse_packet_stage == 3) {
                        mouse_packet_stage = 0;
                        handle_mouse_packet();
                    }
                }
            } else if (scan_code == 0xE0) {
                keyboard_extended = true;
            } else {
                const bool extended = keyboard_extended;
                keyboard_extended = false;
                if (extended && (scan_code == 0x5B || scan_code == 0x5C)) {
                    restore_mouse_cursor();
                    start_menu_open = !start_menu_open;
                    refresh_start_menu();
                    draw_mouse_cursor();
                } else if (extended && start_menu_open &&
                           (scan_code == 0x48 || scan_code == 0x50)) {
                    menu_selection = scan_code == 0x48 ? 0 : 1;
                    restore_mouse_cursor();
                    refresh_start_menu_actions();
                    draw_mouse_cursor();
                } else if (!extended && start_menu_open && scan_code == 0x1C) {
                    if (menu_selection == 0) {
                        restart_system();
                    } else {
                        shutdown_system();
                    }
                } else if (scan_code == 0x01 && start_menu_open) {
                    restore_mouse_cursor();
                    start_menu_open = false;
                    refresh_start_menu();
                    draw_mouse_cursor();
                }
            }
        }
        __asm__ volatile("pause");
    }
}
