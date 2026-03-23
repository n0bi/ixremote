#include <furi.h>
#include <furi_hal_speaker.h>
#include <gui/gui.h>
#include <gui/view_port.h>
#include <gui/canvas.h>
#include <input/input.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define COUNT_OF(x) (sizeof(x) / sizeof((x)[0]))
#define LANE_COUNT 4U
#define EVENT_QUEUE_SIZE 16U
#define FRAME_TIME_MS 16U
#define NOTE_SCROLL_MS 1700U
#define SONG_START_OFFSET_MS 900U
#define SPEAKER_ACQUIRE_TIMEOUT_MS 20U
#define SPEAKER_VOLUME 0.35f
#define MAX_JUDGE_TEXT 12U
#define NO_INDEX 0xFFFFU

typedef enum {
    ScreenMenu = 0,
    ScreenPlaying,
    ScreenResults,
} Screen;

typedef enum {
    LaneLeft = 0,
    LaneDown,
    LaneUp,
    LaneRight,
} Lane;

typedef enum {
    JudgeNone = 0,
    JudgePerfect,
    JudgeGreat,
    JudgeGood,
    JudgeMiss,
} Judge;

typedef enum {
    StepPending = 0,
    StepHit,
    StepMiss,
} StepState;

typedef struct {
    uint16_t tick;
    uint8_t lane;
    uint8_t tone;
    uint8_t duration_ticks;
} ChartStep;

typedef struct {
    const char* title;
    uint16_t bpm;
    uint16_t total_ticks;
    const ChartStep* chart;
    uint16_t chart_len;
} SongDef;

typedef struct {
    Gui* gui;
    ViewPort* view_port;
    FuriMessageQueue* event_queue;

    bool running;
    bool speaker_owned;
    Screen screen;
    uint8_t menu_index;

    const SongDef* current_song;
    uint32_t song_start_tick;
    uint32_t song_elapsed_ms;
    uint16_t next_note_index;
    uint16_t active_note_index;
    StepState* step_states;

    uint32_t score;
    uint16_t combo;
    uint16_t max_combo;
    uint16_t perfect;
    uint16_t great;
    uint16_t good;
    uint16_t miss;
    Judge last_judge;
    uint8_t lane_flash[LANE_COUNT];
} PocketStepApp;

static const float tone_table[] = {
    261.63f, 293.66f, 329.63f, 349.23f, 392.00f, 440.00f, 493.88f, 523.25f,
};

#define STEP(tick_, lane_, tone_, len_) {tick_, lane_, tone_, len_}

static const ChartStep neon_run_chart[] = {
    STEP(0, LaneLeft, 0, 1),   STEP(2, LaneDown, 1, 1),  STEP(4, LaneUp, 2, 1),
    STEP(6, LaneRight, 4, 1),  STEP(8, LaneLeft, 2, 1),  STEP(10, LaneDown, 1, 1),
    STEP(12, LaneUp, 2, 1),    STEP(14, LaneRight, 4, 1), STEP(16, LaneLeft, 0, 1),
    STEP(18, LaneDown, 1, 1),  STEP(20, LaneUp, 2, 1),   STEP(22, LaneRight, 4, 1),
    STEP(24, LaneUp, 5, 2),    STEP(28, LaneDown, 4, 1), STEP(30, LaneLeft, 2, 1),
    STEP(32, LaneRight, 5, 1), STEP(34, LaneUp, 6, 1),   STEP(36, LaneDown, 4, 1),
    STEP(38, LaneLeft, 2, 1),  STEP(40, LaneRight, 5, 1), STEP(42, LaneUp, 6, 1),
    STEP(44, LaneDown, 4, 1),  STEP(46, LaneLeft, 2, 1), STEP(48, LaneLeft, 0, 1),
    STEP(50, LaneUp, 2, 1),    STEP(52, LaneRight, 4, 1), STEP(54, LaneDown, 1, 1),
    STEP(56, LaneLeft, 0, 2),  STEP(60, LaneRight, 4, 2), STEP(64, LaneUp, 7, 2),
};

static const ChartStep bit_shift_chart[] = {
    STEP(0, LaneUp, 4, 1),     STEP(1, LaneRight, 5, 1), STEP(2, LaneDown, 4, 1),
    STEP(3, LaneLeft, 2, 1),   STEP(4, LaneUp, 4, 1),    STEP(5, LaneRight, 5, 1),
    STEP(6, LaneDown, 4, 1),   STEP(7, LaneLeft, 2, 1),  STEP(8, LaneRight, 6, 1),
    STEP(10, LaneUp, 5, 1),    STEP(12, LaneDown, 4, 1), STEP(14, LaneLeft, 2, 1),
    STEP(16, LaneRight, 6, 1), STEP(18, LaneUp, 5, 1),   STEP(20, LaneDown, 4, 1),
    STEP(22, LaneLeft, 2, 1),  STEP(24, LaneLeft, 0, 1), STEP(25, LaneDown, 1, 1),
    STEP(26, LaneUp, 2, 1),    STEP(27, LaneRight, 4, 1), STEP(28, LaneLeft, 0, 1),
    STEP(29, LaneDown, 1, 1),  STEP(30, LaneUp, 2, 1),   STEP(31, LaneRight, 4, 1),
    STEP(32, LaneUp, 6, 2),    STEP(36, LaneDown, 5, 1), STEP(38, LaneLeft, 4, 1),
    STEP(40, LaneRight, 6, 1), STEP(42, LaneUp, 7, 1),   STEP(44, LaneRight, 6, 1),
    STEP(46, LaneDown, 5, 1),  STEP(48, LaneLeft, 4, 2),
};

static const ChartStep staircase_chart[] = {
    STEP(0, LaneLeft, 0, 2),   STEP(4, LaneDown, 1, 2),  STEP(8, LaneUp, 2, 2),
    STEP(12, LaneRight, 4, 2), STEP(16, LaneUp, 5, 2),   STEP(20, LaneDown, 4, 2),
    STEP(24, LaneLeft, 2, 2),  STEP(28, LaneDown, 1, 2), STEP(32, LaneUp, 2, 1),
    STEP(34, LaneRight, 4, 1), STEP(36, LaneUp, 5, 1),   STEP(38, LaneDown, 4, 1),
    STEP(40, LaneLeft, 2, 1),  STEP(42, LaneDown, 1, 1), STEP(44, LaneUp, 2, 1),
    STEP(46, LaneRight, 4, 1), STEP(48, LaneUp, 5, 1),   STEP(50, LaneRight, 6, 1),
    STEP(52, LaneUp, 7, 2),    STEP(56, LaneDown, 5, 2), STEP(60, LaneLeft, 4, 2),
    STEP(64, LaneDown, 2, 1),  STEP(66, LaneUp, 4, 1),   STEP(68, LaneRight, 5, 1),
    STEP(70, LaneUp, 7, 1),    STEP(72, LaneDown, 5, 1), STEP(74, LaneLeft, 4, 1),
    STEP(76, LaneDown, 2, 2),  STEP(80, LaneLeft, 0, 3),
};

static const SongDef songs[] = {
    {"Neon Run", 128, 68, neon_run_chart, COUNT_OF(neon_run_chart)},
    {"Bit Shift", 144, 54, bit_shift_chart, COUNT_OF(bit_shift_chart)},
    {"Staircase", 110, 86, staircase_chart, COUNT_OF(staircase_chart)},
};

static const uint8_t arrow_up_bits[] = {
    0x20, 0x00, 0x70, 0x00, 0xD8, 0x00, 0x8C, 0x01, 0xFE, 0x03, 0x20,
    0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x00, 0x00,
};
static const uint8_t arrow_down_bits[] = {
    0x00, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20,
    0x00, 0xFE, 0x03, 0x8C, 0x01, 0xD8, 0x00, 0x70, 0x00, 0x20, 0x00,
};
static const uint8_t arrow_left_bits[] = {
    0x20, 0x00, 0x38, 0x00, 0x1E, 0x00, 0xFF, 0x03, 0x1E, 0x00, 0x38,
    0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00,
};
static const uint8_t arrow_right_bits[] = {
    0x20, 0x00, 0xE0, 0x00, 0xC0, 0x03, 0xFE, 0x07, 0xC0, 0x03, 0xE0,
    0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static const uint8_t* lane_bitmap(uint8_t lane) {
    switch(lane) {
    case LaneLeft:
        return arrow_left_bits;
    case LaneDown:
        return arrow_down_bits;
    case LaneUp:
        return arrow_up_bits;
    case LaneRight:
    default:
        return arrow_right_bits;
    }
}

static inline uint32_t abs_i32(int32_t v) {
    return (v < 0) ? (uint32_t)(-v) : (uint32_t)v;
}

static uint32_t tick_ms(const SongDef* song, uint16_t tick) {
    return SONG_START_OFFSET_MS + ((uint32_t)tick * 30000UL) / song->bpm;
}

static uint32_t duration_ms(const SongDef* song, uint8_t duration_ticks) {
    if(duration_ticks == 0U) duration_ticks = 1U;
    return ((uint32_t)duration_ticks * 30000UL) / song->bpm;
}

static const char* judge_string(Judge judge) {
    switch(judge) {
    case JudgePerfect:
        return "PERFECT";
    case JudgeGreat:
        return "GREAT";
    case JudgeGood:
        return "GOOD";
    case JudgeMiss:
        return "MISS";
    case JudgeNone:
    default:
        return "";
    }
}

static uint16_t judge_points(Judge judge) {
    switch(judge) {
    case JudgePerfect:
        return 1000U;
    case JudgeGreat:
        return 700U;
    case JudgeGood:
        return 400U;
    case JudgeMiss:
    case JudgeNone:
    default:
        return 0U;
    }
}

static Judge delta_to_judge(uint32_t abs_delta_ms) {
    if(abs_delta_ms <= 70U) return JudgePerfect;
    if(abs_delta_ms <= 120U) return JudgeGreat;
    if(abs_delta_ms <= 170U) return JudgeGood;
    return JudgeNone;
}

static int32_t lane_x(uint8_t lane) {
    static const int32_t positions[LANE_COUNT] = {8, 24, 40, 56};
    return positions[lane];
}

static void draw_text_center(Canvas* canvas, int32_t y, Font font, const char* text) {
    int32_t x;

    canvas_set_font(canvas, font);
    x = ((int32_t)canvas_width(canvas) - (int32_t)canvas_string_width(canvas, text)) / 2;
    if(x < 0) x = 0;
    canvas_draw_str(canvas, x, y, text);
}

static void draw_text_right(Canvas* canvas, int32_t right_x, int32_t y, Font font, const char* text) {
    int32_t x;

    canvas_set_font(canvas, font);
    x = right_x - (int32_t)canvas_string_width(canvas, text);
    if(x < 0) x = 0;
    canvas_draw_str(canvas, x, y, text);
}

static void draw_arrow_sprite(Canvas* canvas, int32_t cx, int32_t cy, uint8_t lane) {
    canvas_draw_xbm(canvas, cx - 5, cy - 5, 11, 11, lane_bitmap(lane));
}

static void speaker_stop_release(PocketStepApp* app) {
    if(app->speaker_owned) {
        furi_hal_speaker_stop();
        furi_hal_speaker_release();
        app->speaker_owned = false;
    }
    app->active_note_index = NO_INDEX;
}

static void reset_stats(PocketStepApp* app) {
    app->score = 0;
    app->combo = 0;
    app->max_combo = 0;
    app->perfect = 0;
    app->great = 0;
    app->good = 0;
    app->miss = 0;
    app->last_judge = JudgeNone;
    memset(app->lane_flash, 0, sizeof(app->lane_flash));
}

static void clear_step_states(PocketStepApp* app) {
    clear_step_states(app);
}

static void return_to_menu(PocketStepApp* app) {
    speaker_stop_release(app);
    clear_step_states(app);
    app->screen = ScreenMenu;
    app->current_song = NULL;
    app->song_elapsed_ms = 0;
    app->next_note_index = 0;
    app->active_note_index = NO_INDEX;
    app->last_judge = JudgeNone;
}

static bool start_song(PocketStepApp* app, const SongDef* song) {
    size_t state_size;

    if(app->step_states) {
        free(app->step_states);
        app->step_states = NULL;
    }

    state_size = (size_t)song->chart_len * sizeof(StepState);
    app->step_states = malloc(state_size);
    if(!app->step_states) {
        return false;
    }

    memset(app->step_states, 0, state_size);
    app->speaker_owned = furi_hal_speaker_acquire(SPEAKER_ACQUIRE_TIMEOUT_MS);
    app->current_song = song;
    app->screen = ScreenPlaying;
    app->song_start_tick = furi_get_tick();
    app->song_elapsed_ms = 0;
    app->next_note_index = 0;
    app->active_note_index = NO_INDEX;
    reset_stats(app);
    return true;
}

static void finish_song(PocketStepApp* app) {
    speaker_stop_release(app);
    clear_step_states(app);
    app->screen = ScreenResults;
}

static void register_judge(PocketStepApp* app, Judge judge) {
    app->last_judge = judge;
    app->score += judge_points(judge);

    switch(judge) {
    case JudgePerfect:
        app->perfect++;
        app->combo++;
        break;
    case JudgeGreat:
        app->great++;
        app->combo++;
        break;
    case JudgeGood:
        app->good++;
        app->combo++;
        break;
    case JudgeMiss:
        app->miss++;
        app->combo = 0;
        break;
    case JudgeNone:
    default:
        break;
    }

    if(app->combo > app->max_combo) {
        app->max_combo = app->combo;
    }
}

static void try_hit_lane(PocketStepApp* app, uint8_t lane) {
    uint16_t best_index = NO_INDEX;
    uint32_t best_delta = 171U;
    uint16_t i;

    if(!app->current_song || !app->step_states) return;

    for(i = 0; i < app->current_song->chart_len; i++) {
        uint32_t note_time;
        int32_t delta;
        uint32_t abs_delta_ms;

        if(app->step_states[i] != StepPending) continue;
        if(app->current_song->chart[i].lane != lane) continue;

        note_time = tick_ms(app->current_song, app->current_song->chart[i].tick);
        delta = (int32_t)app->song_elapsed_ms - (int32_t)note_time;
        abs_delta_ms = abs_i32(delta);
        if(abs_delta_ms <= 170U && abs_delta_ms < best_delta) {
            best_delta = abs_delta_ms;
            best_index = i;
        }
    }

    if(best_index != NO_INDEX) {
        Judge judge;
        uint32_t note_time = tick_ms(app->current_song, app->current_song->chart[best_index].tick);
        judge = delta_to_judge(abs_i32((int32_t)app->song_elapsed_ms - (int32_t)note_time));
        app->step_states[best_index] = StepHit;
        register_judge(app, judge);
    }

    app->lane_flash[lane] = 5U;
}

static bool all_steps_resolved(const PocketStepApp* app) {
    uint16_t i;

    if(!app->current_song || !app->step_states) return true;
    for(i = 0; i < app->current_song->chart_len; i++) {
        if(app->step_states[i] == StepPending) return false;
    }
    return true;
}

static void update_misses(PocketStepApp* app) {
    uint16_t i;

    if(!app->current_song || !app->step_states) return;

    for(i = 0; i < app->current_song->chart_len; i++) {
        uint32_t note_time;
        if(app->step_states[i] != StepPending) continue;
        note_time = tick_ms(app->current_song, app->current_song->chart[i].tick);
        if(app->song_elapsed_ms > note_time + 220U) {
            app->step_states[i] = StepMiss;
            register_judge(app, JudgeMiss);
        }
    }
}

static void update_music(PocketStepApp* app) {
    if(!app->speaker_owned || !app->current_song) return;

    if(app->active_note_index != NO_INDEX) {
        const ChartStep* active = &app->current_song->chart[app->active_note_index];
        uint32_t end_ms = tick_ms(app->current_song, active->tick) + duration_ms(app->current_song, active->duration_ticks);
        if(app->song_elapsed_ms >= end_ms) {
            furi_hal_speaker_stop();
            app->active_note_index = NO_INDEX;
        }
    }

    if(app->active_note_index == NO_INDEX) {
        while(app->next_note_index < app->current_song->chart_len) {
            const ChartStep* next = &app->current_song->chart[app->next_note_index];
            uint32_t start_ms = tick_ms(app->current_song, next->tick);
            if(app->song_elapsed_ms < start_ms) break;

            furi_hal_speaker_start(tone_table[next->tone % COUNT_OF(tone_table)], SPEAKER_VOLUME);
            app->active_note_index = app->next_note_index;
            app->next_note_index++;
            break;
        }
    }
}

static void update_game(PocketStepApp* app) {
    uint8_t lane;
    uint32_t end_ms;

    if(app->screen != ScreenPlaying || !app->current_song) return;

    app->song_elapsed_ms = furi_get_tick() - app->song_start_tick;

    for(lane = 0; lane < LANE_COUNT; lane++) {
        if(app->lane_flash[lane] > 0U) app->lane_flash[lane]--;
    }

    update_misses(app);
    update_music(app);

    end_ms = tick_ms(app->current_song, app->current_song->total_ticks);
    if(app->song_elapsed_ms > end_ms && all_steps_resolved(app)) {
        finish_song(app);
    }
}

static void draw_lane_receptor(Canvas* canvas, int32_t cx, int32_t cy, uint8_t lane, bool flash) {
    canvas_draw_rframe(canvas, cx - 7, cy - 7, 14, 14, 2);
    canvas_draw_rframe(canvas, cx - 8, cy - 8, 16, 16, 2);
    if(flash) {
        canvas_draw_line(canvas, cx - 9, cy, cx - 13, cy);
        canvas_draw_line(canvas, cx + 9, cy, cx + 13, cy);
        canvas_draw_line(canvas, cx, cy - 9, cx, cy - 13);
        canvas_draw_line(canvas, cx, cy + 9, cx, cy + 13);
    }
    draw_arrow_sprite(canvas, cx, cy, lane);
}

static void draw_menu(Canvas* canvas, const PocketStepApp* app) {
    uint8_t i;
    char line[32];

    canvas_clear(canvas);
    canvas_draw_frame(canvas, 0, 0, 64, 128);
    draw_text_center(canvas, 12, FontPrimary, "POCKET STEP");
    draw_text_center(canvas, 24, FontSecondary, "portrait rhythm");

    for(i = 0; i < COUNT_OF(songs); i++) {
        int32_t y = 42 + (int32_t)i * 22;
        if(i == app->menu_index) {
            canvas_draw_rbox(canvas, 6, y - 10, 52, 16, 2);
            canvas_set_color(canvas, ColorWhite);
            draw_text_center(canvas, y, FontSecondary, songs[i].title);
            canvas_set_color(canvas, ColorBlack);
        } else {
            canvas_draw_rframe(canvas, 6, y - 10, 52, 16, 2);
            draw_text_center(canvas, y, FontSecondary, songs[i].title);
        }
    }

    snprintf(line, sizeof(line), "%u songs", (unsigned)COUNT_OF(songs));
    draw_text_center(canvas, 104, FontSecondary, line);
    draw_text_center(canvas, 116, FontSecondary, "UP/DN select");
    draw_text_center(canvas, 126, FontSecondary, "OK play  BACK quit");
}

static void draw_results(Canvas* canvas, const PocketStepApp* app) {
    char line[32];

    canvas_clear(canvas);
    canvas_draw_frame(canvas, 0, 0, 64, 128);
    draw_text_center(canvas, 12, FontPrimary, "RESULTS");
    if(app->current_song) draw_text_center(canvas, 24, FontSecondary, app->current_song->title);

    snprintf(line, sizeof(line), "Score %lu", (unsigned long)app->score);
    draw_text_center(canvas, 42, FontSecondary, line);
    snprintf(line, sizeof(line), "Max combo %u", app->max_combo);
    draw_text_center(canvas, 54, FontSecondary, line);
    snprintf(line, sizeof(line), "Perfect %u", app->perfect);
    draw_text_center(canvas, 70, FontSecondary, line);
    snprintf(line, sizeof(line), "Great %u", app->great);
    draw_text_center(canvas, 82, FontSecondary, line);
    snprintf(line, sizeof(line), "Good %u", app->good);
    draw_text_center(canvas, 94, FontSecondary, line);
    snprintf(line, sizeof(line), "Miss %u", app->miss);
    draw_text_center(canvas, 106, FontSecondary, line);

    draw_text_center(canvas, 124, FontSecondary, "OK/BACK menu");
}

static void draw_playfield(Canvas* canvas, const PocketStepApp* app) {
    static const int32_t receptor_y = 22;
    static const int32_t spawn_y = 118;
    char line[32];
    uint16_t i;

    canvas_clear(canvas);
    canvas_draw_frame(canvas, 0, 0, 64, 128);

    canvas_draw_line(canvas, 4, receptor_y + 10, 60, receptor_y + 10);
    canvas_draw_line(canvas, 16, 10, 16, 122);
    canvas_draw_line(canvas, 32, 10, 32, 122);
    canvas_draw_line(canvas, 48, 10, 48, 122);

    snprintf(line, sizeof(line), "%05lu", (unsigned long)app->score);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 3, 8, line);

    snprintf(line, sizeof(line), "x%u", app->combo);
    draw_text_right(canvas, 61, 8, FontSecondary, line);

    for(i = 0; i < LANE_COUNT; i++) {
        draw_lane_receptor(canvas, lane_x(i), receptor_y, i, app->lane_flash[i] > 0U);
    }

    if(app->current_song && app->step_states) {
        for(i = 0; i < app->current_song->chart_len; i++) {
            const ChartStep* step = &app->current_song->chart[i];
            uint32_t note_time;
            int32_t delta;
            int32_t y;

            if(app->step_states[i] != StepPending) continue;

            note_time = tick_ms(app->current_song, step->tick);
            delta = (int32_t)note_time - (int32_t)app->song_elapsed_ms;
            if(delta > (int32_t)NOTE_SCROLL_MS || delta < -220) continue;

            y = receptor_y + (int32_t)(((spawn_y - receptor_y) * delta) / (int32_t)NOTE_SCROLL_MS);
            if(y < receptor_y - 12) y = receptor_y - 12;
            if(y > spawn_y + 6) y = spawn_y + 6;
            draw_arrow_sprite(canvas, lane_x(step->lane), y, step->lane);
        }

        canvas_draw_str(canvas, 3, 126, judge_string(app->last_judge));
        if(app->speaker_owned) {
            draw_text_right(canvas, 61, 126, FontSecondary, "AUDIO");
        } else {
            draw_text_right(canvas, 61, 126, FontSecondary, "MUTED");
        }

        if(app->song_elapsed_ms > SONG_START_OFFSET_MS) {
            uint32_t total_ms = tick_ms(app->current_song, app->current_song->total_ticks);
            uint32_t progress_ms = app->song_elapsed_ms - SONG_START_OFFSET_MS;
            uint32_t max_ms = total_ms - SONG_START_OFFSET_MS;
            uint32_t fill = 0U;
            if(max_ms > 0U) fill = (54U * progress_ms) / max_ms;
            if(fill > 54U) fill = 54U;
            canvas_draw_rframe(canvas, 5, 111, 54, 7, 2);
            canvas_draw_box(canvas, 7, 113, fill, 3);
        }
    }
}

static void draw_callback(Canvas* canvas, void* context) {
    PocketStepApp* app = context;

    switch(app->screen) {
    case ScreenMenu:
        draw_menu(canvas, app);
        break;
    case ScreenPlaying:
        draw_playfield(canvas, app);
        break;
    case ScreenResults:
        draw_results(canvas, app);
        break;
    }
}

static void input_callback(InputEvent* event, void* context) {
    PocketStepApp* app = context;
    furi_message_queue_put(app->event_queue, event, 0U);
}

static void handle_menu_input(PocketStepApp* app, const InputEvent* event) {
    if(event->type != InputTypeShort) return;

    switch(event->key) {
    case InputKeyUp:
        app->menu_index = (app->menu_index + COUNT_OF(songs) - 1U) % COUNT_OF(songs);
        break;
    case InputKeyDown:
        app->menu_index = (app->menu_index + 1U) % COUNT_OF(songs);
        break;
    case InputKeyOk:
        start_song(app, &songs[app->menu_index]);
        break;
    case InputKeyBack:
        app->running = false;
        break;
    default:
        break;
    }
}

static void handle_play_input(PocketStepApp* app, const InputEvent* event) {
    if(event->key == InputKeyBack && event->type == InputTypeShort) {
        return_to_menu(app);
        return;
    }

    if(event->type != InputTypePress) return;

    switch(event->key) {
    case InputKeyLeft:
        try_hit_lane(app, LaneLeft);
        break;
    case InputKeyDown:
        try_hit_lane(app, LaneDown);
        break;
    case InputKeyUp:
        try_hit_lane(app, LaneUp);
        break;
    case InputKeyRight:
        try_hit_lane(app, LaneRight);
        break;
    default:
        break;
    }
}

static void handle_results_input(PocketStepApp* app, const InputEvent* event) {
    if(event->type != InputTypeShort) return;
    if(event->key == InputKeyOk || event->key == InputKeyBack) {
        return_to_menu(app);
    }
}

static void handle_event(PocketStepApp* app, const InputEvent* event) {
    switch(app->screen) {
    case ScreenMenu:
        handle_menu_input(app, event);
        break;
    case ScreenPlaying:
        handle_play_input(app, event);
        break;
    case ScreenResults:
        handle_results_input(app, event);
        break;
    }
}

static PocketStepApp* pocket_step_alloc(void) {
    PocketStepApp* app = malloc(sizeof(PocketStepApp));
    if(!app) return NULL;

    memset(app, 0, sizeof(PocketStepApp));
    app->running = true;
    app->screen = ScreenMenu;
    app->active_note_index = NO_INDEX;
    app->event_queue = furi_message_queue_alloc(EVENT_QUEUE_SIZE, sizeof(InputEvent));
    app->view_port = view_port_alloc();
    app->gui = furi_record_open(RECORD_GUI);

    view_port_set_orientation(app->view_port, ViewPortOrientationVertical);
    view_port_draw_callback_set(app->view_port, draw_callback, app);
    view_port_input_callback_set(app->view_port, input_callback, app);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);

    return app;
}

static void pocket_step_free(PocketStepApp* app) {
    if(!app) return;

    speaker_stop_release(app);
    clear_step_states(app);

    view_port_enabled_set(app->view_port, false);
    gui_remove_view_port(app->gui, app->view_port);
    view_port_free(app->view_port);
    furi_message_queue_free(app->event_queue);
    furi_record_close(RECORD_GUI);
    free(app);
}

int32_t pocket_step_app(void* p) {
    PocketStepApp* app;

    UNUSED(p);
    app = pocket_step_alloc();
    if(!app) return -1;

    while(app->running) {
        InputEvent event;
        FuriStatus status = furi_message_queue_get(app->event_queue, &event, FRAME_TIME_MS);
        if(status == FuriStatusOk) {
            handle_event(app, &event);
        }
        update_game(app);
        view_port_update(app->view_port);
    }

    pocket_step_free(app);
    return 0;
}
