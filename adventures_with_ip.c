#include "adventures_with_ip.h"
#include <stdio.h>
#include <sleep.h>
#include "xil_io.h"
#include "xil_mmu.h"
#include "platform.h"
#include "xil_printf.h"
#include "xpseudo_asm.h"
#include "xil_exception.h"

#define sev() __asm__("sev")
#define ARM1_STARTADR 0xFFFFFFF0
#define ARM1_BASEADDR 0x10080000
#define COMM_VAL (*(volatile unsigned long *)(0xFFFF0000))
#define AUDIO_SAMPLE_CURRENT_MOMENT (*(volatile unsigned long *)(0xFFFF0001))
// dow -data C:/Users/tmm12/Desktop/sources/audio_samples/left_list.data 0x018D2008
// dow -data C:/Users/tmm12/Desktop/sources/audio_samples/left_list_drum.data 0x020BB00C
// dow -data C:/Users/tmm12/Desktop/sources/audio_samples/left_list_snare.data 0x028A4010
// dow -data C:/Users/tmm12/Desktop/sources/audio_samples/left_list_clap.data 0x0308D014

// Need to make super long to work for some reason
// So the "record seconds" and playback time isn't fully accurate
#define SAMPLES_PER_SECOND 48000
#define RECORD_SECONDS 35
#define MAX_SAMPLES (SAMPLES_PER_SECOND * RECORD_SECONDS * 15)
int NUM_BYTES_BUFFER = 5242880;
int j = 0; // for drum reset

#include "xscugic.h"
#include "xil_exception.h"
#include "xil_io.h"

// Parameter definitions
#define INTC_DEVICE_ID 		XPAR_PS7_SCUGIC_0_DEVICE_ID
#define TMR_DEVICE_ID		XPAR_TMRCTR_0_DEVICE_ID
#define BTNS_DEVICE_ID		XPAR_AXI_GPIO_1_DEVICE_ID
#define LEDS_DEVICE_ID		XPAR_AXI_GPIO_1_DEVICE_ID
#define INTC_GPIO_INTERRUPT_ID XPAR_FABRIC_AXI_GPIO_1_IP2INTC_IRPT_INTR

#define BTN_INT 			XGPIO_IR_CH1_MASK
#define TMR_LOAD			0xF8000000

XGpio LEDInst, BTNInst;
XScuGic INTCInst;

// static int led_data;
static int btn_value;

// Stereo buffer
static u32 audio_buffer[MAX_SAMPLES * 2];
static int recorded_samples = 0;
static int playing = 0;
static int playing_drum = 0;
static int paused = 0;
static int record_flag = 0;
static int play_flag = 0;
static int pause_flag = 0;
static int drum_flag = 0;
static int snare_flag = 0;
static int clap_flag = 0;

u32 delay_us = 476;

// access in the core possibly
int * song = (int *)0x018D2008;
int NUM_SAMPLES = 1755840;
int * drum = (int *)0x020BB00C;
int NUM_SAMPLES_DRUM = 26880;
int * snare = (int *)0x028A4010;
int NUM_SAMPLES_SNARE = 32256;
int * clap = (int *)0x0308D014;
int NUM_SAMPLES_CLAP = 35712;

// u32 delay_us_drum = 60;

//----------------------------------------------------
// PROTOTYPE FUNCTIONS
//----------------------------------------------------
static void BTN_Intr_Handler(void *baseaddr_p);
static int InterruptSystemSetup(XScuGic *XScuGicInstancePtr);
static int IntcInitFunction(u16 DeviceId, XGpio *GpioInstancePtr);

//----------------------------------------------------
// INTERRUPT HANDLER FUNCTIONS
//----------------------------------------------------

void BTN_Intr_Handler(void *InstancePtr) {
    XGpio_InterruptDisable(&BTNInst, BTN_INT);
    if ((XGpio_InterruptGetStatus(&BTNInst) & BTN_INT) != BTN_INT) {
        return;
    }

    btn_value = XGpio_DiscreteRead(&BTNInst, 1);
    if (btn_value == 8) {
    	// Recording functionality
        // record_flag = 1;

    	// Play/pause functionality
		// paused = !paused;  // Toggle paused directly
		// xil_printf("Audio %s.\r\n", paused ? "Paused" : "Resumed");

    	// Sound testing - have in separate buttons later once switches work
    	//drum_flag = 1;
    	snare_flag = 1;
    	//clap_flag = 1;
    	j=0;
    } else if (btn_value == 4) {
        play_flag = 1;
    } else if (btn_value == 16) {
        delay_us = delay_us + 1;
        xil_printf("Delay (us): %d", delay_us);
    } else if(btn_value == 2){
    	if (delay_us > 1){
    		delay_us = delay_us - 1;
    	}
        xil_printf("Delay (us): %d", delay_us);
    } else if (btn_value == 1){
    	// Center button
    	//COMM_VAL = 1;
    	clap_flag=1
    }

    (void)XGpio_InterruptClear(&BTNInst, BTN_INT);
    XGpio_InterruptEnable(&BTNInst, BTN_INT);
}
//----------------------------------------------------
// INITIAL SETUP FUNCTIONS
//----------------------------------------------------

int InterruptSystemSetup(XScuGic *XScuGicInstancePtr) {
    XGpio_InterruptEnable(&BTNInst, BTN_INT);
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
    XGpio_InterruptGlobalEnable(GpioInstancePtr);
    XScuGic_Enable(&INTCInst, INTC_GPIO_INTERRUPT_ID);

    return XST_SUCCESS;
}

void record_audio() {

    xil_printf("Recording for %d seconds...\r\n", RECORD_SECONDS);
    recorded_samples = 0;

    while (recorded_samples < MAX_SAMPLES && !XUartPs_IsReceiveData(UART_BASEADDR)) {
        audio_buffer[recorded_samples * 2] = Xil_In32(I2S_DATA_RX_L_REG);
        audio_buffer[recorded_samples * 2 + 1] = Xil_In32(I2S_DATA_RX_R_REG);

        recorded_samples++;
    }

    xil_printf("Recording stopped. \r\n");
    record_flag = 0;
}

void play_audio() {
    xil_printf("Playing sample from memory...\r\n");
    playing = 1;
    int i = 0;

    while (playing) {
        while (paused) {
            // Stay in this loop until unpaused
            usleep(500);  // Prevent CPU overuse
        }

        // Milestone 2 stuff: Add drum sound here inside if statement
        // Then add drum effects at that point to the song indices
        int audio_sample = song[i]*5;

        if (drum_flag && j < NUM_SAMPLES_DRUM) {
           audio_sample += drum[j] * 150;  // Simple addition mixing
        	 j++;  // Move drum sample forward
         }
        if (snare_flag && j < NUM_SAMPLES_SNARE) {
			audio_sample += snare[j] * 100;  // Simple addition mixing
			j++;  // Move drum sample forward
		}
        if (clap_flag && j < NUM_SAMPLES_CLAP) {
			audio_sample += clap[j] * 100;  // Simple addition mixing
			j++;  // Move drum sample forward
		}

        // write to the global thing for like dual core connection
        AUDIO_SAMPLE_CURRENT_MOMENT = audio_sample;
        Xil_Out32(I2S_DATA_TX_L_REG, audio_sample);  // Send left channel
        Xil_Out32(I2S_DATA_TX_R_REG, audio_sample);  // Send right channel

        i++; // Move to the next left sample for the next iteration

		for(int d=0;d<delay_us;d++){
			asm("NOP");
		}

		if (i >= NUM_SAMPLES) {
			// To loop
 			// xil_printf("Looping\n");
 			// i = 0;  // Reset index to loop through samples
			// To exit
			playing = 0;
		}

		// Stop drum playback if it ends
		// Can prob get rid of dis
		if (j >= NUM_SAMPLES_DRUM) {
			drum_flag = 0;
			j=0;
		}
    }
    xil_printf("Playback stopped.\r\n");
    play_flag = 0;
    drum_flag = 0;
    snare_flag = 0;
    clap_flag = 0;
}

void play_drum() {
	xil_printf("Playing drum sample from memory...\r\n");
	playing_drum = 1;
	int i = 0;


	while (playing_drum) {
		while (paused) {
			// Stay in this loop until unpaused
			usleep(500);  // Prevent CPU overuse
		}

		Xil_Out32(I2S_DATA_TX_L_REG, drum[i]*100);  // Send left channel
		Xil_Out32(I2S_DATA_TX_R_REG, drum[i]*100);  // Send right channel

		i++; // Move to the next left sample for the next iteration

		for(int j=0;j<delay_us;j++){
			asm("NOP");
		}

		if (i >= NUM_SAMPLES_DRUM) {
			playing_drum = 0;
		}
	}
	xil_printf("Drum effect complete.\r\n");
	drum_flag = 0;
}


void menu() {
    while (1) {
//        if (record_flag) {
//            record_audio();
//        }
    	if (drum_flag) {
    		play_drum();
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
