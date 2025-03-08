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

// MILESTONE 3 COMMUNICATION VARS FOR SCREEN
#define RIGHT_FLAG (*(volatile unsigned long *)(0xFFFF0008))
#define CENTER_FLAG (*(volatile unsigned long *)(0xFFFF0012))
#define RECORD_FLAG (*(volatile unsigned long *)(0xFFFF0016))
#define SAMPLE_FLAG (*(volatile unsigned long *)(0xFFFF0032))

// dow -data C:/Users/tmm12/Desktop/sources/images/Homepage.data 0x018D2012
int * homepage = (int *)0x018D2012;
int * recording = (int *)0x020BB00C;
int * sample = (int *)0x048BB00C;

// milestone 3 flags
static int home_flag = 1;
static int dj_flag = 0;
static int record_flag = 0;
static int sample_sel_flag = 0;

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
static int draw_sine = 0;
static int draw_rect = 0;

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

void loadImage(int *frameBuffer, int screenWidth, int screenHeight, int *image)
{
    for (int y = 0; y < screenHeight; y++) {
        for (int x = 0; x < screenWidth; x++) {
            frameBuffer[y * screenWidth + x] = image[y * screenWidth + x];
        }
    }
}

void draw_pixel(int *frameBuffer, int x, int y, int screenWidth, int color) {
    if (x >= 0 && x < screenWidth && y >= 0) {
        frameBuffer[y * screenWidth + x] = color;
    }
}

void draw_rectangle(int *frameBuffer, int screenWidth, int screenHeight, int x, int y, int width, int height, int color) {
    for (int i = 0; i < width; i++) {
        draw_pixel(frameBuffer, x + i, y, screenWidth, color);                  // Top edge
        draw_pixel(frameBuffer, x + i, y + height - 1, screenWidth, color);     // Bottom edge
    }
    for (int i = 0; i < height; i++) {
        draw_pixel(frameBuffer, x, y + i, screenWidth, color);                  // Left edge
        draw_pixel(frameBuffer, x + width - 1, y + i, screenWidth, color);      // Right edge
    }
}

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

    int square_x = 540;  // Example: Centered square
    int square_y = 412;
    int square_width = 200;
    int square_height = 200;
    int square_color = 0xFF0000;  // Red

	// need to add label on each subpage labelled "home" to go back and draw rectangle or maybe not we'll see lol
    while(1){
    	// start at home page with box on DJ
    	// home_flag = 1
    	if (home_flag == 1) {
    		int counter = 0;
    		// dont forget to draw the image!
    		loadImage(frameBuffer, screenWidth, screenHeight, homepage);
    		loadImage(frameBuffer, screenWidth, screenHeight, homepage);
        	// draw rectangle on DJ
        	// if middle pressed and counter = 0, go to DJ mode (and load sine wave)
    			// do it like: if CENTER_FLAG = 1 then dj_flag = 1 and home_flag = 0
    		if (CENTER_FLAG == 1 && counter == 0) {
    			dj_flag = 1;
    			home_flag = 0;
    			CENTER_FLAG = 0;
    		}

    		// if down pressed, draw rectangle on Record and clear rectangle on DJ and set counter = 1
    		// if middle pressed and counter = 1, go to Record mode (set record flag to 1 and home flag to 0)

    		// if down pressed, draw rectangle on select sample and clear rectangle on record and set counter = 1
    		// if middle pressed and counter = 1, go to Select Sample mode
    	} else if (dj_flag == 1) {
    		// draw sine wave
    		xil_printf("Drawing Sine Wave!");
    		//draw_sine = 1;
    		draw_sine_wave(frameBuffer, screenWidth, screenHeight);

    		// to go to home page: if RIGHT_FLAG = 1, set home_flag = 1 and dj_flag = 0
    	}
    		else if (record_flag == 1) {
    		// have pages that correspond to sounds for each switch value
    		// amaan whack recording logic stuff

    		// to go to home page: if RIGHT_FLAG = 1, set home_flag = 1 and record_flag = 0
    	} else if (sample_sel_flag == 1) {
    		// 3 flags for each sample and same thing if center pressed go to that sample etc blah

    		// to go to home page: if RIGHT_FLAG = 1, set home_flag = 1 and sample_sel_flag = 0
    	}


    	// ALL DA OLD STUFF
//    	if (RIGHT_FLAG == 1) {
//			xil_printf("Drawing Sine Wave!");
//			draw_sine = 1;
//			RIGHT_FLAG = 0;
//	        draw_rect = 0;  // Turn off square when drawing sine wave
//		}
//		if (draw_sine == 1) {
//			draw_sine_wave(frameBuffer, screenWidth, screenHeight);
//		}
//		if (CENTER_FLAG == 1) {
//			draw_sine = 0;
//			xil_printf("Loading Homepage Image...\n");
//			draw_rectangle(frameBuffer, screenWidth, screenHeight, square_x, square_y, square_width, square_height, square_color);
//			loadImage(frameBuffer, screenWidth, screenHeight, homepage);
//			loadImage(frameBuffer, screenWidth, screenHeight, homepage);
//			draw_rectangle(frameBuffer, screenWidth, screenHeight, square_x, square_y, square_width, square_height, square_color);
//			draw_rect = 1;
//			CENTER_FLAG = 0;
//		}
//
//		if (RECORD_FLAG == 1) {
//			draw_sine = 0;
//			xil_printf("Loading Recording Image...\n");
//			loadImage(frameBuffer, screenWidth, screenHeight, recording);
//			loadImage(frameBuffer, screenWidth, screenHeight, recording);
//			draw_rect = 1;
//			RECORD_FLAG = 0;
//		}
//    	if (SAMPLE_FLAG == 1) {
//			draw_sine = 0;
//			xil_printf("Loading Sample Image...\n");
//			loadImage(frameBuffer, screenWidth, screenHeight, sample);
//			loadImage(frameBuffer, screenWidth, screenHeight, sample);
//			SAMPLE_FLAG = 0;  // Reset the flag after loading
//		}
//
////    	if(COMM_VAL == 1){
////    		draw_sine_wave(frameBuffer, screenWidth, screenHeight);
////    		COMM_VAL = 0;
////    	}
////
////    	draw_sine_wave(frameBuffer, screenWidth, screenHeight);
//    	if (draw_rect == 1) {
//        	draw_sine = 0;
//			draw_rectangle(frameBuffer, screenWidth, screenHeight, square_x, square_y, square_width, square_height, square_color);
//			draw_rectangle(frameBuffer, screenWidth, screenHeight, square_x, square_y, square_width, square_height, square_color);
//			draw_rect = 0;
//		}

    }

    cleanup_platform();
    return 0;
}




