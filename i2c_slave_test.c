/*
The ports in use are:
RC3-RC4 I2C CSL and SDA lines
RC5-RC7 not used
RD0-RD7 Indicator LEDS power, step, microstep
RE0-RE2 Not used

Hardware units in use :
USART - I2C

Revisions :
Date    Change

To Do:


Notes:
*/                          
//#defines 
#define DEBUG
#include <stddef.h>
#include "..\i2c_slave_test\i2c_slave_test.h"
#include <errno.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#use delay (internal=8M, restart_WDT)
#use standard_io(C)
#ZERO_RAM


/*
scenarios to handle are : 
ST ADDR/W CMD STP                   ... Write 1 byte cmd
ST ADDR/W CMD DATA0...N STP         ... Write one or more bytes, 1 byte cmd, rest as arguments 
ST ADDR/W CMD <DATA0..N> RST ADDR/R DATA STP  ... write cmd, read result after restart
ST ADDR/R DATA0..N STP              ... Read or re-read results for existing cmd 
*/
#int_SSP // interrupt on i2c activity 
void i2c_isr(void)
{
//http://www.ccsinfo.com/forum/viewtopic.php?t=34335&postdays=0&postorder=asc&start=15&sid=a8c963db192abaa64c2d07c709d466a4
   static BYTE incoming, state, byteCount;
   state = SSPSTAT;
   
#if defined __DEBUG__
   //Keep a running record of I2C states for debugging on halt.
   if( stateCount< STATEMAPSIZE ) 
   {
       i2cState[stateCount] = state;
       stateCount++;
   }    
   else
   {
       stateCount = 0;
       i2cState[stateCount] = state;
   }      
   Debug leds output_D( state );   
#endif

   switch (state)
   {
   // i2c read operation, last byte was address ( S & RWL )
   //Note that we might not get this state in the case of ST ADDR WR RST RD STP sequence
   //We are being read, hence Buffer is empty and needs filling 
   /* Two cases to handle
   - where a cmd has been written and data has already been prepared or 
   is being prepared as a response for reading back. NULL_CMD <= CMD <= LAST_CMD
   - where no cmd has been issued and we have just a straight read. 
   In either case, wait for main loop to handle
   */
   case 0B00001100: // /DAL | S | RWL | /BF
         serialOutCount = 0;
         if (  (serialBuffOutLength > 0) &&
               (interruptStatus & I2C_READ_READY)  )
         {
            SSPBUF = serialBuffOut[serialOutCount++];  // data write
         }
         else
         {
            interruptStatus |= I2C_READ_READY;
            //stretch clock 
            SSPCON.CKP = 0;

            //Handle WR RST RD condition where RST hasn't given us time or let us know the write data is complete
            //to execute cmd and return any data in read
            if( serialInCount > 0 ) //we still have (command and/or data) bytes in the in buffer to examine, flag main and wait
            {               
               interruptStatus |= I2C_CMD_READY;
               //Still have risk that cmd produces no response data.... In which case we are in error.
            }
            else //we need to prepare a response without a synchronous command
            {
               //What is being read if there is no current command? Assume re-use of last one.
               //Flag main loop to prepare the data.
               interruptStatus |= I2C_READ_READY;
            }
         }
         
      break;
   /* i2c read operation, last byte read was data
   
   */
   case 0B00101100:  // DAL | S | RWL | /BF
      //If data is flagged as being ready to go.. send it
      if ( interruptStatus & I2C_READ_READY_MASK )
      {          
          if ( serialBuffOutLength > 0 && 
               serialOutCount < serialBuffOutLength && 
               serialOutCount < SERIAL_BUFF_SIZE )
            {
                SSPBUF = serialBuffOut[serialOutCount++];  // data write
            }
            else 
            {                
            //Error condition - have ready flag but are being asked for
            //more data than expected or data when not expected.
                SSPBUF = 0xFE; //dummy data - NULL
            }
            SSPCON.CKP = 1; // release the clock         
      }
      //clean up if there is a stop signal - potential error if RST with no stop - flag not cleared. 
      //Wont get this in this 'case' statement.
      if ( SSPSTAT.P )
      {
         interruptStatus &= !I2C_READ_READY_MASK;     
         serialOutcount = 0;
         serialBuffoutLength = 0;
         SSPCON.CKP = 1; // release the clock                  
      }         
      break;

   case 0B00110100:  // DAL | P | RWL | /BF NACK error condition - stop and cleanup.
      //Validate case.
      //clean up if there is a stop signal potential error if RST with no stop - flag not cleared. 
      //reset interrupt flags
      interruptStatus &= !(I2C_READ_READY_MASK | I2C_CMD_READY_MASK);
      //reset counters
      serialOutcount = 0;
      serialBuffoutLength = 0;
      SSPCON.CKP = 1; // release the clock         
      break;
   
   //----------------------------------------------Writing
   case 0B00001001: // /DAL | S | BF i2c write operation, last byte was device address
      incoming = SSPBUF;   //dummy read to cleardown for next byte
      SSPCON.SSPOV = 0;
      serialInCount = 0;          
      for ( byteCount=0; byteCount< SERIAL_BUFF_SIZE; byteCount++)
          serialBuffIn[byteCount]=0;
   break;

   //case 0B00100001:  // DAL | BF (S no longer asserted ) - not actually seen in hardware
   case 0B00101001:  // DAL | S | BF (S still asserted ) i2c write operation, last byte was data
      incoming = SSPBUF;   //data read
      if ( serialInCount < SERIAL_BUFF_SIZE )
      {
         //must be more data - add to buffer
         serialBuffIn[serialInCount] = incoming;
         serialInCount++;
      }
      break;
   
   case 0B00110001:  // DAL | P | BF ... last byte was write data and finish with STOP
      incoming = SSPBUF;   //data read
      if ( serialInCount < SERIAL_BUFF_SIZE )
      {
         serialBuffIn[serialInCount] = incoming;//must be more data - add to buffer
         serialInCount++;
      }
      if( SSPSTAT.P ) //Need to check this is a valid state here
      {
         //Write finished.
         //Flag for main loop to handle.
         interruptStatus |= I2C_CMD_READY;
      } 
      break;
   default:
         SSPSTAT.BF = 0;
         SSPCON.SSPOV = 0;
         SSPCON.CKP = 1; // release the clock         
   break;
   }
}

void setup_i2c()
{
   int i;
   for (i=0; i < SERIAL_BUFF_SIZE;i++)
   {
      serialBuffIn[i]  = 0x00;
      serialBuffOut[i] = 0x00;
   }
   serialInCount = 0;
   serialOutCount =  0;
   serialBuffOutLength = 0;
   SSPSTAT.SMP=0;
   
   /* Set up SSP module for 7-bit */
   PIC_SSPCON1 = 0b00110101;
   PIC_SSPADD = NODE_ADDR;  /* Set the slave's address */
   PIC_SSPSTAT = 0x00;     /* Clear the SSPSTAT register. */
   lastCmd = 0;
}

void init_config()
{
   //Initialise hardware config
   /*
   set_TRIS_a(0x00); //All outputs
   port_b_pullups(TRUE);
   set_TRIS_B(0b11110000); //RB6-4 inputs
   set_tris_d( 0x00 );//All outputs
   output_d( 0x00 );
   set_tris_e( 0xFF );//All inputs
   */
   SET_TRIS_C(0b00011000); //Need PWM pin (c2) set out & SCL/SDA pins (C3/4) set to input.
   
   setup_counters(RTCC_INTERNAL,RTCC_DIV_32);
   setup_i2c();
     
   //Setup and enable interrupts
   enable_interrupts(INT_SSP);
   //Finally the global enable
   enable_interrupts(GLOBAL);
}
void main() 
{ 
    init_config();
   
    while (TRUE) 
    { 
        if ( interruptStatus & I2C_CMD_READY )
        {
            //Handle comands and generate output if there is any
            //Don't set ready flag - may not be asked for it. 

            if ( serialInCount >= 1 )
               serialCmd = serialBuffIn[0];
            else 
               serialCmd = lastCmd;
            
            switch( serialCmd )
            {
               /*
               case SERIAL_CMD_APP_RESET:
                    reset_cpu();
                    break;

                case SERIAL_CMD_NULL_CMD:
                    serialOutCount=0;
                    serialBuffOut[0] = 0xFF;
                    serialBuffOut[1] = 0xFd;
                     serialBuffOutLength = 2;
                    interruptStatus ^= I2C_CMD_READY;
                    break;
                */
                case 0:
                case 1: // ADDR /Wr <cmd> P i.e. no data to be read
                    serialOutCount=0;
                    lastCmd = serialCmd;
                    //Do something involving being commanded - reset debug leds
                    output_d( serialCmd );
                    //Clear flag  - job done.
                    interruptStatus ^= I2C_CMD_READY;
                    break;                              

                case 2: // ADDR /Wr <cmd> <data..N> P
                    serialOutCount=0;
                    lastCmd = serialCmd;
                    
                    //Do something involving writing data - set leds to a value
                    output_d( serialBuffIn[1] );
                    
                    //Clear flag, job done.
                    interruptStatus ^= I2C_CMD_READY;
                    break;                              
                    
                    break;
                case 3:// ADDR /Wr <cmd> <data 0..N> RST ADDR Rd <data 1..N> P i.e. a read but command waiting to be executed.
                    //If the command generates data (like a function call) - put it in the serial out buffer. 
                    //read command
                    //use arguments
                    //Load output.
                    serialOutCount=0;
                    serialBuffOut[0] = 0xFF;
                    serialBuffOut[1] = 0xFE;
                    serialBuffOut[2] = 0xFD;                    
                    serialBuffOutLength = 3;
                    lastCmd = serialCmd;
                    interruptStatus ^= I2C_CMD_READY;
                    break;              
                default: //we don't recognise the cmd received
                    serialBuffOutLength = 0;
                    serialOutCount = 0;
                    //Clear down
                    interruptStatus ^= I2C_CMD_READY;
                    //Force Ack ?
                break;
            }    
        }    
        else if ( interruptStatus & I2C_READ_READY )
        {
            /*
            We have been asked to provide data.
            There may not have been a recent command to tell us which data to send back
            Is also not possible to work out (yet) whether we have been asked for data 
            as part of a write|RST|Rd combo
            This assumes to re-use the last command.
            Could also choose to iterate over all data fields but then would have to respond
            for each data read request on a byte by byte basis
            */
            switch (lastCmd)
            {
               case 0:
               case 1:
                    serialOutCount = 0;
                    serialBuffOut[0] = 0x01;
                    serialBuffOutLength = 1;
                    break;
               case 2:
               case 3:                        
               default:
                    serialOutCount = 0;
                    serialBuffOutLength = 0;
                  break;
            }
            
            interruptStatus &= ! I2C_READ_READY;
            interruptStatus |= I2C_READ_PENDING;
            SSPCON.CKP = 1;
        }
        else if ( interruptStatus & I2C_READ_PENDING )
        {
            //We are in the process of writing out a data response - do the first here 
            // and let ISR to do the rest of the buffer
            if( serialBuffOutLength > 0 ) 
            {
                SSPBUF = serialBuffOut[0];
                serialOutCount++;
            }
            //if there is no data we expect to write we should flag and release 
            interruptStatus ^= I2C_READ_PENDING;
            interruptStatus |= I2C_READ_READY;
            SSPCON.CKP = 1;
        }    
    } 
}


/*
//Tested and working - need to add complexity for command sets.
void  i2c_isr(void) 
{ 
    int state, incoming; 
    state = i2c_isr_state(); 
    output_bit( 70, 1);
    if(state < 0x80) 
    { 
        incoming = i2c_read(); 
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
        i2c_write(wrt_buf[state-0x80]); 
    } 
   output_bit( 70, 0);
}
*/
