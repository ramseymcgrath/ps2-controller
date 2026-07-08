#ifndef DS2_IDS_H
#define DS2_IDS_H

// Controller ID byte (also = current mode)
#define MODE_DIGITAL          0x41
#define MODE_ANALOG           0x73
#define MODE_ANALOG_PRESSURE  0x79
#define MODE_CONFIG           0xF3

// Console command byte (2nd byte of a packet)
#define CMD_PRES_CONFIG        0x40
#define CMD_POLL_CONFIG_STATUS 0x41
#define CMD_POLL               0x42
#define CMD_CONFIG             0x43
#define CMD_ANALOG_SWITCH      0x44
#define CMD_STATUS             0x45
#define CMD_CONST_46           0x46
#define CMD_CONST_47           0x47
#define CMD_CONST_4C           0x4C
#define CMD_ENABLE_RUMBLE      0x4D
#define CMD_POLL_CONFIG        0x4F

// buttons1 (BTNL) masks — active-low
#define PS_SELECT 0x01
#define PS_L3     0x02
#define PS_R3     0x04
#define PS_START  0x08
#define PS_UP     0x10
#define PS_RIGHT  0x20
#define PS_DOWN   0x40
#define PS_LEFT   0x80

// buttons2 (BTNH) masks — active-low
#define PS_L2   0x01
#define PS_R2   0x02
#define PS_L1   0x04
#define PS_R1   0x08
#define PS_TRI  0x10
#define PS_CIR  0x20
#define PS_X    0x40
#define PS_SQU  0x80

#endif // DS2_IDS_H
