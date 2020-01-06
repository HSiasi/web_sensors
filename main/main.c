/*
 * Copyright (C) u-blox Melbourn Ltd
 * u-blox Melbourn Ltd, Melbourn, UK
 *
 * All rights reserved.
 *
 * This source file is the sole property of u-blox Melbourn Ltd.
 * Reproduction or utilisation of this source in whole or part is
 * forbidden without the written consent of u-blox Melbourn Ltd.
 */

#include <stdio.h>
#include <string.h> // For memcpy() and strncpy()
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_task_wdt.h" // for esp_task_wdt_reset()
#include "esp_system.h"
#include "esp_event.h"
#include "esp_event_loop.h"
#include "nvs_flash.h" // For nvs_flash_init();
#include "esp_spi_flash.h" // For spi_flash_get_chip_size()
#include "esp_timer.h" // For esp_timer_get_time()
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "sys/time.h"
#include "driver/rtc_io.h"
#include "driver/gpio.h"
#include "compile_time.h"
#include "whre_config.h"
#include "log.h"

#include "i2c_helper.h"
#include "battery_charger.h"
#include "battery_charger_bq24295.h"
#include "accelerometer.h"
#include "accelerometer_lis2dw.h"
#include "uart_helper.h"
#include "at_client.h"
#include "cellular.h"
#include "cellular_sara_r412m.h"
#include "location.h"
#include "location_sara_r412m.h"
#include "lwm2m.h"
#include "lwm2m_sara_r412m.h"

/**************************************************************************
 * MANIFEST CONSTANTS
 *************************************************************************/

#define SHTC1_ADDR 0x20                          // SHTC1 I2C address 0x20
//#define SHTC1_ADDR 0x5D                        // SHTC1 I2C address 0x70
//#define SHTC1_MEASUREMENT_CMD 0x7866           // Measurement Command, Clock Stretching Disabled Read T First 
 

#define LWM2M_WAKEUP_WAIT_SECONDS           15
#define SLEEP_TIME_USECONDS                 (60 * 1000000)
#define LWM2M_REGISTRATION_LIFETIME_SECONDS 60 // Deliberately a small value
                                               // in order that the lifetime
                                               // will have expired at each
                                               // wakeup and hence the server
                                               // will be contacted when we
                                               // awake
#define LWM2M_SERVER_REGISTRATION_UPDATE_RETRIES   10
#define LWM2M_SERVER_WAIT_TIME_SECONDS      10
#define WHRE_LWM2M_SERVER_SHORT_ID          100

// The OMA IDs for the custom objects
#define LWM2M_OBJECT_OMA_ID_I2C_GENERIC_COMMAND               33059 //33050

// Instances to use for objects
#define LWM2M_OBJECT_INSTANCE_ID_SECURITY              2
#define LWM2M_OBJECT_INSTANCE_ID_SERVER                2
#define LWM2M_OBJECT_INSTANCE_ID_I2C_GENERIC_COMMAND   1
#define LWM2M_OBJECT_INSTANCE_ID_LOCATION              0 // Has to be zero, a single instance resource

// The maximum number of entries in an I2C sequence: any bigger
// than this and the I2C object is too big for SARA-R412M
#define I2C_SEQUENCE_WRITE_MAX_LENGTH 5
#define I2C_SEQUENCE_READ_MAX_LENGTH 10
#define I2C_SEQUENCE_MAX_LENGTH I2C_SEQUENCE_READ_MAX_LENGTH

/**************************************************************************
 * TYPES
 *************************************************************************/

// Struct to hold a BSSID item as a component of a linked list.
typedef struct Bssid {
   uint8_t id[6];
   struct Bssid *pNext;
} Bssid;

typedef enum {
    LED_STATE_OFF,
    LED_STATE_GOOD,      // == green
    LED_STATE_MIDDLIN,   // == yellow
    LED_STATE_BAD        // == red
} LedState;

// Structure to hold an I2C read or write sequence.
typedef struct {
    int32_t length;
    uint8_t sequence[I2C_SEQUENCE_MAX_LENGTH];
} I2cSequence;

/**************************************************************************
 * LOCAL VARIABLES
 *************************************************************************/

// The logging buffer.
static char gLoggingBuffer[LOG_STORE_SIZE];

// Stop time for keepGoingCallback().
static int64_t gStopTimeCellularMS;

// Stop time for getting a location fix.
static int64_t gStopTimeLocationMS;

// Stop time for waiting for LWM2M operations by the serve.
static int64_t gStopTimeLwm2mMS;

// Event queue for the UART driver.
static QueueHandle_t gUartEventQueue;

// The last location data
static float gLatitudeDegrees = 0;
static float gLongitudeDegrees = 0;
static float gRadiusMetres = 0;
static float gAltitudeMetres = 0;
static bool  gGotLocationFix = false;

/**************************************************************************
 * STATIC FUNCTIONS
 *************************************************************************/

// Set the state of the LEDs permanently
static void ledSet(LedState state)
{
    switch (state) {
        case LED_STATE_OFF:
            gpio_set_level(CONFIG_PIN_DEBUG_LED_RED, 1);
            gpio_set_level(CONFIG_PIN_DEBUG_LED_GREEN, 1);
            gpio_set_level(CONFIG_PIN_DEBUG_LED_BLUE, 1);
        break;
        case LED_STATE_GOOD:
            gpio_set_level(CONFIG_PIN_DEBUG_LED_RED, 1);
            gpio_set_level(CONFIG_PIN_DEBUG_LED_GREEN, 0);
            gpio_set_level(CONFIG_PIN_DEBUG_LED_BLUE, 1);
        break;
        case LED_STATE_MIDDLIN:
            gpio_set_level(CONFIG_PIN_DEBUG_LED_RED, 0);
            gpio_set_level(CONFIG_PIN_DEBUG_LED_GREEN, 0);
            gpio_set_level(CONFIG_PIN_DEBUG_LED_BLUE, 1);
        break;
        case LED_STATE_BAD:
            gpio_set_level(CONFIG_PIN_DEBUG_LED_RED, 0);
            gpio_set_level(CONFIG_PIN_DEBUG_LED_GREEN, 1);
            gpio_set_level(CONFIG_PIN_DEBUG_LED_BLUE, 1);
        break;
        default:
        break;
    }
}

// Set the state of the LED for a given time and
// then return to what they were previously
static void ledSetTemporary(LedState state, int32_t timeMilliseconds)
{
    bool red = gpio_get_level(CONFIG_PIN_DEBUG_LED_RED);
    bool green = gpio_get_level(CONFIG_PIN_DEBUG_LED_GREEN);
    bool blue = gpio_get_level(CONFIG_PIN_DEBUG_LED_BLUE);

    ledSet(state);
    vTaskDelay(timeMilliseconds / portTICK_PERIOD_MS);

    gpio_set_level(CONFIG_PIN_DEBUG_LED_RED, red);
    gpio_set_level(CONFIG_PIN_DEBUG_LED_GREEN, green);
    gpio_set_level(CONFIG_PIN_DEBUG_LED_BLUE, blue);
}

// Callback function for the cellular connect and
// location establishment processes
static bool keepGoingCallback()
{
    bool keepGoing = true;

    esp_task_wdt_reset();
    if ((esp_timer_get_time() / 1000) > gStopTimeCellularMS) {
        keepGoing = false;
        ledSetTemporary(LED_STATE_BAD, 100);
    } else {
        ledSetTemporary(LED_STATE_MIDDLIN, 100);
    }

    return keepGoing;
}


static void locationFixCallback(int32_t latitudeX10e7,
                                int32_t longitudeX10e7,
                                int32_t altitudeMetres,
                                int32_t radiusMetres,
                                int32_t speedMps,
                                int32_t svs)
{
    (void) speedMps;
    (void) svs;

    gLatitudeDegrees = ((float) latitudeX10e7) / 10000000;
    gLongitudeDegrees = ((float) longitudeX10e7) / 10000000;
    gRadiusMetres = (float) radiusMetres;
    gAltitudeMetres = (float) altitudeMetres;

    gGotLocationFix = true;
    printf("MAIN: location call-back called, location is %.6f/%.6f +/-%d metres, %d metre(s) high.\n",
           gLatitudeDegrees, gLongitudeDegrees, radiusMetres, altitudeMetres);
    printf("MAIN: paste this into a browser: https://maps.google.com/?q=%.6f,%.6f\n",
           gLatitudeDegrees, gLongitudeDegrees);
}

// Convert from ESP32 Wifi authentication mode enum to our bitmap.
static uint8_t convertFromEsp32AuthMode(wifi_auth_mode_t authMode)
{
    uint8_t authModeBitmap = 0;

    switch (authMode) {
        case WIFI_AUTH_OPEN:
            authModeBitmap = LOCATION_WIFI_AUTH_NONE;
        break;
        case WIFI_AUTH_WEP:
            authModeBitmap = LOCATION_WIFI_AUTH_WEP;
        break;
        case WIFI_AUTH_WPA_PSK:
            authModeBitmap = LOCATION_WIFI_AUTH_WPA | LOCATION_WIFI_AUTH_PSK;
        break;
        case WIFI_AUTH_WPA2_PSK:
            authModeBitmap = LOCATION_WIFI_AUTH_WPA2 | LOCATION_WIFI_AUTH_PSK;
        break;
        case WIFI_AUTH_WPA_WPA2_PSK:
            authModeBitmap = LOCATION_WIFI_AUTH_WPA | LOCATION_WIFI_AUTH_WPA2 | LOCATION_WIFI_AUTH_PSK;
        break;
        case WIFI_AUTH_WPA2_ENTERPRISE:
            authModeBitmap = LOCATION_WIFI_AUTH_WPA2 | LOCATION_WIFI_AUTH_EAP;
        break;
        default:
        break;
    }

    return authModeBitmap;
}

// Convert from ESP32 Wifi cipher type enum to our bitmap.
static uint8_t convertFromEsp32Cipher(wifi_cipher_type_t cipher)
{
    uint8_t cipherBitmap = 0;

    switch (cipher) {
        case WIFI_CIPHER_TYPE_NONE:
            cipherBitmap = LOCATION_WIFI_CIPHER_NONE;
        break;
        case WIFI_CIPHER_TYPE_WEP40:
            cipherBitmap = LOCATION_WIFI_CIPHER_WEP64;
        break;
        case WIFI_CIPHER_TYPE_WEP104:
            cipherBitmap = LOCATION_WIFI_CIPHER_WEP128;
        break;
        case WIFI_CIPHER_TYPE_TKIP:
            cipherBitmap = LOCATION_WIFI_CIPHER_TKIP;
        break;
        case WIFI_CIPHER_TYPE_CCMP:
            cipherBitmap = LOCATION_WIFI_CIPHER_AES_CCMP;
        break;
        case WIFI_CIPHER_TYPE_TKIP_CCMP:
            cipherBitmap = LOCATION_WIFI_CIPHER_TKIP | LOCATION_WIFI_CIPHER_AES_CCMP;
        break;
        case WIFI_CIPHER_TYPE_UNKNOWN:
        break;
        default:
        break;
    }

    return cipherBitmap;
}

// Set up the IO lines for the LEDs
static void ledInit()
{
    gpio_config_t config;

    config.intr_type = GPIO_PIN_INTR_DISABLE;
    config.mode = GPIO_MODE_INPUT_OUTPUT;
    config.pull_down_en = 0;
    config.pull_up_en = 0;
    config.pin_bit_mask = (1ULL << CONFIG_PIN_DEBUG_LED_RED);
    gpio_config(&config);
    gpio_set_level(CONFIG_PIN_DEBUG_LED_RED, 1);
    config.pin_bit_mask = (1ULL << CONFIG_PIN_DEBUG_LED_GREEN);
    gpio_config(&config);
    gpio_set_level(CONFIG_PIN_DEBUG_LED_GREEN, 1);
    config.pin_bit_mask = (1ULL << CONFIG_PIN_DEBUG_LED_BLUE);
    gpio_config(&config);
    gpio_set_level(CONFIG_PIN_DEBUG_LED_BLUE, 1);

    gpio_set_level(CONFIG_PIN_DEBUG_LED_RED, 0);
    gpio_set_level(CONFIG_PIN_DEBUG_LED_GREEN, 1);
    gpio_set_level(CONFIG_PIN_DEBUG_LED_BLUE, 1);
    vTaskDelay(250 / portTICK_PERIOD_MS);
    gpio_set_level(CONFIG_PIN_DEBUG_LED_RED, 1);
    gpio_set_level(CONFIG_PIN_DEBUG_LED_GREEN, 0);
    gpio_set_level(CONFIG_PIN_DEBUG_LED_BLUE, 1);
    vTaskDelay(250 / portTICK_PERIOD_MS);
    gpio_set_level(CONFIG_PIN_DEBUG_LED_RED, 1);
    gpio_set_level(CONFIG_PIN_DEBUG_LED_GREEN, 1);
    gpio_set_level(CONFIG_PIN_DEBUG_LED_BLUE, 0);
    vTaskDelay(250 / portTICK_PERIOD_MS);
    gpio_set_level(CONFIG_PIN_DEBUG_LED_RED, 1);
    gpio_set_level(CONFIG_PIN_DEBUG_LED_GREEN, 1);
    gpio_set_level(CONFIG_PIN_DEBUG_LED_BLUE, 1);
    vTaskDelay(250 / portTICK_PERIOD_MS);
}

// Required for Wifi
static esp_err_t wifi_event_handler(void *ctx, system_event_t *event)
{
    printf("MAIN: Wifi event, ID %d.\n", event->event_id);

    switch (event->event_id) {
        case SYSTEM_EVENT_STA_START:
            printf("MAIN: Wifi scan started.\n");
        break;
        case SYSTEM_EVENT_SCAN_DONE:
            printf("MAIN: Wifi scan (ID %d) done, status %d, %d AP(s) found.\n",
                   event->event_info.scan_done.scan_id,
                   event->event_info.scan_done.status,
                   event->event_info.scan_done.number);
        break;
        case SYSTEM_EVENT_STA_STOP:
            printf("MAIN: Wifi scan stopped.\n");
        break;
        default:
        break;
    }

    return ESP_OK;
}

// Write to the Location object.  Returns zero
// on succesa, otherwise negative error code.
static int32_t locationSetLocation(int32_t objectInstanceId,
                                   float gLatitudeDegrees,
                                   float gLongitudeDegrees,
                                   float gRadiusMetres,
                                   float gAltitudeMetres)
{
    int32_t errorCode = SARA_R412M_LWM2M_OUT_OF_MEMORY;
    Lwm2mObjectInstance *pObject;
    Lwm2mValue value;
 
    // Prepare the entire object
    pObject = pLwm2mObjectPrepare(LWM2M_OBJECT_ID_LOCATION,
                                  objectInstanceId);
    if (pObject != NULL) {
       // Add the single Latitude resource instance
        value.number = gLatitudeDegrees;
        errorCode = lwm2mResourcePrepare(0, -1, /* the Latitude resource */
                                         LWM2M_RESOURCE_TYPE_FLOAT,
                                         value, pObject);
        if (errorCode == 0) {
           // Add the single Longitude resource instance
            value.number = gLatitudeDegrees;
            errorCode = lwm2mResourcePrepare(1, -1, /* the Longitude resource */
                                             LWM2M_RESOURCE_TYPE_FLOAT,
                                             value, pObject);
        }
        if (errorCode == 0) {
           // Add the single Altitude resource instance
            value.number = gAltitudeMetres;
            errorCode = lwm2mResourcePrepare(2, -1, /* the Altitude resource */
                                             LWM2M_RESOURCE_TYPE_FLOAT,
                                             value, pObject);
        }
        if (errorCode == 0) {
           // Add the single Radius resource instance
            value.number = gRadiusMetres;
            errorCode = lwm2mResourcePrepare(3, -1, /* the Radius resource */
                                             LWM2M_RESOURCE_TYPE_FLOAT,
                                             value, pObject);
        }
        if (errorCode == 0) {
            // Now write to the object
            errorCode = lwm2mObjectSet(pObject);
            if (errorCode != 0) {
                printf("MAIN: error: unable to write to /%d/%d (%d).\n",
                       pObject->omaId, pObject->instanceId, errorCode);
            }
        } else {
            printf("MAIN: error: out of memory preparing resource for object /%d/%d (%d).\n",
                   pObject->omaId, pObject->instanceId, errorCode);
        }
        lwm2mObjectUnprepare(pObject);
    } else {
        printf("MAIN: error: out of memory preparing object /%d/%d (%d).\n",
               LWM2M_OBJECT_ID_LOCATION, objectInstanceId, errorCode);
    }

    return errorCode;
}

// Read the 7-bit I2C address from the given instance of the
// I2C Generic Command object.  Returns zero on succesa, otherwise
// negative error code.
static int32_t i2cGenericCommandGetDeviceI2cAddress(int32_t objectInstanceId,
                                                    char *pI2cAddress)
{
    int32_t errorCode;
    Lwm2mResourceDescription resourceDescription;
    Lwm2mValue value;
 
    resourceDescription.objectOmaId = LWM2M_OBJECT_OMA_ID_I2C_GENERIC_COMMAND;
    resourceDescription.objectInstanceId = objectInstanceId;
    // Device I2C Address resource
    resourceDescription.resourceOmaId = 1;
    resourceDescription.resourceInstanceId = -1;
    resourceDescription.resourceType = LWM2M_RESOURCE_TYPE_INTEGER;
    errorCode = lwm2mResourceGet(&resourceDescription, &value, NULL);
    if (errorCode == 0) {
        if (pI2cAddress != NULL) {
            *pI2cAddress = (char) ((int32_t) SHTC1_ADDR) & 0x7F; //hack!
        }
    } else {
        printf("MAIN: error: failed to read Device I2C Address resource from /%d/%d (%d).\n",
               LWM2M_OBJECT_OMA_ID_I2C_GENERIC_COMMAND, objectInstanceId, errorCode);
    }

    return errorCode;
}

// Read the trigger condition from the given instance of the
// I2C Generic Command object  Returns zero on succesa, otherwise
// negative error code.
static int32_t i2cGenericCommandGetTriggerCondition(int32_t objectInstanceId,
                                                    int32_t *pTriggerCondition)
{
    int32_t errorCode;
    Lwm2mResourceDescription resourceDescription;
    Lwm2mValue value;
 
    resourceDescription.objectOmaId = LWM2M_OBJECT_OMA_ID_I2C_GENERIC_COMMAND;
    resourceDescription.objectInstanceId = objectInstanceId;
    // Trigger Condition resource
    resourceDescription.resourceOmaId = 4;
    resourceDescription.resourceInstanceId = -1;
    resourceDescription.resourceType = LWM2M_RESOURCE_TYPE_INTEGER;
    errorCode = lwm2mResourceGet(&resourceDescription, &value, NULL);
    if (errorCode == 0) {
        if (pTriggerCondition != NULL) {
            *pTriggerCondition = (int32_t) value.number;
        }
    } else {
        printf("MAIN: error: failed to read Trigger Condition resource from /%d/%d (%d).\n",
               LWM2M_OBJECT_OMA_ID_I2C_GENERIC_COMMAND, objectInstanceId, errorCode);
    }

    return errorCode;
}

// Read the write sequence from the given instance of the
// I2C Generic Command object  Returns zero on succesa, otherwise
// negative error code.
static int32_t i2cGenericCommandGetWriteSequence(int32_t objectInstanceId,
                                                 I2cSequence *pI2cSequence)
{
    int32_t errorCode;
    Lwm2mObjectInstance *pObject;
    Lwm2mResourceInstance *pResource;
 
    // Read the whole object this time, as the write sequence is a
    // multi-instance resource and we need all of them
    errorCode = lwm2mObjectGet(LWM2M_OBJECT_OMA_ID_I2C_GENERIC_COMMAND,
                               objectInstanceId, &pObject);
    if (errorCode == 0) {
        if (pI2cSequence != NULL) {
            // Read all of the instances of the Write Sequence resource
            pI2cSequence->length = 0;
            pResource = pObject->pResources;
			
            while (pResource != NULL) {
                if (pResource->omaId == 5) { 
				    // 5 is the Write Sequence resource
                    // Handle the single instance case (in which 
                    // the instance ID will be set to -1)
                    if (pResource->instanceId < 0) {
                        pResource->instanceId = 0;
                    }
                    if (pResource->instanceId < I2C_SEQUENCE_WRITE_MAX_LENGTH) {
                        pI2cSequence->sequence[pResource->instanceId] =
                            (uint8_t) pResource->value.number;
                        (pI2cSequence->length)++;
                    }
                }
                pResource = pResource->pNext;
            }
        }
		// HACK
		pI2cSequence->length = 2;
		pI2cSequence->sequence[0] = 0xFF;
		pI2cSequence->sequence[1] = 0xFF;
		
        lwm2mObjectFree(&pObject);
    } else {
        printf("MAIN: error: unable to read /%d/%d (%d).\n",
               LWM2M_OBJECT_OMA_ID_I2C_GENERIC_COMMAND,
               objectInstanceId, errorCode);
    }

    return errorCode;
}

// Set the Write Success resource in the given instance of the
// I2C Generic Command object.  Returns zero on succesa, otherwise
// negative error code.
static int32_t i2cGenericCommandSetWriteSuccess(int32_t objectInstanceId,
                                                bool writeSuccess)
{
    int32_t errorCode;
    Lwm2mResourceDescription resourceDescription;
    Lwm2mValue value;
 
    resourceDescription.objectOmaId = LWM2M_OBJECT_OMA_ID_I2C_GENERIC_COMMAND;
    resourceDescription.objectInstanceId = objectInstanceId;
    // Device Write Success resource
    resourceDescription.resourceOmaId = 6;
    resourceDescription.resourceInstanceId = -1;
    resourceDescription.resourceType = LWM2M_RESOURCE_TYPE_BOOLEAN;
    value.boolean = writeSuccess;
    errorCode = lwm2mResourceSet(&resourceDescription, value);
    if (errorCode != 0) {
        printf("MAIN: error: failed to write Write Success resource in object /%d/%d (%d).\n",
               LWM2M_OBJECT_OMA_ID_I2C_GENERIC_COMMAND, objectInstanceId, errorCode);
    }

    return errorCode;
}

// Read the Delay resource from the given instance of the
// I2C Generic Command object.  Returns zero on success, otherwise
// negative error code.
static int32_t i2cGenericCommandGetDelay(int32_t objectInstanceId,
                                         int32_t *pDelay)
{
    int32_t errorCode;
    Lwm2mResourceDescription resourceDescription;
    Lwm2mValue value;
 
    resourceDescription.objectOmaId = LWM2M_OBJECT_OMA_ID_I2C_GENERIC_COMMAND;
    resourceDescription.objectInstanceId = objectInstanceId;
    // Delay resource
    resourceDescription.resourceOmaId = 7;
    resourceDescription.resourceInstanceId = -1;
    resourceDescription.resourceType = LWM2M_RESOURCE_TYPE_INTEGER;
    errorCode = lwm2mResourceGet(&resourceDescription, &value, NULL);
    if (errorCode == 0) {
        if (pDelay != NULL) {
            *pDelay = (int32_t) 60000.0; //HACK
        }
    } else {
        printf("MAIN: error: failed to read Delay resource from /%d/%d (%d).\n",
               LWM2M_OBJECT_OMA_ID_I2C_GENERIC_COMMAND, objectInstanceId, errorCode);
    }

    return errorCode;
}




// return the value of 33059/1/5
static int32_t web_sensor_demo(int32_t *v)
{
    int32_t errorCode;
    Lwm2mResourceDescription resourceDescription;
    Lwm2mValue value;
 
    resourceDescription.objectOmaId = LWM2M_OBJECT_OMA_ID_I2C_GENERIC_COMMAND;
    resourceDescription.objectInstanceId = 1 ;
    // Delay resource
    resourceDescription.resourceOmaId = 5;
    resourceDescription.resourceInstanceId = -1;
    resourceDescription.resourceType = LWM2M_RESOURCE_TYPE_INTEGER;
    errorCode = lwm2mResourceGet(&resourceDescription, &value, NULL);
    if (errorCode == 0) {
		printf("--> web_sensor_demo: value:%f \n\n", value.number);
        *v = (int32_t) value.number;
    } else {
        printf("--> web_sensor_demo:\n");
    }

    return errorCode;
}





// Get the number of instance of Read Response in the given instance of the
// I2C Generic Command object.  Returns zero on success, otherwise
// negative error code.
static int32_t i2cGenericCommandGetReadResponseLength(int32_t objectInstanceId,
                                                      int32_t *pReadResponseLength)
{
    int32_t errorCode;
    Lwm2mObjectInstance *pObject;
    Lwm2mResourceInstance *pResource;
 
    // Read the whole object to count the Read Response instances
    errorCode = lwm2mObjectGet(LWM2M_OBJECT_OMA_ID_I2C_GENERIC_COMMAND,
                               objectInstanceId, &pObject);
    if (errorCode == 0) {
        if (pReadResponseLength != NULL) {
            // Read all of the instances of the Read Response resource
            *pReadResponseLength = 0;
            pResource = pObject->pResources;
            while (pResource != NULL) {
                if (pResource->omaId == 9) { // 9 is the Read Response resource
                    (*pReadResponseLength)++;
                }
                pResource = pResource->pNext;
            }
        }
		//HACK
		*pReadResponseLength = (int32_t) 6.0;
        lwm2mObjectFree(&pObject);
    } else {
        printf("MAIN: error: unable to read /%d/%d (%d).\n",
               LWM2M_OBJECT_OMA_ID_I2C_GENERIC_COMMAND,
               objectInstanceId, errorCode);
    }

    return errorCode;
}

// Write the Response Size and Read Response into the given
// instance of the I2C Generic Command object  Returns zero
// on succesa, otherwise negative error code.
static int32_t i2cGenericCommandSetReadResponse(int32_t objectInstanceId,
                                                I2cSequence *pI2cSequence)
{
    int32_t errorCode = SARA_R412M_LWM2M_OUT_OF_MEMORY;
    int32_t x;
    Lwm2mObjectInstance *pObject;
    Lwm2mValue value;
 
    // Prepare an object which matches the instance but only
    // populate the resources we want to modify
    pObject = pLwm2mObjectPrepare(LWM2M_OBJECT_OMA_ID_I2C_GENERIC_COMMAND,
                                  objectInstanceId);
    if (pObject != NULL) {
        // Add the Response Size resource instances
        value.number = (float) pI2cSequence->length;
        errorCode = lwm2mResourcePrepare(8, -1, /* the Response Size resource */
                                         LWM2M_RESOURCE_TYPE_INTEGER,
                                         value, pObject);
        // Add the Read Response resource instances
        for (x = 0; (x < pI2cSequence->length) && (errorCode == 0); x++) {
            value.number = (float) pI2cSequence->sequence[x];
            errorCode = lwm2mResourcePrepare(9, x, /* the Read Response resource */
                                             LWM2M_RESOURCE_TYPE_INTEGER,
                                             value, pObject);
        }
        if (errorCode == 0) {
            // Now write to the object
            errorCode = lwm2mObjectSet(pObject);
            if (errorCode != 0) {
                printf("MAIN: error: unable to write to /%d/%d (%d).\n",
                       pObject->omaId, pObject->instanceId, errorCode);
            }
        } else {
            printf("MAIN: error: out of memory preparing resources for object /%d/%d (%d).\n",
                   pObject->omaId, pObject->instanceId, errorCode);
        }
        lwm2mObjectUnprepare(pObject);
    } else {
        printf("MAIN: error: out of memory preparing object /%d/%d (%d).\n",
               LWM2M_OBJECT_OMA_ID_I2C_GENERIC_COMMAND, objectInstanceId, errorCode);
    }

    return errorCode;
}

// Create the WHRE LWM2M Security object.
static int32_t createObjectWhreLwm2mSecurity(int32_t objectInstanceId,
                                             int32_t shortServerId)
{
    int32_t errorCode = SARA_R412M_LWM2M_OUT_OF_MEMORY;
    Lwm2mObjectInstance *pObject;
    Lwm2mValue value;
 
    // Prepare an object
    pObject = pLwm2mObjectPrepare(LWM2M_OBJECT_ID_SECURITY,
                                  objectInstanceId);
    if (pObject != NULL) {
        // Add the single LWM2M Server URI resource instance
        value.pString = "coaps://3.14.135.88:5684";
        errorCode = lwm2mResourcePrepare(0, -1, /* the LWM2M Server URI resource */
                                         LWM2M_RESOURCE_TYPE_STRING,
                                         value, pObject);
        if (errorCode == 0) {
            // Add the single Bootstrap Server resource instance
            value.boolean = false;
            errorCode = lwm2mResourcePrepare(1, -1, /* the Bootstrap Server resource */
                                             LWM2M_RESOURCE_TYPE_BOOLEAN,
                                             value, pObject);
        }
        if (errorCode == 0) {
            // Add the single Security Mode resource instance
            value.number = 0; // Pre-shared key mode
            errorCode = lwm2mResourcePrepare(2, -1, /* the Security Mode resource */
                                             LWM2M_RESOURCE_TYPE_INTEGER,
                                             value, pObject);
        }
        if (errorCode == 0) {
            // Add the single Public Key or Identity resource instance
            value.pString = "d2hyZV9kZXZpY2VfMDAx";
            errorCode = lwm2mResourcePrepare(3, -1, /* the Public Key or Identity resource */
                                             LWM2M_RESOURCE_TYPE_OPAQUE,
                                             value, pObject);
        }
        if (errorCode == 0) {
            // Add the single Secret Key resource instance
            value.pString = "d2hyZTAwMDE=";
            errorCode = lwm2mResourcePrepare(5, -1, /* the Secret Key resource */
                                             LWM2M_RESOURCE_TYPE_OPAQUE,
                                             value, pObject);
        }
        if (errorCode == 0) {
            // Add the single Short Server ID resource instance
            value.number = (float) shortServerId;
            errorCode = lwm2mResourcePrepare(10, -1, /* the Short Server ID resource */
                                             LWM2M_RESOURCE_TYPE_INTEGER,
                                             value, pObject);
        }
        if (errorCode == 0) {
            // Now create the object
            errorCode = lwm2mObjectCreate(pObject, shortServerId);
            if (errorCode != 0) {
                printf("MAIN: error: unable to create object /%d/%d (%d).\n",
                       pObject->omaId, pObject->instanceId, errorCode);
            }
        } else {
            printf("MAIN: error: out of memory preparing resources for object /%d/%d (%d).\n",
                   pObject->omaId, pObject->instanceId, errorCode);
        }
        lwm2mObjectUnprepare(pObject);
    } else {
        printf("MAIN: error: out of memory preparing object /%d/%d (%d).\n",
               LWM2M_OBJECT_ID_SECURITY, objectInstanceId, errorCode);
    }

    return errorCode;
}

// Create the WHRE LWM2M Server object with the given registration
// lifetime and instance ID.
static int32_t createObjectWhreLwm2mServer(int32_t objectInstanceId,
                                           int32_t lifetimeSeconds,
                                           int32_t shortServerId)
{
    int32_t errorCode = SARA_R412M_LWM2M_OUT_OF_MEMORY;
    Lwm2mObjectInstance *pObject;
    Lwm2mValue value;
 
    // Prepare an object
    pObject = pLwm2mObjectPrepare(LWM2M_OBJECT_ID_SERVER,
                                  objectInstanceId);
    if (pObject != NULL) {
        // Add the single Short Server Id resource instance
        value.number = (float) shortServerId;
        errorCode = lwm2mResourcePrepare(0, -1, /* the Short Server ID resource */
                                         LWM2M_RESOURCE_TYPE_INTEGER,
                                         value, pObject);

        if (errorCode == 0) {
            // Add the single Lifetime resource instance
            value.number = (float) lifetimeSeconds;
            errorCode = lwm2mResourcePrepare(1, -1, /* the Lifetime resource */
                                             LWM2M_RESOURCE_TYPE_INTEGER,
                                             value, pObject);
        }
        if (errorCode == 0) {
            // Add the single Notification Storing When Disabled or Offline resource instance
            value.boolean = true; // Mr Server, please queue stuff for me
            errorCode = lwm2mResourcePrepare(6, -1, /* the Notification Storing When
                                                       Disabled or Offline resource */
                                             LWM2M_RESOURCE_TYPE_BOOLEAN,
                                             value, pObject);
        }
        if (errorCode == 0) {
            // Add the single Binding resource instance
            value.pString = "UQ"; // UDP with queueing
            errorCode = lwm2mResourcePrepare(7, -1, /* the Binding resource */
                                             LWM2M_RESOURCE_TYPE_STRING,
                                             value, pObject);
        }
        if (errorCode == 0) {
            // Add the single APN Link resource instance
            value.pString = "11:0"; // Must be set to this, don't know why...
            errorCode = lwm2mResourcePrepare(10, -1, /* the APN Link resource */
                                             LWM2M_RESOURCE_TYPE_OBJLINK,
                                             value, pObject);
        }
        if (errorCode == 0) {
            // Now create the object
            errorCode = lwm2mObjectCreate(pObject, shortServerId);
            if (errorCode != 0) {
                printf("MAIN: error: unable to create object /%d/%d (%d).\n",
                       pObject->omaId, pObject->instanceId, errorCode);
            }
        } else {
            printf("MAIN: error: out of memory preparing resources for object /%d/%d (%d).\n",
                   pObject->omaId, pObject->instanceId, errorCode);
        }
        lwm2mObjectUnprepare(pObject);
    } else {
        printf("MAIN: error: out of memory preparing object /%d/%d (%d).\n",
               LWM2M_OBJECT_ID_SERVER, objectInstanceId, errorCode);
    }

    return errorCode;
}

// Create the Location object.
static int32_t createObjectLocation(int32_t objectInstanceId,
                                    int32_t shortServerId)
{
    int32_t errorCode = SARA_R412M_LWM2M_OUT_OF_MEMORY;
    Lwm2mObjectInstance *pObject;
    Lwm2mValue value;
 
    // Prepare an object
    pObject = pLwm2mObjectPrepare(LWM2M_OBJECT_ID_LOCATION,
                                  objectInstanceId);
    if (pObject != NULL) {
        // Add the single Latitude resource instance
        value.number = 0;
        errorCode = lwm2mResourcePrepare(0, -1, /* the Latitude resource */
                                         LWM2M_RESOURCE_TYPE_FLOAT,
                                         value, pObject);
        if (errorCode == 0) {
            // Add the single Longitude resource instance
            value.number = 0;
            errorCode = lwm2mResourcePrepare(1, -1, /* the Longitude resource */
                                             LWM2M_RESOURCE_TYPE_FLOAT,
                                             value, pObject);
        }
        if (errorCode == 0) {
            // Add the single Altitude resource instance
            value.number = 0;
            errorCode = lwm2mResourcePrepare(2, -1, /* the Altitude resource */
                                             LWM2M_RESOURCE_TYPE_FLOAT,
                                             value, pObject);
        }
        if (errorCode == 0) {
            // Add the single Radius resource instance
            value.number = 0;
            errorCode = lwm2mResourcePrepare(3, -1, /* the Radius resource */
                                             LWM2M_RESOURCE_TYPE_FLOAT,
                                             value, pObject);
        }
        // No velocity resource as Cell Locate doesn't provide it
        // TODO leaving out Timestamp resource for the moment as it upsets SARA-R412M
        // No speed  resource as Cell Locate doesn't provide it
        if (errorCode == 0) {
            // Now create the object
            errorCode = lwm2mObjectCreate(pObject, shortServerId);
            if (errorCode != 0) {
                printf("MAIN: error: unable to create object /%d/%d (%d).\n",
                       pObject->omaId, pObject->instanceId, errorCode);
            }
        } else {
            printf("MAIN: error: out of memory preparing resources for object /%d/%d (%d).\n",
                   pObject->omaId, pObject->instanceId, errorCode);
        }
        lwm2mObjectUnprepare(pObject);
    } else {
        printf("MAIN: error: out of memory preparing object /%d/%d (%d).\n",
               LWM2M_OBJECT_ID_LOCATION, objectInstanceId, errorCode);
    }

    return errorCode;
}

// Create the Generic I2C object with the given instance ID.
static int32_t createObjectGenericI2c(int32_t objectInstanceId,
                                      int32_t shortServerId)
{
    int32_t errorCode = SARA_R412M_LWM2M_OUT_OF_MEMORY;
    Lwm2mObjectInstance *pObject;
    Lwm2mValue value;
 
    // Prepare an object
    pObject = pLwm2mObjectPrepare(LWM2M_OBJECT_OMA_ID_I2C_GENERIC_COMMAND,
                                  objectInstanceId);
    if (pObject != NULL) {
        // Add the single Device I2C Address resource instance
        value.number = 0;
        errorCode = lwm2mResourcePrepare(1, -1, /* the Device I2C Address resource */
                                         LWM2M_RESOURCE_TYPE_INTEGER,
                                         value, pObject);
        if (errorCode == 0) {
            // Add the single Device Name resource instance
            value.pString = "";
            errorCode = lwm2mResourcePrepare(2, -1, /* the Device Name resource */
                                             LWM2M_RESOURCE_TYPE_STRING,
                                             value, pObject);
        }
        if (errorCode == 0) {
            // Add the single Command Name resource instance
            value.pString = "";
            errorCode = lwm2mResourcePrepare(3, -1, /* the Command Name resource */
                                             LWM2M_RESOURCE_TYPE_STRING,
                                             value, pObject);
        }
        if (errorCode == 0) {
            // Add the single Trigger Condition resource instance
            value.number = 0;
            errorCode = lwm2mResourcePrepare(4, -1, /* the Trigger Condition resource */
                                             LWM2M_RESOURCE_TYPE_INTEGER,
                                             value, pObject);
        }
        if (errorCode == 0) {
            // Add the Write Sequence resource instances
            for (size_t x = 0; (x < I2C_SEQUENCE_WRITE_MAX_LENGTH) && (errorCode == 0); x++) {
                value.number = 0;
                errorCode = lwm2mResourcePrepare(5, x, /* the Write Sequence resource */
                                                 LWM2M_RESOURCE_TYPE_INTEGER,
                                                 value, pObject);
            }
        }
        if (errorCode == 0) {
            // Add the single Write Success resource instance
            value.boolean = 0;
            errorCode = lwm2mResourcePrepare(6, -1, /* the Write Success resource */
                                             LWM2M_RESOURCE_TYPE_BOOLEAN,
                                             value, pObject);
        }
        if (errorCode == 0) {
            // Add the single Delay resource instance
            value.number = 0;
            errorCode = lwm2mResourcePrepare(7, -1, /* the Delay resource */
                                             LWM2M_RESOURCE_TYPE_INTEGER,
                                             value, pObject);
        }
        if (errorCode == 0) {
            // Add the single Response Size resource instance
            value.number = 0;
            errorCode = lwm2mResourcePrepare(8, -1, /* the Response Size resource */
                                             LWM2M_RESOURCE_TYPE_INTEGER,
                                             value, pObject);
        }
        if (errorCode == 0) {
            // Add the Read Response resource instances
            for (size_t x = 0; (x < I2C_SEQUENCE_READ_MAX_LENGTH) && (errorCode == 0); x++) {
                value.number = 0;
                errorCode = lwm2mResourcePrepare(9, x, /* the Read Response resource */
                                                 LWM2M_RESOURCE_TYPE_INTEGER,
                                                 value, pObject);
            }
        }
        // TODO: leaving out the Read Timestamp resource for now as it might upset SARA-R412M
        if (errorCode == 0) {
            // Now create the object
            errorCode = lwm2mObjectCreate(pObject, shortServerId);
            if (errorCode != 0) {
                printf("MAIN: error: unable to create object /%d/%d (%d).\n",
                       pObject->omaId, pObject->instanceId, errorCode);
            }
        } else {
            printf("MAIN: error: out of memory preparing resources for object /%d/%d (%d).\n",
                   pObject->omaId, pObject->instanceId, errorCode);
        }
        lwm2mObjectUnprepare(pObject);
    } else {
        printf("MAIN: error: out of memory preparing object /%d/%d (%d).\n",
               LWM2M_OBJECT_OMA_ID_I2C_GENERIC_COMMAND, objectInstanceId, errorCode);
    }

    return errorCode;
}

// Initialise everything.
// Warning: here be multiple return statements
static bool init()
{
    esp_err_t espError;
    int32_t errorCode;
    wifi_init_config_t wifiConfig = WIFI_INIT_CONFIG_DEFAULT();
    esp_chip_info_t chipInfo;

    // Initialise non-volatile storage (required by Wifi for some reason)
    espError = nvs_flash_init();
    if (espError != 0) {
        printf("MAIN: error: unable to initialise flash non-volatile storage (0x%x).\n",
                espError);
        return false;
    }
    // Initialise TCP-IP (required by Wifi, even though we only want to scan)
    tcpip_adapter_init();
    // Initialise event loops (Wifi needs them)
    espError = esp_event_loop_init(wifi_event_handler, NULL);
    if (espError != 0) {
        printf("MAIN: error: unable to initialise event loops (0x%x).\n",
                espError);
        return false;
    }
    // Initialise Wifi
    espError = esp_wifi_init(&wifiConfig);
    if (espError != 0) {
        printf("MAIN: error: unable to initialise Wifi (0x%x).\n", espError);
        return false;
    }
    // Set Wifi to station mode
    espError = esp_wifi_set_mode(WIFI_MODE_STA);
    if (espError != 0) {
        printf("MAIN: error: unable to set Wifi to station mode (0x%x).\n", espError);
        return false;
    }
    // Initialise I2C helper
    errorCode = i2cInit(CONFIG_I2C_PORT, CONFIG_PIN_I2C_SDA, CONFIG_PIN_I2C_SCL);
    if (errorCode != 0) {
        printf("MAIN: error: unable to initialise I2C helper (%d).\n", errorCode);
        return false;
    }
    // Initialise BQ24295, if it's there
    if (bq24295Init(CONFIG_I2C_PORT, CONFIG_BQ24295_DEFAULT_ADDRESS) != 0) {
        printf("MAIN: warn: unable to find BQ24295 (%d), guess we're not in a development carrier.\n", errorCode);
    }
    // Initialise LIS2DW
    errorCode =  lis2dwInit(CONFIG_I2C_PORT, CONFIG_LIS2DW_DEFAULT_ADDRESS,
                            CONFIG_PIN_INT_ACCELEROMETER, CONFIG_LIS2DW_USE_INTERRUPT_2,
                            CONFIG_LIS2DW_INTERRUPT_IS_OPEN_DRAIN);
    if (errorCode != 0) {
        printf("MAIN: error: unable to initialise LIS2DW driver (%d).\n", errorCode);
        return false;
    }
    // Initialise UART helper
    errorCode = uartInit(CONFIG_CELLULAR_UART_PORT, CONFIG_PIN_UART_TXD_CELLULAR,
                         CONFIG_PIN_UART_RXD_CELLULAR, CONFIG_CELLULAR_UART_BAUD_RATE,
                         false, &gUartEventQueue);
    if (errorCode != 0) {
        printf("MAIN: error: unable to UART I2C helper (%d).\n", errorCode);
        return false;
    }
    // Initialise AT client
    errorCode = at_client_init(CONFIG_CELLULAR_UART_PORT, gUartEventQueue, 8000, "\r", 0);
    if (errorCode != 0) {
        printf("MAIN: error: unable to initialise AT client (%d).\n", errorCode);
        return false;
    }
    // Initialise SARA-R412M component
    errorCode = saraR412mInit(CONFIG_PIN_CELLULAR_ENABLE_POWER,
                              CONFIG_PIN_CELLULAR_CP_ON,
                              CONFIG_PIN_CELLULAR_VINT);
    if (errorCode != 0) {
        printf("MAIN: error: unable to initialise SARA-R412M component (%d).\n", errorCode);
        return false;
    }
    // Initialise location component
    errorCode = locationInit();
    if (errorCode != 0) {
        printf("MAIN: error: unable to initialise location component (%d).\n", errorCode);
        return false;
    }
    // Initialise LWM2M component
    errorCode = lwm2mInit();
    if (errorCode != 0) {
        printf("MAIN: error: unable to initialise LWM2M component (%d).\n", errorCode);
        return false;
    }

    // Set external wake-up interrupt pin to be RTC pin
    if (rtc_gpio_init(CONFIG_PIN_INT_ACCELEROMETER) != ESP_OK) {
        printf("MAIN: error: unable to initalise accelerometer GPIO (ESP32 GPIO %d) pin.\n", CONFIG_PIN_INT_ACCELEROMETER);
        errorCode = -1;
        return false;
    }
    // Set external wake-up interrupt pin direction as input
    // Note: BE CAREFUL to use RTC_GPIO_MODE_* here, not GPIO_MODE_*, they are different!
    if (rtc_gpio_set_direction(CONFIG_PIN_INT_ACCELEROMETER, RTC_GPIO_MODE_INPUT_ONLY) != ESP_OK) {
        printf("MAIN: error: unable to set accelerometer GPIO (ESP32 GPIO %d) pin as input.\n", CONFIG_PIN_INT_ACCELEROMETER);
        errorCode = -1;
        return false;
    }
    // Set no pullp on external wake-up pin
    if ((gpio_pulldown_dis(CONFIG_PIN_INT_ACCELEROMETER) != ESP_OK) ||
        (gpio_pullup_dis(CONFIG_PIN_INT_ACCELEROMETER) != ESP_OK)) {
        printf("MAIN: error: unable to set accelerometer GPIO (ESP32 GPIO %d) pin as no-pull.\n", CONFIG_PIN_INT_ACCELEROMETER);
        errorCode = -1;
        return false;
    }

    // Set pin that monitors M_STAT from SARA-R4 as input
    if (gpio_set_direction(CONFIG_PIN_CELLULAR_M_STAT, GPIO_MODE_INPUT) != ESP_OK) {
        printf("MAIN: error: unable to set cellular M_STAT GPIO (ESP32 GPIO %d) pin as input.\n", CONFIG_PIN_CELLULAR_M_STAT);
        errorCode = -1;
        return false;
    }
    // Set pin that monitors M_STAT from SARA-R4 as no-pull
    if (gpio_set_pull_mode(CONFIG_PIN_CELLULAR_M_STAT, GPIO_FLOATING) != ESP_OK) {
        printf("MAIN: error: unable to set cellular M_STAT GPIO (ESP32 GPIO %d) pin as no-pull.\n", CONFIG_PIN_CELLULAR_M_STAT);
        errorCode = -1;
        return false;
    }

    // Print chip information
    esp_chip_info(&chipInfo);
    printf("MAIN: this is an ESP32 chip with %d CPU core(s), Wifi%s%s, ",
           chipInfo.cores,
           (chipInfo.features & CHIP_FEATURE_BT) ? "/BT" : "",
           (chipInfo.features & CHIP_FEATURE_BLE) ? "/BLE" : "");

    printf("silicon revision %d, ", chipInfo.revision);

    printf("%d Mbyte(s) %s flash.\n", spi_flash_get_chip_size() / (1024 * 1024),
           (chipInfo.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");

    return true;
}

// Shut everyting down
static void deInit()
{
    lwm2mDeinit();
    locationDeinit();
    saraR412mDeinit();
    at_client_deinit();
    uartDeinit(CONFIG_CELLULAR_UART_PORT);
    lis2dwDeinit();
    bq24295Deinit();
    i2cDeinit(CONFIG_I2C_PORT);
    esp_wifi_deinit();
}

// Configure SARA-R4
static bool cfgSaraR4()
{
    int32_t errorCode = 0;

    if (saraR412mGetMnoProfile() != 100) {
        errorCode = saraR412mSetMnoProfile(100);
        if (errorCode == 0) {
            errorCode = cellularReboot();
        }
    }
    if ((errorCode == 0) && (cellularGetRat(0) != CELLULAR_RAT_GPRS)) {
        errorCode = cellularSetRatRank(CELLULAR_RAT_GPRS, 0);
        if (errorCode == 0) {
            errorCode = cellularReboot();
        }
    }

    if (errorCode == 0) {
        errorCode = saraR412mLocationCfg(CONFIG_CELL_LOCATE_AUTHENTICATION_TOKEN, CONFIG_CELL_LOCATE_SERVER_URL, NULL,
                                         CONFIG_SARA_R412M_MODULE_PIN_GNSS_POWER, CONFIG_SARA_R412M_MODULE_PIN_GNSS_DATA_READY);
    }

    return (errorCode == 0);
}

// Check if LWM2M is ready by issuing a read for the
// UFOTA server object
static bool lwm2mReady()
{
    return (lwm2mObjectGet(LWM2M_OBJECT_ID_SERVER, 1, NULL) == 0);
} 

// Configure LWM2M
static bool cfgLwm2m()
{
    int32_t errorCode = 0;
    bool rebootRequired = false;

    // Configure LWM2M to use context ID 1
	// TOOD: ignoring return value for now
	//saraR412mLwm2mConfigure(WHRE_LWM2M_SERVER_SHORT_ID, 1, false);

    // Check that the required objects exist
    if (lwm2mObjectGet(LWM2M_OBJECT_ID_SECURITY,
                       LWM2M_OBJECT_INSTANCE_ID_SECURITY,
                       NULL) != 0) {
        createObjectWhreLwm2mSecurity(LWM2M_OBJECT_INSTANCE_ID_SECURITY,
                                      WHRE_LWM2M_SERVER_SHORT_ID);
        rebootRequired = true;
    }
    if (lwm2mObjectGet(LWM2M_OBJECT_ID_SERVER,
                       LWM2M_OBJECT_INSTANCE_ID_SERVER,
                       NULL) != 0) {
        createObjectWhreLwm2mServer(LWM2M_OBJECT_INSTANCE_ID_SERVER,
                                    LWM2M_REGISTRATION_LIFETIME_SECONDS,
                                    WHRE_LWM2M_SERVER_SHORT_ID);
        rebootRequired = true;
    }
    
    // If neither of the above two objects exist the creation of
    // the following to will also (since the short server ID won'tan
    // be valid), so reboot here.
    if (rebootRequired) {
        rebootRequired = false;
        errorCode = cellularReboot();
        lwm2mReady();
    }

    if (lwm2mObjectGet(LWM2M_OBJECT_OMA_ID_I2C_GENERIC_COMMAND,
                       LWM2M_OBJECT_INSTANCE_ID_I2C_GENERIC_COMMAND,
                       NULL) != 0) {
        createObjectGenericI2c(LWM2M_OBJECT_INSTANCE_ID_I2C_GENERIC_COMMAND,
                               WHRE_LWM2M_SERVER_SHORT_ID);
        rebootRequired = true;
    }
    if (lwm2mObjectGet(LWM2M_OBJECT_ID_LOCATION,
                       LWM2M_OBJECT_INSTANCE_ID_LOCATION,
                       NULL) != 0) {
        createObjectLocation(LWM2M_OBJECT_INSTANCE_ID_LOCATION,
                             WHRE_LWM2M_SERVER_SHORT_ID);
        rebootRequired = true;
    }
    
    if (rebootRequired) {
        errorCode = cellularReboot();
    }

    return (errorCode == 0);
}

// Perform the I2C demo operation
static bool doI2cDemo()
{
    bool success = false;
    char i2cAddress;
    int32_t triggerCondition;
    I2cSequence writeSequence;
    I2cSequence readSequence;
    int32_t bytesReadOrError = -1;
    int32_t writeError = 0;
    int32_t delayUS;
    bool writeSuccess = false;
	int32_t hamedsNewByte;
	uint8_t firstArray[]     = {0, 0}; //ori = {9, 255}
	uint8_t hamedsNewArray[] = {9, 0}; //ori = {0, 0}
	
	//Power-up and measurement within 1 ms 
	vTaskDelay((1 / 1000) / portTICK_PERIOD_MS);

    if (i2cGenericCommandGetDeviceI2cAddress(LWM2M_OBJECT_INSTANCE_ID_I2C_GENERIC_COMMAND, //0x20
                                             &i2cAddress) == 0) {
        if (i2cGenericCommandGetTriggerCondition(LWM2M_OBJECT_INSTANCE_ID_I2C_GENERIC_COMMAND,
                                                 &triggerCondition) == 0) {
            if (web_sensor_demo(&hamedsNewByte) == 0) {
				hamedsNewArray[1] = (uint8_t) hamedsNewByte;
				
				writeError = i2cSendReceive(CONFIG_I2C_PORT,
											i2cAddress,
											firstArray,
											sizeof(firstArray) / sizeof(firstArray[0]),
											NULL, 0);
											
				
				
				writeError = i2cSendReceive(CONFIG_I2C_PORT,
											i2cAddress,
											hamedsNewArray,
											sizeof(hamedsNewArray) / sizeof(hamedsNewArray[0]),
											NULL, 0);
											
				printf("-> MAIN: Hamed I2C write operation returned %d.\n", writeError);
            } else {
                printf("-> MAIN: error: unable to read Write Sequence from I2C Generic Command object.\n");
            }
        } else {
            printf("-> MAIN: error: unable to read Trigge Condition from I2C Generic Command object.\n");
        }
    } else {
        printf("-> MAIN: error: unable to read I2C Device Address from I2C Generic Command object.\n");
    }

    return success;
}

/**************************************************************************
 * PUBLIC FUNCTIONS
 *************************************************************************/

void app_main()
{
    esp_err_t espError = 0;
    int32_t errorCode = 0;
    wifi_scan_config_t wifiScanConfig;
    uint16_t numWifiApsFound;
    wifi_ap_record_t *pEsp32WifiRecords;
    LocationWifiAp *pWifiRecord;
    bool lwm2mSuccess = false;
    bool dataReady = false;
    int32_t wakeupCause = esp_sleep_get_wakeup_cause();
    struct timeval now;

    gettimeofday(&now, NULL);

    logInit(gLoggingBuffer);
    ledInit();

    // Log some fundamentals
    LOGX(EVENT_SYSTEM_VERSION, SYSTEM_VERSION_INT);
    // Note: the following line will log the time that THIS file was
    // last built so, when doing a formal release, make sure
    // it is a clean build
    LOGX(EVENT_BUILD_TIME_UNIX_FORMAT, __COMPILE_TIME_UNIX__);

    switch (wakeupCause) {
        case ESP_SLEEP_WAKEUP_TIMER:
            printf("Wake up from RTC alarm at %d second(s).\n", (int) now.tv_sec);
        break;
        case ESP_SLEEP_WAKEUP_EXT1:
            printf("Wake up from EXT1 interrupt at %d second(s).\n", (int) now.tv_sec);
        default:
            printf("Wake up at %d second(s), cause %d.\n", (int) now.tv_sec, wakeupCause);
        break;
    }

    // Start everything up
    printf("MAIN: starting up...\n");
    if (init()) {
        ledSetTemporary(LED_STATE_GOOD, 100);
        printf("MAIN: powering up SARA-R4...\n");
        errorCode = cellularPowerOn(NULL);
        if (errorCode == 0) {
            ledSetTemporary(LED_STATE_GOOD, 100);
            printf("MAIN: configuring SARA-R4...\n");
            if (cfgSaraR4()) {
                printf("MAIN: registering with the cellular network...\n");
                gStopTimeCellularMS = esp_timer_get_time() / 1000 + (240 * 1000);
                errorCode = cellularRegister(keepGoingCallback, NULL, NULL, NULL);
                if (errorCode == 0) {
					// While we're waiting for the location, configure LWM2M
					// and tell the server we're up.  Note that if
					// this is our first time to be awake then configuring
					// LWM2M will cause a reboot of the cellular modem which
					// will stop the location fix and the connection, but it's
					// better than waiting around for LWM2M to be ready at
					// startup each time to find out.
					for (int x = 0; (x < LWM2M_WAKEUP_WAIT_SECONDS) && !lwm2mSuccess; x++) {
						lwm2mSuccess = lwm2mReady();
						printf("MAIN: waiting for LWM2M on SARA-R4 to be ready...\n");
						ledSetTemporary(LED_STATE_BAD, 1000);
					}
					if (lwm2mSuccess && cfgLwm2m()) {
						// Check if configuring LWM2M may have caused a reboot of the
						// system, in which case reconnect
						if (cellularGetRegisteredRan() < 0) {
							if (errorCode == 0) {
								// Wait for LWM2M to come back again
								lwm2mSuccess = false;
								for (int x = 0; (errorCode == 0) && (x < LWM2M_WAKEUP_WAIT_SECONDS) && !lwm2mSuccess; x++) {
									lwm2mSuccess = lwm2mReady();
									printf("MAIN: waiting for LWM2M on SARA-R4 to be ready again...\n");
									ledSetTemporary(LED_STATE_BAD, 1000);
								}
							} else {
								ledSetTemporary(LED_STATE_BAD, 1000);
								printf("MAIN: error: unable to re-start a location fix.\n");
							}
						}
						if (errorCode == 0) {
							// If we've got here we have LWM2M configured, we're connected
							// with the network once more and LWM2M is awake.
							while (lwm2mSuccess) {
								// Wait for the server to write stuff if it wants to
								gStopTimeLwm2mMS = esp_timer_get_time() / 1000 + (LWM2M_SERVER_WAIT_TIME_SECONDS * 500);
								printf("MAIN: waiting for LWM2M server to do stuff if it wants to...\n");
								while ((esp_timer_get_time() / 1000) < gStopTimeLwm2mMS) {
									// For debug
									lwm2mRegistrationStatus(WHRE_LWM2M_SERVER_SHORT_ID);
									esp_task_wdt_reset();
									vTaskDelay(100 / portTICK_PERIOD_MS);
									ledSetTemporary(LED_STATE_MIDDLIN, 100);
								}
								// Now read out stuff from the objects which the server might
								// have written to.  All I do here is do some demo I2C
								// operations for now
								dataReady = doI2cDemo();
								if (dataReady) {
									// If we have updated some data in LWM2M,
									// hang around for it to get to the server
									gStopTimeLwm2mMS = esp_timer_get_time() / 1000 + (LWM2M_SERVER_WAIT_TIME_SECONDS * 1000);
									printf("MAIN: waiting for LWM2M server to get new data...\n");
									while ((esp_timer_get_time() / 1000) < gStopTimeLwm2mMS) {
										// For debug
										lwm2mRegistrationStatus(WHRE_LWM2M_SERVER_SHORT_ID);
										esp_task_wdt_reset();
										vTaskDelay(100 / portTICK_PERIOD_MS);
										ledSetTemporary(LED_STATE_MIDDLIN, 100);
									}
								}
							}
						} else {
							ledSet(LED_STATE_BAD);
							printf("MAIN: error: unable to re-register with the cellular network (%d).\n", errorCode);
						}
					} else {
						printf("MAIN: warning: unable to configure LWM2M on SARA-R4.\n");
						ledSet(LED_STATE_BAD);
					}
                    cellularDisconnect();
                } else {
                    ledSet(LED_STATE_BAD);
                    printf("MAIN: error: unable to register with the cellular network (%d).\n", errorCode);
                }
            } else {
                ledSet(LED_STATE_BAD);
                printf("MAIN: error: unable to configure SARA-R4.\n");
            }
            cellularPowerOff();
        } else {
            ledSet(LED_STATE_BAD);
            printf("MAIN: error: unable to power up SARA-R4 (%d).\n", errorCode);
        }
    } else {
        ledSet(LED_STATE_BAD);
    }

    // Set ext1 interrupt, which uses RTC HW, unlike ext 0 which requires the RTC peripherals to remain powered)
    if (esp_sleep_enable_ext1_wakeup(1ULL << CONFIG_PIN_INT_ACCELEROMETER, ESP_EXT1_WAKEUP_ALL_LOW) == ESP_OK) {
        printf("MAIN: accelerometer GPIO (ESP32 GPIO %d) set as EXT1 wake-up source, low level.\n", CONFIG_PIN_INT_ACCELEROMETER);
    }
    // Install ISR
    if (gpio_install_isr_service(ESP_INTR_FLAG_LOWMED) == ESP_OK) {
        printf("MAIN: GPIO service installed with flags 0x%02x.\n", ESP_INTR_FLAG_LOWMED);
    }
    // Set up accelerometer interrupt
    errorCode = accelerometerSetInterruptThreshold(CONFIG_LIS2DW_INTERRUPT_THRESHOLD_MG,
                                                   CONFIG_LIS2DW_INTERRUPT_DURATION_SECONDS);
    if (errorCode == 0) {
        errorCode = accelerometerSetInterruptEnable(true, NULL, NULL);
        if (errorCode == 0) {
            printf("MAIN: accelerometer threshold enabled, set to %d mg for %d second(s)\n",
                   CONFIG_LIS2DW_INTERRUPT_THRESHOLD_MG, CONFIG_LIS2DW_INTERRUPT_DURATION_SECONDS);
        } else {
            printf("MAIN: error, unable to set enable accelerometer interrupt (%d).\n",
                   errorCode);
        }
    } else {
        printf("MAIN: error, unable to set accelerometer threshold to %d mg for %d second(s) (%d).\n",
               CONFIG_LIS2DW_INTERRUPT_THRESHOLD_MG, CONFIG_LIS2DW_INTERRUPT_DURATION_SECONDS, errorCode);
    }

    deInit();
    gettimeofday(&now, NULL);
    printf("MAIN: entering hibernate for %d second(s) at %d second(s)...\n",
           SLEEP_TIME_USECONDS / 1000000, (int) (now.tv_sec) + 1);
    ledSet(LED_STATE_OFF);
    vTaskDelay(1000 / portTICK_PERIOD_MS);

    // The ESP32 SW API reference doesn't refer to hibernate but, from this
    // forum question: https://www.esp32.com/viewtopic.php?f=2&t=3083, it seems
    // that configuring for deep sleep in a certain way should result in
    // hibernate. See also this example:
    // https://github.com/espressif/esp-idf/blob/cc5673435be92c4beceb7108c738d6741ea7230f/examples/system/deep_sleep/main/deep_sleep_example_main.c
    // This should put the processor into hibernate, from which it
    // can awake via RTC timer or ext1 interrupt.
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_SLOW_MEM, ESP_PD_OPTION_OFF);
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_FAST_MEM, ESP_PD_OPTION_OFF);
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_OFF);
    esp_sleep_enable_timer_wakeup(SLEEP_TIME_USECONDS);
    esp_deep_sleep_start();
}

// End of file
