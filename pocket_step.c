#include <furi.h>
#include <furi_hal_speaker.h>
#include <gui/elements.h>
#include <gui/gui.h>
#include <input/input.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define COUNT_OF(x) (sizeof(x) / sizeof((x)[0]))
#define LANE_COUNT 4
#define MAX_STEPS 128
#define FRAME_TIME_MS 16
#define SCROLL_WINDOW_MS 1600
#define PERFECT_WINDOW_MS 60
#define GREAT_WINDOW_MS 100
#define GOOD_WINDOW_MS 150
#define MISS_WINDOW_MS 180
#define SPEAKER_TIMEOUT_MS 100
#define SPEAKER_VOLUME 0.32f
#define INVALID_INDEX ((size_t)-1)

typedef enum {
    LaneLeft = 0,
    LaneDown = 1,
    LaneUp = 2,
    LaneRight = 3,
} Lane;

typedef enum {
    ScreenMenu,
    ScreenPlaying,
    ScreenResults,
} Screen;

typedef enum {
    JudgeNone,
    JudgePerfect,
    JudgeGreat,
    JudgeGood,
    JudgeMiss,
} Judge;

typedef enum {
    StepPending,
    StepHit,
    StepMissed,
} StepState;

typedef struct {
    uint32_t time_ms;
    uint8_t lane;
} StepNote;

typedef struct {
    uint32_t time_ms;
    uint16_t duration_ms;
    float frequency;
} MelodyNote;

typedef struct {
    const char* title;
    uint16_t bpm;
    const StepNote* steps;
    size_t step_count;
    const MelodyNote* notes;
    size_t note_count;
    uint32_t length_ms;
} Song;

typedef struct {
    FuriMessageQueue* input_queue;
    FuriMutex* mutex;
    ViewPort* view_port;
    Gui* gui;

    bool running;
    bool speaker_owned;

    Screen screen;
    uint8_t menu_index;

    const Song* current_song;
    uint32_t song_start_tick;
    uint32_t song_elapsed_ms;
    size_t next_miss_index;
    size_t next_note_index;
    size_t active_note_index;

    StepState step_state[MAX_STEPS];

    uint32_t score;
    uint16_t combo;
    uint16_t max_combo;
    uint16_t perfect_count;
    uint16_t great_count;
    uint16_t good_count;
    uint16_t miss_count;
    Judge last_judge;
    uint8_t lane_flash[LANE_COUNT];
} PocketStepApp;

static const StepNote pulse_grid_steps[] = {
    {1000, LaneLeft},  {1500, LaneDown}, {2000, LaneUp},    {2500, LaneRight},
    {3000, LaneLeft},  {3000, LaneRight}, {3500, LaneDown}, {4000, LaneUp},
    {4500, LaneRight}, {5000, LaneLeft}, {5250, LaneDown},  {5500, LaneUp},
    {5750, LaneRight}, {6500, LaneLeft}, {7000, LaneDown},  {7500, LaneUp},
    {8000, LaneRight}, {8500, LaneLeft}, {9000, LaneRight}, {9500, LaneUp},
    {10000, LaneDown}, {10500, LaneLeft}, {11000, LaneLeft}, {11000, LaneUp},
    {11500, LaneDown}, {11500, LaneRight}, {12000, LaneUp},  {12500, LaneDown},
    {13000, LaneLeft}, {13500, LaneRight}, {14000, LaneUp},  {14500, LaneDown},
    {15000, LaneLeft}, {15500, LaneRight},
};

static const MelodyNote pulse_grid_notes[] = {
    {1000, 340, 523.25f},  {1500, 340, 587.33f},  {2000, 340, 659.25f},
    {2500, 340, 783.99f},  {3000, 340, 880.00f},  {3500, 340, 783.99f},
    {4000, 340, 659.25f},  {4500, 340, 587.33f},  {5000, 340, 523.25f},
    {5500, 340, 659.25f},  {6000, 340, 783.99f},  {6500, 340, 880.00f},
    {7000, 340, 987.77f},  {7500, 340, 880.00f},  {8000, 340, 783.99f},
    {8500, 340, 659.25f},  {9000, 340, 587.33f},  {9500, 340, 523.25f},
    {10000, 340, 659.25f}, {10500, 340, 783.99f}, {11000, 340, 880.00f},
    {11500, 340, 783.99f}, {12000, 340, 659.25f}, {12500, 340, 587.33f},
    {13000, 340, 523.25f}, {13500, 340, 587.33f}, {14000, 340, 659.25f},
    {14500, 340, 783.99f}, {15000, 340, 659.25f}, {15500, 340, 523.25f},
};

static const StepNote byte_runner_steps[] = {
    {1000, LaneUp},    {1429, LaneRight}, {1858, LaneDown}, {2287, LaneLeft},
    {2716, LaneUp},    {3145, LaneUp},    {3574, LaneRight}, {4003, LaneDown},
    {4432, LaneLeft},  {4861, LaneRight}, {5075, LaneDown},  {5289, LaneUp},
    {5718, LaneLeft},  {6147, LaneDown},  {6576, LaneRight}, {7005, LaneUp},
    {7434, LaneLeft},  {7863, LaneLeft},  {7863, LaneRight}, {8292, LaneDown},
    {8721, LaneUp},    {9150, LaneRight}, {9579, LaneLeft},  {10008, LaneDown},
    {10437, LaneUp},   {10866, LaneRight}, {11295, LaneDown}, {11724, LaneLeft},
    {12153, LaneUp},   {12582, LaneRight}, {13011, LaneUp},  {13440, LaneDown},
    {13869, LaneLeft}, {14298, LaneRight},
};

static const MelodyNote byte_runner_notes[] = {
    {1000, 300, 659.25f},  {1429, 300, 783.99f},  {1858, 300, 739.99f},
    {2287, 300, 587.33f},  {2716, 300, 659.25f},  {3145, 300, 880.00f},
    {3574, 300, 783.99f},  {4003, 300, 659.25f},  {4432, 300, 587.33f},
    {4861, 180, 523.25f},  {5075, 180, 587.33f},  {5289, 180, 659.25f},
    {5718, 300, 783.99f},  {6147, 300, 880.00f},  {6576, 300, 987.77f},
    {7005, 300, 880.00f},  {7434, 300, 783.99f},  {7863, 300, 739.99f},
    {8292, 300, 659.25f},  {8721, 300, 587.33f},  {9150, 300, 659.25f},
    {9579, 300, 783.99f},  {10008, 300, 880.00f}, {10437, 300, 783.99f},
    {10866, 300, 659.25f}, {11295, 300, 587.33f}, {11724, 300, 523.25f},
    {12153, 300, 587.33f}, {12582, 300, 659.25f}, {13011, 300, 783.99f},
    {13440, 300, 739.99f}, {13869, 300, 659.25f}, {14298, 300, 523.25f},
};

static const Song songs[] = {
    {
        .title = "Pulse Grid",
        .bpm = 120,
        .steps = pulse_grid_steps,
        .step_count = COUNT_OF(pulse_grid_steps),
        .notes = pulse_grid_notes,
        .note_count = COUNT_OF(pulse_grid_notes),
        .length_ms = 16600,
    },
    {
        .title = "Byte Runner",
        .bpm = 140,
        .steps = byte_runner_steps,
        .step_count = COUNT_OF(byte_runner_steps),
        .notes = byte_runner_notes,
        .note_count = COUNT_OF(byte_runner_notes),
        .length_ms = 15400,
    },
};

static inline int32_t iabs32(int32_t value) {
    return (value < 0) ? -value : value;
}

static const char* judge_to_string(Judge judge) {
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
        return 1000;
    case JudgeGreat:
        return 700;
    case JudgeGood:
        return 400;
    case JudgeMiss:
    case JudgeNone:
    default:
        return 0;
    }
}

static Judge delta_to_judge(int32_t delta_ms) {
    int32_t abs_delta = iabs32(delta_ms);
    if(abs_delta <= PERFECT_WINDOW_MS) return JudgePerfect;
    if(abs_delta <= GREAT_WINDOW_MS) return JudgeGreat;
    if(abs_delta <= GOOD_WINDOW_MS) return JudgeGood;
    return JudgeNone;
}

static int32_t lane_to_x(uint8_t lane) {
    static const int32_t lane_x[LANE_COUNT] = {18, 46, 74, 102};
    return lane_x[lane];
}

static const Song* current_menu_song(const PocketStepApp* app) {
    return &songs[app->menu_index % COUNT_OF(songs)];
}

static void stop_audio(PocketStepApp* app) {
    if(app->speaker_owned) {
        furi_hal_speaker_stop();
    }
    app->active_note_index = INVALID_INDEX;
}

static void end_song(PocketStepApp* app) {
    stop_audio(app);
    app->screen = ScreenResults;
}

static void reset_results(PocketStepApp* app) {
    app->score = 0;
    app->combo = 0;
    app->max_combo = 0;
    app->perfect_count = 0;
    app->great_count = 0;
    app->good_count = 0;
    app->miss_count = 0;
    app->last_judge = JudgeNone;
    memset(app->lane_flash, 0, sizeof(app->lane_flash));
}

static void start_song(PocketStepApp* app, const Song* song) {
    app->current_song = song;
    app->screen = ScreenPlaying;
    app->song_start_tick = furi_get_tick();
    app->song_elapsed_ms = 0;
    app->next_miss_index = 0;
    app->next_note_index = 0;
    app->active_note_index = INVALID_INDEX;
    memset(app->step_state, 0, sizeof(app->step_state));
    reset_results(app);
    stop_audio(app);
}

static void return_to_menu(PocketStepApp* app) {
    stop_audio(app);
    app->screen = ScreenMenu;
    app->current_song = NULL;
    app->song_elapsed_ms = 0;
    app->last_judge = JudgeNone;
}

static void register_judgement(PocketStepApp* app, Judge judge) {
    app->last_judge = judge;

    switch(judge) {
    case JudgePerfect:
        app->perfect_count++;
        app->combo++;
        break;
    case JudgeGreat:
        app->great_count++;
        app->combo++;
        break;
    case JudgeGood:
        app->good_count++;
        app->combo++;
        break;
    case JudgeMiss:
        app->miss_count++;
        app->combo = 0;
        break;
    case JudgeNone:
    default:
        break;
    }

    if(app->combo > app->max_combo) {
        app->max_combo = app->combo;
    }

    app->score += judge_points(judge);
}

static void try_hit_lane(PocketStepApp* app, uint8_t lane) {
    if(!app->current_song) return;

    size_t best_index = INVALID_INDEX;
    int32_t best_delta = GOOD_WINDOW_MS + 1;

    for(size_t i = 0; i < app->current_song->step_count; i++) {
        if(app->step_state[i] != StepPending) continue;
        if(app->current_song->steps[i].lane != lane) continue;

        int32_t delta = (int32_t)app->song_elapsed_ms - (int32_t)app->current_song->steps[i].time_ms;
        int32_t abs_delta = iabs32(delta);
        if(abs_delta <= GOOD_WINDOW_MS && abs_delta < best_delta) {
            best_delta = abs_delta;
            best_index = i;
        }
    }

    if(best_index != INVALID_INDEX) {
        Judge judge = delta_to_judge(
            (int32_t)app->song_elapsed_ms - (int32_t)app->current_song->steps[best_index].time_ms);
        app->step_state[best_index] = StepHit;
        register_judgement(app, judge);
    }

    app->lane_flash[lane] = 6;
}

static void process_misses(PocketStepApp* app) {
    if(!app->current_song) return;

    while(app->next_miss_index < app->current_song->step_count) {
        const StepNote* step = &app->current_song->steps[app->next_miss_index];
        if(app->step_state[app->next_miss_index] != StepPending) {
            app->next_miss_index++;
            continue;
        }

        if(app->song_elapsed_ms > step->time_ms + MISS_WINDOW_MS) {
            app->step_state[app->next_miss_index] = StepMissed;
            register_judgement(app, JudgeMiss);
            app->next_miss_index++;
            continue;
        }

        break;
    }
}

static bool all_steps_resolved(const PocketStepApp* app) {
    if(!app->current_song) return true;
    for(size_t i = 0; i < app->current_song->step_count; i++) {
        if(app->step_state[i] == StepPending) return false;
    }
    return true;
}

static void update_music(PocketStepApp* app) {
    if(!app->speaker_owned || !app->current_song) return;

    if(app->active_note_index != INVALID_INDEX) {
        const MelodyNote* active = &app->current_song->notes[app->active_note_index];
        if(app->song_elapsed_ms >= (active->time_ms + active->duration_ms)) {
            furi_hal_speaker_stop();
            app->active_note_index = INVALID_INDEX;
        }
    }

    if(app->active_note_index == INVALID_INDEX && app->next_note_index < app->current_song->note_count) {
        const MelodyNote* next = &app->current_song->notes[app->next_note_index];
        if(app->song_elapsed_ms >= next->time_ms) {
            furi_hal_speaker_start(next->frequency, SPEAKER_VOLUME);
            app->active_note_index = app->next_note_index;
            app->next_note_index++;
        }
    }
}

static void update_gameplay(PocketStepApp* app) {
    if(app->screen != ScreenPlaying || !app->current_song) return;

    app->song_elapsed_ms = furi_get_tick() - app->song_start_tick;

    process_misses(app);
    update_music(app);

    for(size_t i = 0; i < LANE_COUNT; i++) {
        if(app->lane_flash[i] > 0) app->lane_flash[i]--;
    }

    if(app->song_elapsed_ms >= app->current_song->length_ms && all_steps_resolved(app)) {
        end_song(app);
    }
}

static void draw_arrow(Canvas* canvas, int32_t cx, int32_t cy, uint8_t lane) {
    switch(lane) {
    case LaneLeft:
        canvas_draw_box(canvas, cx, cy - 1, 6, 3);
        canvas_draw_line(canvas, cx - 4, cy, cx, cy - 4);
        canvas_draw_line(canvas, cx - 4, cy, cx, cy + 4);
        canvas_draw_line(canvas, cx, cy - 3, cx, cy + 3);
        canvas_draw_line(canvas, cx - 1, cy - 2, cx - 1, cy + 2);
        break;
    case LaneDown:
        canvas_draw_box(canvas, cx - 1, cy - 5, 3, 6);
        canvas_draw_line(canvas, cx, cy + 4, cx - 4, cy);
        canvas_draw_line(canvas, cx, cy + 4, cx + 4, cy);
        canvas_draw_line(canvas, cx - 3, cy, cx + 3, cy);
        canvas_draw_line(canvas, cx - 2, cy + 1, cx + 2, cy + 1);
        break;
    case LaneUp:
        canvas_draw_box(canvas, cx - 1, cy, 3, 6);
        canvas_draw_line(canvas, cx, cy - 4, cx - 4, cy);
        canvas_draw_line(canvas, cx, cy - 4, cx + 4, cy);
        canvas_draw_line(canvas, cx - 3, cy, cx + 3, cy);
        canvas_draw_line(canvas, cx - 2, cy - 1, cx + 2, cy - 1);
        break;
    case LaneRight:
        canvas_draw_box(canvas, cx - 5, cy - 1, 6, 3);
        canvas_draw_line(canvas, cx + 4, cy, cx, cy - 4);
        canvas_draw_line(canvas, cx + 4, cy, cx, cy + 4);
        canvas_draw_line(canvas, cx, cy - 3, cx, cy + 3);
        canvas_draw_line(canvas, cx + 1, cy - 2, cx + 1, cy + 2);
        break;
    default:
        break;
    }
}

static void draw_receptor(Canvas* canvas, int32_t cx, int32_t cy, uint8_t lane, bool active) {
    if(active) {
        canvas_draw_rbox(canvas, cx - 8, cy - 8, 16, 16, 2);
        canvas_set_color(canvas, ColorWhite);
        draw_arrow(canvas, cx, cy, lane);
        canvas_set_color(canvas, ColorBlack);
    } else {
        canvas_draw_rframe(canvas, cx - 8, cy - 8, 16, 16, 2);
        draw_arrow(canvas, cx, cy, lane);
    }
}

static void draw_menu(Canvas* canvas, const PocketStepApp* app) {
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 11, AlignCenter, AlignBottom, "POCKET STEP");

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 64, 20, AlignCenter, AlignBottom, "Flipper rhythm demo");

    for(size_t i = 0; i < COUNT_OF(songs); i++) {
        int32_t y = 34 + (int32_t)i * 11;
        if(i == app->menu_index) {
            canvas_draw_rbox(canvas, 14, y - 8, 100, 10, 2);
            canvas_set_color(canvas, ColorWhite);
            canvas_draw_str(canvas, 18, y, songs[i].title);
            canvas_set_color(canvas, ColorBlack);
        } else {
            canvas_draw_rframe(canvas, 14, y - 8, 100, 10, 2);
            canvas_draw_str(canvas, 18, y, songs[i].title);
        }
    }

    if(app->speaker_owned) {
        canvas_draw_str_aligned(canvas, 64, 59, AlignCenter, AlignBottom, "OK start  BACK exit");
    } else {
        canvas_draw_str_aligned(canvas, 64, 59, AlignCenter, AlignBottom, "Muted: speaker busy");
    }
}

static void draw_results(Canvas* canvas, const PocketStepApp* app) {
    char line[32];
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 11, AlignCenter, AlignBottom, "RESULTS");

    canvas_set_font(canvas, FontSecondary);
    if(app->current_song) {
        canvas_draw_str_aligned(canvas, 64, 20, AlignCenter, AlignBottom, app->current_song->title);
    }

    snprintf(line, sizeof(line), "Score %lu", (unsigned long)app->score);
    canvas_draw_str(canvas, 10, 32, line);

    snprintf(line, sizeof(line), "Max combo %u", app->max_combo);
    canvas_draw_str(canvas, 10, 41, line);

    snprintf(
        line,
        sizeof(line),
        "P%u G%u O%u M%u",
        app->perfect_count,
        app->great_count,
        app->good_count,
        app->miss_count);
    canvas_draw_str(canvas, 10, 50, line);

    canvas_draw_str_aligned(canvas, 64, 61, AlignCenter, AlignBottom, "OK menu  BACK menu");
}

static void draw_playfield(Canvas* canvas, const PocketStepApp* app) {
    static const int32_t receptor_y = 18;
    static const int32_t spawn_y = 56;
    char line[24];

    canvas_clear(canvas);
    canvas_draw_frame(canvas, 0, 0, 128, 64);

    canvas_set_font(canvas, FontSecondary);
    snprintf(line, sizeof(line), "%06lu", (unsigned long)app->score);
    canvas_draw_str(canvas, 4, 8, line);

    snprintf(line, sizeof(line), "x%u", app->combo);
    canvas_draw_str_aligned(canvas, 64, 8, AlignCenter, AlignBottom, line);

    if(app->speaker_owned) {
        if(app->current_song) canvas_draw_str_aligned(canvas, 124, 8, AlignRight, AlignBottom, app->current_song->title);
    } else {
        canvas_draw_str_aligned(canvas, 124, 8, AlignRight, AlignBottom, "MUTE");
    }

    canvas_draw_line(canvas, 6, receptor_y + 8, 121, receptor_y + 8);
    canvas_draw_line(canvas, 32, 10, 32, 52);
    canvas_draw_line(canvas, 60, 10, 60, 52);
    canvas_draw_line(canvas, 88, 10, 88, 52);

    for(size_t lane = 0; lane < LANE_COUNT; lane++) {
        draw_receptor(canvas, lane_to_x(lane), receptor_y, lane, app->lane_flash[lane] > 0);
    }

    if(app->current_song) {
        for(size_t i = 0; i < app->current_song->step_count; i++) {
            if(app->step_state[i] != StepPending) continue;

            int32_t delta = (int32_t)app->current_song->steps[i].time_ms - (int32_t)app->song_elapsed_ms;
            if(delta > SCROLL_WINDOW_MS || delta < -MISS_WINDOW_MS) continue;

            int32_t y = receptor_y + ((int32_t)(spawn_y - receptor_y) * delta) / SCROLL_WINDOW_MS;
            draw_arrow(canvas, lane_to_x(app->current_song->steps[i].lane), y, app->current_song->steps[i].lane);
        }

        canvas_draw_str(canvas, 4, 58, judge_to_string(app->last_judge));

        snprintf(line, sizeof(line), "%lus", (unsigned long)(app->song_elapsed_ms / 1000));
        canvas_draw_str_aligned(canvas, 124, 58, AlignRight, AlignBottom, line);

        canvas_draw_frame(canvas, 28, 56, 72, 5);
        if(app->current_song->length_ms > 0) {
            size_t width = (size_t)((68UL * app->song_elapsed_ms) / app->current_song->length_ms);
            if(width > 68) width = 68;
            canvas_draw_box(canvas, 30, 58, width, 1);
        }
    }
}

static void draw_callback(Canvas* canvas, void* context) {
    PocketStepApp* app = context;
    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);

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

    furi_mutex_release(app->mutex);
}

static void input_callback(InputEvent* input_event, void* context) {
    FuriMessageQueue* queue = context;
    furi_message_queue_put(queue, input_event, 0);
}

static void handle_menu_input(PocketStepApp* app, const InputEvent* event) {
    if(event->type != InputTypeShort) return;

    if(event->key == InputKeyUp) {
        app->menu_index = (app->menu_index + COUNT_OF(songs) - 1) % COUNT_OF(songs);
    } else if(event->key == InputKeyDown) {
        app->menu_index = (app->menu_index + 1) % COUNT_OF(songs);
    } else if(event->key == InputKeyOk) {
        start_song(app, current_menu_song(app));
    } else if(event->key == InputKeyBack) {
        app->running = false;
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

static void handle_input(PocketStepApp* app, const InputEvent* event) {
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

int32_t pocket_step_app(void* p) {
    UNUSED(p);

    PocketStepApp app = {0};
    app.running = true;
    app.screen = ScreenMenu;
    app.menu_index = 0;
    app.active_note_index = INVALID_INDEX;

    app.input_queue = furi_message_queue_alloc(8, sizeof(InputEvent));
    app.mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app.view_port = view_port_alloc();
    app.gui = furi_record_open(RECORD_GUI);

    if(furi_hal_speaker_acquire(SPEAKER_TIMEOUT_MS)) {
        app.speaker_owned = true;
    }

    view_port_draw_callback_set(app.view_port, draw_callback, &app);
    view_port_input_callback_set(app.view_port, input_callback, app.input_queue);
    gui_add_view_port(app.gui, app.view_port, GuiLayerFullscreen);

    while(app.running) {
        InputEvent event;
        bool has_event =
            furi_message_queue_get(app.input_queue, &event, FRAME_TIME_MS) == FuriStatusOk;

        furi_check(furi_mutex_acquire(app.mutex, FuriWaitForever) == FuriStatusOk);
        if(has_event) {
            handle_input(&app, &event);
        }
        update_gameplay(&app);
        furi_mutex_release(app.mutex);

        view_port_update(app.view_port);
    }

    furi_check(furi_mutex_acquire(app.mutex, FuriWaitForever) == FuriStatusOk);
    stop_audio(&app);
    furi_mutex_release(app.mutex);

    gui_remove_view_port(app.gui, app.view_port);
    view_port_free(app.view_port);
    furi_record_close(RECORD_GUI);

    if(app.speaker_owned) {
        furi_hal_speaker_release();
    }

    furi_message_queue_free(app.input_queue);
    furi_mutex_free(app.mutex);

    return 0;
}
