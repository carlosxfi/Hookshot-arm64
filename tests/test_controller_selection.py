"""Compile the real SDL poll with three fake controllers; isolate input, rumble and gyro candidates."""
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def function(source, marker):
    start = source.index(marker)
    at = source.index("{", start)
    depth = 0
    for end in range(at, len(source)):
        depth += (source[end] == "{") - (source[end] == "}")
        if depth == 0:
            return source[start:end + 1]
    raise AssertionError(marker)


class ControllerSelection(unittest.TestCase):
    def test_real_poll(self):
        pad = (ROOT / "host/pad_sdl.c").read_text()
        hid = (ROOT / "host/nn_hid.c").read_text()
        # Compile the actual XInput eligibility expression too: no fallback for selected/missing/invalid pads.
        gate = re.search(r"if \((hos_controller_filter\(\) == 0 && \(!sdl_pads \|\| poll_xinput_env\))\) \{", hid)
        self.assertIsNotNone(gate)
        self.assertIn("controller_choice.number = hos_controller_filter();", function(pad, "static int pads_start("))
        structs = pad[pad.index("static struct {"):pad.index("static uint32_t pads_opened;")]
        enums = pad[pad.index("enum { BTN_A"):pad.index("#define MAX_PADS")]
        fixture = r'''
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "host/controller_selection.h"
#include "host/hd_rumble.h"
#include "host/steam_pads.h"
#include "gpu/vk/vk_api.h"
ENUMS
#define MAX_PADS 8
STRUCTS
static HosControllerSelection controller_choice;
static uint32_t pads_opened;
static uint64_t state_buttons;
static int32_t state_stick[4];
static int state_count, state_lock, hd_lock;
static char state_name[128];
static unsigned rumble_low=100, rumble_high=200;
static float hd_values[2][4];
static int seen[3], motors[3], effects[3], sensors[3], movement;
static int nlist=3, order[3]={0,1,2}, disconnected=-1;
static int64_t clock_ns=1000000000LL;
static int fake_gc[3];
static int index_of(void *gc) { int i=(int)((int *)gc-fake_gc); assert(i>=0 && i<3); return i; }
static void hos_log(const char *format, ...) { (void)format; }
static int64_t hos_now_ns(void) { return clock_ns; }
static void hos_sdl_pads_update(void) {}
static int hos_sdl_pad_connected(void *gc) { return index_of(gc)!=disconnected; }
static const char *hos_sdl_pad_name(void *gc) { (void)gc; return "identically named pad"; }
static void hos_sdl_pad_close(void *gc) { (void)gc; }
static int hos_sdl_pads_list(int32_t *ids, int *opens, int cap) {
    assert(cap>=nlist);
    for (int j=0;j<nlist;j++) ids[j]=11+11*order[j], opens[j]=order[j];
    return nlist;
}
static void *hos_sdl_pad_open(int i) { seen[i]++; return &fake_gc[i]; }
static int32_t hos_sdl_pad_id(void *gc) { return 11+11*index_of(gc); }
enum { HOS_SDL_SENSOR_ACCEL=1, HOS_SDL_SENSOR_GYRO=2 };
static int hos_sdl_pad_has_sensor(void *gc, int sensor) { (void)sensor; sensors[index_of(gc)]++; return 1; }
static unsigned hos_sdl_pad_vendor(void *gc) { (void)gc; return 0; }
static uint64_t hos_sdl_pad_steam_handle(void *gc) { (void)gc; return 0; }
static void hos_sdl_pad_log_connection(void *gc, int motion, int steam) { (void)gc; (void)motion; (void)steam; }
static int hos_sdl_pad_button(void *gc, int button) { return button==index_of(gc); }
static int16_t hos_sdl_pad_axis(void *gc, int axis) { return axis==AX_LX && movement ? (int16_t)(10000+1000*index_of(gc)) : 0; }
static int rx_option(int id, int fallback) { (void)id; return fallback; }
static void hos_filter_stick(int16_t x, int16_t y, int zone, int32_t *out) { (void)zone; out[0]=x; out[1]=y; }
static void hd_open(int i, void *gc) { (void)i; (void)gc; }
static int use_hd;
static int hd_poll(int i, const uint8_t side[3][4], int64_t now) {
    (void)side; (void)now;
    if (use_hd) effects[index_of(pads[i].gc)]++;
    return use_hd;
}
static void hos_sdl_pad_rumble(void *gc, uint16_t lo, uint16_t hi, uint32_t ms) {
    assert(lo && hi && ms); motors[index_of(gc)]++;
}
static void motion_only_used(int i, int64_t now) { (void)i; (void)now; }
static void motion_poll(int64_t now, int count) { (void)now; (void)count; }
#define AcquireSRWLockShared(p) ((void)(p))
#define ReleaseSRWLockShared(p) ((void)(p))
#define AcquireSRWLockExclusive(p) ((void)(p))
#define ReleaseSRWLockExclusive(p) ((void)(p))
RUMBLE_DUE
RUMBLE_STRENGTH
POLL
typedef int32_t LONG;
static const char *selection_text;
static const char *fake_getenv(const char *name) { assert(!strcmp(name,"HOS_CONTROLLER")); return selection_text; }
static LONG InterlockedCompareExchange(volatile LONG *p, LONG value, LONG expected) {
    return __sync_val_compare_and_swap(p,expected,value);
}
#define getenv fake_getenv
FILTER
#undef getenv
static int mode;
static int xinput_allowed(int sdl_pads, int poll_xinput_env) { return GATE; }
int main(int argc, char **argv) {
    assert(argc==2);
    assert(hos_controller_parse(NULL)==0 && hos_controller_parse("")==0 && hos_controller_parse("all")==0);
    assert(hos_controller_parse("none")==-1 && hos_controller_parse("0")==-1);
    assert(hos_controller_parse("32")==32 && hos_controller_parse("01")==1);
    const char *bad[]={"-1","+1","1junk"," 1","1 ","33","999999999999999999999","All","00"};
    for (unsigned i=0;i<sizeof bad/sizeof *bad;i++) assert(hos_controller_parse(bad[i])==-2);
    selection_text=argv[1]; mode=hos_controller_parse(argv[1]); controller_choice.number=mode;
    assert(hos_controller_filter()==mode);
    selection_text="all"; assert(hos_controller_filter()==mode); // immutable process choice
    for (int s=0;s<2;s++) for (int force=0;force<2;force++)
        assert(xinput_allowed(s,force)==(mode==0 && (!s || force)));
    pads_poll(); // sticks first seen at rest
    movement=1; clock_ns+=2000000000LL; pads_poll();
    int expected_count=mode==0?3:mode>0 && mode<=3?1:0;
    assert(state_count==expected_count);
    uint64_t buttons=0;
    for (int i=0;i<3;i++) {
        int selected=mode==0 || mode==i+1;
        assert(seen[i]==selected);
        assert((motors[i]>0)==selected);
        assert((sensors[i]>0)==selected); // only the selected pad is offered to gyro setup
        if (selected) buttons|=1ull<<i;
    }
    assert(state_buttons==buttons);
    assert(state_stick[0]==(expected_count?(mode==0?12000:10000+1000*(mode-1)):0));
    use_hd=1; clock_ns+=2000000000LL; pads_poll();
    for (int i=0;i<3;i++) assert((effects[i]>0)==(mode==0 || mode==i+1));
    if (mode==2) {
        // Unplug selected pad, reorder the identical neighbours: no reassignment and neutral output.
        disconnected=1; nlist=2; order[0]=2; order[1]=0; clock_ns+=2000000000LL; pads_poll();
        assert(state_count==0 && state_buttons==0 && state_stick[0]==0);
        assert(seen[0]==0 && seen[2]==0);
        assert(controller_choice.bound && controller_choice.id==22);
    }
    if (mode==4) {
        // A missing ordinal never opens an unrelated device or falls through to XInput.
        assert(!controller_choice.bound);
        order[0]=2; order[1]=0; clock_ns+=2000000000LL; pads_poll();
        assert(state_count==0 && !state_buttons);
    }
}
'''
        for key, value in {"ENUMS": enums, "STRUCTS": structs,
                           "RUMBLE_DUE": function(pad, "static int rumble_due("),
                           "RUMBLE_STRENGTH": function(pad, "static void rumble_strength("),
                           "POLL": function(pad, "static void pads_poll("), "GATE": gate.group(1),
                           "FILTER": function(pad, "int hos_controller_filter(")}.items():
            fixture = fixture.replace(key, value)
        cc = os.environ.get("CC") or shutil.which("cc")
        self.assertIsNotNone(cc)
        with tempfile.TemporaryDirectory(prefix="controller-selection-") as tmp:
            source, binary = Path(tmp) / "test.c", Path(tmp) / "test.exe"
            source.write_text(fixture)
            subprocess.run([cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT),
                            str(source), "-lm", "-o", str(binary)], check=True)
            for value in ("all", "1", "2", "3", "4", "none", "invalid", "0"):
                subprocess.run([str(binary), value], check=True, timeout=10)


if __name__ == "__main__":
    unittest.main()
