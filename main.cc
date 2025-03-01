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
#include <stdlib.h>
#include <time.h>    // For timing

#define COMM_VAL (*(volatile unsigned long *)(0xFFFF0000))
#define AUDIO_SAMPLE_CURRENT_MOMENT (*(volatile unsigned long *)(0xFFFF0001))

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


void clearScreen(int *frameBuffer, int screenWidth, int screenHeight)
{
    for (int y = 0; y < screenHeight; y++) {
        for (int x = 0; x < screenWidth; x++) {
            frameBuffer[y * screenWidth + x] = 0x000000;  // Set pixel to black (0x000000)
        }
    }
}


void draw_pixel(int *frameBuffer, int x, int y, int screenWidth, int color) {
    if (x >= 0 && x < screenWidth && y >= 0) {
        frameBuffer[y * screenWidth + x] = color;
    }
}

//void draw_sine_wave(int *frameBuffer, int screenWidth, int screenHeight) {
//    static float phase = 0.0;
//
//    // Read the latest audio value
//    unsigned long audio_value = AUDIO_SAMPLE_CURRENT_MOMENT;
//
//    // Normalize and increase amplitude
//    float amplitude = ((float)(audio_value & 0xFFF) / 19.0f) * 1.5f; // Increase amplitude
//    amplitude = fminf(amplitude, (screenHeight / 2 ));  // Slightly less restrictive clamp
//
//    // Set frequency to a fixed value
//    float frequency = 0.025f;
//
//    // Clear the framebuffer
//    for (int i = 0; i < screenWidth * screenHeight; i++) {
//        frameBuffer[i] = 0x7dff00;  // background
//    }
//
//    // Draw the sine wave
//    for (int x = 0; x < screenWidth; x++) {
//        int y = (int)(screenHeight / 2 + amplitude * sinf(phase + x * frequency));
//
//        if (y >= 0 && y < screenHeight) {
//        	for (int dx = -2; dx <= 2; dx++) {
//        	    for (int dy = -2; dy <= 2; dy++) {
//        	        if (dx * dx + dy * dy <= 4) {  // Circle-like shape
//        	            int x_thick = x + dx;
//        	            int y_thick = y + dy;
//        	            if (x_thick >= 0 && x_thick < screenWidth && y_thick >= 0 && y_thick < screenHeight) {
//        	                draw_pixel(frameBuffer, x_thick, y_thick, screenWidth, 0xFFFFFF);
//        	            }
//        	        }
//        	    }
//        	}
//        }
//    }
//
//}

typedef struct {
    int background;
    int wave;
} ColorTheme;

#define NUM_THEMES 11
ColorTheme colorThemes[NUM_THEMES] = {
    {0x000000, 0xFFFFFF},  // Black bg, White wave
	{0x1A1A2E, 0xE94560},  // Deep Blue & Vibrant Red
	{0x162447, 0x1F4068},  // Navy & Light Blue
	{0x2E1A47, 0xE4A5FF},  // Purple & Soft Pink
	{0x102027, 0x26A69A},  // Dark Teal & Cyan
	{0x0F2027, 0x4A90E2},  // Midnight Blue & Sky Blue
	{0x1B1B1B, 0xF2A365},  // Charcoal & Warm Orange
	{0x232931, 0x4ECCA3},  // Dark Gray & Mint Green
	{0x3E1F47, 0xFFB6C1},  // Plum Purple & Pastel Pink
	{0x283149, 0xDA0463},   // Steel Blue & Bright Pink
	{0xFFFFFF, 0x000000}  // Black bg, White wave
};

static int current_theme_index = 0;
static int frame_counter = 0;
static int color_switch_interval = 30;

void draw_sine_wave(int *frameBuffer, int screenWidth, int screenHeight) {
    static float phase = 0.0;

    // Read the latest audio value
    unsigned long audio_value = AUDIO_SAMPLE_CURRENT_MOMENT;

    // Normalize and scale amplitude
    float amplitude = ((float)(audio_value & 0xFFF) / 19.0f) * 1.5f;
    amplitude = fminf(amplitude, (screenHeight / 2));

    // Set frequency to a fixed value
    float frequency = 0.025f;

    if (frame_counter >= color_switch_interval) {
		current_theme_index = rand() % NUM_THEMES;
		frame_counter = 0;  // Reset frame counter
	} else {
		frame_counter++;  // Increment frame counter
	}



    // Set background color
    int backgroundColor = colorThemes[current_theme_index].background;
   int waveColor = colorThemes[current_theme_index].wave;

    // Clear framebuffer with new background color
    for (int i = 0; i < screenWidth * screenHeight; i++) {
        frameBuffer[i] = backgroundColor;
    }


    //  Draw the sine wave
    for (int x = 0; x < screenWidth; x++) {
        int y = (int)(screenHeight / 2 + amplitude * sinf(phase + x * frequency));

        if (y >= 0 && y < screenHeight) {
            // Thicker sine wave using circular shape
            for (int dx = -2; dx <= 2; dx++) {
                for (int dy = -2; dy <= 2; dy++) {
                    if (dx * dx + dy * dy <= 4) { // Rounded thickness
                        int x_thick = x + dx;
                        int y_thick = y + dy;
                        if (x_thick >= 0 && x_thick < screenWidth && y_thick >= 0 && y_thick < screenHeight) {
                            draw_pixel(frameBuffer, x_thick, y_thick, screenWidth, waveColor);
                        }
                    }
                }
            }

            // Glow effect (soft outer aura)
            for (int glow_offset = 3; glow_offset <= 5; glow_offset++) {
                int glow_y1 = y - glow_offset;
                int glow_y2 = y + glow_offset;

                if (glow_y1 >= 0 && glow_y1 < screenHeight) {
                    draw_pixel(frameBuffer, x, glow_y1, screenWidth, (waveColor & 0xFFFFFF) | 0x22000000); // Faint glow
                }
                if (glow_y2 >= 0 && glow_y2 < screenHeight) {
                    draw_pixel(frameBuffer, x, glow_y2, screenWidth, (waveColor & 0xFFFFFF) | 0x22000000); // Faint glow
                }
            }
        }
    }

//    phase += 0.05;  // Move wave forward
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
    		draw_sine_wave(frameBuffer, screenWidth, screenHeight);
    		COMM_VAL = 0;
    	}

    	draw_sine_wave(frameBuffer, screenWidth, screenHeight);

    }

    cleanup_platform();
    return 0;
}





