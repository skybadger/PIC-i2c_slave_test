#include <16F887.h>
//#include <16F877a.h>
//#include <16C62a.h>
#device ICD=TRUE
#device *=16
//#use delay(clock=19660800)
#use delay(clock=4000000)
#fuses NOWDT,HS, NOPUT, NOPROTECT, BROWNOUT

//#define CLOCK ((int32)19660800)
#define CLOCK ((int32)4000000)
#define STEP_A   PIN_A0
#define STEP_B   PIN_A1
#define STEP_C   PIN_A2
#define STEP_D   PIN_A3
#define ENC_A    PIN_B4          
#define ENC_B    PIN_B5
#define ENC_I    PIN_B6
#define STEP_LED  PIN_D0
#define ENC_LED   PIN_D1

//Setup TRIS in init sequence.
#use fast_io(ALL)
#use standard_io(C)

//Global variables
byte interruptStatus;             
#define  TMR1_TIMEDOUT           0x01
#define  TMR2_TIMEDOUT           0x02
#define  TMR1_TIMEDOUT_MASK      0x01
#define  TMR2_TIMEDOUT_MASK      0x02
#define  I2C_READ_PENDING        0x04
#define  I2C_READ_READY          0x08
#define  I2C_CMD_READY           0x10
#define  I2C_READ_PENDING_MASK   0x04
#define  I2C_READ_READY_MASK     0x08
#define  I2C_CMD_READY_MASK      0x10

//Setup TRIS in init sequence.
#use fast_io(ALL)
#use standard_io(C)

#define NODE_ADDR 0xd8
#use i2c(Slave,fast,sda=PIN_C4,scl=PIN_C3,force_hw,address=NODE_ADDR)
#byte SSPBUF = 0x13
#define SSPSTATMASK 0x3F
#byte PIC_SSPBUF=0x13
#byte PIC_SSPADD=0x93
#byte PIC_SSPSTAT=0x94
#byte PIC_SSPCON1=0x14

/* Bit defines */
#define PIC_SSPSTAT_BIT_SMP     0x80
#define PIC_SSPSTAT_BIT_CKE     0x40
#define PIC_SSPSTAT_BIT_DA      0x20
#define PIC_SSPSTAT_BIT_P       0x10
#define PIC_SSPSTAT_BIT_S       0x08
#define PIC_SSPSTAT_BIT_RW      0x04
#define PIC_SSPSTAT_BIT_UA      0x02
#define PIC_SSPSTAT_BIT_BF      0x01

#define PIC_SSPCON1_BIT_WCOL    0x80
#define PIC_SSPCON1_BIT_SSPOV   0x40
#define PIC_SSPCON1_BIT_SSPEN   0x20
#define PIC_SSPCON1_BIT_CKP     0x10
#define PIC_SSPCON1_BIT_SSPM3   0x08
#define PIC_SSPCON1_BIT_SSPM2   0x04
#define PIC_SSPCON1_BIT_SSPM1   0x02
#define PIC_SSPCON1_BIT_SSPM0   0x01

struct SSPSTATStruct
{
//Buffer Full : (receive) 1 = receive complete SSPBUFF full
//Buffer Full : (transmit) 1 = transmit in progress, SSPBUFF full
      int BF:1;
//Update address , 1 indicates that user needs to update the address in the 10 bit SSPADD field
      int UA:1;
//Write Low, holds R/WL bit of last address match. 1=read
      int RWL:1;
//Start bit, 1 = start detected last.
      int S:1;
//Stop bit, 1=stop detected last
      int P:1;
//D/AL 1 - last byte received was data
      int DAL:1;
//SMBus inputs enable
      int CKE:1;
//Slew rate control bit - 1 disables for 100KHz and 1MHz
      int SMP:1;
};
struct SSPSTATStruct SSPSTAT;
#pragma BYTE SSPSTAT = 0x94 // Place structure right over SSPSTAT at location 0x94

#define SSPCONMASK 0xF0
struct SSPCONTROLStruct
{
//SSP select
      int SSPSEL:4;
//SSP Clock polarity
      int CKP:1;
//Sync Serial port Enable - in I2C mode enables SSP as I2C using SDA and SCL
      int SSPEN:1;
//SSP overflow, 1 when I2C byte received while SSPBUFF full, don't care in transmit.
//Must be cleared by software in either case
      int SSPOV:1;
//Write collision, SSPBUF written while still transmitting previous byte, 1= collision
      int WCOL:1;
};
struct SSPCONTROLStruct SSPCON;
#pragma BYTE SSPCON = 0x14

#define CMD   0
#define DATA0 1
#define DATA1 2
#define DATA2 3  
#define DATA3 4
#define SERIAL_BUFF_SIZE 10

byte serialCmd;         //Used to identify the I2C command
byte serialStatus = 0;
byte serialInCount = 0;   //How many bytes have been processed inwards by the ISR
byte serialOutcount = 0;  //How many bytes have been processed outwards by the ISR
byte serialBuffIn[SERIAL_BUFF_SIZE];
byte serialBuffOut[SERIAL_BUFF_SIZE];
byte serialBuffOutLength = 0;
#if defined __DEBUG__
#define STATEMAPSIZE 10
BYTE i2cState[STATEMAPSIZE] = {0,};  
BYTE stateCount =0;          
#endif 

BYTE lastCmd = 0;

