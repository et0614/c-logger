/**
 * Generated Driver File
 * 
 * @file pins.c
 * 
 * @ingroup  pinsdriver
 * 
 * @brief This is generated driver implementation for pins. 
 *        This file provides implementations for pin APIs for all pins selected in the GUI.
 *
 * @version Driver Version 1.1.0
*/

/*
? [2026] Microchip Technology Inc. and its subsidiaries.

    Subject to your compliance with these terms, you may use Microchip 
    software and any derivatives exclusively with Microchip products. 
    You are responsible for complying with 3rd party license terms  
    applicable to your use of 3rd party software (including open source  
    software) that may accompany Microchip software. SOFTWARE IS ?AS IS.? 
    NO WARRANTIES, WHETHER EXPRESS, IMPLIED OR STATUTORY, APPLY TO THIS 
    SOFTWARE, INCLUDING ANY IMPLIED WARRANTIES OF NON-INFRINGEMENT,  
    MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE. IN NO EVENT 
    WILL MICROCHIP BE LIABLE FOR ANY INDIRECT, SPECIAL, PUNITIVE, 
    INCIDENTAL OR CONSEQUENTIAL LOSS, DAMAGE, COST OR EXPENSE OF ANY 
    KIND WHATSOEVER RELATED TO THE SOFTWARE, HOWEVER CAUSED, EVEN IF 
    MICROCHIP HAS BEEN ADVISED OF THE POSSIBILITY OR THE DAMAGES ARE 
    FORESEEABLE. TO THE FULLEST EXTENT ALLOWED BY LAW, MICROCHIP?S 
    TOTAL LIABILITY ON ALL CLAIMS RELATED TO THE SOFTWARE WILL NOT 
    EXCEED AMOUNT OF FEES, IF ANY, YOU PAID DIRECTLY TO MICROCHIP FOR 
    THIS SOFTWARE.
*/

#include "../pins.h"

static void (*MISO_InterruptHandler)(void);
static void (*MOSI_InterruptHandler)(void);
static void (*SCK_InterruptHandler)(void);
static void (*VBAT_InterruptHandler)(void);
static void (*SCL_InterruptHandler)(void);
static void (*SDA_InterruptHandler)(void);
static void (*IO_PA0_InterruptHandler)(void);
static void (*IO_PA7_InterruptHandler)(void);
static void (*IO_PC3_InterruptHandler)(void);
static void (*IO_PD0_InterruptHandler)(void);
static void (*IO_PD1_InterruptHandler)(void);
static void (*IO_PD2_InterruptHandler)(void);
static void (*IO_PD3_InterruptHandler)(void);
static void (*IO_PD4_InterruptHandler)(void);
static void (*RST_InterruptHandler)(void);
static void (*IO_PD7_InterruptHandler)(void);
static void (*IO_PF0_InterruptHandler)(void);
static void (*IO_PF1_InterruptHandler)(void);
static void (*UDTC_InterruptHandler)(void);
static void (*IO_PF3_InterruptHandler)(void);
static void (*IO_PF4_InterruptHandler)(void);
static void (*IO_PF6_InterruptHandler)(void);
static void (*IO_PF7_InterruptHandler)(void);
static void (*FLASH_CS_InterruptHandler)(void);
static void (*LED_InterruptHandler)(void);

void PIN_MANAGER_Initialize()
{

  /* OUT Registers Initialization */
    PORTA.OUT = 0x2;
    PORTC.OUT = 0x0;
    PORTD.OUT = 0x0;
    PORTF.OUT = 0x0;

  /* DIR Registers Initialization */
    PORTA.DIR = 0x52;
    PORTC.DIR = 0x0;
    PORTD.DIR = 0x20;
    PORTF.DIR = 0x0;

  /* PINxCTRL registers Initialization */
    PORTA.PIN0CTRL = 0x4;
    PORTA.PIN1CTRL = 0x8;
    PORTA.PIN2CTRL = 0x0;
    PORTA.PIN3CTRL = 0x0;
    PORTA.PIN4CTRL = 0x0;
    PORTA.PIN5CTRL = 0x0;
    PORTA.PIN6CTRL = 0x0;
    PORTA.PIN7CTRL = 0x4;
    PORTC.PIN0CTRL = 0x0;
    PORTC.PIN1CTRL = 0x0;
    PORTC.PIN2CTRL = 0x0;
    PORTC.PIN3CTRL = 0x4;
    PORTC.PIN4CTRL = 0x0;
    PORTC.PIN5CTRL = 0x0;
    PORTC.PIN6CTRL = 0x0;
    PORTC.PIN7CTRL = 0x0;
    PORTD.PIN0CTRL = 0x4;
    PORTD.PIN1CTRL = 0x4;
    PORTD.PIN2CTRL = 0x4;
    PORTD.PIN3CTRL = 0x4;
    PORTD.PIN4CTRL = 0x4;
    PORTD.PIN5CTRL = 0x0;
    PORTD.PIN6CTRL = 0x9;
    PORTD.PIN7CTRL = 0x4;
    PORTF.PIN0CTRL = 0x4;
    PORTF.PIN1CTRL = 0x4;
    PORTF.PIN2CTRL = 0x1;
    PORTF.PIN3CTRL = 0x4;
    PORTF.PIN4CTRL = 0x4;
    PORTF.PIN5CTRL = 0x4;
    PORTF.PIN6CTRL = 0x4;
    PORTF.PIN7CTRL = 0x4;

  /* PORTMUX Initialization */
    PORTMUX.CCLROUTEA = 0x0;
    PORTMUX.EVSYSROUTEA = 0x0;
    PORTMUX.SPIROUTEA = 0x0;
    PORTMUX.TCAROUTEA = 0x0;
    PORTMUX.TCBROUTEA = 0x0;
    PORTMUX.TWIROUTEA = 0x0;
    PORTMUX.USARTROUTEA = 0x0;

  // register default ISC callback functions at runtime; use these methods to register a custom function
    MISO_SetInterruptHandler(MISO_DefaultInterruptHandler);
    MOSI_SetInterruptHandler(MOSI_DefaultInterruptHandler);
    SCK_SetInterruptHandler(SCK_DefaultInterruptHandler);
    VBAT_SetInterruptHandler(VBAT_DefaultInterruptHandler);
    SCL_SetInterruptHandler(SCL_DefaultInterruptHandler);
    SDA_SetInterruptHandler(SDA_DefaultInterruptHandler);
    IO_PA0_SetInterruptHandler(IO_PA0_DefaultInterruptHandler);
    IO_PA7_SetInterruptHandler(IO_PA7_DefaultInterruptHandler);
    IO_PC3_SetInterruptHandler(IO_PC3_DefaultInterruptHandler);
    IO_PD0_SetInterruptHandler(IO_PD0_DefaultInterruptHandler);
    IO_PD1_SetInterruptHandler(IO_PD1_DefaultInterruptHandler);
    IO_PD2_SetInterruptHandler(IO_PD2_DefaultInterruptHandler);
    IO_PD3_SetInterruptHandler(IO_PD3_DefaultInterruptHandler);
    IO_PD4_SetInterruptHandler(IO_PD4_DefaultInterruptHandler);
    RST_SetInterruptHandler(RST_DefaultInterruptHandler);
    IO_PD7_SetInterruptHandler(IO_PD7_DefaultInterruptHandler);
    IO_PF0_SetInterruptHandler(IO_PF0_DefaultInterruptHandler);
    IO_PF1_SetInterruptHandler(IO_PF1_DefaultInterruptHandler);
    UDTC_SetInterruptHandler(UDTC_DefaultInterruptHandler);
    IO_PF3_SetInterruptHandler(IO_PF3_DefaultInterruptHandler);
    IO_PF4_SetInterruptHandler(IO_PF4_DefaultInterruptHandler);
    IO_PF6_SetInterruptHandler(IO_PF6_DefaultInterruptHandler);
    IO_PF7_SetInterruptHandler(IO_PF7_DefaultInterruptHandler);
    FLASH_CS_SetInterruptHandler(FLASH_CS_DefaultInterruptHandler);
    LED_SetInterruptHandler(LED_DefaultInterruptHandler);
}

/**
  Allows selecting an interrupt handler for MISO at application runtime
*/
void MISO_SetInterruptHandler(void (* interruptHandler)(void)) 
{
    MISO_InterruptHandler = interruptHandler;
}

void MISO_DefaultInterruptHandler(void)
{
    // add your MISO interrupt custom code
    // or set custom function using MISO_SetInterruptHandler()
}
/**
  Allows selecting an interrupt handler for MOSI at application runtime
*/
void MOSI_SetInterruptHandler(void (* interruptHandler)(void)) 
{
    MOSI_InterruptHandler = interruptHandler;
}

void MOSI_DefaultInterruptHandler(void)
{
    // add your MOSI interrupt custom code
    // or set custom function using MOSI_SetInterruptHandler()
}
/**
  Allows selecting an interrupt handler for SCK at application runtime
*/
void SCK_SetInterruptHandler(void (* interruptHandler)(void)) 
{
    SCK_InterruptHandler = interruptHandler;
}

void SCK_DefaultInterruptHandler(void)
{
    // add your SCK interrupt custom code
    // or set custom function using SCK_SetInterruptHandler()
}
/**
  Allows selecting an interrupt handler for VBAT at application runtime
*/
void VBAT_SetInterruptHandler(void (* interruptHandler)(void)) 
{
    VBAT_InterruptHandler = interruptHandler;
}

void VBAT_DefaultInterruptHandler(void)
{
    // add your VBAT interrupt custom code
    // or set custom function using VBAT_SetInterruptHandler()
}
/**
  Allows selecting an interrupt handler for SCL at application runtime
*/
void SCL_SetInterruptHandler(void (* interruptHandler)(void)) 
{
    SCL_InterruptHandler = interruptHandler;
}

void SCL_DefaultInterruptHandler(void)
{
    // add your SCL interrupt custom code
    // or set custom function using SCL_SetInterruptHandler()
}
/**
  Allows selecting an interrupt handler for SDA at application runtime
*/
void SDA_SetInterruptHandler(void (* interruptHandler)(void)) 
{
    SDA_InterruptHandler = interruptHandler;
}

void SDA_DefaultInterruptHandler(void)
{
    // add your SDA interrupt custom code
    // or set custom function using SDA_SetInterruptHandler()
}
/**
  Allows selecting an interrupt handler for IO_PA0 at application runtime
*/
void IO_PA0_SetInterruptHandler(void (* interruptHandler)(void)) 
{
    IO_PA0_InterruptHandler = interruptHandler;
}

void IO_PA0_DefaultInterruptHandler(void)
{
    // add your IO_PA0 interrupt custom code
    // or set custom function using IO_PA0_SetInterruptHandler()
}
/**
  Allows selecting an interrupt handler for IO_PA7 at application runtime
*/
void IO_PA7_SetInterruptHandler(void (* interruptHandler)(void)) 
{
    IO_PA7_InterruptHandler = interruptHandler;
}

void IO_PA7_DefaultInterruptHandler(void)
{
    // add your IO_PA7 interrupt custom code
    // or set custom function using IO_PA7_SetInterruptHandler()
}
/**
  Allows selecting an interrupt handler for IO_PC3 at application runtime
*/
void IO_PC3_SetInterruptHandler(void (* interruptHandler)(void)) 
{
    IO_PC3_InterruptHandler = interruptHandler;
}

void IO_PC3_DefaultInterruptHandler(void)
{
    // add your IO_PC3 interrupt custom code
    // or set custom function using IO_PC3_SetInterruptHandler()
}
/**
  Allows selecting an interrupt handler for IO_PD0 at application runtime
*/
void IO_PD0_SetInterruptHandler(void (* interruptHandler)(void)) 
{
    IO_PD0_InterruptHandler = interruptHandler;
}

void IO_PD0_DefaultInterruptHandler(void)
{
    // add your IO_PD0 interrupt custom code
    // or set custom function using IO_PD0_SetInterruptHandler()
}
/**
  Allows selecting an interrupt handler for IO_PD1 at application runtime
*/
void IO_PD1_SetInterruptHandler(void (* interruptHandler)(void)) 
{
    IO_PD1_InterruptHandler = interruptHandler;
}

void IO_PD1_DefaultInterruptHandler(void)
{
    // add your IO_PD1 interrupt custom code
    // or set custom function using IO_PD1_SetInterruptHandler()
}
/**
  Allows selecting an interrupt handler for IO_PD2 at application runtime
*/
void IO_PD2_SetInterruptHandler(void (* interruptHandler)(void)) 
{
    IO_PD2_InterruptHandler = interruptHandler;
}

void IO_PD2_DefaultInterruptHandler(void)
{
    // add your IO_PD2 interrupt custom code
    // or set custom function using IO_PD2_SetInterruptHandler()
}
/**
  Allows selecting an interrupt handler for IO_PD3 at application runtime
*/
void IO_PD3_SetInterruptHandler(void (* interruptHandler)(void)) 
{
    IO_PD3_InterruptHandler = interruptHandler;
}

void IO_PD3_DefaultInterruptHandler(void)
{
    // add your IO_PD3 interrupt custom code
    // or set custom function using IO_PD3_SetInterruptHandler()
}
/**
  Allows selecting an interrupt handler for IO_PD4 at application runtime
*/
void IO_PD4_SetInterruptHandler(void (* interruptHandler)(void)) 
{
    IO_PD4_InterruptHandler = interruptHandler;
}

void IO_PD4_DefaultInterruptHandler(void)
{
    // add your IO_PD4 interrupt custom code
    // or set custom function using IO_PD4_SetInterruptHandler()
}
/**
  Allows selecting an interrupt handler for RST at application runtime
*/
void RST_SetInterruptHandler(void (* interruptHandler)(void)) 
{
    RST_InterruptHandler = interruptHandler;
}

void RST_DefaultInterruptHandler(void)
{
    // add your RST interrupt custom code
    // or set custom function using RST_SetInterruptHandler()
}
/**
  Allows selecting an interrupt handler for IO_PD7 at application runtime
*/
void IO_PD7_SetInterruptHandler(void (* interruptHandler)(void)) 
{
    IO_PD7_InterruptHandler = interruptHandler;
}

void IO_PD7_DefaultInterruptHandler(void)
{
    // add your IO_PD7 interrupt custom code
    // or set custom function using IO_PD7_SetInterruptHandler()
}
/**
  Allows selecting an interrupt handler for IO_PF0 at application runtime
*/
void IO_PF0_SetInterruptHandler(void (* interruptHandler)(void)) 
{
    IO_PF0_InterruptHandler = interruptHandler;
}

void IO_PF0_DefaultInterruptHandler(void)
{
    // add your IO_PF0 interrupt custom code
    // or set custom function using IO_PF0_SetInterruptHandler()
}
/**
  Allows selecting an interrupt handler for IO_PF1 at application runtime
*/
void IO_PF1_SetInterruptHandler(void (* interruptHandler)(void)) 
{
    IO_PF1_InterruptHandler = interruptHandler;
}

void IO_PF1_DefaultInterruptHandler(void)
{
    // add your IO_PF1 interrupt custom code
    // or set custom function using IO_PF1_SetInterruptHandler()
}
/**
  Allows selecting an interrupt handler for UDTC at application runtime
*/
void UDTC_SetInterruptHandler(void (* interruptHandler)(void)) 
{
    UDTC_InterruptHandler = interruptHandler;
}

void UDTC_DefaultInterruptHandler(void)
{
    // add your UDTC interrupt custom code
    // or set custom function using UDTC_SetInterruptHandler()
}
/**
  Allows selecting an interrupt handler for IO_PF3 at application runtime
*/
void IO_PF3_SetInterruptHandler(void (* interruptHandler)(void)) 
{
    IO_PF3_InterruptHandler = interruptHandler;
}

void IO_PF3_DefaultInterruptHandler(void)
{
    // add your IO_PF3 interrupt custom code
    // or set custom function using IO_PF3_SetInterruptHandler()
}
/**
  Allows selecting an interrupt handler for IO_PF4 at application runtime
*/
void IO_PF4_SetInterruptHandler(void (* interruptHandler)(void)) 
{
    IO_PF4_InterruptHandler = interruptHandler;
}

void IO_PF4_DefaultInterruptHandler(void)
{
    // add your IO_PF4 interrupt custom code
    // or set custom function using IO_PF4_SetInterruptHandler()
}
/**
  Allows selecting an interrupt handler for IO_PF6 at application runtime
*/
void IO_PF6_SetInterruptHandler(void (* interruptHandler)(void)) 
{
    IO_PF6_InterruptHandler = interruptHandler;
}

void IO_PF6_DefaultInterruptHandler(void)
{
    // add your IO_PF6 interrupt custom code
    // or set custom function using IO_PF6_SetInterruptHandler()
}
/**
  Allows selecting an interrupt handler for IO_PF7 at application runtime
*/
void IO_PF7_SetInterruptHandler(void (* interruptHandler)(void)) 
{
    IO_PF7_InterruptHandler = interruptHandler;
}

void IO_PF7_DefaultInterruptHandler(void)
{
    // add your IO_PF7 interrupt custom code
    // or set custom function using IO_PF7_SetInterruptHandler()
}
/**
  Allows selecting an interrupt handler for FLASH_CS at application runtime
*/
void FLASH_CS_SetInterruptHandler(void (* interruptHandler)(void)) 
{
    FLASH_CS_InterruptHandler = interruptHandler;
}

void FLASH_CS_DefaultInterruptHandler(void)
{
    // add your FLASH_CS interrupt custom code
    // or set custom function using FLASH_CS_SetInterruptHandler()
}
/**
  Allows selecting an interrupt handler for LED at application runtime
*/
void LED_SetInterruptHandler(void (* interruptHandler)(void)) 
{
    LED_InterruptHandler = interruptHandler;
}

void LED_DefaultInterruptHandler(void)
{
    // add your LED interrupt custom code
    // or set custom function using LED_SetInterruptHandler()
}
ISR(PORTA_PORT_vect)
{ 
    // Call the interrupt handler for the callback registered at runtime
    if(VPORTA.INTFLAGS & PORT_INT5_bm)
    {
       MISO_InterruptHandler(); 
    }
    if(VPORTA.INTFLAGS & PORT_INT4_bm)
    {
       MOSI_InterruptHandler(); 
    }
    if(VPORTA.INTFLAGS & PORT_INT6_bm)
    {
       SCK_InterruptHandler(); 
    }
    if(VPORTA.INTFLAGS & PORT_INT3_bm)
    {
       SCL_InterruptHandler(); 
    }
    if(VPORTA.INTFLAGS & PORT_INT2_bm)
    {
       SDA_InterruptHandler(); 
    }
    if(VPORTA.INTFLAGS & PORT_INT0_bm)
    {
       IO_PA0_InterruptHandler(); 
    }
    if(VPORTA.INTFLAGS & PORT_INT7_bm)
    {
       IO_PA7_InterruptHandler(); 
    }
    if(VPORTA.INTFLAGS & PORT_INT1_bm)
    {
       FLASH_CS_InterruptHandler(); 
    }
    /* Clear interrupt flags */
    VPORTA.INTFLAGS = 0xff;
}

ISR(PORTC_PORT_vect)
{ 
    // Call the interrupt handler for the callback registered at runtime
    if(VPORTC.INTFLAGS & PORT_INT3_bm)
    {
       IO_PC3_InterruptHandler(); 
    }
    /* Clear interrupt flags */
    VPORTC.INTFLAGS = 0xff;
}

ISR(PORTD_PORT_vect)
{ 
    // Call the interrupt handler for the callback registered at runtime
    if(VPORTD.INTFLAGS & PORT_INT0_bm)
    {
       IO_PD0_InterruptHandler(); 
    }
    if(VPORTD.INTFLAGS & PORT_INT1_bm)
    {
       IO_PD1_InterruptHandler(); 
    }
    if(VPORTD.INTFLAGS & PORT_INT2_bm)
    {
       IO_PD2_InterruptHandler(); 
    }
    if(VPORTD.INTFLAGS & PORT_INT3_bm)
    {
       IO_PD3_InterruptHandler(); 
    }
    if(VPORTD.INTFLAGS & PORT_INT4_bm)
    {
       IO_PD4_InterruptHandler(); 
    }
    if(VPORTD.INTFLAGS & PORT_INT6_bm)
    {
       RST_InterruptHandler(); 
    }
    if(VPORTD.INTFLAGS & PORT_INT7_bm)
    {
       IO_PD7_InterruptHandler(); 
    }
    if(VPORTD.INTFLAGS & PORT_INT5_bm)
    {
       LED_InterruptHandler(); 
    }
    /* Clear interrupt flags */
    VPORTD.INTFLAGS = 0xff;
}

ISR(PORTF_PORT_vect)
{ 
    // Call the interrupt handler for the callback registered at runtime
    if(VPORTF.INTFLAGS & PORT_INT5_bm)
    {
       VBAT_InterruptHandler(); 
    }
    if(VPORTF.INTFLAGS & PORT_INT0_bm)
    {
       IO_PF0_InterruptHandler(); 
    }
    if(VPORTF.INTFLAGS & PORT_INT1_bm)
    {
       IO_PF1_InterruptHandler(); 
    }
    if(VPORTF.INTFLAGS & PORT_INT2_bm)
    {
       UDTC_InterruptHandler(); 
    }
    if(VPORTF.INTFLAGS & PORT_INT3_bm)
    {
       IO_PF3_InterruptHandler(); 
    }
    if(VPORTF.INTFLAGS & PORT_INT4_bm)
    {
       IO_PF4_InterruptHandler(); 
    }
    if(VPORTF.INTFLAGS & PORT_INT6_bm)
    {
       IO_PF6_InterruptHandler(); 
    }
    if(VPORTF.INTFLAGS & PORT_INT7_bm)
    {
       IO_PF7_InterruptHandler(); 
    }
    /* Clear interrupt flags */
    VPORTF.INTFLAGS = 0xff;
}

/**
 End of File
*/