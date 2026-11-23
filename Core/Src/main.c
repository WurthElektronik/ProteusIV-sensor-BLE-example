/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <ProteusIV/ProteusIV.h>
#include <global/global.h>
#include <global_platform.h>
#include <print.h>
#include <stdio.h>
#include <string.h>

#include "WeSensorsSDK.h"
#include "platform.h"
#include "WSEN_HIDS_2525020210002.h"
#include "WSEN_ITDS_2533020201601.h"
#include "WSEN_PADS_2511020213301.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define MIN(x, y) ((x) < (y) ? (x) : (y))
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
I2C_HandleTypeDef hi2c1;

/* USER CODE BEGIN PV */
/* Is set to true when a pass key indication has been received. */
static bool passkeyRequestReceived = false;

/* Is set to true when a display pass key indication has been received. */
static bool displayPasskeyRequestReceived = false;

/* Conn_ID that requested authentication */
static uint8_t authentication_conn_ID = 0;

/* WSEN sensor interfaces */
WE_sensorInterface_t hids;
int32_t hids_temp = 0;
int32_t hids_hum = 0;

WE_sensorInterface_t pads;
float pads_press;
float pads_temp;

WE_sensorInterface_t itds;
int16_t itds_xAcc = 0;
int16_t itds_yAcc = 0;
int16_t itds_zAcc = 0;

char printBuffer[100];

#if defined(WE_PLATFORM_STM32)
/**
 * @brief Definition of the Proteus-IV pins (using the .ioc user-label defines)
 */
ProteusIV_Pins_t ProteusIV_pins = {
        .ProteusIV_Pin_Reset  = WE_PIN((void*)&WE_STM32_PIN(RESET_GPIO_Port,       RESET_Pin)),
        .ProteusIV_Pin_Mode0    = WE_PIN((void*)&WE_STM32_PIN(MODE_0_GPIO_Port,      MODE_0_Pin)),
        .ProteusIV_Pin_Mode1    = WE_PIN((void*)&WE_STM32_PIN(MODE_1_GPIO_Port,      MODE_1_Pin)),
        .ProteusIV_Pin_Led1     = WE_PIN((void*)&WE_STM32_PIN(LED_1_GPIO_Port,       LED_1_Pin)),
        .ProteusIV_UART_Enable  = WE_PIN((void*)&WE_STM32_PIN(UART_ENABLE_GPIO_Port, UART_ENABLE_Pin)),
};

/**
 * @brief Definition of the Proteus-IV uart
 */
WE_UART_t ProteusIV_uart = {
    .uartInit = WE_UART1_Init,
    .uartDeinit = WE_UART1_DeInit,
    .uartTransmit = WE_UART1_Transmit,
};
#endif /* defined(WE_PLATFORM_STM32) */
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_I2C1_Init(void);
/* USER CODE BEGIN PFP */
/* Per-device initialization */
static bool ProteusIV_Device_Init(void);
static void HIDS_Device_Init(void);
static void ITDS_Device_Init(void);
static void PADS_Device_Init(void);

/* Application example */
static void Peripheral_CommandMode_Temperature_Example(void);

/* Helpers */
static void Examples_Print(char* str, bool success);

/* Callback functions for various indications sent by the Proteus-IV. */
static void RxCallback(uint8_t conn_ID, uint8_t* payload, uint16_t payloadLength, int8_t rssi);
static void ConnectCallback(uint8_t conn_ID, bool success, ProteusIV_Bluetooth_Address_Type_t type, uint8_t* btMac);
static void MaxPayloadCallback(uint8_t conn_ID, uint16_t max_payload);
static void SecurityCallback(uint8_t conn_ID, ProteusIV_SecurityState_t securityState);
static void PasskeyCallback(uint8_t conn_ID);
static void DisplayPasskeyCallback(uint8_t conn_ID, ProteusIV_DisplayPasskeyAction_t action, uint8_t* passkey);
static void DisconnectCallback(uint8_t conn_ID, ProteusIV_DisconnectReason_t reason);
static void LinkOpenCallback(uint8_t conn_ID, bool link_open);
static void PhyUpdateCallback(uint8_t conn_ID, uint8_t phyTx, uint8_t phyRx);
static void ScanCallback(ProteusIV_Bluetooth_Address_Type_t type, uint8_t* btMac, int8_t rssi, char* devicename);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
static void Examples_Print(char* str, bool success) { WE_APP_PRINT("%s%s\r\n", success ? "OK    " : "NOK   ", str); }

/**
 * @brief Scan the I2C bus and print the addresses of all responding devices.
 */
void I2C_Scan(void)
{
    HAL_StatusTypeDef ret;
    uint8_t addr;
    uint8_t found = 0;
    for (addr = 1; addr < 126; addr++)
    {
        ret = HAL_I2C_IsDeviceReady(&hi2c1, addr << 1, 1, 100);
        if (ret == HAL_OK)
        {
            char buf[12];
            WE_APP_PRINT("I2C device found at ");
            sprintf(buf, "0x%02X\r\n", addr);
            WE_APP_PRINT(buf);
            found++;
        }
    }
    if (found == 0) WE_APP_PRINT("No Devices Found  \r\n");
}

/**
 * @brief Initialize the Proteus-IV radio module in command mode and register callbacks.
 * @retval true on success, false on failure.
 */
static bool ProteusIV_Device_Init(void)
{
    bool ret = false;

    ProteusIV_uart.baudrate = PROTEUSIV_DEFAULT_BAUDRATE;
    ProteusIV_uart.flowControl = WE_FlowControl_RTSAndCTS;
    ProteusIV_uart.parity = WE_Parity_None;

    ProteusIV_CallbackConfig_t callbackConfig = {0};
    callbackConfig.rxCb = RxCallback;
    callbackConfig.connectCb = ConnectCallback;
    callbackConfig.maxPayloadCb = MaxPayloadCallback;
    callbackConfig.disconnectCb = DisconnectCallback;
    callbackConfig.linkOpenCb = LinkOpenCallback;
    callbackConfig.securityCb = SecurityCallback;
    callbackConfig.passkeyCb = PasskeyCallback;
    callbackConfig.displayPasskeyCb = DisplayPasskeyCallback;
    callbackConfig.phyUpdateCb = PhyUpdateCallback;
    callbackConfig.scanCb = ScanCallback;

    if (false == ProteusIV_Init(&ProteusIV_uart, &ProteusIV_pins, ProteusIV_OperationMode_CommandMode, callbackConfig))
    {
        WE_APP_PRINT("ProteusIV: Initialisation error\r\n");
        return false;
    }

    ret = ProteusIV_SetSecFlags(ProteusIV_SecFlags_NO_IO_MINPERIPHERAL_LEVEL_L2);
    Examples_Print("ProteusIV: Set SecFlags", ret);
    WE_Delay(500);

    return true;
}

/**
 * @brief Initialize the WSEN-HIDS humidity/temperature sensor on I2C1.
 */
static void HIDS_Device_Init(void)
{
    HIDS_Get_Default_Interface(&hids);
    hids.interfaceType = WE_i2c;
    hids.handle = &hi2c1;

    if (HIDS_Sensor_Init(&hids) == 0)
    {
        WE_APP_PRINT("HIDS Initialized\r\n");
    }
    else
    {
        WE_APP_PRINT("Failed to Initialize HIDS!\r\n");
    }

    if (HIDS_Set_Measurement_Type(&hids, HIDS_MEASURE_LPM) == 0)
    {
        WE_APP_PRINT("Changed the Measurement type to Low Precision Measurement\r\n");
    }
    else
    {
        WE_APP_PRINT("Failed to set Measurement Type!\r\n");
    }
}

/**
 * @brief Initialize the WSEN-ITDS accelerometer on I2C1.
 */
static void ITDS_Device_Init(void)
{
    ITDS_getDefaultInterface(&itds);
    itds.handle = &hi2c1;

    ITDS_setOperatingMode(&itds, ITDS_normalOrLowPower);
    ITDS_setPowerMode(&itds, ITDS_normalMode);
    /* Sampling rate of 200 Hz */
    ITDS_setOutputDataRate(&itds, ITDS_odr6);
    /* Enable block data update */
    ITDS_enableBlockDataUpdate(&itds, ITDS_enable);
    /* Enable address auto increment */
    ITDS_enableAutoIncrement(&itds, ITDS_enable);
    /* Filter bandwidth = ODR/2 */
    ITDS_setFilteringCutoff(&itds, ITDS_outputDataRate_2);
    /* Full scale +-16g */
    ITDS_setFullScale(&itds, ITDS_sixteenG);
}

/**
 * @brief Initialize the WSEN-PADS pressure sensor on I2C1.
 */
static void PADS_Device_Init(void)
{
    PADS_getDefaultInterface(&pads);
    pads.handle = &hi2c1;

    /* Enable low-noise configuration */
    PADS_setPowerMode(&pads, PADS_lowNoise);
    /* Automatic increment register address */
    PADS_enableAutoIncrement(&pads, PADS_enable);
    /* Enable additional low pass filter */
    PADS_enableLowPassFilter(&pads, PADS_enable);
    /* Set filter bandwidth of ODR/20 */
    PADS_setLowPassFilterConfig(&pads, PADS_lpFilterBW2);
    /* Enable block data update */
    PADS_enableBlockDataUpdate(&pads, PADS_enable);
    /* Enable continuous operation with an update rate of 50 Hz */
    PADS_setOutputDataRate(&pads, PADS_outputDataRate50Hz);
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */
  WE_Platform_Init();

#if ((WE_DEBUG_PRINT_LEVEL > WE_DEBUG_PRINT_LEVEL_OFF) || defined(WE_APP_PRINT_ENABLED))
  WE_Print_Init();
#endif
  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_I2C1_Init();
  /* USER CODE BEGIN 2 */
  uint8_t driverVersion[3];
  WE_GetDriverVersion(driverVersion);
  WE_APP_PRINT("Wuerth Elektronik eiSos Wireless Connectivity SDK version %d.%d.%d\r\n", driverVersion[0], driverVersion[1], driverVersion[2]);

  Peripheral_CommandMode_Temperature_Example();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
      WE_Delay(5000);
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  LL_FLASH_SetLatency(LL_FLASH_LATENCY_4);
  while(LL_FLASH_GetLatency()!= LL_FLASH_LATENCY_4)
  {
  }
  LL_PWR_SetRegulVoltageScaling(LL_PWR_REGU_VOLTAGE_SCALE1);
  while (LL_PWR_IsActiveFlag_VOS() != 0)
  {
  }
  LL_RCC_HSI_Enable();

   /* Wait till HSI is ready */
  while(LL_RCC_HSI_IsReady() != 1)
  {

  }
  LL_RCC_HSI_SetCalibTrimming(16);
  LL_RCC_PLL_ConfigDomain_SYS(LL_RCC_PLLSOURCE_HSI, LL_RCC_PLLM_DIV_1, 10, LL_RCC_PLLR_DIV_2);
  LL_RCC_PLL_EnableDomain_SYS();
  LL_RCC_PLL_Enable();

   /* Wait till PLL is ready */
  while(LL_RCC_PLL_IsReady() != 1)
  {

  }
  LL_RCC_SetSysClkSource(LL_RCC_SYS_CLKSOURCE_PLL);

   /* Wait till System clock is ready */
  while(LL_RCC_GetSysClkSource() != LL_RCC_SYS_CLKSOURCE_STATUS_PLL)
  {

  }
  LL_RCC_SetAHBPrescaler(LL_RCC_SYSCLK_DIV_1);
  LL_RCC_SetAPB1Prescaler(LL_RCC_APB1_DIV_1);
  LL_RCC_SetAPB2Prescaler(LL_RCC_APB2_DIV_1);
  LL_SetSystemCoreClock(80000000);

   /* Update the time base */
  if (HAL_InitTick (TICK_INT_PRIORITY) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief I2C1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C1_Init(void)
{

  /* USER CODE BEGIN I2C1_Init 0 */

  /* USER CODE END I2C1_Init 0 */

  /* USER CODE BEGIN I2C1_Init 1 */

  /* USER CODE END I2C1_Init 1 */
  hi2c1.Instance = I2C1;
  hi2c1.Init.Timing = 0x10D19CE4;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Analogue filter
  */
  if (HAL_I2CEx_ConfigAnalogFilter(&hi2c1, I2C_ANALOGFILTER_ENABLE) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Digital filter
  */
  if (HAL_I2CEx_ConfigDigitalFilter(&hi2c1, 0) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C1_Init 2 */

  /* USER CODE END I2C1_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  LL_GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  LL_AHB2_GRP1_EnableClock(LL_AHB2_GRP1_PERIPH_GPIOC);
  LL_AHB2_GRP1_EnableClock(LL_AHB2_GRP1_PERIPH_GPIOA);
  LL_AHB2_GRP1_EnableClock(LL_AHB2_GRP1_PERIPH_GPIOB);

  /**/
  LL_GPIO_ResetOutputPin(GPIOA, UART_ENABLE_Pin|RESET_Pin);

  /**/
  LL_GPIO_ResetOutputPin(GPIOB, MODE_0_Pin|MODE_1_Pin|LD3_Pin);

  /**/
  GPIO_InitStruct.Pin = UART_ENABLE_Pin|RESET_Pin;
  GPIO_InitStruct.Mode = LL_GPIO_MODE_OUTPUT;
  GPIO_InitStruct.Speed = LL_GPIO_SPEED_FREQ_LOW;
  GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
  GPIO_InitStruct.Pull = LL_GPIO_PULL_NO;
  LL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /**/
  GPIO_InitStruct.Pin = LED_1_Pin;
  GPIO_InitStruct.Mode = LL_GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = LL_GPIO_PULL_NO;
  LL_GPIO_Init(LED_1_GPIO_Port, &GPIO_InitStruct);

  /**/
  GPIO_InitStruct.Pin = MODE_0_Pin|MODE_1_Pin|LD3_Pin;
  GPIO_InitStruct.Mode = LL_GPIO_MODE_OUTPUT;
  GPIO_InitStruct.Speed = LL_GPIO_SPEED_FREQ_LOW;
  GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
  GPIO_InitStruct.Pull = LL_GPIO_PULL_NO;
  LL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
/**
 * @brief Proteus-IV peripheral command mode example with WSEN sensor readout.
 *
 * @details Initializes every device through its own init function, then in the
 * main loop measures HIDS/ITDS/PADS and transmits the results over every open
 * Bluetooth LE connection.
 */
static void Peripheral_CommandMode_Temperature_Example(void)
{
    WE_APP_PRINT("Peripheral_CommandMode_Temperature_Example started\r\n");
    bool ret = false;

    /* --- Device initialization ------------------------------------------- */
    if (!ProteusIV_Device_Init())
    {
        return;
    }

    I2C_Scan();
    WE_Delay(100);
    HIDS_Device_Init();
    ITDS_Device_Init();
    PADS_Device_Init();

    /* --- Main application loop ------------------------------------------- */
    while (1)
    {
        if (passkeyRequestReceived)
        {
            /* Respond to passkey request */
            passkeyRequestReceived = false;

            /* default pass key */
            uint8_t passkey[6] = {'1', '2', '3', '1', '2', '3'};
#pragma message("Please use the passkey of the connection here displayed by the peer device")
            ret = ProteusIV_Passkey(authentication_conn_ID, passkey);
            Examples_Print("Enter passkey", ret);
        }

        if (displayPasskeyRequestReceived)
        {
            /* Respond to display passkey request */
            displayPasskeyRequestReceived = false;

            /* Confirm key */
            ret = ProteusIV_NumericCompareConfirm(authentication_conn_ID, true);
            Examples_Print("Numeric comparison confirmation", ret);
        }

        bool link_open = false;

        /* transmit data to all open connections */
        for (int conn_ID = 0; conn_ID < PROTEUSIV_MAX_NUMBER_OF_CONNECTEDDEVICES; conn_ID++)
        {
            if (ProteusIV_connection_list[conn_ID].is_linkopen)
            {
                link_open = true;

                /* Measure and Send HIDS */
                if (HIDS_Sensor_Measure_Raw(&hids, HIDS_MEASURE_LPM, &hids_temp, &hids_hum) == 0)
                {
                	sprintf(printBuffer,
                	            "HIDS:\n"
                	            " Temperature: %ld.%02ld degC\n"
                	            " Humidity: %ld.%01ld %%\r\n",
                	            hids_temp / 1000,
						        (hids_temp % 1000) / 10,//Temperature with 2 digit after Decimal Point
                	            hids_hum / 1000,
                	            (hids_hum % 1000)/100);//Humidity with 1 digit after Decimal Point
                	WE_APP_PRINT(printBuffer);
                    ret = ProteusIV_Transmit(conn_ID, (uint8_t *)printBuffer, MIN(strlen(printBuffer), ProteusIV_connection_list[conn_ID].max_payload));
                }
                else
                {
                    WE_APP_PRINT("Failed to Read HIDS Values\r\n");
                }

                /* Measure and Send ITDS */
                if (ITDS_getAccelerations_int(&itds, 1, &itds_xAcc, &itds_yAcc, &itds_zAcc) == WE_SUCCESS)
                {
                	sprintf(printBuffer,
                	            "ITDS:\n"
                	            " X: %d mg\n"
                	            " Y: %d mg\n"
                	            " Z: %d mg\r\n",
                	            itds_xAcc,
                	            itds_yAcc,
                	            itds_zAcc);
                    WE_APP_PRINT(printBuffer);
                    ret = ProteusIV_Transmit(conn_ID, (uint8_t *)printBuffer, MIN(strlen(printBuffer), ProteusIV_connection_list[conn_ID].max_payload));
                }
                else
                {
                    WE_APP_PRINT("**** ITDS_getAccelerations_int(): NOT OK ****\r\n");
                }

                /* Measure and Send PADS */
                if ((PADS_getPressure_float(&pads, &pads_press) == WE_SUCCESS) && (PADS_getTemperature_float(&pads, &pads_temp) == WE_SUCCESS))
                {
                	sprintf(printBuffer,
                	            "PADS:\n"
                	            " Temperature: %.2f degC\n"
                	            " Pressure: %.2f hPa\r\n",
                	            pads_temp,
                	            pads_press);
                	WE_APP_PRINT(printBuffer);
                    ret = ProteusIV_Transmit(conn_ID, (uint8_t *)printBuffer, MIN(strlen(printBuffer), ProteusIV_connection_list[conn_ID].max_payload));
                }
                else
                {
                    WE_APP_PRINT("**** PADS NOT OK ****\r\n");
                }

                Examples_Print("ProteusIV_Transmit", ret);
                WE_APP_PRINT("-> to Connection ID %d\r\n", conn_ID);
                WE_Delay(1500);
            }
        }

        if (!link_open)
        {
            WE_APP_PRINT("Waiting for link to open\r\n");
            WE_Delay(2000);
        }
    }
}

/* --- Proteus-IV callbacks ------------------------------------------------- */

/**
 * @brief Callback called when data has been received via radio
 */
static void RxCallback(uint8_t conn_ID, uint8_t* payload, uint16_t length, int8_t rssi)
{
    uint16_t i = 0;
    WE_APP_PRINT("Conn_ID %d: Data received (RSSI %d dBm):\r\n-> ", conn_ID, rssi);
    WE_APP_PRINT("0x");
    for (i = 0; i < length; i++)
    {
        WE_APP_PRINT("%02x", *(payload + i));
    }
    WE_APP_PRINT(" (");
    for (i = 0; i < length; i++)
    {
        WE_APP_PRINT("%c", *(payload + i));
    }
    WE_APP_PRINT(")\r\n");
}

/**
 * @brief Callback called when a Bluetooth connection request is received
 */
static void ConnectCallback(uint8_t conn_ID, bool success, ProteusIV_Bluetooth_Address_Type_t type, uint8_t* btMac)
{
    if (success)
    {
        WE_APP_PRINT("Conn_ID %d: Connected to device (type %d, BTMAC 0x%02x%02x%02x%02x%02x%02x)\r\n", conn_ID, type, btMac[0], btMac[1], btMac[2], btMac[3], btMac[4], btMac[5]);
    }
    else
    {
        WE_APP_PRINT("Connection failed to device (type %d, BTMAC 0x%02x%02x%02x%02x%02x%02x)\r\n", type, btMac[0], btMac[1], btMac[2], btMac[3], btMac[4], btMac[5]);
    }
}

/**
 * @brief Callback called when the maximum payload of a Bluetooth link is updated
 */
static void MaxPayloadCallback(uint8_t conn_ID, uint16_t max_payload) { WE_APP_PRINT("Conn_ID %d: Max payload updated to %d bytes\r\n", conn_ID, max_payload); }

/**
 * @brief Callback called when security level has changed
 */
static void SecurityCallback(uint8_t conn_ID, ProteusIV_SecurityState_t securityState) { WE_APP_PRINT("Conn_ID %d: Encrypted link to peer device established (success: %d, level: %d)\r\n", conn_ID, securityState.success, securityState.level); }

/**
 * @brief Callback called when remote device has requested for pass key
 */
static void PasskeyCallback(uint8_t conn_ID)
{
    WE_APP_PRINT("Conn_ID %d: Pass key requested\r\n", conn_ID);

    /* Handle pass key request asynchronously in main (must not send response directly from callback) */
    authentication_conn_ID = conn_ID;
    passkeyRequestReceived = true;
}

/**
 * @brief Callback called when a pass key is displayed to enter it on the remote device
 */
static void DisplayPasskeyCallback(uint8_t conn_ID, ProteusIV_DisplayPasskeyAction_t action, uint8_t* passkey)
{
    WE_APP_PRINT("Conn_ID %d: Pass key displayed (%c%c%c%c%c%c)\r\n", conn_ID, passkey[0], passkey[1], passkey[2], passkey[3], passkey[4], passkey[5]);

    if (ProteusIV_DisplayPasskeyAction_PleaseConfirm == action)
    {
        /* Handle display pass key request asynchronously in main (must not send response directly from callback) */
        authentication_conn_ID = conn_ID;
        displayPasskeyRequestReceived = true;
        WE_APP_PRINT("Please confirm\r\n");
    }
}

/**
 * @brief Callback called when connection has been dropped
 */
static void DisconnectCallback(uint8_t conn_ID, ProteusIV_DisconnectReason_t reason)
{
    static const char* reasonStrings[] = {"unknown", "connection timeout", "user terminated connection", "host terminated connection", "connection interval unacceptable", "MIC failure", "connection setup failed"};
    WE_APP_PRINT("Conn_ID %d: Disconnected from device (reason: %s)\r\n", conn_ID, reasonStrings[reason]);

    displayPasskeyRequestReceived = false;
    passkeyRequestReceived = false;
}

/**
 * @brief Callback called when a Bluetooth link has been opened. Now data transmission can be done
 */
static void LinkOpenCallback(uint8_t conn_ID, bool link_open) { WE_APP_PRINT("Conn_ID %d: Link %s\r\n", conn_ID, link_open ? "opened" : "closed"); }

/**
 * @brief Callback called when a device has been found while scanning
 */
static void ScanCallback(ProteusIV_Bluetooth_Address_Type_t type, uint8_t* btMac, int8_t rssi, char* devicename) { WE_APP_PRINT("Found device (type %d, BTMAC 0x%02x%02x%02x%02x%02x%02x, RSSI %d dBm, name %s) during scan\r\n", type, btMac[0], btMac[1], btMac[2], btMac[3], btMac[4], btMac[5], rssi, devicename); }

/**
 * @brief Callback called when the PHY of the connection has changed
 */
static void PhyUpdateCallback(uint8_t conn_ID, uint8_t phyTx, uint8_t phyRx) { WE_APP_PRINT("Conn_ID %d: Phy of connection updated (RX: %d, TX: %d)\r\n", conn_ID, phyRx, phyTx); }
/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
