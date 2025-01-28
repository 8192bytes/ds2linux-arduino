#include <stdio.h>
#include <errno.h>
#include <fcntl.h> 
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <linux/uinput.h>
#include <stdbool.h>

int serial_init(int fd, int speed, int parity){
    struct termios tty;
    if (tcgetattr (fd, &tty) != 0){
        perror ("error from tcgetattr");
        return -1;
    }

    cfsetospeed (&tty, speed);
    cfsetispeed (&tty, speed);

    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;     // 8-bit chars
    tty.c_iflag &= ~IGNBRK;         // disable break processing
    tty.c_lflag = 0;                // no signaling chars, no echo,
                                    // no canonical processing
    tty.c_oflag = 0;                // no remapping, no delays
    tty.c_cc[VMIN]  = 0;            // read doesn't block
    tty.c_cc[VTIME] = 5;            // 0.5 seconds read timeout

    tty.c_iflag &= ~(IXON | IXOFF | IXANY); // shut off xon/xoff ctrl

    tty.c_cflag |= (CLOCAL | CREAD);// ignore modem controls,
                                    // enable reading
    tty.c_cflag &= ~(PARENB | PARODD);      // shut off parity
    tty.c_cflag |= parity;
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CRTSCTS;

    if (tcsetattr (fd, TCSANOW, &tty) != 0){
        perror ("error from tcsetattr");
        return -1;
    }
    return 0;
}

static void setup_abs(int fd, unsigned chan, int min, int max) {
    if (ioctl(fd, UI_SET_ABSBIT, chan))
        perror("UI_SET_ABSBIT");
    
    struct uinput_abs_setup s = {
        .code = chan,
        .absinfo = { .minimum = min,  .maximum = max },
    };

    if (ioctl(fd, UI_ABS_SETUP, &s))
        perror("UI_ABS_SETUP");
}

#define CHECK_BIT(var,pos) (var & (1<<pos))

// We need more absolutes so we are using undefined (max 63)
enum {
    ABS_NORTH = 11,
    ABS_SOUTH,
    ABS_EAST,
    ABS_WEST,
    ABS_DPAD_UP,
    ABS_DPAD_DOWN = 29,
    ABS_DPAD_LEFT,
    ABS_DPAD_RIGHT,
    ABS_TR = 33,
    ABS_TL,
    ABS_TR2,
    ABS_TL2
};

int main(int argc, char **argv){
    bool digital=1, analog=1;

    // Parse options
    if(argc > 1){
        if (strncmp(argv[1], "-h", 3) == 0){
            printf("Options:\n\t-h\tDisplay help\n\t-a\tDisable analog input\n\t-d\tDisable digital input\n");
            return 0;
        }
        else if(strncmp(argv[1], "-a", 3) == 0) analog  = 0;
        else if(strncmp(argv[1], "-d", 3) == 0) digital = 0;
    }


    // Serial init
    const char* serial_port_name[] = {"/dev/ttyUSB0", "/dev/ttyACM0"};
    int serial;
    for(int i=0; i<sizeof(serial_port_name) / sizeof(char *); i++){
        serial = open (serial_port_name[i], O_RDWR | O_NOCTTY | O_SYNC);
        if(serial >= 0) break;
    }
    if (serial < 0) {
        perror ("error on opening serial");
        return 1;
    }
    serial_init(serial, B57600, 0);
    

    // uinput init
    const char* uinput_name[] = {"/dev/uinput", "/dev/misc/uinput", "/dev/input/uinput"};
    
    int fd;
    for(int i=0; i < sizeof(uinput_name) / sizeof(char *); i++){
        fd  = open(uinput_name[i], O_RDWR | O_NONBLOCK);
        if(fd >= 0) break;
    }

    if(fd<0){
        perror("/dev/uinput open error");
        return 1;
    }


    // Buttons
    const int buttons[] = {
        BTN_SELECT, BTN_THUMBL, BTN_THUMBR, BTN_START,                  // Menu Pad and Joystick buttons
        BTN_DPAD_UP, BTN_DPAD_RIGHT, BTN_DPAD_DOWN, BTN_DPAD_LEFT,      // Dpad
        BTN_TL2, BTN_TR2, BTN_TL, BTN_TR,                               // Front Triggers
        BTN_WEST, BTN_EAST, BTN_SOUTH, BTN_NORTH                        // Action Pad
    };

    ioctl(fd, UI_SET_EVBIT, EV_KEY);
    for(int i=0; i<sizeof(buttons) / sizeof(int); i++){
        ioctl(fd, UI_SET_KEYBIT, buttons[i]);
    }


    // Absolutes
    const int absolutes[] = {
        ABS_RX, ABS_RY, ABS_X, ABS_Y,                                   // Joysticks
        ABS_DPAD_RIGHT, ABS_DPAD_LEFT, ABS_DPAD_UP, ABS_DPAD_DOWN,      // Analog Dpad
        ABS_NORTH, ABS_EAST, ABS_SOUTH, ABS_WEST,                       // Analog action pad
        ABS_TL, ABS_TR, ABS_TL2, ABS_TR2                                // Analog front triggers
    };
    
    ioctl(fd, UI_SET_EVBIT, EV_ABS);
    for(int i=0; i<sizeof(buttons) / sizeof(int); i++){
        setup_abs(fd, absolutes[i], 0, 255);
    }
    

    // Force Feedback
    ioctl(fd, UI_SET_FFBIT, FF_RUMBLE);
    ioctl(fd, UI_SET_EVBIT, EV_FF);

    
    //uinput setup
    struct uinput_setup setup = {
        .name = "DualShock 2",
        .id = {.bustype = BUS_VIRTUAL, .vendor = 0x1234, .product = 0x5678},
        .ff_effects_max = 16
    };

    if(ioctl(fd, UI_DEV_SETUP, &setup)){
        perror("UI_DEV_SETUP");
        return 1;
    }

    if(ioctl(fd, UI_DEV_CREATE)){
        perror("UI_DEV_CREATE");
        return 1;
    }

    // EVENT LOOP
    struct input_event fevent;
    struct uinput_ff_upload upload;
    struct uinput_ff_erase erase;
    struct input_event ev[35];
    unsigned char PS2data[21];
    unsigned char rumble_val[2] = { 0 };
    int i = 0, j = 0;

    while(1){
        // Read controller state from serial, check if it configured correctly
        read(serial, PS2data, 21);
        if(PS2data[1] != 0x79){
            perror("Controller Error. Retrying...");
            sleep(1);
            continue;
        }
        
        // Prepare events
        memset(&ev, 0, sizeof(ev));
        for(i=0, j=0; j < 4; i++, j++){ // Menu Pad and Joystick buttons; always enabled
            ev[i].type = EV_KEY;
            ev[i].code = buttons[i];
            ev[i].value = !(bool)CHECK_BIT(PS2data[3], j);
        }
        
        if(digital){                        
            for(; j < 8; i++,j++){      // Dpad
                ev[i].type = EV_KEY;
                ev[i].code = buttons[i];
                ev[i].value = !(bool)CHECK_BIT(PS2data[3], j);
            }
            
            for(j=0; j < 8; i++, j++){  // Front triggers & Action pad
                ev[i].type = EV_KEY;
                ev[i].code = buttons[i];
                ev[i].value = !(bool)CHECK_BIT(PS2data[4], j);
            }
        }
        
        // ABS VALUES
        for(j=0; j < 4; i++, j++){      // Joysticks; always enabled
            ev[i].type = EV_ABS;
            ev[i].code = absolutes[j];
            ev[i].value = PS2data[j+5];
        }
        
        if(analog){
            for(; j < 16; i++, j++){    // Analog dpad, action pad, front triggers
                ev[i].type = EV_ABS;
                ev[i].code = absolutes[j];
                ev[i].value = PS2data[j+5];
            }
        }

        ev[i].type = EV_SYN;           // report
        ev[i].code = SYN_REPORT;
        ev[i].value = 0;

        // write all events to uinput
        if(write(fd, &ev, sizeof(ev)) < 0){
            perror("write");
            return 1;
        }


        // Force Feedback
        if (read(fd, &fevent, sizeof(fevent)) == sizeof(fevent)){
            if (fevent.type == EV_UINPUT){
                if(fevent.code == UI_FF_UPLOAD){
                    memset(&upload, 0, sizeof(upload));
                    upload.request_id = fevent.value;
                    if(ioctl(fd, UI_BEGIN_FF_UPLOAD, &upload) < 0){
                        perror("UI_BEGIN_FF_UPLOAD");
                    }
                    
                    if(upload.effect.type == 0x50){
                        rumble_val[0] = upload.effect.u.rumble.weak_magnitude/257;
                        rumble_val[1] = upload.effect.u.rumble.strong_magnitude/257;
                        upload.retval = 0;
                    }

                    if (ioctl(fd, UI_END_FF_UPLOAD, &upload) < 0){
                        perror("UI_END_FF_UPLOAD");
                    }
                }
                else if (fevent.code == UI_FF_ERASE){
                    memset(&erase, 0, sizeof(erase));
                    erase.request_id = fevent.value;
                    if(ioctl(fd, UI_BEGIN_FF_ERASE, &erase) < 0){
                        perror("UI_BEGIN_FF_ERASE");
                    }
                    erase.retval = 0;
                    if(ioctl(fd, UI_END_FF_ERASE, &erase) < 0){
                        perror("UI_END_FF_ERASE");
                    }
                    rumble_val[0] = rumble_val[1] = 0;
                }
            }
        }
        write(serial, &rumble_val, sizeof(rumble_val));
        sleep(0.1);
    }
}
