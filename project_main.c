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

//sensors
#include "sensors/mpu9250.h"
#include "sensors/buzzer.h"
#include "sensors/opt3001.h"

/* Task */
#define STACKSIZE 2048
Char sensorTaskStack[STACKSIZE];
Char uartTaskStack[STACKSIZE];
Char buzzerTaskStack[STACKSIZE];

static PIN_Handle hBuzzer;
static PIN_State sBuzzer;

//buzzer config
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

//Buzzer sound
int sound = 0;



// MPU power pin global variables
static PIN_Handle hMpuPin;
static PIN_State  MpuPinState;

//napit
static PIN_Handle buttonHandle;
static PIN_State buttonState;

//Ledit
static PIN_Handle ledHandle;
static PIN_State ledState;


// Vakio BOARD_BUTTON_0 vastaa toista painonappia
PIN_Config buttonConfig[] = {
   Board_BUTTON0  | PIN_INPUT_EN | PIN_PULLUP | PIN_IRQ_NEGEDGE,
   PIN_TERMINATE // Asetustaulukko lopetetaan aina tällä vakiolla
};

//punainen ledi
PIN_Config ledConfigRed[] = {
   Board_LED1 | PIN_GPIO_OUTPUT_EN | PIN_GPIO_LOW | PIN_PUSHPULL | PIN_DRVSTR_MAX,
   PIN_TERMINATE
};

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
   //haetaan 4 ensinmäistä merkkiä (id)
   while(i<4){
       id[i] = uartBuffer[i];
       i++;
   }
   //Katsotaan, että tamakotchin joku arvo menee alle 2
   if(strstr(rxBuf, "food") || strstr(rxBuf, "scratch") || strstr(rxBuf, "wellbeing")){
       checker = 1;
       sound = 1;
   }
   //jos lemmikki karkaa soitetaan mario
   if(strstr(rxBuf, "Too late")){
          checker = 1;
          sound = 2;
      }
   //jos id on 3420 ja tamakotchin joku arvo on alle 2 niin laitetaan tilakoneen arvo buzzer
   if(strcmp(id,"3420") == 0 && checker == 1){
       programState = BUZZER;
   }



   // Käsittelijän viimeisenä asiana siirrytään odottamaan uutta keskeytystä..
   UART_read(handle, rxBuf, 80);
}

void ledOn() {
    //vaihdetaan vihreä ledit vähäksi aikaan päälle
    uint_t pinValue = PIN_getOutputValue( Board_LED0 );
    pinValue = 1;
    PIN_setOutputValue( ledHandle, Board_LED0, pinValue);
    Task_sleep(1000000 / Clock_tickPeriod);
    pinValue = 0;
    PIN_setOutputValue( ledHandle, Board_LED0, pinValue);
}

// Napinpainalluksen keskeytyksen käsittelijäfunktio
void buttonFxn(PIN_Handle handle, PIN_Id pinId) {
   if(programState == WAITING){
       // Vaihdetaan led-pinnin tila päälle
       uint_t pinValue = PIN_getOutputValue( Board_LED1 );
       pinValue = 1;
       PIN_setOutputValue( ledHandle, Board_LED1, pinValue );
       programState = MENU;
   }else if(programState == MENU){
        // Vaihdetaan led-pinni pois päältä
        uint_t pinValue = PIN_getOutputValue( Board_LED1 );
        pinValue = 0;
        PIN_setOutputValue( ledHandle, Board_LED1, pinValue );
        programState = WAITING;
    }
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

        // Kun tila on oikea, lähetetään viesti backendille ja laitetaan ledi päälle
       if (programState == DATA_READY) {
           sprintf(merkkijono,"%s", viesti);
           UART_write(uart, merkkijono, strlen(merkkijono)+1);
           sprintf(merkkijono,"%s", message);
           UART_write(uart, merkkijono, strlen(merkkijono)+1);
           ledOn();
           programState = WAITING;
        }

        // Once per second, you can modify this
        Task_sleep(1000000 / Clock_tickPeriod);
    }
}

Void sensorTaskFxn(UArg arg0, UArg arg1) {

    float ax, ay, az, gx, gy, gz;



    I2C_Handle i2cMPU; // Own i2c-interface for MPU9250 sensor
    I2C_Params i2cMPUParams;





    //mpu9250
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


        if(programState == WAITING){


            mpu9250_get_data(&i2cMPU, &ax, &ay, &az, &gx, &gy, &gz);
            float acc_xyVector = sqrt(ax * ax + ay * ay);


            //check if sensortag is moving
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


        // Once per second, you can modify this
        Task_sleep(1000000 / Clock_tickPeriod);
    }

}
//buzzer sounds
Void buzzerTaskFxn(UArg arg0, UArg arg1){
    while(1){
        if(programState == BUZZER && sound == 1){
            //Nokia tune
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
        }else if(programState == BUZZER && sound == 2){
            //pirkka mario death sound
            buzzerOpen(hBuzzer);
            buzzerSetFrequency(1975); //B
            Task_sleep(150000 / Clock_tickPeriod); //16th note
            buzzerSetFrequency(2794); //F
            Task_sleep(75000 / Clock_tickPeriod); //32th note
            buzzerSetFrequency(0); //break
            Task_sleep(150000 / Clock_tickPeriod); //16th note
            buzzerSetFrequency(2794); //F
            Task_sleep(333333 / Clock_tickPeriod); //eight note
            buzzerSetFrequency(2637); //E
            Task_sleep(333333 / Clock_tickPeriod); //eight note
            buzzerSetFrequency(2350); //D
            Task_sleep(333333 / Clock_tickPeriod); //eight note
            buzzerSetFrequency(2093); //C
            Task_sleep(333333 / Clock_tickPeriod); //eight note
            buzzerSetFrequency(1318); //E
            Task_sleep(333333 / Clock_tickPeriod); //eight note
            buzzerSetFrequency(0); //break
            Task_sleep(333333 / Clock_tickPeriod); //eight note
            buzzerSetFrequency(1318); //E
            Task_sleep(333333 / Clock_tickPeriod); //eight note
            buzzerSetFrequency(1046); //C
            Task_sleep(666666 / Clock_tickPeriod); //quarter note
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

    //Initialize i2c bus
    Board_initI2C();


    // Initialize UART
    Board_initUART();

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
   //nappi käyttöön ohjelmassa
    buttonHandle = PIN_open(&buttonState, buttonConfig);
     if(!buttonHandle) {
        System_abort("Error initializing button pins\n");
     }
     //punainen ledi käyttöön
     ledHandle = PIN_open(&ledState, ledConfigRed);
     if(!ledHandle) {
        System_abort("Error initializing LED pins\n");
      }

      // Asetetaan painonappi-pinnille keskeytyksen käsittelijäksi
      // funktio buttonFxn
      if (PIN_registerIntCb(buttonHandle, &buttonFxn) != 0) {
         System_abort("Error registering button callback function");
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
