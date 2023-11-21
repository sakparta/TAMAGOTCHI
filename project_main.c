/* C Standard library */
#include <stdio.h>


#include <string.h>
#include <math.h>
/* XDCtools files */
#include <xdc/std.h>
#include <xdc/runtime/System.h>

/* BIOS Header files */
#include <ti/sysbios/BIOS.h>
#include <ti/sysbios/knl/Clock.h>
#include <ti/sysbios/knl/Task.h>
#include <ti/drivers/PIN.h>
#include <ti/drivers/pin/PINCC26XX.h>
#include <ti/drivers/I2C.h>
#include <ti/drivers/i2c/I2CCC26XX.h>
#include <ti/drivers/Power.h>
#include <ti/drivers/power/PowerCC26XX.h>
#include <ti/drivers/UART.h>
#include <ti/drivers/PIN.h>
#include <ti/drivers/pin/PINCC26XX.h>

/* Board Header files */
#include "Board.h"
#include "sensors/mpu9250.h"

/* Board Header files */
#include "sensors/buzzer.h"

/* Task */
#define STACKSIZE 2048
Char sensorTaskStack[STACKSIZE];
Char uartTaskStack[STACKSIZE];
Char buzzerTaskStack[STACKSIZE];

static PIN_Handle hBuzzer;
static PIN_State sBuzzer;
PIN_Config cBuzzer[] = {
  Board_BUZZER | PIN_GPIO_OUTPUT_EN | PIN_GPIO_LOW | PIN_PUSHPULL | PIN_DRVSTR_MAX,
  PIN_TERMINATE
};


// Tilakoneen esittely

enum state { WAITING=1, DATA_READY, BUZZER, MENU};
enum state programState = WAITING;


//viesti
char viesti[30];
//messages
char message[30];

// JTKJ: Teht�v� 1. Lis�� painonappien RTOS-muuttujat ja alustus
// JTKJ: Exercise 1. Add pins RTOS-variables and configuration here

// MPU power pin global variables
static PIN_Handle hMpuPin;
static PIN_State  MpuPinState;

//Ledit
static PIN_Handle ledHandle;
static PIN_State ledState;


// MPU power pin
static PIN_Config MpuPinConfig[] = {
    Board_MPU_POWER  | PIN_GPIO_OUTPUT_EN | PIN_GPIO_HIGH | PIN_PUSHPULL | PIN_DRVSTR_MAX,
    PIN_TERMINATE
};

// MPU uses its own I2C interface
static const I2CCC26XX_I2CPinCfg i2cMPUCfg = {
    .pinSDA = Board_I2C0_SDA1,
    .pinSCL = Board_I2C0_SCL1
};


// Vakio Board_LED0 vastaa toista lediä

PIN_Config ledConfig[] = {
   Board_LED0 | PIN_GPIO_OUTPUT_EN | PIN_GPIO_LOW | PIN_PUSHPULL | PIN_DRVSTR_MAX,
   PIN_TERMINATE // Asetustaulukko lopetetaan aina tällä vakiolla
};
char uartBuffer[80]; // Vastaanottopuskuri

// Käsittelijäfunktio
static void uartFxn(UART_Handle handle, void *rxBuf, size_t len) {


   char id[5] = "";

   int checker = 0;


   int i=0;
   while(i<4){
       id[i] = uartBuffer[i];
       i++;
   }
   //Katsotaan, että tamakotchin joku arvo menee alle 2
   if(strstr(rxBuf, "food") || strstr(rxBuf, "scratch") || strstr(rxBuf, "wellbeing")){
       checker = 1;
   }
   //jos id on 3420 ja tamakotchin joku arvo on alle 2 niin laitetaan tilakoneen arvo buzzer
   if(strcmp(id,"3420") == 0 && checker == 1){
       programState = BUZZER;
   }



   // Käsittelijän viimeisenä asiana siirrytään odottamaan uutta keskeytystä..
   UART_read(handle, rxBuf, 80);
}

void ledOn() {

    uint_t pinValue = PIN_getOutputValue( Board_LED0 );
    pinValue = 1;
    PIN_setOutputValue( ledHandle, Board_LED0, pinValue);
    Task_sleep(1000000 / Clock_tickPeriod);
    pinValue = 0;
    PIN_setOutputValue( ledHandle, Board_LED0, pinValue);
}

/* Task Functions */
Void uartTaskFxn(UArg arg0, UArg arg1) {

    char merkkijono[30];

    //UARTin alustus: 9600,8n1

    UART_Handle uart;
    UART_Params uartParams;

    UART_Params_init(&uartParams);
    uartParams.writeDataMode = UART_DATA_TEXT;
    uartParams.readDataMode = UART_DATA_TEXT;
    uartParams.readEcho = UART_ECHO_OFF;
    uartParams.readMode=UART_MODE_CALLBACK;
    uartParams.readCallback  = &uartFxn; // Käsittelijäfunktio
    uartParams.baudRate = 9600; // nopeus 9600baud
    uartParams.dataLength = UART_LEN_8; // 8
    uartParams.parityType = UART_PAR_NONE; // n
    uartParams.stopBits = UART_STOP_ONE; // 1

    uart = UART_open(Board_UART0, &uartParams);
      if (uart == NULL) {
         System_abort("Error opening the UART");
      }

    UART_read(uart, uartBuffer, 80);
    while (1) {

        // JTKJ: Teht�v� 3. Kun tila on oikea, tulosta sensoridata merkkijonossa debug-ikkunaan
        //       Muista tilamuutos
        // JTKJ: Exercise 3. Print out sensor data as string to debug window if the state is correct
        //       Remember to modify state
       if (programState == DATA_READY) {
           sprintf(merkkijono,"%s", viesti);
           UART_write(uart, merkkijono, strlen(merkkijono)+1);
           sprintf(merkkijono,"%s", message);
           UART_write(uart, merkkijono, strlen(merkkijono)+1);
           ledOn();
           programState = WAITING;
        }

        // JTKJ: Teht�v� 4. L�het� sama merkkijono UARTilla
        // JTKJ: Exercise 4. Send the same sensor data string with UART




        // Once per second, you can modify this
        Task_sleep(1000000 / Clock_tickPeriod);
    }
}

Void sensorTaskFxn(UArg arg0, UArg arg1) {

    float ax, ay, az, gx, gy, gz;

    I2C_Handle i2cMPU; // Own i2c-interface for MPU9250 sensor
    I2C_Params i2cMPUParams;

    I2C_Params_init(&i2cMPUParams);
    i2cMPUParams.bitRate = I2C_400kHz;
    // Note the different configuration below
    i2cMPUParams.custom = (uintptr_t)&i2cMPUCfg;
    // MPU power on
    PIN_setOutputValue(hMpuPin,Board_MPU_POWER, Board_MPU_POWER_ON);

     // Wait 100ms for the MPU sensor to power up
    Task_sleep(100000 / Clock_tickPeriod);
    System_printf("MPU9250: Power ON\n");
    System_flush();

    // MPU open i2c
    i2cMPU = I2C_open(Board_I2C, &i2cMPUParams);
    if (i2cMPU == NULL) {
       System_abort("Error Initializing I2CMPU\n");
    }

      // MPU setup and calibration
    System_printf("MPU9250: Setup and calibration...\n");
    System_flush();

    mpu9250_setup(&i2cMPU);

    System_printf("MPU9250: Setup and calibration OK\n");
    System_flush();

    while (1) {

        // JTKJ: Teht�v� 2. Lue sensorilta dataa ja tulosta se Debug-ikkunaan merkkijonona
        // JTKJ: Exercise 2. Read sensor data and print it to the Debug window as string

       /* double valoisuus = opt3001_get_data(&i2c);

        sprintf(merkkijono,"%f\n",valoisuus);
        System_printf(merkkijono);*/


        // JTKJ: Teht�v� 3. Tallenna mittausarvo globaaliin muuttujaan
        //       Muista tilamuutos
        if(programState == WAITING){
            mpu9250_get_data(&i2cMPU, &ax, &ay, &az, &gx, &gy, &gz);
            float acc_xyVector = sqrt(ax * ax + ay * ay);
            //sprintf(merkkijono,"%f\n", acc_xyVector);
            //System_printf(merkkijono);
            if (acc_xyVector > 1 && az < 1){
                strcpy(viesti, "id:3420,EAT:2");
                strcpy(message, "id:3420,MSG1:Hyvaa!,MSG2:Food level + 2");
                programState = DATA_READY;
            }
            if (fabs(az) > 1.5 && ay < 1 && ax < 1){
                strcpy(viesti, "id:3420,EXERCISE:2");
                strcpy(message, "id:3420,MSG1:Terveellista,MSG2:Fitness level + 2");
                programState = DATA_READY;
            }
            if (gx > 100 && gy < 100 && gz < 100){
                strcpy(viesti, "id:3420,PET:2");
                strcpy(message, "id:3420,MSG1:Naurattaahan se!,MSG2:Happiness level + 2");
                programState = DATA_READY;
            }
        }

        // JTKJ: Exercise 3. Save the sensor value into the global variable
        //       Remember to modify state

        // Just for sanity check for exercise, you can comment this out



        // Once per second, you can modify this
        Task_sleep(1000000 / Clock_tickPeriod);
    }

}
Void buzzerTaskFxn(UArg arg0, UArg arg1){
    while(1){
        if(programState == BUZZER){

            buzzerOpen(hBuzzer);
            buzzerSetFrequency(2637); //E
            Task_sleep(333333 / Clock_tickPeriod); //eight note
            buzzerSetFrequency(2350); //D
            Task_sleep(333333 / Clock_tickPeriod); //eight note
            buzzerSetFrequency(1480); //F#
            Task_sleep(166667 / Clock_tickPeriod); //quarter note
            buzzerSetFrequency(1661); //G#
            Task_sleep(166667 / Clock_tickPeriod); //quarter note
            buzzerSetFrequency(2217); //C#
            Task_sleep(333333 / Clock_tickPeriod); //eight note
            buzzerSetFrequency(1975); //B
            Task_sleep(333333 / Clock_tickPeriod); //eight note
            buzzerSetFrequency(1175); //D
            Task_sleep(166667 / Clock_tickPeriod); //quarter note
            buzzerSetFrequency(1318); //E
            Task_sleep(166667 / Clock_tickPeriod); //quarter note
            buzzerSetFrequency(1975); //B
            Task_sleep(333333 / Clock_tickPeriod); //eight note
            buzzerSetFrequency(1760); //A
            Task_sleep(333333 / Clock_tickPeriod); //eight note
            buzzerSetFrequency(1108); //C#
            Task_sleep(166667 / Clock_tickPeriod); //quarter note
            buzzerSetFrequency(1318); //E
            Task_sleep(166667 / Clock_tickPeriod); //quarter note
            buzzerSetFrequency(1760); //A
            Task_sleep(666667 / Clock_tickPeriod); //half note
            buzzerClose();
            programState = WAITING;
        }
        Task_sleep(1000000 / Clock_tickPeriod);
    }
}


Int main(void) {

    // Task variables
    Task_Handle sensorTaskHandle;
    Task_Params sensorTaskParams;
    Task_Handle uartTaskHandle;
    Task_Params uartTaskParams;
    Task_Handle buzzerTaskHandle;
    Task_Params buzzerTaskParams;

    // Initialize board
    Board_initGeneral();




    // JTKJ: Teht�v� 2. Ota i2c-v�yl� k�ytt��n ohjelmassa
    // JTKJ: Exercise 2. Initialize i2c bus

    Board_initI2C();

    // JTKJ: Teht�v� 4. Ota UART k�ytt��n ohjelmassa
    // JTKJ: Exercise 4. Initialize UART
    Board_initUART();
    // JTKJ: Teht�v� 1. Ota painonappi ja ledi ohjelman k�ytt��n
    //       Muista rekister�id� keskeytyksen k�sittelij� painonapille
    // JTKJ: Exercise 1. Open the button and led pins
    //       Remember to register the above interrupt handler for button

    // Open MPU power pin
    hMpuPin = PIN_open(&MpuPinState, MpuPinConfig);
    if (hMpuPin == NULL) {
        System_abort("Pin open failed!");
    }

    // Ledi käyttöön ohjelmassa
    ledHandle = PIN_open( &ledState, ledConfig );
    if(!ledHandle) {
       System_abort("Error initializing LED pin\n");
    }
    // Buzzer
    hBuzzer = PIN_open(&sBuzzer, cBuzzer);
    if (hBuzzer == NULL) {
        System_abort("Pin open failed!");
   }


    /* Task */
    Task_Params_init(&sensorTaskParams);
    sensorTaskParams.stackSize = STACKSIZE;
    sensorTaskParams.stack = &sensorTaskStack;
    sensorTaskParams.priority=2;
    sensorTaskHandle = Task_create(sensorTaskFxn, &sensorTaskParams, NULL);
    if (sensorTaskHandle == NULL) {
        System_abort("Task create failed!");
    }

    Task_Params_init(&uartTaskParams);
    uartTaskParams.stackSize = STACKSIZE;
    uartTaskParams.stack = &uartTaskStack;
    uartTaskParams.priority=2;
    uartTaskHandle = Task_create(uartTaskFxn, &uartTaskParams, NULL);
    if (uartTaskHandle == NULL) {
        System_abort("Task create failed!");
    }
    Task_Params_init(&buzzerTaskParams);
    buzzerTaskParams.stackSize = STACKSIZE;
    buzzerTaskParams.stack = &buzzerTaskStack;
    buzzerTaskParams.priority=2;
    buzzerTaskHandle = Task_create(buzzerTaskFxn, &buzzerTaskParams, NULL);
       if (buzzerTaskHandle == NULL) {
           System_abort("Task create failed!");
       }

    /* Sanity check */
    System_printf("Hello world!\n");
    System_flush();

    /* Start BIOS */
    BIOS_start();

    return (0);
}
