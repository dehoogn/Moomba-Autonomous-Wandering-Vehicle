// Title: 	Moomba - autonomous wandering vehicle
// Authors:	Neil deHoog, Peimon Dadkhah
// Date: 	November, 2018
// Description: A small vehicle that utilizes RTOS concepts to 
//		autonomously wander around a space. Constructed
//		using TI's SYS/BIOS and the piccolo F28027 processor.

#define xdc__strict // gets rid of #303-D typedef warning regarding Uint16, Uint32
#include <xdc/std.h>
#include <ti/sysbios/BIOS.h>
#include <xdc/runtime/System.h>
#include <ti/sysbios/knl/Swi.h>
#include <ti/sysbios/knl/Task.h>
#include <ti/sysbios/knl/Semaphore.h>
#include <Peripheral_Headers/F2802x_Device.h>

//function prototypes
extern void DeviceInit(void);       // Initializes device peripherals

// Global Constant variables
#define	ZERO	0
#define T2DIS 	4524              	// Value t oconvert time to distance in cm
#define	D2SPEED 834			// Convert distance by # tach ticks to speed
#define MAXPWM	550
#define	MINPWM	200
#define MIDDIST	30
#define	MINDIST	10
#define REVCNT	30
#define	TURNCNT	45

// Global variables (GV) 
volatile UInt FpwmDC1 = 0;          // Pwm duty cycle between ZERO - MAXPWM
volatile UInt FpwmDC2 = 0;          // Pwm duty cycle between ZERO - MAXPWM
volatile UInt RpwmDC = 0;           // Pwm duty cycle between ZERO - MAXPWM

volatile UInt carState = 0;         // status of car ==> 0=Stopped,  1=Slow,  2=Fast,  3=Turning

//=======GVs for UltraSonic Sensor============
volatile UInt range = 0;            	// Range from ultrasonic sensor in cm
volatile UInt riseE = 0;            	// Rising edge detector of ultrasonic echo
volatile UInt trig = 0;             	// Tracks triggering of the ultrasonic sensor
volatile ULong uStartT = 0;       	// Start of ultrasonic echo pulse
volatile ULong uStopT = 0;       	// End of ultrasonic echo pulse
volatile long uPulseT = 0;        	// Total time of ultrasonic echo pulse in timer counts
volatile ULong period = 0;		// time period for Ultrasonic range sensor to calculate distance
volatile ULong dist = 0;		// distance in cm to an object in front

//=======GVs for tachometer Sensors============
volatile ULong tGetSpeed = 0;       	// Count number of 6mS interrupts and get speed after 5 interrupts
volatile UInt tachCntr1 = 0;		// number of tachometer interrupts in 30mS
volatile UInt speed1 = 0;		// Speed of motors on side 1
volatile UInt tachCntr2 = 0;		// number of tachometer interrupts in 0.3 seconds
volatile UInt speed2 = 0;		// Speed of motors on side 2

//=======Semaphores declarations============
extern const Semaphore_Handle ReverseTsk;
extern const Semaphore_Handle ForwardTsk;
extern const Swi_Handle SpeedSWI;
//==============================================================================

/*========================================================
 * Main function initializes everything and then runs
 * the SYS/BIOS
 ========================================================*/
void main(void)

{
    DeviceInit();                           //Initialize peripherals
    CpuTimer0Regs.TCR.bit.TIE = 1;          // Enable timer0 ISR
    System_printf("Initialized\n");         //Confirm initialization occurred
    BIOS_start(); // Initiate real time operating system, does not return
}

//==============================================================================

/*========================================================
 *ReverseTskFxn is priority 2 and backs up the car for REVCNT
 * distance then turn for (TURNCNT-REVCNT) distance. Then hands the car 
 * back to ForwardTskFxn & Pends.
 ========================================================*/
Void ReverseTskFxn(Void)
{
    for(;;){
        GpioDataRegs.GPASET.bit.GPIO0 = 1;	// set GPIO 1 to High to get CPU utilization
		
		//First back up the car for REVCNT/20 revelotion
        if (tachCntr1 < REVCNT && carState==3 ){ 
            FpwmDC2 = FpwmDC1 = ZERO;
            RpwmDC = MINPWM;
            GpioDataRegs.GPASET.bit.GPIO12 = 1;
            GpioDataRegs.GPASET.bit.GPIO19 = 1;
            EPwm2Regs.CMPA.half.CMPA = FpwmDC1;  		//Forward speed set for motors1
            EPwm3Regs.CMPA.half.CMPA = FpwmDC2;			//Forward speed set for motors2
            EPwm2Regs.CMPB = RpwmDC;				//Reverse speed set for motors1
            EPwm3Regs.CMPB = RpwmDC;				//Reverse speed set for motors2
        }
		//Then turn the car left for (TURNCNT-REVCNT)/20 revelotion
        else if (tachCntr1 < TURNCNT && carState==3){
            FpwmDC1 = MINPWM+50;
            FpwmDC2 = ZERO;
            RpwmDC = MINPWM+50;
            GpioDataRegs.GPASET.bit.GPIO12 = 1;
            GpioDataRegs.GPASET.bit.GPIO19 = 1;
			EPwm2Regs.CMPA.half.CMPA = FpwmDC1;  	//Forward speed set for motors1
            EPwm3Regs.CMPA.half.CMPA = FpwmDC2;			//Forward speed set for motors2
            EPwm2Regs.CMPB = FpwmDC2;				//Reverse speed set for motors1
            EPwm3Regs.CMPB = FpwmDC1;				//Reverse speed set for motors2

        }
		// when done stop the car and change car state to 1. post ForwardTsk and pend  ReverseTsk
        else{
            FpwmDC2 = FpwmDC1 = ZERO;
            RpwmDC = ZERO;
            GpioDataRegs.GPACLEAR.bit.GPIO12 = 1;
            GpioDataRegs.GPACLEAR.bit.GPIO19 = 1;
			EPwm2Regs.CMPA.half.CMPA = FpwmDC1;  	//Forward speed set for motors1
            EPwm3Regs.CMPA.half.CMPA = FpwmDC2;			//Forward speed set for motors2
            EPwm2Regs.CMPB = FpwmDC2;				//Reverse speed set for motors1
            EPwm3Regs.CMPB = FpwmDC1;				//Reverse speed set for motors2

			tachCntr1 = 0;
			tachCntr2 = 0;
            carState = 2;
            Semaphore_post(ForwardTsk);             // Post ForwardTsk task
            Semaphore_pend(ReverseTsk, BIOS_WAIT_FOREVER);
        }
    }
}

/*========================================================
 *ForwardTskFxn is priority 1 and sets the car speed based
 * on distance to obsticle posts ReverseTskFxn when closer
 * that MINDIST cm to wall. This task gets pended everytime
 ========================================================*/
Void ForwardTskFxn(Void)
{
    for(;;){
        GpioDataRegs.GPASET.bit.GPIO0 = 1;	// set GPIO 1 to High to get CPU utilization
	
	// when distannce less than MINDIST stop car and post ReverseTsk
        if (dist < MINDIST && dist!= 1){ //NOTE: dist == 1 is special case of out of range
            FpwmDC1 = FpwmDC2 = ZERO;
            RpwmDC = ZERO;
            GpioDataRegs.GPACLEAR.bit.GPIO12 = 1;
            GpioDataRegs.GPACLEAR.bit.GPIO19 = 1;
			EPwm2Regs.CMPA.half.CMPA = FpwmDC1;  	//Forward speed set for motors1
            EPwm3Regs.CMPA.half.CMPA = FpwmDC2;			//Forward speed set for motors2
            EPwm2Regs.CMPB = FpwmDC2;				//Reverse speed set for motors1
            EPwm3Regs.CMPB = FpwmDC1;				//Reverse speed set for motors2
            carState = 3;
            Semaphore_post(ReverseTsk);				// Post Reverse task
        }
	    
	// When distance b/w MIDDIST and MINDIST change speed dynamicly slowing down
        else if (dist > MINDIST && dist < MIDDIST){
            FpwmDC2 = FpwmDC1 = (MAXPWM-MINPWM)*(((dist-MINDIST)*100)/(MIDDIST-MINDIST))/100 + MINPWM;
            RpwmDC = ZERO;
   	    EPwm2Regs.CMPA.half.CMPA = FpwmDC1;  		//Forward speed set for motors1
            EPwm3Regs.CMPA.half.CMPA = FpwmDC2;			//Forward speed set for motors2
            EPwm2Regs.CMPB = FpwmDC2;				//Reverse speed set for motors1
            EPwm3Regs.CMPB = FpwmDC1;				//Reverse speed set for motors2
            GpioDataRegs.GPASET.bit.GPIO12 = 1;
            GpioDataRegs.GPASET.bit.GPIO19 = 1;
            carState = 1;
        }
	 
	//When distance greater than MINDIST ensure the car is going straight even when 
	// there is friction on one side 
        else if(dist >= MIDDIST || dist == 1){ //NOTE: dist == 1 is special case of out of range
           
		   if (speed1 >= (speed2+100)){
               FpwmDC1 = MAXPWM - 50;
               FpwmDC2 = MAXPWM;
           }
           else if (speed2 >= (speed1+100)){
               FpwmDC1 = MAXPWM;
               FpwmDC2 = MAXPWM - 50;
           }
           else
               FpwmDC1 = FpwmDC2 = MAXPWM;
            RpwmDC = ZERO;
   			EPwm2Regs.CMPA.half.CMPA = FpwmDC1;  	//Forward speed set for motors1
            EPwm3Regs.CMPA.half.CMPA = FpwmDC2;			//Forward speed set for motors2
            EPwm2Regs.CMPB = FpwmDC2;				//Reverse speed set for motors1
            EPwm3Regs.CMPB = FpwmDC1;				//Reverse speed set for motors2
            GpioDataRegs.GPASET.bit.GPIO12 = 1;
            GpioDataRegs.GPASET.bit.GPIO19 = 1;
            carState = 2;
        }
        Semaphore_pend(ForwardTsk, BIOS_WAIT_FOREVER);	//pend task everytime (this is for easy measurement of CPU utilization
    }
}


/*========================================================
 * Timer 0 and 1 create a 10us pulse every 60ms to activate
 * the ultraSonic sensor, and wait for the response to be
 * measured
 * Timer 0 is only 10us for the pulse
 * Timer 1 is 60ms to wait for the UltraSonic response and
 * Get the speed of the wheels
 ========================================================*/
void timer0Fxn(void)
{
	GpioDataRegs.GPASET.bit.GPIO0 = 1;	// set GPIO 1 to High to get CPU utilization
	
	//make a 10us trigger and then interrupt is disabled
    	if (!trig){
        	trig++;
        	GpioDataRegs.GPASET.bit.GPIO6 = 1;      // Set Trigger Pin
        	Semaphore_post(ForwardTsk);
    	}
    	else if (trig == 1){
        	GpioDataRegs.GPACLEAR.bit.GPIO6 = 1;    // Clear Trigger Pin
        	XIntruptRegs.XINT1CR.bit.ENABLE = 1;    // Enable Peripheral Interrupt 1
        	CpuTimer0Regs.TCR.bit.TIE = 0;          // Disable timer0 ISR
        	trig++;
    	}
}


/*========================================================
* 1 period of 60ms interrupt which pulses the ultraSonic sensor and counts 
* the time for distance traveled
 ========================================================*/
void timer1Fxn(void)
{
	GpioDataRegs.GPASET.bit.GPIO0 = 1;	// set GPIO 1 to High to get CPU utilization
    	trig = 0;                               // Reset Trigger tracker
    	riseE = 0;                            	// Clear rising edge count
    	CpuTimer0Regs.TCR.bit.TIE = 1;          // Enable timer0 ISR
    	XIntruptRegs.XINT1CR.bit.ENABLE = 0;    // Disable Peripheral Interrupt 1
	
	// counts to 5 before speed is calculated (get speed every 0.3 seconds)
    	tGetSpeed++; 
}


/*========================================================
 * UltraSonicHWIFxn uses an external interrupt to find the
 * time elapsed between the rising edge and falling edge
 * this time elapsed then gives us the distance to obsticle.
 ========================================================*/
void ultraSonicHWIFxn(void)
{
	GpioDataRegs.GPASET.bit.GPIO0 = 1;	// set GPIO 1 to High to get CPU utilization
    	if (riseE == 0){ //at rising edge get cpu time
        	riseE++;
        	uStartT = ReadCpuTimer1Counter();
        	period = ReadCpuTimer1Period();
    	}

	//at falling edge get cpu time and find elapsed time
    	else if (riseE == 1){ /
        	uStopT = ReadCpuTimer1Counter();
        	uPulseT = (uStartT - uStopT);
        	XIntruptRegs.XINT1CR.bit.ENABLE = 0;    // Disable Peripheral Interrupt 1
        	riseE++;
        	Swi_post(SpeedSWI);		//post SWI semaphor to get car speed and obsticle distance
    	}
}


/*========================================================
 * Increments the tachometer counter 1 at negative edge. 
 * Then calcualte distance traveled & car speed 
 ========================================================*/
void Tach1HWIFxn(void)
{
	GpioDataRegs.GPASET.bit.GPIO0 = 1;	// set GPIO 1 to High to get CPU utilization
    	tachCntr1++;
}


/*========================================================
 * Increments the tachometer counter 2 at negative edge. 
 * Then calcualte distance traveled & car speed 
 ========================================================*/
void Tach2HWIFxn(void)
{
	GpioDataRegs.GPASET.bit.GPIO0 = 1;	// set GPIO 1 to High to get CPU utilization
    	tachCntr2++;
}


/*========================================================
 *Speed SWI gets the speed of travel and distance to object
 ========================================================*/
void SpeedSWIFxn(UArg arg)
{
	GpioDataRegs.GPASET.bit.GPIO0 = 1;	// set GPIO 1 to High to get CPU utilization
	
	// get speed from distance traveled every 0.3 seconds
    	if (tGetSpeed>4 && carState!=3){	//get speed every 0.3 seconds
        	tGetSpeed = ZERO;
        	speed1 = tachCntr1 * D2SPEED;
        	tachCntr1 = ZERO;
		speed2 = tachCntr2 * D2SPEED;;
        	tachCntr2 = ZERO;
    	}
	
	// Calculate the distance and update the register
	dist = uPulseT/(T2DIS);
}


void myIdleFxn(void)
{
	GpioDataRegs.GPACLEAR.bit.GPIO0 = 1;	// set GPIO 1 to Low to get CPU utilization
}
