
#include <Adafruit_TCA8418.h>
#define BOARD_I2C_ADDR_KEYBOARD   0x34 
#define KEYPAD_SDA          13
#define KEYPAD_SCL          14
#define KEYPAD_IRQ          15

//
#define KEYPAD_ROWS 4
#define KEYPAD_COLS 10

Adafruit_TCA8418 keypad;

void setup(void)
{
    Serial.begin(115200);
    while (!Serial) {
        delay(10);
    }
    Serial.println(__FILE__);

    Wire.begin(KEYPAD_SDA, KEYPAD_SCL);

    if (!keypad.begin(BOARD_I2C_ADDR_KEYBOARD, &Wire)) {
        Serial.println("keypad not found, check wiring & pullups!");
        while (1);
    }
    // configure the size of the keypad matrix.
    // all other pins will be inputs
    keypad.matrix(KEYPAD_ROWS, KEYPAD_COLS);

    // flush the internal buffer
    keypad.flush();
}

void loop(void)
{
    if (keypad.available() > 0)
    {
        //  datasheet page 15 - Table 1
        int k = keypad.getEvent();
        if (k & 0x80) Serial.print("PRESS\tR: ");
        else Serial.print("RELEASE\tR: ");
        k &= 0x7F;
        k--;
        // NOTE: these are RAW matrix coordinates. The pda2 driver
        // (peri_keypad.cpp) mirrors the columns: driver_col = 9 - raw_col.
        // To compare with the pda2 keymap, convert the printed C first.
        // e.g. raw (R2 C9) = Alt = driver (2,0).
        Serial.print(k / 10);
        Serial.print("\tC: ");
        Serial.print(k % 10);
        Serial.println();
    }
}
