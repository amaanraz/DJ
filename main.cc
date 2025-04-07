/*---Project Headers---*/
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
#include "xpseudo_asm.h"
#include "math.h"
#include <stdlib.h>
#include <time.h>
#include "led_controller.h"

// source C:/Users/tmm12/Desktop/sources/script/load_data.tcl
// source C:/Users/Admin/Documents/ENSC452/sources/script/load_data.tcl

/*--- Communication Between Cores ---*/
// Shared flags between both cores
#define COMM_VAL (*(volatile unsigned long *)(0x020BB00C))
#define AUDIO_SAMPLE_CURRENT_MOMENT (*(volatile unsigned long *)(0xFFFF0001))
#define AUDIO_SAMPLE_READY (*(volatile unsigned long *)(0xFFFF0028))
#define RECORDING (*(volatile unsigned long *)(0xFFFF0010))
#define PLAYING_R (*(volatile unsigned long *)(0xFFFF0014))

/*--- Interrupt & Device Parameter Definitions ---*/
#define INTC_DEVICE_ID 		XPAR_PS7_SCUGIC_0_DEVICE_ID
#define TMR_DEVICE_ID		XPAR_TMRCTR_0_DEVICE_ID
#define BTNS_DEVICE_ID		XPAR_AXI_GPIO_0_DEVICE_ID
#define LEDS_DEVICE_ID		XPAR_AXI_GPIO_1_DEVICE_ID
#define INTC_GPIO_INTERRUPT_ID XPAR_FABRIC_AXI_GPIO_1_IP2INTC_IRPT_INTR
#define INTC_TMR_INTERRUPT_ID XPAR_FABRIC_AXI_TIMER_0_INTERRUPT_INTR

/*--- Button and Switch Constant Vals + LEDs ---*/
#define BTN_INT 			XGPIO_IR_CH1_MASK
#define TMR_LOAD			0xF8000000
#define LED_BASE XPAR_LED_CONTROLLER_0_S00_AXI_BASEADDR

/*--- Flags for VGA Events Between Cores ---*/
#define RIGHT_FLAG (*(volatile unsigned long *)(0xFFFF0008))
#define CENTER_FLAG (*(volatile unsigned long *)(0xFFFF1012))
#define DOWN_FLAG (*(volatile unsigned long *)(0xFFFF2016))
#define UP_FLAG (*(volatile unsigned long *)(0xFFFF3032))
#define SWITCHES_ON (*(volatile unsigned long *)(0xFFFF4032))

/*--- Menu Page Images ---*/
int * homepageDJ = (int *)0x0F9F2012;
int * homepageRecord = (int *)0x088D2012;
int * homepageSample = (int *)0x028DD00C;
int * dj = (int *)0x060BB00C;
int * recording = (int *)0x093DB00C;
int * sample1 = (int *)0x044CB00C;
int * sampleBack = (int *)0x034D2012;

/*--- Image Flags - Start on Homepage ---*/
static int home_flag = 1;
static int dj_flag = 0;
static int record_flag = 0;
static int sample_sel_flag = 0;
static int sample1_flag = 1;
static int sample2_flag = 0;
static int sample3_flag = 0;

/* --- Audio Samples & Memory Locations --- */
// Main song sample
#define SONG_ADDR 0x01300000
#define NUM_SAMPLES 1726590//1755840
volatile int *song = (volatile int *)SONG_ADDR;
int recording_song[1726590];

extern u32 MMUTable;

/*--- Sine Wave Parameters ---*/
#define AMPLITUDE 200        // Amplitude
#define FREQUENCY 2          // Frequency
#define OFFSET 512           // Vertical offset - so wave doesn't go out of screen
#define PI 3.14159265358979

// Base audio playback delay
u32 delay_us = 476;

/*--- Xilinx Peripheral Instances ---*/
XGpio LEDInst, BTNInst;
XScuGic INTCInst;
XTmrCtr TMRInst;

static int led_data;
static int btn_value;
static int tmr_count;

/*--- FrameBuffer Data and Screen Size ---*/
int *frameBuffer = (int *)0x00900000;  // Base address for framebuffer
int screenWidth = 1280;   			   // screen width
int screenHeight = 1024; 			   // screen height

/*--- FUNCTION PROTOTYPES ---*/
static void TMR_Intr_Handler(void *baseaddr_p);

/*--- INTERRUPT HANDLER FUNCTIONS (Timer) ---*/
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

/*--- Screen Draw/Clear Functions ---*/
void clearScreen(int *frameBuffer, int screenWidth, int screenHeight)
{
    for (int y = 0; y < screenHeight; y++) {
        for (int x = 0; x < screenWidth; x++) {
            frameBuffer[y * screenWidth + x] = 0x000000;  // Set pixel to black (0x000000)
        }
    }
}

// Draw pixel at desired location in screen
void draw_pixel(int *frameBuffer, int x, int y, int screenWidth, int color) {
    if (x >= 0 && x < screenWidth && y >= 0) {
        frameBuffer[y * screenWidth + x] = color;
    }
}


// Load specified images from mem
void loadImage(int *frameBuffer, int screenWidth, int screenHeight, int *image)
{
	//xil_printf("in the load Image function\n");
    for (int y = 0; y < screenHeight; y++) {
        for (int x = 0; x < screenWidth; x++) {
            frameBuffer[y * screenWidth + x] = image[y * screenWidth + x];
        }
    }
}

// Sine wave themes
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

// Function to draw sine wave based on samples
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
}

/*--- Progress Bar Functions ---*/
void draw_background_progress(int screenWidth, int screenHeight){
	int barWidth = screenWidth - 40;
	int barHeight = 10;  // Thin progress bar
	int barX = 20;  // Start X position
	int barY = screenHeight - 30;  // Position near the bottom
	// Draw the background of the progress bar (Gray)
	for (int x = 0; x < barWidth; x++) {
		for (int y = 0; y < barHeight; y++) {
			draw_pixel(frameBuffer, barX + x, barY + y, screenWidth, 0x555555);
		}
	}
}

// Draws progress bar based on how much of sample is complete
void draw_progress_bar(int *frameBuffer, int screenWidth, int screenHeight, int i) {
    int barWidth = screenWidth - 40;  // Leave some padding on the sides
    int barHeight = 10;  // Thin progress bar
    int barX = 20;  // Start X position
    int barY = screenHeight - 30;  // Position near the bottom

    // Calculate progress based on the current sample index
    float progress = (float)i / NUM_SAMPLES;
    int progressWidth = (int)(progress * barWidth);  // Width of the filled part


    // Draw the filled progress (Blue)
    for (int x = 0; x < progressWidth; x++) {
        for (int y = 0; y < barHeight; y++) {
            draw_pixel(frameBuffer, barX + x, barY + y, screenWidth, 0x0000FF);
        }
    }
}

void record_audio() {
	// Start recording when RECORDING =1 and the play button has been pressed
	// Save audio running to another buffer.
	// After loop exits, either by playback ending, or user leaving early
	// Save changes by copying contents of new buffer to song buffer to play
	bool finished_flag = 0;
	unsigned int index = 0;
	while(RECORDING == 1 && PLAYING_R == 1){
	//	xil_printf("STARTING RECORDING...");
		if (AUDIO_SAMPLE_READY) {  // Only read when data is ready
			if (index < NUM_SAMPLES) {
				recording_song[index] = AUDIO_SAMPLE_CURRENT_MOMENT / 150; // Normalize
				index++;
				finished_flag = 1;
			}
		}

	}
	// Copying contents of new buffer to song buffer to play
	if(finished_flag){
		xil_printf("SAVING CHANGES... ");
		for(int i = 0; i < NUM_SAMPLES; i++) {
			song[i] = recording_song[i];  // Copy the recorded data to the song buffer
		}
		finished_flag = 0;

		record_flag = 0;
		home_flag = 1;
	}
}

/*--- MAIN FUNCTION ---*/
int main()
{
    init_platform();
    print("CPU1: init_platform\n\r");

    //Disable cache on OCM
    // S=b1 TEX=b100 AP=b11, Domain=b1111, C=b0, B=b0
    Xil_SetTlbAttributes(0xFFFF0000,0x14de2);

    clearScreen(frameBuffer, screenWidth, screenHeight);
        for(int i=0;i<100000000;i++){
        	asm("NOP");
        }

        int counter = 0;
        int sampleCounter = 0;
        while(1){
        	// Xil_DCacheFlush();
        	// Start at home page with box on DJ
        	if (home_flag == 1) {
        		LED_CONTROLLER_mWriteReg(LED_BASE, 0, 1);
        		xil_printf("On home page...\r\n");
        		if (counter == 0) {
    				loadImage(frameBuffer, screenWidth, screenHeight, homepageDJ);
    			}
    			if (counter == 1) {
    				loadImage(frameBuffer, screenWidth, screenHeight, homepageRecord);
    			}
    			if (counter == 2) {
    				loadImage(frameBuffer, screenWidth, screenHeight, homepageSample);
    			}
            	// Draw rectangle on DJ
            	// If middle pressed and counter = 0, go to DJ mode (and load sine wave)
    			if(SWITCHES_ON == 0){
					if (CENTER_FLAG == 1 && counter == 0) {
						home_flag = 0;
						dj_flag = 1;
						loadImage(frameBuffer, screenWidth, screenHeight, dj);
						Xil_DCacheFlush();
						draw_background_progress(screenWidth, screenHeight-205);
						CENTER_FLAG = 0;
						Xil_DCacheFlush();
					}
					// If middle pressed and counter = 1, go to Record mode (set record flag to 1 and home flag to 0)
					else if (CENTER_FLAG == 1 && counter == 1) {
						home_flag = 0;
						record_flag = 1;
						CENTER_FLAG = 0;
						Xil_DCacheFlush();
					}
					// If middle pressed and counter = 2, go to Select Sample mode
					else if (CENTER_FLAG == 1 && counter == 2) {
						home_flag = 0;
						sample_sel_flag = 1;
						CENTER_FLAG = 0;
						Xil_DCacheFlush();
					}
					// If down pressed, draw rectangle on Record and clear rectangle on DJ and set counter = 1
					if (DOWN_FLAG == 1) {
						counter++;
						xil_printf("Counter value: %d\r\n", counter);

						if(counter > 2) {
							counter = 0;
						}
						DOWN_FLAG=0;
					}
					if (UP_FLAG == 1) {
						counter--;
						xil_printf("Counter value: %d\r\n", counter);

						if (counter < 0) {
							counter = 2;
						}
						UP_FLAG=0;
					}
    			}
        	} else if (dj_flag == 1) {
        		LED_CONTROLLER_mWriteReg(LED_BASE, 0, 2);

        		// Draw sine wave
        		draw_sine_wave(frameBuffer, screenWidth, screenHeight-215);
        		draw_progress_bar(frameBuffer, screenWidth, screenHeight-205, COMM_VAL);
        		Xil_DCacheFlush();

        		// To go to home page: if RIGHT_FLAG = 1, set home_flag = 1 and dj_flag = 0
        		if (CENTER_FLAG == 1 && SWITCHES_ON == 0) {
        			dj_flag = 0;
        			home_flag = 1;
        			CENTER_FLAG = 0;
        			Xil_DCacheFlush();
        		}
        	} else if (record_flag == 1) {
        		LED_CONTROLLER_mWriteReg(LED_BASE, 0, 4);
        		xil_printf("In recording mode...\r\n");
        		loadImage(frameBuffer, screenWidth, screenHeight, recording);
        		RECORDING = 1;
        		while (!PLAYING_R){
        			asm("NOP");
        			xil_printf("WAiting for play");

        			if (RIGHT_FLAG == 1 && SWITCHES_ON == 0) {

						record_flag = 0;
						home_flag = 1;
						RIGHT_FLAG = 0;
						break;
					}
        		}
        		record_audio();

        	} else if (sample_sel_flag == 1) {
        		// 3 flags for each sample and same thing if center pressed go to that sample etc blah
        		LED_CONTROLLER_mWriteReg(LED_BASE, 0, 8);
        		xil_printf("In sample select mode...\r\n");
				if (sampleCounter == 0) {
					loadImage(frameBuffer, screenWidth, screenHeight, sample1);
				}
        		// Selected sample
        		if(SWITCHES_ON == 0){
					if (CENTER_FLAG == 1) {
						// return to home screen
						sample_sel_flag = 0;
						home_flag = 1;
						CENTER_FLAG = 0;
					}
        		}
        	}
        }

    cleanup_platform();
    return 0;
}
