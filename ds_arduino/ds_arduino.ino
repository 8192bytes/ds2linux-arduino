#include "PS2X_lib.h"  //for v1.6

PS2X ps2x; // create PS2 Controller Class

#define PS2_DAT     13
#define PS2_CMD     11
#define PS2_SEL     10
#define PS2_CLK     12

unsigned char rumble_val[2] = { 0 };
unsigned char PS2data[21] = { 0 };

void setup(){
    Serial.begin(57600);
    ps2x.config_gamepad(PS2_CLK, PS2_CMD, PS2_SEL, PS2_DAT, true, true);
}

void loop(){
    ps2x.read_gamepad(rumble_val[1], rumble_val[0]);
    for(int i = 0; i < 21; i++){
        PS2data[i] = ps2x.Analog(i);
    }
    Serial.write(ps2x.PS2data, 21);
    
    if (Serial.available() > 0){
        Serial.readBytes(rumble_val, 2);
    } else rumble_val[0] = rumble_val[1] = 0;

    delay(10);
}
