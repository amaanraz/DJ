#include "xparameters.h"
#include "xgpio.h"
#include "xtmrctr.h"
#include "xscugic.h"
#include "xil_exception.h"
#include "xil_printf.h"
#include <stdio.h>
#include <sleep.h>
#include "xil_io.h"
#include "xil_mmu.h"
#include "platform.h"
#include "xil_cache.h"
#include "xil_printf.h"
#include "xparameters.h"
#include "xpseudo_asm.h"
#include "xil_exception.h"
#include "math.h"

#define COMM_VAL (*(volatile unsigned long *)(0xFFFF0000))
extern u32 MMUTable;

// Parameter definitions
#define INTC_DEVICE_ID 		XPAR_PS7_SCUGIC_0_DEVICE_ID
#define TMR_DEVICE_ID		XPAR_TMRCTR_0_DEVICE_ID
#define BTNS_DEVICE_ID		XPAR_AXI_GPIO_0_DEVICE_ID
#define LEDS_DEVICE_ID		XPAR_AXI_GPIO_1_DEVICE_ID
#define INTC_GPIO_INTERRUPT_ID XPAR_FABRIC_AXI_GPIO_1_IP2INTC_IRPT_INTR
#define INTC_TMR_INTERRUPT_ID XPAR_FABRIC_AXI_TIMER_0_INTERRUPT_INTR

#define BTN_INT 			XGPIO_IR_CH1_MASK
#define TMR_LOAD			0xF8000000

// Define the sine wave parameters
#define AMPLITUDE 200     // Amplitude of the sine wave
#define FREQUENCY 2      // Frequency of the sine wave
#define OFFSET 512       // Vertical offset for sine wave, so it doesn't go out of screen
#define PI 3.14159265358979

XGpio LEDInst, BTNInst;
XScuGic INTCInst;
XTmrCtr TMRInst;
static int led_data;
static int btn_value;
static int tmr_count;

// Bar colors in RGB format
unsigned int barColors[6] = {0xFF0000,  // Red
                              0x00FF00,  // Green
                              0x0000FF,  // Blue
                              0xFFFF00,  // Yellow
                              0xFF00FF, // Magenta
							  0xFFFFFF}; // White

int *frameBuffer = (int *)0x00900000;  // Example base address for framebuffer
int screenWidth = 1280;   // screen width
int screenHeight = 1024;  // screen height

//----------------------------------------------------
// PROTOTYPE FUNCTIONS
//----------------------------------------------------
static void BTN_Intr_Handler(void *baseaddr_p);
static void TMR_Intr_Handler(void *baseaddr_p);
static int InterruptSystemSetup(XScuGic *XScuGicInstancePtr);
static int IntcInitFunction(u16 DeviceId, XTmrCtr *TmrInstancePtr, XGpio *GpioInstancePtr);

//----------------------------------------------------
// INTERRUPT HANDLER FUNCTIONS
// - called by the timer, button interrupt, performs
// - LED flashing
//----------------------------------------------------

// Function to render vertical bars on the screen
void renderVerticalBars(int *frameBuffer, int screenWidth, int screenHeight)
{
    int barWidth = screenWidth / 6;  // Divide the screen into 5 equal bars
    int barHeight = screenHeight;

    for (int i = 0; i < 6; i++) {
        // Set the color for the current bar
        unsigned int color = barColors[i];

        // Render each bar in the corresponding section of the screen
        for (int y = 0; y < barHeight; y++) {
            for (int x = i * barWidth; x < (i + 1) * barWidth; x++) {
                frameBuffer[y * screenWidth + x] = color;  // Set pixel color
            }
        }
    }
}

void clearScreen(int *frameBuffer, int screenWidth, int screenHeight)
{
    for (int y = 0; y < screenHeight; y++) {
        for (int x = 0; x < screenWidth; x++) {
            frameBuffer[y * screenWidth + x] = 0x000000;  // Set pixel to black (0x000000)
        }
    }
}

// Function to render a sine wave on the screen
void renderSineWave(int *frameBuffer, int screenWidth, int screenHeight)
{
	// First clear the screen to black
	clearScreen(frameBuffer, screenWidth, screenHeight);

    int midHeight = screenHeight / 2;  // Center of the screen vertically
    for (int x = 0; x < screenWidth; x++) {
        // Calculate sine value for current x-coordinate
        float sineValue = sin(2 * PI * FREQUENCY * (float)x / (float)screenWidth);

        // Calculate the corresponding y-coordinate for sine wave
        int y = (int)(AMPLITUDE * sineValue) + midHeight;

        // Set the pixel color for the sine wave point
        // Assuming we want the sine wave to be white
        frameBuffer[y * screenWidth + x] = 0xFFFFFF;  // White color
    }
}

void shiftBarsLeft(){
	// Shift array like circular left
	unsigned int temp = barColors[0];  // Save the first element
	for (int i = 0; i < 5; i++) {
	    barColors[i] = barColors[i + 1];  // Shift each element left
	}
	barColors[5] = temp;  // Place the first element in the last position
	renderVerticalBars(frameBuffer, screenWidth, screenHeight);
}

void shiftBarsRight(){
	 // Circular right shift: Shift all elements to the right, and the last element moves to the first position
	unsigned int temp = barColors[5];  // Save the last element
	for (int i = 5; i > 0; i--) {
		barColors[i] = barColors[i - 1];  // Shift each element to the right
	}
	barColors[0] = temp;  // Place the last element in the first position
	renderVerticalBars(frameBuffer, screenWidth, screenHeight);
}

void BTN_Intr_Handler(void *InstancePtr)
{
	// Disable GPIO interrupts
	XGpio_InterruptDisable(&BTNInst, BTN_INT);
	// Ignore additional button presses
	if ((XGpio_InterruptGetStatus(&BTNInst) & BTN_INT) !=
			BTN_INT) {
			return;
		}
	btn_value = XGpio_DiscreteRead(&BTNInst, 1);
	// Increment counter based on button value
	// Reset if centre button pressed
//	if(btn_value != 1) led_data = led_data + btn_value;
	if(btn_value == 8) {
		shiftBarsRight();
		renderVerticalBars(frameBuffer, screenWidth, screenHeight);
	}
	else if (btn_value == 4) {
		shiftBarsLeft();
		renderVerticalBars(frameBuffer, screenWidth, screenHeight);
	}
	//else led_data = 0;
    //XGpio_DiscreteWrite(&LEDInst, 1, led_data);
    (void)XGpio_InterruptClear(&BTNInst, BTN_INT);
    // Enable GPIO interrupts
    XGpio_InterruptEnable(&BTNInst, BTN_INT);
}

void TMR_Intr_Handler(void *data)
{
	if (XTmrCtr_IsExpired(&TMRInst,0)){
		// Once timer has expired 3 times, stop, increment counter
		// reset timer and start running again
		if(tmr_count == 3){
			XTmrCtr_Stop(&TMRInst,0);
			tmr_count = 0;
			led_data++;
			XGpio_DiscreteWrite(&LEDInst, 1, led_data);
			XTmrCtr_Reset(&TMRInst,0);
			XTmrCtr_Start(&TMRInst,0);

		}
		else tmr_count++;
	}
}

//----------------------------------------------------
// MAIN FUNCTION
//----------------------------------------------------

int main()
{
    init_platform();
    print("CPU1: init_platform\n\r");

    //Disable cache on OCM
    // S=b1 TEX=b100 AP=b11, Domain=b1111, C=b0, B=b0
    Xil_SetTlbAttributes(0xFFFF0000,0x14de2);

    while(1){

    	if(COMM_VAL == 1){
    		shiftBarsRight();
//    		renderVerticalBars(frameBuffer, screenWidth, screenHeight);
    		renderSineWave(frameBuffer, screenWidth, screenHeight);
    		COMM_VAL = 0;
    	}
//    	renderVerticalBars(frameBuffer, screenWidth, screenHeight);
    	renderSineWave(frameBuffer, screenWidth, screenHeight);

    }

    cleanup_platform();
    return 0;
}

//----------------------------------------------------
// INITIAL SETUP FUNCTIONS
//----------------------------------------------------

int InterruptSystemSetup(XScuGic *XScuGicInstancePtr)
{
	// Enable interrupt
	XGpio_InterruptEnable(&BTNInst, BTN_INT);
	XGpio_InterruptGlobalEnable(&BTNInst);

	Xil_ExceptionRegisterHandler(XIL_EXCEPTION_ID_INT,
			 	 	 	 	 	 (Xil_ExceptionHandler)XScuGic_InterruptHandler,
			 	 	 	 	 	 XScuGicInstancePtr);
	Xil_ExceptionEnable();


	return XST_SUCCESS;

}

int IntcInitFunction(u16 DeviceId, XTmrCtr *TmrInstancePtr, XGpio *GpioInstancePtr)
{
	XScuGic_Config *IntcConfig;
	int status;

	// Interrupt controller initialisation
	IntcConfig = XScuGic_LookupConfig(DeviceId);
	status = XScuGic_CfgInitialize(&INTCInst, IntcConfig, IntcConfig->CpuBaseAddress);
	if(status != XST_SUCCESS) return XST_FAILURE;

	// Call to interrupt setup
	status = InterruptSystemSetup(&INTCInst);
	if(status != XST_SUCCESS) return XST_FAILURE;

	// Connect GPIO interrupt to handler
	status = XScuGic_Connect(&INTCInst,
					  	  	 INTC_GPIO_INTERRUPT_ID,
					  	  	 (Xil_ExceptionHandler)BTN_Intr_Handler,
					  	  	 (void *)GpioInstancePtr);
	if(status != XST_SUCCESS) return XST_FAILURE;


	// Connect timer interrupt to handler
	status = XScuGic_Connect(&INTCInst,
							 INTC_TMR_INTERRUPT_ID,
							 (Xil_ExceptionHandler)TMR_Intr_Handler,
							 (void *)TmrInstancePtr);
	if(status != XST_SUCCESS) return XST_FAILURE;

	// Enable GPIO interrupts interrupt
	XGpio_InterruptEnable(GpioInstancePtr, 1);
	XGpio_InterruptGlobalEnable(GpioInstancePtr);

	// Enable GPIO and timer interrupts in the controller
	XScuGic_Enable(&INTCInst, INTC_GPIO_INTERRUPT_ID);

	XScuGic_Enable(&INTCInst, INTC_TMR_INTERRUPT_ID);


	return XST_SUCCESS;
}


/*
 *   	while(1){
  		renderVerticalBars(frameBuffer, screenWidth, screenHeight);


//  		XTmrCtr_Start(&TimerInstancePtr,0);
//  		while(TIMER_INTR_FLG == false){
//  		}
//
//  		TIMER_INTR_FLG = false;
//
//  		if(loop == 0){
//  			shiftBarsRight();
//  			renderVerticalBars(frameBuffer, screenWidth, screenHeight);
//  		}
//  		else if(loop==1){
//  			shiftBarsRight();
//  			renderVerticalBars(frameBuffer, screenWidth, screenHeight);
//  		}
//  		else if(loop==2){
//  			shiftBarsRight();
//  			renderVerticalBars(frameBuffer, screenWidth, screenHeight);
//  		}
//  		else if(loop==3){
//  			shiftBarsRight();
//  			renderVerticalBars(frameBuffer, screenWidth, screenHeight);
//  		}
//  		else if(loop==4){
//  			shiftBarsRight();
//  			renderVerticalBars(frameBuffer, screenWidth, screenHeight);
//  		}
//  		loop++;
//  		loop = loop % 5;
  	}*/
