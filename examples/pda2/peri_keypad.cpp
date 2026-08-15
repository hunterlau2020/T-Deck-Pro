
#include <Adafruit_TCA8418.h>
#include "utilities.h"
#include "peripheral.h"

#define KEYPAD_ROWS 4
#define KEYPAD_COLS 10
#define KEYPAD_PRESS_VAL_MIN   129
#define KEYPAD_PRESS_VAL_MAX   163
#define KEYPAD_RELEASE_VAL_MIN 1
#define KEYPAD_RELEASE_VAL_MAX 35

// Row 2/3 physical layout (decoded from raw events on HD-V2, 2025-09-15):
//   col:  0     1  2  3  4  5      6    7      8    9
//   row2: Alt   z  x  c  v  b  n    m    $      Enter
//   row3:  ?  ?  ?  ?  ?  Shift   Mic  Space  Sym  Shift
// Note: there is NO Ctrl key on this hardware. The two silkscreened
// "Shift" keys are at (3,5) and (3,9); the Z-row left key is silkscreened
// "Alt". Semantics per review decision B: Alt = momentary sym layer,
// Shift = uppercase layer, Sym = sym layer lock.

// Primary layer (normal)
const char keymap[KEYPAD_ROWS][KEYPAD_COLS] = {
    {'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p'},
    {'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', '\b'},
    {  0, 'z', 'x', 'c', 'v', 'b', 'n', 'm', '$', '\n'},
    {  0,   0,   0,   0,   0,   0,   0, ' ',   0,   0},
};

// Secondary layer (sym; locked by Sym, momentary by Alt)
const char keymap_sym[KEYPAD_ROWS][KEYPAD_COLS] = {
    {'#', '1', '2', '3', '(', ')', '_', '-', '+', '@'},
    {'*', '4', '5', '6', '/', ':', ';', '\'','"', '\b'},
    {  0, '7', '8', '9', '?', '!', ',', '.', '0', '\n'},
    {  0,   0,   0,   0,   0,   0,   0, ' ',   0,   0},
};

// Tertiary layer (shift = uppercase; held while typing)
const char keymap_shift[KEYPAD_ROWS][KEYPAD_COLS] = {
    {'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P'},
    {'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', '\b'},
    {  0, 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '$', '\n'},
    {  0,   0,   0,   0,   0,   0,   0, ' ',   0,   0},
};

// Modifier positions (HD-V2 physical layout, decoded from raw events)
#define KEY_ALT_ROW     2
#define KEY_ALT_COL     0   /* Z-row left key, silkscreened "Alt" */
#define KEY_SHIFT_L_ROW 3
#define KEY_SHIFT_L_COL 5   /* bottom-row left Shift */
#define KEY_SHIFT_R_ROW 3
#define KEY_SHIFT_R_COL 9   /* bottom-row right Shift */
#define KEY_SYM_ROW     3
#define KEY_SYM_COL     8

Adafruit_TCA8418 keypad;
keypad_cb keypad_listener = NULL;

/* Software character FIFO (review finding 2.1): keypad_loop drains the whole
 * hardware FIFO into this queue; keypad_get_val pops one char per call, so
 * several presses arriving within one loop pass are delivered in order
 * instead of overwriting a single shared slot. */
#define KBD_CHAR_FIFO_LEN 16
static char kbd_char_fifo[KBD_CHAR_FIFO_LEN];
static int kbd_char_cnt  = 0;
static int kbd_char_head = 0;                   /* next pop position */
static int kbd_char_tail = 0;                   /* next push position */

/* Each modifier is tracked independently and combined on use: releasing one
 * Shift while the other is still held must keep the uppercase layer
 * (review finding 1.3). */
static bool alt_pressed   = false;   /* Alt (2,0): momentary sym layer */
static bool shift_l_press = false;   /* Shift (3,5): uppercase layer */
static bool shift_r_press = false;   /* Shift (3,9): uppercase layer */
static bool sym_lock      = false;   /* Sym (3,8): locked sym layer */

static void keypad_modifiers_recover(void)
{
    /* FIFO overflow dropped unknown press/release events: reset every
     * modifier so a lost release can't stick a layer on (finding 1.4). */
    alt_pressed = false;
    shift_l_press = false;
    shift_r_press = false;
    sym_lock = false;
    Serial.println("[KBD] FIFO overflow - modifiers reset");
}

bool keypad_init(int address)
{
    if(!i2cIsInit(0)){
        Wire.begin(BOARD_KEYBOARD_SDA, BOARD_KEYBOARD_SCL);
        Wire.beginTransmission(address);
        Wire.endTransmission(true);
    }

    if (!keypad.begin(address, &Wire)) {
        // Serial.println("keypad not found, check wiring & pullups!");
        log_e("keypad not found, check wiring & pullups!");
        return false;
    }

    // configure the size of the keypad matrix.
    // all other pins will be inputs
    keypad.matrix(KEYPAD_ROWS, KEYPAD_COLS);

    // flush the internal buffer
    keypad.flush();

    return true;
}

int keypad_get_val(char *c)
{
    if (kbd_char_cnt <= 0) {
        return 0;
    }
    if (c) {
        *c = kbd_char_fifo[kbd_char_head];
    }
    kbd_char_head = (kbd_char_head + 1) % KBD_CHAR_FIFO_LEN;
    kbd_char_cnt--;
    return 1;
}

void keypad_set_flag(void)
{
    /* Legacy API: with the software FIFO, get_val already consumes the char,
     * there is nothing left to clear. */
}

void keypad_loop(void)
{
    /* Drain the whole FIFO each pass: during slow operations (e.g. WiFi
     * scan) several events can pile up and must be consumed in one go
     * (review finding 1.4). */
    while (keypad.available() > 0) {
        int k = keypad.getEvent();
        int state = -1;
        if (k == 0) break;

        if (k >= KEYPAD_RELEASE_VAL_MIN && k <= KEYPAD_RELEASE_VAL_MAX) {
            k = k - KEYPAD_RELEASE_VAL_MIN;
            state = KEYPAD_RELEASE;
        }

        if (k >= KEYPAD_PRESS_VAL_MIN && k <= KEYPAD_PRESS_VAL_MAX) {
            k = k - KEYPAD_PRESS_VAL_MIN;
            state = KEYPAD_PRESS;
        }

        if (state < 0) continue;

        int row = k / KEYPAD_COLS;
        int col = (KEYPAD_COLS - 1) - k % KEYPAD_COLS;
        bool pressed = (state == KEYPAD_PRESS);

        if (row == KEY_SYM_ROW && col == KEY_SYM_COL) {
            if (pressed) {
                sym_lock = !sym_lock;
                Serial.printf("[KBD] sym_lock=%d\n", sym_lock);
            }
            continue;
        }

        if (row == KEY_ALT_ROW && col == KEY_ALT_COL) {
            alt_pressed = pressed;
            Serial.printf("[KBD] alt=%d\n", alt_pressed);
            continue;
        }

        if (row == KEY_SHIFT_L_ROW && col == KEY_SHIFT_L_COL) {
            shift_l_press = pressed;
            Serial.printf("[KBD] shift(l)=%d\n", shift_l_press);
            continue;
        }

        if (row == KEY_SHIFT_R_ROW && col == KEY_SHIFT_R_COL) {
            shift_r_press = pressed;
            Serial.printf("[KBD] shift(r)=%d\n", shift_r_press);
            continue;
        }

        if (!pressed) continue;

        char c = 0;
        const bool shift_active = shift_l_press || shift_r_press;
        if (sym_lock || alt_pressed) {
            c = keymap_sym[row][col];
        } else if (shift_active) {
            c = keymap_shift[row][col];
        } else {
            c = keymap[row][col];
        }

        if (c == 0) continue;

        /* Alt+Enter → '\t': app-level scan shortcut. Alt is momentary, so a
         * plain Enter (no Alt held) still emits '\n'. */
        if (c == '\n' && alt_pressed) c = '\t';

        Serial.printf("[KBD] row=%d col=%d char='%c' (0x%02x)\n",
                      row, col, c >= 0x20 ? c : '?', c);

        if (kbd_char_cnt >= KBD_CHAR_FIFO_LEN) {
            Serial.println("[KBD] char fifo full - drop");
            continue;
        }
        kbd_char_fifo[kbd_char_tail] = c;
        kbd_char_tail = (kbd_char_tail + 1) % KBD_CHAR_FIFO_LEN;
        kbd_char_cnt++;
    }

    /* INT_STAT bits are write-1-to-clear: OVR_FLOW_INT means press/release
     * events were dropped while the FIFO was full (review finding 1.4/2.2).
     * Without the W1C write the bit stays set forever and every loop pass
     * would reset the modifiers again. */
    uint8_t int_stat = keypad.readRegister(TCA8418_REG_INT_STAT);
    if (int_stat & TCA8418_REG_STAT_OVR_FLOW_INT) {
        keypad_modifiers_recover();
        keypad.writeRegister(TCA8418_REG_INT_STAT,
                             TCA8418_REG_STAT_OVR_FLOW_INT);   /* W1C */
    }
}

void keypad_regetser_cb(keypad_cb cb)
{
    keypad_listener = cb;
}
