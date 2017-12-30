/**************************************************************************************************
  Filename:       BlueBasic.c
  Revised:        $Date: 2010-08-06 08:56:11 -0700 (Fri, 06 Aug 2010) $
  Revision:       $Revision: 23333 $

  Description:    Basic interpreter for the CC254x BLE device.

  Copyright 2014 Tim Wilkinson. All rights reserved.

  Based on sample code with the following copyright notice:

  Copyright 2010 - 2013 Texas Instruments Incorporated. All rights reserved.

  IMPORTANT: Your use of this Software is limited to those specific rights
  granted under the terms of a software license agreement between the user
  who downloaded the software, his/her employer (which must be your employer)
  and Texas Instruments Incorporated (the "License").  You may not use this
  Software unless you agree to abide by the terms of the License. The License
  limits your use, and you acknowledge, that the Software may not be modified,
  copied or distributed unless embedded on a Texas Instruments microcontroller
  or used solely and exclusively in conjunction with a Texas Instruments radio
  frequency transceiver, which is integrated into your product.  Other than for
  the foregoing purpose, you may not use, reproduce, copy, prepare derivative
  works of, modify, distribute, perform, display or sell this Software and/or
  its documentation for any purpose.

  YOU FURTHER ACKNOWLEDGE AND AGREE THAT THE SOFTWARE AND DOCUMENTATION ARE
  PROVIDED �AS IS� WITHOUT WARRANTY OF ANY KIND, EITHER EXPRESS OR IMPLIED,
  INCLUDING WITHOUT LIMITATION, ANY WARRANTY OF MERCHANTABILITY, TITLE,
  NON-INFRINGEMENT AND FITNESS FOR A PARTICULAR PURPOSE. IN NO EVENT SHALL
  TEXAS INSTRUMENTS OR ITS LICENSORS BE LIABLE OR OBLIGATED UNDER CONTRACT,
  NEGLIGENCE, STRICT LIABILITY, CONTRIBUTION, BREACH OF WARRANTY, OR OTHER
  LEGAL EQUITABLE THEORY ANY DIRECT OR INDIRECT DAMAGES OR EXPENSES
  INCLUDING BUT NOT LIMITED TO ANY INCIDENTAL, SPECIAL, INDIRECT, PUNITIVE
  OR CONSEQUENTIAL DAMAGES, LOST PROFITS OR LOST DATA, COST OF PROCUREMENT
  OF SUBSTITUTE GOODS, TECHNOLOGY, SERVICES, OR ANY CLAIMS BY THIRD PARTIES
  (INCLUDING BUT NOT LIMITED TO ANY DEFENSE THEREOF), OR OTHER SIMILAR COSTS.

  Should you have any questions regarding your right to use this Software,
  contact Texas Instruments Incorporated at www.TI.com.
**************************************************************************************************/

/*********************************************************************
 * INCLUDES
 */

#include "bcomdef.h"
#include "OSAL.h"
#include "OSAL_PwrMgr.h"
#include "hal_uart.h"

#include "OnBoard.h"

#include "gatt.h"

#include "hci.h"

#include "gapgattserver.h"
#include "gattservapp.h"
#include "devinfoservice.h"
#include "linkdb.h"

#include "observer.h"
#include "peripheral.h"

#include "gapbondmgr.h"

#include "BlueBasic.h"

#if defined FEATURE_OAD
  #include "oad.h"
  #include "oad_target.h"
#endif

#include "os.h"

/*********************************************************************
 * MACROS
 */

/*********************************************************************
 * CONSTANTS
 */

// What is the advertising interval when device is discoverable (units of 625us, 160=100ms, 1600=1s)
// according to Apple Bluetooth Design Guide R8:
// The recommended advertising pattern and advertising intervals are:
// - First, advertise at 20 ms intervals for at least 30 seconds
// - If not discovered after 30 seconds, you may change to one of the following longer intervals:
//   152.5 ms, 211.25 ms, 318.75 ms, 417.5 ms, 546.25 ms, 760 ms, 852.5 ms, 1022.5 ms, 1285 ms

//#define  DEFAULT_ADVERTISING_INTERVAL          160  // 100 ms
#define  DEFAULT_ADVERTISING_INTERVAL          244  // 152.5 ms
//#define  DEFAULT_ADVERTISING_INTERVAL          338  // 211.25 ms
//#define  DEFAULT_ADVERTISING_INTERVAL          510  // 318.75 ms
//#define  DEFAULT_ADVERTISING_INTERVAL          668  // 417.5 ms
//#define  DEFAULT_ADVERTISING_INTERVAL          874  // 546.25 ms
//#define  DEFAULT_ADVERTISING_INTERVAL          1216 // 760 ms
//#define  DEFAULT_ADVERTISING_INTERVAL          1364 // 852.5 ms
//#define  DEFAULT_ADVERTISING_INTERVAL          1636 // 1022.5 ms
//#define  DEFAULT_ADVERTISING_INTERVAL          2056 // 1285 ms

// initial connection interval in units of 1.25ms
#define DEFAULT_CONNECTION_INTERVAL_MIN 40  //30ms
#define DEFAULT_CONNECTION_INTERVAL_MAX 52  //45ms
#define DEFAULT_CONNECTION_LATENCY 0
#define DEFAULT_CONNECTION_TIMEOUT 600

// Minimum connection interval (units of 1.25ms, 80=100ms) if automatic parameter update request is enabled
#define DEFAULT_DESIRED_MIN_CONN_INTERVAL     DEFAULT_CONNECTION_INTERVAL_MIN
// Maximum connection interval (units of 1.25ms, 800=1000ms) if automatic parameter update request is enabled
#define DEFAULT_DESIRED_MAX_CONN_INTERVAL     DEFAULT_CONNECTION_INTERVAL_MAX
// Slave latency to use if automatic parameter update request is enabled
#define DEFAULT_DESIRED_SLAVE_LATENCY         DEFAULT_CONNECTION_LATENCY
// Supervision timeout value (units of 10ms, 1000=10s) if automatic parameter update request is enabled
#define DEFAULT_DESIRED_CONN_TIMEOUT          DEFAULT_CONNECTION_TIMEOUT
// Whether to enable automatic parameter update request when a connection is formed
#define DEFAULT_ENABLE_UPDATE_REQUEST         TRUE

#define INVALID_CONNHANDLE                    0xFFFF

/*********************************************************************
 * TYPEDEFS
 */

/*********************************************************************
 * GLOBAL VARIABLES
 */
uint8 blueBasic_TaskID;   // Task ID for internal task/event processing

/*********************************************************************
 * EXTERNAL VARIABLES
 */

extern uint8 sbuf_read_pos;
extern unsigned char sbuf[];
extern unsigned char sflow;

/*********************************************************************
 * EXTERNAL FUNCTIONS
 */
extern void ble_connection_status(uint16 connHandle, uint8 changeType, int8 rssi);


/*********************************************************************
 * LOCAL VARIABLES
 */

#ifdef ENABLE_BLE_CONSOLE

#include "gatt_uuid.h"
#include "gap.h"

#define CONSOLE_PROFILE_SERV_UUID 0x3A, 0xA7, 0xBD, 0x64, 0x0a, 0xF7, 0xA3, 0xB5, 0x8D, 0x44, 0x16, 0x16, 0x91, 0x9E, 0xFB, 0x25
static CONST uint8 consoleProfileServUUID[] = { CONSOLE_PROFILE_SERV_UUID };
static CONST gattAttrType_t consoleProfileService = { ATT_UUID_SIZE, consoleProfileServUUID };
static CONST uint8 inputUUID[] = { 0xB2, 0x07, 0xCF, 0x1D, 0x47, 0x2F, 0x49, 0x37, 0xB5, 0x9F, 0x6B, 0x67, 0xE2, 0xC9, 0xFB, 0xC3 };
static CONST uint8 inputProps = GATT_PROP_READ|GATT_PROP_NOTIFY;
static CONST uint8 outputUUID[] = { 0x6D, 0x7E, 0xE5, 0x7D, 0xFB, 0x7A, 0x4B, 0xF7, 0xB2, 0x1C, 0x92, 0xFE, 0x3C, 0x9B, 0xAF, 0xD6 };
static CONST uint8 outputProps = GATT_PROP_WRITE;
static gattCharCfg_t *consoleProfileCharCfg1 = NULL;
static gattCharCfg_t *consoleProfileCharCfg2 = NULL;

#define SERVICE_CHANGE 0

#if SERVICE_CHANGE
static CONST uint8 serviceProps = GATT_PROP_READ | GATT_PROP_INDICATE;
static gattCharCfg_t serviceChanged = {0};
static CONST uint8 serviceHandles[] = {1, 0, 0xff, 0xff}; 
static gattCharCfg_t *serviceChangedCharCfg = &serviceChanged;
#endif

unsigned char ble_console_enabled;
unsigned char block_during_con;

static struct
{
  //uint8 write[128];
  uint8 write[(ATT_MTU_SIZE - 3)*1 + 1]; // MTU payload + 1 byte = 21 bytes
  uint8* writein;
  uint8* writeout;
} io;

static CONST unsigned char consoleInputDesc[] = "Console in";
static CONST unsigned char consoleOutputDesc[] = "Console out";

#define GATT_PERMIT_RW GATT_PERMIT_READ|GATT_PERMIT_WRITE

static gattAttribute_t consoleProfile[] =
{
  // Primary Service
  { { ATT_BT_UUID_SIZE, primaryServiceUUID },   GATT_PERMIT_READ,   0, (uint8*)&consoleProfileService },
  // Console Input Characteristics
  { { ATT_BT_UUID_SIZE, characterUUID },        GATT_PERMIT_READ,   0, (uint8*)&inputProps },
  { { ATT_UUID_SIZE, inputUUID },               GATT_PERMIT_RW,     0, io.write },
  { { ATT_BT_UUID_SIZE, clientCharCfgUUID },    GATT_PERMIT_RW,     0, (uint8*)&consoleProfileCharCfg1 },
  { { ATT_BT_UUID_SIZE, charUserDescUUID },     GATT_PERMIT_READ,   0, (uint8*)consoleInputDesc },
  // Console Output Charactersitics
  { { ATT_BT_UUID_SIZE, characterUUID },        GATT_PERMIT_READ,   0, (uint8*)&outputProps },
  { { ATT_UUID_SIZE, outputUUID },              GATT_PERMIT_WRITE,  0, NULL },
  { { ATT_BT_UUID_SIZE, clientCharCfgUUID },    GATT_PERMIT_RW,     0, (uint8*) &consoleProfileCharCfg2 },
  { { ATT_BT_UUID_SIZE, charUserDescUUID },     GATT_PERMIT_READ,   0, (uint8*)consoleOutputDesc },
#if SERVICE_CHANGE  
  // Generic Attribute Service
  { { ATT_BT_UUID_SIZE, gattServiceUUID},       GATT_PERMIT_READ,   0, (uint8*)&serviceProps },
  { { ATT_BT_UUID_SIZE, serviceChangedUUID},    GATT_PERMIT_READ,   0, (uint8*)&serviceHandles },
  { { ATT_BT_UUID_SIZE, clientCharCfgUUID},     GATT_PERMIT_RW,     0, (uint8*)&serviceChanged }
#endif
};

static bStatus_t consoleProfile_ReadAttrCB(uint16 connHandle, gattAttribute_t *pAttr, uint8 *pValue, uint8 *pLen, uint16 offset, uint8 maxLen, uint8 method);
static bStatus_t consoleProfile_WriteAttrCB(uint16 connHandle, gattAttribute_t *pAttr, uint8 *pValue, uint8 len, uint16 offset, uint8 method);

static CONST gattServiceCBs_t consoleProfileCB =
{
  consoleProfile_ReadAttrCB,
  consoleProfile_WriteAttrCB,
  NULL
};

static CONST uint8 consoleAdvert[] =
{
  0x02, GAP_ADTYPE_FLAGS, GAP_ADTYPE_FLAGS_LIMITED|GAP_ADTYPE_FLAGS_BREDR_NOT_SUPPORTED,
#if 0
  0x11, GAP_ADTYPE_128BIT_MORE, CONSOLE_PROFILE_SERV_UUID,
#else
  // connection interval range
  0x05,   // length of this data
  GAP_ADTYPE_SLAVE_CONN_INTERVAL_RANGE,
  LO_UINT16( DEFAULT_CONNECTION_INTERVAL_MIN ),
  HI_UINT16( DEFAULT_CONNECTION_INTERVAL_MIN ),
  LO_UINT16( DEFAULT_CONNECTION_INTERVAL_MAX ),
  HI_UINT16( DEFAULT_CONNECTION_INTERVAL_MAX ),
#endif    
  0x09, GAP_ADTYPE_LOCAL_NAME_COMPLETE, 'B', 'A', 'S', 'I', 'C', '#', '?', '?'
};

#endif

#if ENABLE_FAKE_OAD_PROFILE

static CONST uint8 oadProfileServiceUUID[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xB0, 0x00, 0x40, 0x51, 0x04, 0xC0, 0xFF, 0x00, 0xF0 };
static CONST uint8 oadIdentUUID[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xB0, 0x00, 0x40, 0x51, 0x04, 0xC1, 0xFF, 0x00, 0xF0 };
static CONST uint8 oadBlockUUID[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xB0, 0x00, 0x40, 0x51, 0x04, 0xC2, 0xFF, 0x00, 0xF0 };
static CONST gattAttrType_t oadProfileService = { ATT_UUID_SIZE, oadProfileServiceUUID };
static CONST uint8 oadCharProps = GATT_PROP_WRITE_NO_RSP | GATT_PROP_WRITE | GATT_PROP_NOTIFY;
static CONST gattCharCfg_t *oadConfig;
static CONST unsigned char oadIdentDesc[] = "Img Identify";
static CONST unsigned char oadBlockDesc[] = "Img Block";

static gattAttribute_t oadProfile[] =
{
  { { ATT_BT_UUID_SIZE, primaryServiceUUID },   GATT_PERMIT_READ,                       0, (uint8*)&oadProfileService },
  { { ATT_BT_UUID_SIZE, characterUUID },        GATT_PERMIT_READ,                       0, (uint8*)&oadCharProps },
  { { ATT_UUID_SIZE, oadIdentUUID },            GATT_PERMIT_WRITE,                      0, NULL },
  { { ATT_BT_UUID_SIZE, clientCharCfgUUID },    GATT_PERMIT_READ|GATT_PERMIT_WRITE,     0, (uint8*)&oadConfig },
  { { ATT_BT_UUID_SIZE, charUserDescUUID },     GATT_PERMIT_READ,                       0, (uint8*)oadIdentDesc },
  { { ATT_BT_UUID_SIZE, characterUUID },        GATT_PERMIT_READ,                       0, (uint8*)&oadCharProps },
  { { ATT_UUID_SIZE, oadBlockUUID },            GATT_PERMIT_WRITE,                      0, NULL },
  { { ATT_BT_UUID_SIZE, clientCharCfgUUID },    GATT_PERMIT_READ|GATT_PERMIT_WRITE,     0, (uint8*)&oadConfig },
  { { ATT_BT_UUID_SIZE, charUserDescUUID },     GATT_PERMIT_READ,                       0, (uint8*)oadBlockDesc }
};

#endif

/*********************************************************************
 * LOCAL FUNCTIONS
 */
   
/*********************************************************************
 * PROFILE CALLBACKS
 */

#if ( HOST_CONFIG & OBSERVER_CFG )
// Observer Role Callback
static uint8 blueBasic_deviceFound( gapObserverRoleEvent_t *pEvent );

// Observer Role Callbacks
static CONST gapObserverRoleCB_t blueBasic_ObserverCBs =
{
  NULL,   //!< RSSI callback.
  blueBasic_deviceFound, //!< Event callback.
};
#endif

// GAP Link callback
static void blueBasic_HandleConnStatusCB(uint16 connHandle, uint8 changeType);
static void blueBasic_RSSIUpdate(int8 rssi);

static void bluebasic_ParamUpdateCB( uint16 connInterval,
                                         uint16 connSlaveLatency,
                                         uint16 connTimeout );
static void bluebasic_StateNotificationCB( gaprole_States_t newState );


// GAP Role Callbacks
static CONST gapRolesCBs_t blueBasic_PeripheralCBs =
{
  bluebasic_StateNotificationCB,  // Profile State Change Callbacks
  blueBasic_RSSIUpdate,           // When a valid RSSI is read from controller
};


#ifdef GAP_BOND_MGR
// GAP Bond Manager Callbacks
static CONST gapBondCBs_t blueBasic_BondMgrCBs =
{
  NULL,                     // Passcode callback (not used by application)
  NULL                      // Pairing / Bonding state Callback (not used by application)
};
#endif

/*********************************************************************
 * PUBLIC FUNCTIONS
 */

/*********************************************************************
 * @fn      BlueBasic_Init
 *
 * @brief   Initialization function for the Blue Basic App Task.
 *          This is called during initialization and should contain
 *          any application specific initialization (ie. hardware
 *          initialization/setup, table initialization, power up
 *          notificaiton ... ).
 *
 * @param   task_id - the ID assigned by OSAL.  This ID should be
 *                    used to send messages and set timers.
 *
 * @return  none
 */
void BlueBasic_Init( uint8 task_id )
{
  blueBasic_TaskID = task_id;

#ifdef ENABLE_BLE_CONSOLE
  GAPRole_SetParameter( GAPROLE_ADVERT_DATA, sizeof(consoleAdvert), (void*)consoleAdvert );
#endif
  
#if 1
  // Setup the GAP Peripheral Role Profile
  {
    uint8 enable_update_request = DEFAULT_ENABLE_UPDATE_REQUEST;
    uint16 desired_min_interval = DEFAULT_DESIRED_MIN_CONN_INTERVAL;
    uint16 desired_max_interval = DEFAULT_DESIRED_MAX_CONN_INTERVAL;
    uint16 desired_slave_latency = DEFAULT_DESIRED_SLAVE_LATENCY;
    uint16 desired_conn_timeout = DEFAULT_DESIRED_CONN_TIMEOUT;

    // Set the GAP Role Parameters
    GAPRole_SetParameter( GAPROLE_PARAM_UPDATE_ENABLE, sizeof( uint8 ), &enable_update_request );
    GAPRole_SetParameter( GAPROLE_MIN_CONN_INTERVAL, sizeof( uint16 ), &desired_min_interval );
    GAPRole_SetParameter( GAPROLE_MAX_CONN_INTERVAL, sizeof( uint16 ), &desired_max_interval );
    GAPRole_SetParameter( GAPROLE_SLAVE_LATENCY, sizeof( uint16 ), &desired_slave_latency );
    GAPRole_SetParameter( GAPROLE_TIMEOUT_MULTIPLIER, sizeof( uint16 ), &desired_conn_timeout );
  }
  
  // Set desired default connection parameters
  GAP_SetParamValue( TGAP_CONN_EST_INT_MIN, DEFAULT_CONNECTION_INTERVAL_MIN);
  GAP_SetParamValue( TGAP_CONN_EST_INT_MAX, DEFAULT_CONNECTION_INTERVAL_MAX);
  GAP_SetParamValue( TGAP_CONN_EST_LATENCY, DEFAULT_CONNECTION_LATENCY);
  GAP_SetParamValue( TGAP_CONN_EST_SUPERV_TIMEOUT, DEFAULT_CONNECTION_TIMEOUT);
  
#endif

  // Set advertising interval
  GAP_SetParamValue( TGAP_LIM_DISC_ADV_INT_MIN, DEFAULT_ADVERTISING_INTERVAL );
  GAP_SetParamValue( TGAP_LIM_DISC_ADV_INT_MAX, DEFAULT_ADVERTISING_INTERVAL );
  GAP_SetParamValue( TGAP_GEN_DISC_ADV_INT_MIN, DEFAULT_ADVERTISING_INTERVAL );
  GAP_SetParamValue( TGAP_GEN_DISC_ADV_INT_MAX, DEFAULT_ADVERTISING_INTERVAL );
  
#if 0 && GAP_BOND_MGR  
   // Setup the GAP Bond Manager to require pairing with pin code
  {
    uint32 passkey = 0; // passkey "000000"
    uint8 pairMode = GAPBOND_PAIRING_MODE_WAIT_FOR_REQ;
    uint8 mitm = TRUE;
    uint8 ioCap = GAPBOND_IO_CAP_DISPLAY_ONLY;
    uint8 bonding = TRUE;

    GAPBondMgr_SetParameter( GAPBOND_DEFAULT_PASSCODE, sizeof ( uint32 ), &passkey );
    GAPBondMgr_SetParameter( GAPBOND_PAIRING_MODE, sizeof ( uint8 ), &pairMode );
    GAPBondMgr_SetParameter( GAPBOND_MITM_PROTECTION, sizeof ( uint8 ), &mitm );
    GAPBondMgr_SetParameter( GAPBOND_IO_CAPABILITIES, sizeof ( uint8 ), &ioCap );
    GAPBondMgr_SetParameter( GAPBOND_BONDING_ENABLED, sizeof ( uint8 ), &bonding );
  }
#endif  
  
  // Initialize GATT attributes
  GGS_AddService( GATT_ALL_SERVICES );            // GAP
  GATTServApp_AddService( GATT_ALL_SERVICES );    // GATT attributes
#if ENABLE_FAKE_OAD_PROFILE 
  GATTServApp_RegisterService(oadProfile, GATT_NUM_ATTRS(oadProfile),
                              GATT_MAX_ENCRYPT_KEY_SIZE, NULL);
#endif
  DevInfo_AddService();                           // Device Information Service
#if defined FEATURE_OAD
  VOID OADTarget_AddService();                    // OAD Profile
#endif

  // Enable clock divide on halt
  // This reduces active current while radio is active and CC254x MCU
  // is halted
#ifdef ENABLE_BLE_CONSOLE
  // do not devide the clock when DMA is configured
#if !HAL_UART_DMA
  // See: http://e2e.ti.com/support/wireless_connectivity/f/538/p/169944/668822.aspx#664740
  HCI_EXT_ClkDivOnHaltCmd(HCI_EXT_ENABLE_CLK_DIVIDE_ON_HALT);
#else
  HCI_EXT_HaltDuringRfCmd(HCI_EXT_HALT_DURING_RF_DISABLE);
  HCI_EXT_ClkDivOnHaltCmd(HCI_EXT_DISABLE_CLK_DIVIDE_ON_HALT);
#endif
#endif

#if !BLUESTECA
  // Overlap enabled
  HCI_EXT_OverlappedProcessingCmd(HCI_EXT_ENABLE_OVERLAPPED_PROCESSING);
#endif
  // Setup a delayed profile startup
  osal_set_event( blueBasic_TaskID, BLUEBASIC_START_DEVICE_EVT );
}

/*********************************************************************
 * @fn      BlueBasic_ProcessEvent
 *
 * @brief   Blue Basic Application Task event processor.  This function
 *          is called to process all events for the task.  Events
 *          include timers, messages and any other user defined events.
 *
 * @param   task_id  - The OSAL assigned task ID.
 * @param   events - events to process.  This is a bit map and can
 *                   contain more than one event.
 *
 * @return  events not processed
 */
uint16 BlueBasic_ProcessEvent( uint8 task_id, uint16 events )
{
  unsigned char i;

  VOID task_id; // OSAL required parameter that isn't used in this function

  if ( events & SYS_EVENT_MSG )
  {
    uint8 *pMsg;

    if ( (pMsg = osal_msg_receive( blueBasic_TaskID )) != NULL )
    {
      // Release the OSAL message
      VOID osal_msg_deallocate( pMsg );
    }

    // return unprocessed events
    return (events ^ SYS_EVENT_MSG);
  }

  if ( events & BLUEBASIC_START_DEVICE_EVT )
  {
#if ( HOST_CONFIG & OBSERVER_CFG )   
     // Start Observer
    VOID GAPObserverRole_StartDevice( (gapObserverRoleCB_t *) &blueBasic_ObserverCBs);
#endif       
    // Start the Device
    VOID GAPRole_StartDevice( (gapRolesCBs_t *) &blueBasic_PeripheralCBs );
    
#ifdef GAP_BOND_MGR
    // Start Bond Manager
    VOID GAPBondMgr_Register( (gapBondCBs_t *) &blueBasic_BondMgrCBs );
#endif
    // Start monitoring links
    VOID linkDB_Register( blueBasic_HandleConnStatusCB );
    
    // Start monitoring parameters updates
    GAPRole_RegisterAppCBs( bluebasic_ParamUpdateCB);
    
#ifdef ENABLE_BLE_CONSOLE
    // Allocate Client Characteristic Configuration table
    consoleProfileCharCfg1 = (gattCharCfg_t *)osal_mem_alloc( sizeof(gattCharCfg_t)
                                                            * linkDBNumConns * 2);  
    if (consoleProfileCharCfg1 != NULL)
    {
      consoleProfileCharCfg2 = consoleProfileCharCfg1 
        + sizeof(gattCharCfg_t) * linkDBNumConns; 
      GATTServApp_InitCharCfg(INVALID_CONNHANDLE, consoleProfileCharCfg1);
      GATTServApp_InitCharCfg(INVALID_CONNHANDLE, consoleProfileCharCfg2);
      GATTServApp_RegisterService(consoleProfile, GATT_NUM_ATTRS(consoleProfile),
                                  GATT_MAX_ENCRYPT_KEY_SIZE, &consoleProfileCB);
    }
#endif

    // Start Interpreter
    interpreter_setup();

    return ( events ^ BLUEBASIC_START_DEVICE_EVT );
  }

#define UART_SCAN_TIME 2
  
#ifdef ENABLE_BLE_CONSOLE
  if ( events & BLUEBASIC_CONNECTION_EVENT )
  {
    while (io.writein != io.writeout)
    {
      uint8* save = io.writeout;
      if (GATTServApp_ProcessCharCfg(consoleProfileCharCfg1, io.write,
                                     FALSE, consoleProfile,
                                     GATT_NUM_ATTRS(consoleProfile), INVALID_TASK_ID,
                                     consoleProfile_ReadAttrCB) != SUCCESS)
      {
        // fail, so we try it later again ...
        io.writeout = save;
        return events;
      }
    }
    // when empty clear the event
    return ( events ^ BLUEBASIC_CONNECTION_EVENT );
  }
#endif

  if ( events & BLUEBASIC_INPUT_AVAILABLE )
  {
    interpreter_loop();
    return (events ^ BLUEBASIC_INPUT_AVAILABLE);
  }

  // fix for Android to successfully read the GATT table
  // this event inidcates that the initial connection
  // has passed and the descriptor should have been read already
  // so we can set the time slice longer so the inzterpreter can run longer
  if ( events & BLUEBASIC_EVENT_CON )
  {
    timeSlice = 21;
//    P1 &= 0xFE;
    block_during_con = 0;
    // we clear the event and continue
    events ^= BLUEBASIC_EVENT_CON;
  }

  if ( block_during_con )
    return 0;
    
  // in case of a valid line number a yield is pending
  // so we clear the line number and let the interpreter run
  if (bluebasic_yield_linenum)
    {
      unsigned short ln = bluebasic_yield_linenum;
      bluebasic_yield_linenum = 0;
      interpreter_run(ln, 0);
    }
  
  // return when yield event was present or a new yield is scheduled
  if ( (events & BLUEBASIC_EVENT_YIELD) || bluebasic_yield_linenum)
  {
    return (events & ~BLUEBASIC_EVENT_YIELD);
  }
  
  if ( events & BLUEBASIC_EVENT_INTERRUPTS )
  {
    for (i = 0; i < OS_MAX_INTERRUPT; i++)
    {
      if (blueBasic_interrupts[i].linenum && (events & (BLUEBASIC_EVENT_INTERRUPT << i)))
      {
        interpreter_run(blueBasic_interrupts[i].linenum, 1);
      }
    }
    return (events ^ (events & BLUEBASIC_EVENT_INTERRUPTS));
  }

#if 1
  if ( events & BLUEBASIC_EVENT_TIMERS )
  {
    for (i = 0; i < OS_MAX_TIMER; i++)
    {
      if (blueBasic_timers[i].linenum && (events & (BLUEBASIC_EVENT_TIMER << i)))
      {
        interpreter_run(blueBasic_timers[i].linenum, i == DELAY_TIMER ? 0 : 1);
      }
    }
    return (events ^ (events & BLUEBASIC_EVENT_TIMERS));
  }
#endif
    
#if 0   
  // experimental to give DELAY prio so no timer can fire in between
  // to avoid potential memory leagage on the the heap
  if ( events & BLUEBASIC_EVENT_TIMERS )
  {
    // first execute DELAY 
    if ( events & (BLUEBASIC_EVENT_TIMER << DELAY_TIMER) )
    {
      if ( blueBasic_timers[DELAY_TIMER].linenum )
      {
        interpreter_run(blueBasic_timers[DELAY_TIMER].linenum, 0);
        blueBasic_timers[DELAY_TIMER].linenum = 0;
      }
      return (events ^ (BLUEBASIC_EVENT_TIMER << DELAY_TIMER));
    }
    // only execute timers when DELAY is not pending anymore
    if (blueBasic_timers[DELAY_TIMER].linenum == 0) 
    {
      for (i = 0; i < OS_MAX_TIMER; i++)
      {
        if (blueBasic_timers[i].linenum && (events & (BLUEBASIC_EVENT_TIMER << i)))
        {
          interpreter_run(blueBasic_timers[i].linenum, i == DELAY_TIMER ? 0 : 1);
          return (events ^ (BLUEBASIC_EVENT_TIMER << i));
        }
      }
    }
//    return (events ^ (events & BLUEBASIC_EVENT_TIMERS));
  }
#endif
  
  if ( events & BLUEBASIC_EVENT_SERIAL )
  {
    if (serial[0].onread && sbuf_read_pos == 16 )
    {
      uint8 len = Hal_UART_RxBufLen(HAL_UART_PORT_0);
      if ( len >= 16 )
      {
#ifdef PROCESS_SERIAL_DATA        
        if (sflow == 'V')
        {
          HalUARTRead(HAL_UART_PORT_0, &sbuf[0], 1);
          if (sbuf[0] == 0xAA)
          {
            uint8 parity = 0;
            uint8 cnt;
            HalUARTRead(HAL_UART_PORT_0, &sbuf[1], 15);
            for (cnt=1; cnt < 15; )
            {
              parity ^= sbuf[cnt++];
            }
            //only send serial data when frame has no parity error
            if (parity == sbuf[15])
            {
              sbuf_read_pos = 0;
              interpreter_run(serial[0].onread, 1);
            }
          }
        }
        else
#endif
        {
          HalUARTRead(HAL_UART_PORT_0, &sbuf[0], 16);
          sbuf_read_pos = 0;
          interpreter_run(serial[0].onread, 1);
        }
      }
    } 
    return (events ^ BLUEBASIC_EVENT_SERIAL);
  }

#ifdef HAL_I2C         
  if ( events & BLUEBASIC_EVENT_I2C )
  {
    if (i2c[0].onread && i2c[0].available_bytes)
    {
      interpreter_run(i2c[0].onread, 1);
    }
    // when read buffer is empty it must have been a write...
    if (i2c[0].onwrite && i2c[0].available_bytes == 0)
    {
      interpreter_run(i2c[0].onwrite, 1);
    }
    return (events ^ BLUEBASIC_EVENT_I2C);
  }
#endif
  
  // Discard unknown events
  return 0;
}

static void blueBasic_HandleConnStatusCB(uint16 connHandle, uint8 changeType)
{
  if (connHandle == LOOPBACK_CONNHANDLE)
  {
    return;
  }
#ifdef ENABLE_BLE_CONSOLE
  if (changeType == LINKDB_STATUS_UPDATE_REMOVED || (changeType == LINKDB_STATUS_UPDATE_STATEFLAGS && !linkDB_Up(connHandle)))
  {
    GATTServApp_InitCharCfg(connHandle, consoleProfileCharCfg1);
    uint8 i;
    for (i = 0; i < linkDBNumConns; i++)
    {
      if (consoleProfileCharCfg1[i].value == 1)
      {
        goto done;
      }
    }
    ble_console_enabled = 0;
  done:;
  }
#endif
  ble_connection_status(connHandle, changeType, 0);
}

static void blueBasic_RSSIUpdate(int8 rssi)
{
  ble_connection_status(0, LINKDB_STATUS_UPDATE_RSSI, rssi);
}

#ifdef ENABLE_BLE_CONSOLE

uint8 ble_console_write(uint8 ch)
{
  if (ble_console_enabled)
  {
    // write buffer is full, so we run osal to get it empty
    while (io.writein - io.writeout + 1 == 0 || io.writein - io.writeout + 1 == sizeof(io.write))
    {
      osal_run_system();
    }
    
    // write buffer empty, so we flag an event for the following ch 
    if (io.writein == io.writeout)
    {
      osal_set_event( blueBasic_TaskID, BLUEBASIC_CONNECTION_EVENT );
    }
 
    *io.writein++ = ch;
    if (io.writein == &io.write[sizeof(io.write)])
    {
      io.writein = io.write;
    }
  }
  return 1;
}

static void bluebasic_ParamUpdateCB( uint16 connInterval,
                                         uint16 connSlaveLatency,
                                         uint16 connTimeout )
{
  //timeSlice = ((connInterval == 0) ? 20 : ((connInterval < 24) ? 0 : connInterval));
}

static void bluebasic_StateNotificationCB( gaprole_States_t newState )
{
//  P1DIR |= 1;
  switch ( newState )
  {
  case GAPROLE_STARTED:
    //P1 &= 0xFE;
    timeSlice = 101;
    block_during_con = 0;
    break;
    
  case GAPROLE_ADVERTISING:
    //P1 &= 0xFE;
    timeSlice = 102;
    block_during_con = 0;
    break;
    
  case GAPROLE_CONNECTED:
    {
      //unsigned short connInterval = 0;
      //GAPRole_GetParameter(GAPROLE_CONN_INTERVAL, &connInterval);
      timeSlice = 20;
//      P1 |= 1;
//      block_during_con = 1;
      osal_start_timerEx(blueBasic_TaskID, BLUEBASIC_EVENT_CON, 4000);
    }
    break;
    
  case GAPROLE_WAITING:
    // Link terminated
//    P1 &= 0xFE;
    timeSlice = 103;
    block_during_con = 0;
    break;
    
  default:
    break;
  }
}


#if ( HOST_CONFIG & OBSERVER_CFG )
static uint8 blueBasic_deviceFound( gapObserverRoleEvent_t *pEvent )
{
  switch ( pEvent->gap.opcode )
  {

    case GAP_DEVICE_INFO_EVENT:
      {
        interpreter_devicefound(pEvent->deviceInfo.addrType,
                                pEvent->deviceInfo.addr,
                                pEvent->deviceInfo.rssi,
                                pEvent->deviceInfo.eventType, 
                                pEvent->deviceInfo.dataLen,
                                pEvent->deviceInfo.pEvtData);
      }
      break;
      
    default:
      break;
  }
  return ( TRUE );  
}
#endif

static bStatus_t consoleProfile_ReadAttrCB(uint16 connHandle, gattAttribute_t *pAttr, uint8 *pValue, uint8 *pLen, uint16 offset, uint8 maxLen, uint8 method)
{
  uint8 len;
  for (len = 0; io.writein != io.writeout && len < maxLen; len++ )
  {
    *pValue++ = *io.writeout++;
    if (io.writeout == &io.write[sizeof(io.write)])
    {
      io.writeout = io.write;
    }
  }
  *pLen = len;

  return SUCCESS;
}

static bStatus_t consoleProfile_WriteAttrCB(uint16 connHandle, gattAttribute_t *pAttr, uint8 *pValue, uint8 len, uint16 offset, uint8 method)
{
  unsigned char i;
  bStatus_t status;

  if (pAttr->type.len == ATT_BT_UUID_SIZE && BUILD_UINT16(pAttr->type.uuid[0], pAttr->type.uuid[1]) == GATT_CLIENT_CHAR_CFG_UUID)
  {
    status = GATTServApp_ProcessCCCWriteReq(connHandle, pAttr, pValue, len, offset, GATT_CLIENT_CFG_NOTIFY);
    // Setup console if we're connected, otherwise disable
    for (i = 0; i < linkDBNumConns; i++)
    {
      if (consoleProfileCharCfg1[i].value == 1)
      {
        io.writein = io.write;
        io.writeout = io.write;
        ble_console_enabled = 1;
        OS_timer_stop(DELAY_TIMER);
        interpreter_banner();
        goto done;
      }
    }
    ble_console_enabled = 0;
  done:
    return status;
  }
  for (i = 0; i < len; i++)
  {
    OS_type(pValue[i]);
  }
  return SUCCESS;
}

#endif

HAL_ISR_FUNCTION(port0Isr, P0INT_VECTOR)
{
  unsigned char status;
  unsigned char i;

  HAL_ENTER_ISR();

  status = P0IFG;
  status &= P0IEN;
  if (status)
  {
    P0IFG = ~status;
    P0IF = 0;

#if HAL_UART_DMA
//    extern uint8 Hal_TaskID;
    extern volatile uint8 dmaRdyIsr;
    dmaRdyIsr = 1;
    // disable power management when UART is selected
//    if (P0SEL & 0x0c)
//    {
//      CLEAR_SLEEP_MODE();
//      osal_pwrmgr_task_state(Hal_TaskID, PWRMGR_HOLD);
//    }
#if HAL_UART_TX_BY_ISR
    if (dmaCfg.txHead == dmaCfg.txTail)
    {
      HAL_UART_DMA_CLR_RDY_OUT();
    }
#endif
#endif // HAL_UART_DMA

    for (i = 0; i < OS_MAX_INTERRUPT; i++)
    {
      if (PIN_MAJOR(blueBasic_interrupts[i].pin) == 0 && (status & (1 << PIN_MINOR(blueBasic_interrupts[i].pin))))
      {
        osal_set_event(blueBasic_TaskID, BLUEBASIC_EVENT_INTERRUPT << i);
      }
    }
  }

  HAL_EXIT_ISR();
}

HAL_ISR_FUNCTION(port1Isr, P1INT_VECTOR)
{
  unsigned char status;
  unsigned char i;

  HAL_ENTER_ISR();

  status = P1IFG;
  status &= P1IEN;
  if (status)
  {
    P1IFG = ~status;
    P1IF = 0;

    for (i = 0; i < OS_MAX_INTERRUPT; i++)
    {
      if (PIN_MAJOR(blueBasic_interrupts[i].pin) == 1 && (status & (1 << PIN_MINOR(blueBasic_interrupts[i].pin))))
      {
        osal_set_event(blueBasic_TaskID, BLUEBASIC_EVENT_INTERRUPT << i);
      }
    }
  }

  HAL_EXIT_ISR();
}

#ifdef HAL_I2C    
// port2Isr conflicts with the i2c interrupt handler from hal_i2c
// so we simply modify it as a function call.
// the hal_i2c isr function should call here to support port 2 interrupts
void bb_port2isr(void)
#else
HAL_ISR_FUNCTION(port2Isr, P2INT_VECTOR)
#endif
{
  unsigned char status;
  unsigned char i;

#ifndef HAL_I2C
  HAL_ENTER_ISR();
#endif
  status = P2IFG;
  status &= P2IEN;
  if (status)
  {
    P2IFG = ~status;
    P2IF = 0;

    for (i = 0; i < OS_MAX_INTERRUPT; i++)
    {
      if (PIN_MAJOR(blueBasic_interrupts[i].pin) == 2 && (status & (1 << PIN_MINOR(blueBasic_interrupts[i].pin))))
      {
        osal_set_event(blueBasic_TaskID, BLUEBASIC_EVENT_INTERRUPT << i);
      }
    }
  }
#ifndef HAL_I2C
  HAL_EXIT_ISR();
#endif
}

         
/*********************************************************************
*********************************************************************/
