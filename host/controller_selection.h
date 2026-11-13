/* Per-process controller choice. Ordinals lock to one SDL instance for the run: unplugging a
   different pad must never make this game start reading its neighbour. No SDL or OS dependencies. */
#ifndef HOS_CONTROLLER_SELECTION_H
#define HOS_CONTROLLER_SELECTION_H
#include <stdint.h>
#include <string.h>

/* 0: legacy all-controller merge; -1: none; -2: invalid (also none); 1..32: SDL ordinal. */
static inline int hos_controller_parse(const char *text) {
    if (!text || !*text || !strcmp(text, "all")) return 0;
    if (!strcmp(text, "none") || !strcmp(text, "0")) return -1;
    unsigned n = 0;
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
        if (*p < '0' || *p > '9' || n > 32) return -2;
        n = n * 10 + (*p - '0');
    }
    return n >= 1 && n <= 32 ? (int)n : -2;
}

typedef struct HosControllerSelection {
    int number, bound;
    int32_t id;
} HosControllerSelection;

/* Called only by SDL's polling thread, with the full recognised-gamepad list. */
static inline void hos_controller_choose(HosControllerSelection *choice, const int32_t *ids, int count) {
    if (choice->number > 0 && !choice->bound && choice->number <= count) {
        choice->id = ids[choice->number - 1];
        choice->bound = 1;
    }
}

static inline int hos_controller_accepts(const HosControllerSelection *choice, int32_t id) {
    return choice->number == 0 || (choice->number > 0 && choice->bound && choice->id == id);
}
#endif
