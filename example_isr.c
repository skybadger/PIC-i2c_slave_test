#include <16F877A.h> 
#device adc=10 

#FUSES NOWDT                     //No Watch Dog Timer 
#FUSES HS                        //High speed Osc (> 4mhz) 
#FUSES PUT                       //Power Up Timer 
#FUSES PROTECT                   //Code protected from reads 
#FUSES NODEBUG                   //No Debug mode for ICD 
#FUSES BROWNOUT                  //Brownout reset 
#FUSES NOLVP                     //No low voltage prgming, B3(PIC16) or B5(PIC18) used for I/O 
#FUSES NOCPD                     //No EE protection 
#FUSES NOWRT                     //Program memory not write protected 

#use delay(clock=20M) 

//NOTE: Must declare MASTER before SLAVE, i2c_isr_state() returns 0 
//      when MASTER is the most recent #use i2c 
#use i2c(MASTER, sda=PIN_C1, scl=PIN_C0, stream=I2CM) 
#use i2c(SLAVE, sda=PIN_C4, scl=PIN_C3, address=0x60, force_hw, stream=I2CS) 
#use rs232(baud=57600, parity=N, xmit=PIN_C6, rcv=PIN_C7, bits=8, stream=COM1) 

#use fast_io(D) 

int rcv_buf[0x10];        
int wrt_buf[0x10];        
int cmd=0xFF;            

#int_SSP // interrupt on i2c activity 
void  i2c_isr(void) 
{ 
    int state, incoming; 
    state = i2c_isr_state(); 
    
    if(state < 0x80) 
    { 
        incoming = i2c_read(I2CS); 
        if (state == 1) 
        { 
            cmd = incoming; 
        } 
        
        else if (state > 1) 
        { 
            rcv_buf[state-2]=incoming; 
        } 
    } 
    else 
    { 
        i2c_write(I2CS,wrt_buf[state-0x80]); 
    } 
} 

void main() 
{ 
    
    enable_interrupts(INT_SSP); 
    enable_interrupts(GLOBAL); 
    
    printf("\n\rbegin"); 
    
    while (TRUE) 
    { 
        if (cmd<0xFF) 
            printf("\n\rcmd: %x", cmd); 
        i2c_start(I2CM); 
        i2c_write(I2CM,0xa0);      //i2c address of a slave device 
        i2c_write(I2CM,0x2e);      //1st byte to slave 
        i2c_write(I2CM,0xa0);      //2nd byte to slave 
        i2c_stop(I2CM); 
    } 
} 
