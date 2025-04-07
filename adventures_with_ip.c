/*---Project Headers---*/
#include "adventures_with_ip.h"
#include <stdio.h>
#include <sleep.h>
#include "xil_io.h"
#include "xil_mmu.h"
#include "platform.h"
#include "xil_printf.h"
#include "xpseudo_asm.h"
#include "xil_exception.h"
#include "xscugic.h"
#include <math.h>

/*--- Communication Between Cores ---*/
#define sev() __asm__("sev")
#define ARM1_STARTADR 0xFFFFFFF0 // Address to boot Core 1
#define ARM1_BASEADDR 0x100000 // Start/base address for Core 1 execution

// Shared flags between both cores
#define COMM_VAL (*(volatile unsigned long *)(0x020BB00C))
#define AUDIO_SAMPLE_CURRENT_MOMENT (*(volatile unsigned long *)(0xFFFF0001))
#define AUDIO_SAMPLE_READY (*(volatile unsigned long *)(0xFFFF0028))
#define RECORDING (*(volatile unsigned long *)(0xFFFF0010))
#define PLAYING_R (*(volatile unsigned long *)(0xFFFF0014))

/*--- Interrupt & Device Parameter Definitions ---*/
#define INTC_DEVICE_ID 		XPAR_PS7_SCUGIC_0_DEVICE_ID
#define TMR_DEVICE_ID		XPAR_TMRCTR_0_DEVICE_ID
#define BTNS_DEVICE_ID		XPAR_AXI_GPIO_1_DEVICE_ID
#define LEDS_DEVICE_ID		XPAR_AXI_GPIO_1_DEVICE_ID
#define INTC_GPIO_INTERRUPT_ID XPAR_FABRIC_AXI_GPIO_1_IP2INTC_IRPT_INTR

/*--- Button and Switch Constant Vals ---*/
#define BTN_INT 			XGPIO_IR_CH1_MASK
#define SWT_INT			    XGPIO_IR_CH2_MASK
#define TMR_LOAD			0xF8000000

/*--- Flags for VGA Events Between Cores ---*/
#define RIGHT_FLAG (*(volatile unsigned long *)(0xFFFF0008))
#define CENTER_FLAG (*(volatile unsigned long *)(0xFFFF1012))
#define DOWN_FLAG (*(volatile unsigned long *)(0xFFFF2016))
#define UP_FLAG (*(volatile unsigned long *)(0xFFFF3032))
#define SWITCHES_ON (*(volatile unsigned long *)(0xFFFF4032))

/*--- Audio Sample Size ---*/
#define SAMPLES_PER_SECOND 48000                                 // 48kHz standard
#define RECORD_SECONDS 35                                        // Recording duration
#define MAX_SAMPLES (SAMPLES_PER_SECOND * RECORD_SECONDS * 15)
int NUM_BYTES_BUFFER = 5242880;                                  // Buffer size (in bytes)

int j = 0; // Flag to reset sound buffer

/*--- Sound Effect Flags - Global in Order to Reset Status --- */
int j_drum = 0;
int j_clap = 0;
int j_kick = 0;
int j_snare = 0;
int j_hihat = 0;

static int btn_value;
static int swt_value;

/* --- Audio Samples & Memory Locations --- */
// Main song sample
#define SONG_ADDR 0x01300000
volatile int *song = (volatile int *)SONG_ADDR;
int NUM_SAMPLES = 1726590;

// Sound effects
int * drum = (int *)0x072BB00C;
int NUM_SAMPLES_DRUM = 19055;

int * snare = (int *)0x028A4010;
int NUM_SAMPLES_SNARE = 32256;

int * clap = (int *)0x0308D014;
int NUM_SAMPLES_CLAP = 35712;

int * kickhard = (int *)0x0FFFFFC0;
int NUM_SAMPLES_KICKHARD = 19584;

int * hihat = (int *)0x0328D014;
int NUM_SAMPLES_HIHAT = 48384;

// Buffer - can be used after recording mode to reset sample to original
static int audio_sample = 0;
int downloaded_song[1726590];

/*--- Audio Effects (Reverb, Tremolo, Distortion, Flanger, LFSR) ---*/
// Reverb
#define REVERB_DELAY 4800                     // Delay in samples (~100ms at 48kHz)
#define REVERB_DECAY 0.85                     // Decay factor (0.0 - 1.0)
static int reverb_buffer[REVERB_DELAY];       // Circular buffer
static int reverb_index = 0;

// Tremolo
#define TREMOLO_RATE 0.1f                   // Frequency of amplitude modulation (higher = faster)
#define TREMOLO_DEPTH 0.8f                  // Modulation depth (0.0 - 1.0)
#define TREMOLO_MAX 1000                    // Maximum counter val for mod
static float tremolo_counter = 0.0f;        // Counter to track modulation

// Distortion
#define DISTORTION_GAIN 3.0f                // Signal amplification (higher = more)
#define DISTORTION_THRESHOLD 22000          // Clipping threshold
#define SMOOTHING_FACTOR 0.9f               // Higher value = smoother sound
static int prev_sample = 0;

// Flanger
#define FLANGER_DELAY 1200                  // Max delay buff (~25ms at 48kHz)
#define FLANGER_DEPTH 500                   // Depth of modulation (lower val = subtler)
#define FLANGER_RATE 1                      // Speed of modulation (oscillation)
static int flanger_buffer[FLANGER_DELAY];   // Delay buffer
static int flanger_index = 0;
static int flanger_lfo = 0;                 // Triangle wave (low freq oscillator)
static int lfo_direction = 1;               // Controls up/down for triangle wave LFO

/*--- Hardware Effects ---*/
// LFSR
#define LFSR_BASE_ADDR    0x43C00000
#define LFSR_REG_OFFSET   0x00
uint32_t random_number;

// Distortion effect
#define EFFECT_BASE_ADDR 0x43C10000 // Effect register base address

// Looping
static int loop_flag = 0;

/*--- Audio Playback Buffers & Flags ---*/
static int playing = 0;
static int playing_snare = 0;
static int paused = 0;
static int play_flag = 0;
static int pause_flag = 0;
static int drum_flag = 0;
static int snare_flag = 0;
static int clap_flag = 0;
static int kickhard_flag = 0;
static int hihat_flag = 0;
static int reverb_flag = 0;
static int tremolo_flag = 0;
static int distortion_flag = 0;
static int flanger_flag = 0;
static int skip_flag = 0;
static int rewind_flag = 0;
static int white_noise = 0;
static int last_swt_value = 0;

// Audio playback delay
u32 delay_us = 476;
u32 base = 476;


/*--- Xilinx Peripheral Instances ---*/
XGpio LEDInst, BTNInst;
XScuGic INTCInst;

/*--- Function Prototypes ---*/
static void BTN_Intr_Handler(void *baseaddr_p);
static int InterruptSystemSetup(XScuGic *XScuGicInstancePtr);
static int IntcInitFunction(u16 DeviceId, XGpio *GpioInstancePtr);

/*--- INTERRUPT HANDLER FUNCTIONS ---*/
// Handles button and switch interrupts
// Waits for an action - enables/disables flags based off of swt/button vals
void BTN_Intr_Handler(void *InstancePtr) {
    XGpio_InterruptDisable(&BTNInst, BTN_INT);
    XGpio_InterruptDisable(&BTNInst, SWT_INT);

    // Read current button and switch values
    btn_value = XGpio_DiscreteRead(&BTNInst, 1);
    swt_value = XGpio_DiscreteRead(&BTNInst, 2);
    SWITCHES_ON = swt_value;

    // Handle cases where switch value exceeds base threshold
    if(swt_value > 128){
    	// To understand other actions while this switch is on
    	swt_value = swt_value - 128;
    } else {
    	skip_flag = 0;
    	if (swt_value != 128 && last_swt_value == 128) {
    	        delay_us = base; // To set back to default delay once speed up is off
    	}
    	if(swt_value != 64){
    		rewind_flag = 0;
    	}

    	if(swt_value != 32){
    		loop_flag = 0;
    	}
    	last_swt_value = swt_value; // Track previous state
    }
    // Sound Effect Flags (snare, clap, kick, hihat)
    if(swt_value == 2){
		if (btn_value == 8) {
			// right button
			snare_flag = 1;
			j_snare=0;
		} else if (btn_value == 4) {
			// left button
			clap_flag=1;
			j_clap=0;
		} else if (btn_value == 16) {
			// up button
			kickhard_flag = 1;
			j_kick=0;
		} else if(btn_value == 2){
			// down button
			hihat_flag=1;
			j_hihat=0;
		} else if (btn_value == 1){
			// center button
			drum_flag = 1;
			j_drum=0;
		}
    } else if (swt_value == 1){ // Playback controls
		if (btn_value == 8) {
			paused = !paused;
			xil_printf("Audio %s.\r\n", paused ? "Paused" : "Resumed");
		} else if (btn_value == 4) {
			// Placeholder - does nothing
		} else if (btn_value == 16) {
			if(!skip_flag){
				delay_us = delay_us + 1;
				xil_printf("delay_us: %d\n\r", delay_us);
			}
		} else if(btn_value == 2){
			if (delay_us > 1){
				if(!skip_flag){
					delay_us = delay_us - 1;
					xil_printf("delay_us: %d\n\r", delay_us);
				}
			}
		} else if (btn_value == 1){
			play_flag = 1;
		}
    } else if (swt_value == 4){ // AUDIO EFFECTS
		if (btn_value == 8) {
			tremolo_flag = !tremolo_flag;
			xil_printf("tremolo: %d\n\r", tremolo_flag);
		} else if (btn_value == 4) {
			reverb_flag = !reverb_flag;
			xil_printf("reverb: %d\n\r", reverb_flag);
		} else if (btn_value == 16) {
			distortion_flag = !distortion_flag;
			xil_printf("distortion: %d\n\r", distortion_flag);
		} else if(btn_value == 2){
			flanger_flag = !flanger_flag;
			xil_printf("flanger: %d\n\r", flanger_flag);
		} else if (btn_value == 1){
			white_noise = !white_noise;
			xil_printf("white noise: %d\n\r", white_noise);
		}
    } else if (swt_value == 8){ // Song management (copy/load/etc)
    	if(btn_value == 16){
    		// Copy over downloaded song
    		for(int i = 0; i < NUM_SAMPLES; i++) {
    			downloaded_song[i] = song[i];  // Copy the data to the song buffer
			}
    		xil_printf("Downloaded: %d\n\r");
    	} else if(btn_value == 1){
    		for(int i = 0; i < NUM_SAMPLES; i++) {
    			song[i] = downloaded_song[i];  // Copy the data to the song buffer
			}
    		xil_printf("Cleared: %d\n\r");
    	}
    } else if((swt_value == 64)) {    // REWIND FUNCTIONALITY
    	rewind_flag = 1;
    } else if (swt_value >= 128) {    // SPEED UP FUNCTIONALITY
    	skip_flag = 1;
    	delay_us = delay_us / 2;
    } else if (swt_value == 32) {     // LOOP FUNCTIONALITY
    	loop_flag = !loop_flag;
	} else {					      // MENU SCREEN
    	if (btn_value == 8) {
    		// To exit from recording mode
			RIGHT_FLAG = 1;
		} else if (btn_value == 4) {
			// Placeholder - does nothing
		} else if (btn_value == 16) {
			UP_FLAG = 1;
		} else if(btn_value == 2){
			DOWN_FLAG = 1;
		} else if (btn_value == 1){
			xil_printf("center button pressed.\r\n");
			CENTER_FLAG = 1;
		}
    }

    // Clear and re-enable interrupts
    (void)XGpio_InterruptClear(&BTNInst, BTN_INT);
    (void)XGpio_InterruptClear(&BTNInst, SWT_INT);
    XGpio_InterruptEnable(&BTNInst, BTN_INT);
    XGpio_InterruptEnable(&BTNInst, SWT_INT);
}

/*--- INITIAL SETUP FUNCTIONS ---*/
int InterruptSystemSetup(XScuGic *XScuGicInstancePtr) {
    XGpio_InterruptEnable(&BTNInst, BTN_INT);
    XGpio_InterruptEnable(&BTNInst, SWT_INT);
    XGpio_InterruptGlobalEnable(&BTNInst);

    Xil_ExceptionRegisterHandler(XIL_EXCEPTION_ID_INT,
                                 (Xil_ExceptionHandler)XScuGic_InterruptHandler,
                                 XScuGicInstancePtr);
    Xil_ExceptionEnable();
    return XST_SUCCESS;
}

int IntcInitFunction(u16 DeviceId, XGpio *GpioInstancePtr) {
    XScuGic_Config *IntcConfig;
    int status;

    IntcConfig = XScuGic_LookupConfig(DeviceId);
    status = XScuGic_CfgInitialize(&INTCInst, IntcConfig, IntcConfig->CpuBaseAddress);
    if (status != XST_SUCCESS) return XST_FAILURE;

    status = InterruptSystemSetup(&INTCInst);
    if (status != XST_SUCCESS) return XST_FAILURE;

    status = XScuGic_Connect(&INTCInst, INTC_GPIO_INTERRUPT_ID,
                             (Xil_ExceptionHandler)BTN_Intr_Handler,
                             (void *)GpioInstancePtr);
    if (status != XST_SUCCESS) return XST_FAILURE;

    XGpio_InterruptEnable(GpioInstancePtr, 1);
    XGpio_InterruptEnable(GpioInstancePtr, 2);
    XGpio_InterruptGlobalEnable(GpioInstancePtr);
    XScuGic_Enable(&INTCInst, INTC_GPIO_INTERRUPT_ID);

    return XST_SUCCESS;
}

// Tremolo effect
int apply_tremolo(int sample) {
	// Increase tremolo counter by the rate specified at top
	tremolo_counter += TREMOLO_RATE;
	if (tremolo_counter >= TREMOLO_MAX) {
		tremolo_counter = 0.0f;  // Reset the counter when exceeds max
	}

	// Modulation factor oscillates btwn 0 and 1 (triangle wave behavior)
	float modulation_factor = 1.0f - TREMOLO_DEPTH * fabs((tremolo_counter / TREMOLO_MAX) * 2.0f - 1.0f);

	return (int)(sample * modulation_factor);
}

// Reverb effect
int apply_reverb(int sample) {
    int delayed_sample = reverb_buffer[reverb_index]; 				 // Get delayed sample
    int new_sample = sample + (int)(delayed_sample * REVERB_DECAY);  // Apply decay
    reverb_buffer[reverb_index] = sample;  							 // Store current sample for future use
    reverb_index = (reverb_index + 1) % REVERB_DELAY;  				 // Loop buffer
    return new_sample;
}

// Approximate tanh() function for smooth soft clipping
int soft_clip(int sample) {
    if (sample > DISTORTION_THRESHOLD) {
        return DISTORTION_THRESHOLD + (sample - DISTORTION_THRESHOLD) / 5; // Smoother transition
    }
    if (sample < -DISTORTION_THRESHOLD) {
        return -DISTORTION_THRESHOLD + (sample + DISTORTION_THRESHOLD) / 5;
    }
    return sample;
}

int apply_distortion(int sample) {
    sample *= DISTORTION_GAIN;
    sample = soft_clip(sample);  // Apply soft clipping to sample

    // Apply LPF to get rid of sharp noise from earlier
    int smoothed_sample = (int)((1.0f - SMOOTHING_FACTOR) * sample + SMOOTHING_FACTOR * prev_sample);
    prev_sample = smoothed_sample;

    return smoothed_sample;
}

int apply_flanger(int sample) {
    // Triangle LFO
    flanger_lfo += FLANGER_RATE * lfo_direction;
    if (flanger_lfo >= FLANGER_DEPTH || flanger_lfo <= 0) {
        lfo_direction = -lfo_direction;  // Change direction
    }

    int delay_offset = flanger_lfo;  // Use LFO value as delay time
    int delayed_index = (flanger_index - delay_offset + FLANGER_DELAY) % FLANGER_DELAY;
    int delayed_sample = flanger_buffer[delayed_index];

    int new_sample = (sample + delayed_sample) / 2;  // Mix original with delayed signal

    flanger_buffer[flanger_index] = sample;  // Store current sample in buffer
    flanger_index = (flanger_index + 1) % FLANGER_DELAY;  // Loop buffer

    return new_sample;
}

// Calling distortion block address for HW distortion to apply
int hw_distortion(int sample) {
	sample = sample / 2;
	Xil_Out32(EFFECT_BASE_ADDR, sample); // write the sample to the hardware block
	return Xil_In32(EFFECT_BASE_ADDR);
}

void generate_white_noise(int *audio_sample, int noise_magnitude) {
    // Read the pseudo-random number from the LFSR hardware
	uint32_t  random_value = Xil_In32(LFSR_BASE_ADDR + LFSR_REG_OFFSET);

    int noise = (random_value % noise_magnitude) - (noise_magnitude / 2);  // Centered around 0

    // Add the generated noise to the audio sample
    *audio_sample += (noise*5000);
}

// Plays main sample and enables all effects to work!
void play_audio() {
    xil_printf("Playing sample from memory...\r\n");
    playing = 1;
    PLAYING_R = 1;
    int i = 0;

    while (playing) {
        while (paused) {
            // Stay in this loop until unpaused
            usleep(500);  // Prevent CPU overuse
        }

        audio_sample = song[i]*150;

        // Audio effects
        if (tremolo_flag) {
			audio_sample = apply_tremolo(audio_sample);
		}
		if (reverb_flag) {
			audio_sample = apply_reverb(audio_sample);
		}
		if (flanger_flag) {
			// Audio_sample = apply_flanger(audio_sample);
			audio_sample = hw_distortion(audio_sample);
		}
		if (distortion_flag) {
			audio_sample = apply_distortion(audio_sample);
		}

		// Sounds playing on top of sample
        if (drum_flag && j_drum < NUM_SAMPLES_DRUM) {
           audio_sample += drum[j_drum] * 300; // Make drum sound loud enough
        	 j_drum++;  // move sample forward
         }
        if (snare_flag && j_snare < NUM_SAMPLES_SNARE) {
			audio_sample += snare[j_snare] * 100;
			j_snare++;
		}
        if (clap_flag && j_clap < NUM_SAMPLES_CLAP) {
			audio_sample += clap[j_clap] * 100;
			j_clap++;
		}
        if (kickhard_flag && j_kick < NUM_SAMPLES_KICKHARD) {
			audio_sample += kickhard[j_kick] * 100;
			j_kick++;
		}
        if (hihat_flag && j_hihat < NUM_SAMPLES_HIHAT) {
			audio_sample += hihat[j_hihat] * 100;
			j_hihat++;
		}

        // White noise from LFSR HW block
        if(white_noise){
		generate_white_noise(&audio_sample, 100);
	    }

        AUDIO_SAMPLE_READY = 1;  // Flag to signal new data is ready
        // Write to the global signal for dual core connection
        AUDIO_SAMPLE_CURRENT_MOMENT = audio_sample;
        Xil_Out32(I2S_DATA_TX_L_REG, audio_sample); // Send left channel
        AUDIO_SAMPLE_READY = 0;  // Flag to signal new data is ready
        Xil_Out32(I2S_DATA_TX_R_REG, audio_sample);  // Send right channel

        // Rewind
        if(!rewind_flag){
        	i++; // Move to the next left sample for the next iteration
        } else {
        	if(i <= 0){
        		i = 0;
        	} else{
        		i--; // Decrement
        	}
        }

        COMM_VAL = i;

        // Delay - ensuring sample is played at the correct speed
		for(int d=0;d<delay_us;d++){
			asm("NOP");
		}

		if (i >= NUM_SAMPLES) {
			// To loop
			if(loop_flag){
				 xil_printf("Looping\n");
				 i = 0;  // Reset index to loop through samples
			} else{
				playing = 0; // Exit
			}
		}

		// Turn off flag once the effect has completed
		if (j_drum >= NUM_SAMPLES_DRUM) {
			drum_flag = 0;
		}
		if (j_snare >= NUM_SAMPLES_SNARE) {
			snare_flag = 0;
		}
		if (j_clap >= NUM_SAMPLES_CLAP) {
			clap_flag = 0;
		}
		if (j_kick >= NUM_SAMPLES_KICKHARD) {
			kickhard_flag = 0;
		}
		if (j_hihat >= NUM_SAMPLES_HIHAT) {
			hihat_flag = 0;
		}
    }

    xil_printf("Playback stopped.\r\n");
    AUDIO_SAMPLE_CURRENT_MOMENT = 0;

    // Reset all flags to 0
    play_flag = 0;
    drum_flag = 0;
    snare_flag = 0;
    clap_flag = 0;
    kickhard_flag = 0;
    hihat_flag = 0;
    PLAYING_R = 0;
    reverb_flag = 0;
    tremolo_flag = 0;
}

// Test - play snare individually
void play_snare() {
	xil_printf("Playing snare sample from memory...\r\n");
	playing_snare = 1;
	int i = 0;

	while (playing_snare) {
		while (paused) {
			// Stay in this loop until unpaused
			usleep(500);  // Prevent CPU overuse
		}

		AUDIO_SAMPLE_CURRENT_MOMENT = snare[i];
		Xil_Out32(I2S_DATA_TX_L_REG, snare[i]*100);  // Send left channel
		Xil_Out32(I2S_DATA_TX_R_REG, snare[i]*100);  // Send right channel

		i++; // Move to the next left sample for the next iteration

		for(int j=0;j<delay_us;j++){
			asm("NOP");
		}

		if (i >= NUM_SAMPLES_SNARE) {
			playing_snare = 0;
		}
	}
	xil_printf("Snare effect complete.\r\n");
	snare_flag = 0;
}

// Menu - loops through this and checks when sample or sounds are played
void menu() {
    while (1) {
    	if (snare_flag) {
			play_snare();
		}
        if (play_flag) {
            play_audio();
        }
        if (pause_flag) {
            paused = !paused;
            xil_printf("Audio %s.\r\n", paused ? "Paused" : "Resumed");
        }
    }
}

int main()
{
    init_platform();
    COMM_VAL = 0;

    // Disable cache on OCM
    // S=b1 TEX=b100 AP=b11, Domain=b1111, C=b0, B=b0
    Xil_SetTlbAttributes(0xFFFF0000,0x14de2);

    xil_printf("ARM0: writing startaddress for ARM1\n\r");
    Xil_Out32(ARM1_STARTADR, ARM1_BASEADDR);
    dmb(); // Waits until write has finished

    print("ARM0: sending the SEV to wake up ARM1\n\r");
    sev();

    int status;

	status = XGpio_Initialize(&BTNInst, BTNS_DEVICE_ID);
	if (status != XST_SUCCESS) return XST_FAILURE;

	XGpio_SetDataDirection(&BTNInst, 1, 0xFF);
	XGpio_SetDataDirection(&BTNInst, 2, 0xFF);

	status = IntcInitFunction(INTC_DEVICE_ID, &BTNInst);
	if (status != XST_SUCCESS) return XST_FAILURE;

	xil_printf("Initializing audio system...\r\n");
	IicConfig(XPAR_XIICPS_0_DEVICE_ID);
	AudioPllConfig();
	AudioConfigureJacks();
	xil_printf("Audio system ready.\r\n");

	menu();

    cleanup_platform();
    return 0;
}
