/*
  xdrv_11_knx.ino - KNX IP Protocol support for Sonoff-Tasmota

  Copyright (C) 2019  Adrian Scillato  (https://github.com/ascillato)

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifdef USE_KNX
/*********************************************************************************************\
 * KNX support
 *
 * Using libraries:
 *   ESP KNX IP library (https://github.com/envy/esp-knx-ip)

Constants in sonoff.h
-----------------------

#define MAX_KNX_GA             10            Max number of KNX Group Addresses to read that can be set
#define MAX_KNX_CB             10            Max number of KNX Group Addresses to write that can be set
                                             If you change MAX_KNX_CB you also have to change on the esp-knx-ip.h file the following:
                                                       #define MAX_CALLBACK_ASSIGNMENTS  10
                                                       #define MAX_CALLBACKS             10
                                             Both to MAX_KNX_CB

Variables in settings.h
-----------------------

bool          Settings.flag.knx_enabled             Enable/Disable KNX Protocol
uint16_t      Settings.knx_physsical_addr           Physical KNX address of this device
uint8_t       Settings.knx_GA_registered            Number of group address to read
uint8_t       Settings.knx_CB_registered            Number of group address to write
uint16_t      Settings.knx_GA_addr[MAX_KNX_GA]      Group address to read
uint16_t      Settings.knx_CB_addr[MAX_KNX_CB]      Group address to write
uint8_t       Settings.knx_GA_param[MAX_KNX_GA]     Type of Input (relay changed, button pressed, sensor read)
uint8_t       Settings.knx_CB_param[MAX_KNX_CB]     Type of Output (set relay, toggle relay, reply sensor value)

\*********************************************************************************************/

#define XDRV_11  11

#include <esp-knx-ip.h>         // KNX Library

address_t KNX_physs_addr;  // Physical KNX address of this device
address_t KNX_addr;        // KNX Address converter variable

#define KNX_Empty 255

#define TOGGLE_INHIBIT_TIME 15 // 15*50mseg = 750mseg (inhibit time for not toggling again relays by a KNX toggle command)

float last_temp;
float last_hum;
uint8_t toggle_inhibit;

typedef struct __device_parameters
{
  uint8_t type;        // PARAMETER_ID. Used as type of GA = relay, button, sensor, etc, (INPUTS)
                       // used when an action on device triggers a MSG to send on KNX
                       // Needed because this is the value that the ESP_KNX_IP library will pass as parameter
                       // to identify the action to perform when a MSG is received

  bool show;           // HARDWARE related. to identify if the parameter exists on the device.

  bool last_state;     // LAST_STATE of relays

  callback_id_t CB_id; // ACTION_ID. To store the ID value of Registered_CB to the library.
                       // The ESP_KNX_IP requires to register the callbacks, and then, to assign an address to the registered callback
                       // So CB_id is needed to store the ID of the callback to then, assign multiple addresses to the same ID (callback)
                       // It is used as type of CB = set relay, toggle relay, reply sensor, etc, (OUTPUTS)
                       // used when a MSG receive  KNX triggers an action on the device
                       // - Multiples address to the same callback (i.e. Set Relay 1 Status) are used on scenes for example
} device_parameters_t;

// device parameters (information that can be sent)
device_parameters_t device_param[] = {
  {  1, false, false, KNX_Empty }, // device_param[ 0] = Relay 1
  {  2, false, false, KNX_Empty }, // device_param[ 1] = Relay 2
  {  3, false, false, KNX_Empty }, // device_param[ 2] = Relay 3
  {  4, false, false, KNX_Empty }, // device_param[ 3] = Relay 4
  {  5, false, false, KNX_Empty }, // device_param[ 4] = Relay 5
  {  6, false, false, KNX_Empty }, // device_param[ 5] = Relay 6
  {  7, false, false, KNX_Empty }, // device_param[ 6] = Relay 7
  {  8, false, false, KNX_Empty }, // device_param[ 7] = Relay 8
  {  9, false, false, KNX_Empty }, // device_param[ 8] = Button 1
  { 10, false, false, KNX_Empty }, // device_param[ 9] = Button 2
  { 11, false, false, KNX_Empty }, // device_param[10] = Button 3
  { 12, false, false, KNX_Empty }, // device_param[11] = Button 4
  { 13, false, false, KNX_Empty }, // device_param[12] = Button 5
  { 14, false, false, KNX_Empty }, // device_param[13] = Button 6
  { 15, false, false, KNX_Empty }, // device_param[14] = Button 7
  { 16, false, false, KNX_Empty }, // device_param[15] = Button 8
  { KNX_TEMPERATURE, false, false, KNX_Empty }, // device_param[16] = Temperature
  { KNX_HUMIDITY   , false, false, KNX_Empty }, // device_param[17] = humidity
  { KNX_ENERGY_VOLTAGE   , false, false, KNX_Empty },
  { KNX_ENERGY_CURRENT   , false, false, KNX_Empty },
  { KNX_ENERGY_POWER   , false, false, KNX_Empty },
  { KNX_ENERGY_POWERFACTOR   , false, false, KNX_Empty },
  { KNX_ENERGY_DAILY   , false, false, KNX_Empty },
  { KNX_ENERGY_START   , false, false, KNX_Empty },
  { KNX_ENERGY_TOTAL   , false, false, KNX_Empty },
  { KNX_SLOT1 , false, false, KNX_Empty },
  { KNX_SLOT2 , false, false, KNX_Empty },
  { KNX_SLOT3 , false, false, KNX_Empty },
  { KNX_SLOT4 , false, false, KNX_Empty },
  { KNX_SLOT5 , false, false, KNX_Empty },
  { KNX_Empty, false, false, KNX_Empty}
};

// device parameters (information that can be sent)
const char * device_param_ga[] = {
  D_TIMER_OUTPUT  " 1",   // Relay 1
  D_TIMER_OUTPUT  " 2",   // Relay 2
  D_TIMER_OUTPUT  " 3",   // Relay 3
  D_TIMER_OUTPUT  " 4",   // Relay 4
  D_TIMER_OUTPUT  " 5",   // Relay 5
  D_TIMER_OUTPUT  " 6",   // Relay 6
  D_TIMER_OUTPUT  " 7",   // Relay 7
  D_TIMER_OUTPUT  " 8",   // Relay 8
  D_SENSOR_BUTTON " 1",   // Button 1
  D_SENSOR_BUTTON " 2",   // Button 2
  D_SENSOR_BUTTON " 3",   // Button 3
  D_SENSOR_BUTTON " 4",   // Button 4
  D_SENSOR_BUTTON " 5",   // Button 5
  D_SENSOR_BUTTON " 6",   // Button 6
  D_SENSOR_BUTTON " 7",   // Button 7
  D_SENSOR_BUTTON " 8",   // Button 8
  D_TEMPERATURE       ,   // Temperature
  D_HUMIDITY          ,   // Humidity
  D_VOLTAGE           ,
  D_CURRENT           ,
  D_POWERUSAGE        ,
  D_POWER_FACTOR      ,
  D_ENERGY_TODAY      ,
  D_ENERGY_YESTERDAY  ,
  D_ENERGY_TOTAL      ,
  D_KNX_TX_SLOT   " 1",
  D_KNX_TX_SLOT   " 2",
  D_KNX_TX_SLOT   " 3",
  D_KNX_TX_SLOT   " 4",
  D_KNX_TX_SLOT   " 5",
  nullptr
};

// device actions (posible actions to be performed on the device)
const char *device_param_cb[] = {
  D_TIMER_OUTPUT " 1", // Set Relay 1 (1-On or 0-OFF)
  D_TIMER_OUTPUT " 2",
  D_TIMER_OUTPUT " 3",
  D_TIMER_OUTPUT " 4",
  D_TIMER_OUTPUT " 5",
  D_TIMER_OUTPUT " 6",
  D_TIMER_OUTPUT " 7",
  D_TIMER_OUTPUT " 8",
  D_TIMER_OUTPUT " 1 " D_BUTTON_TOGGLE, // Relay 1 Toggle (1 or 0 will toggle)
  D_TIMER_OUTPUT " 2 " D_BUTTON_TOGGLE,
  D_TIMER_OUTPUT " 3 " D_BUTTON_TOGGLE,
  D_TIMER_OUTPUT " 4 " D_BUTTON_TOGGLE,
  D_TIMER_OUTPUT " 5 " D_BUTTON_TOGGLE,
  D_TIMER_OUTPUT " 6 " D_BUTTON_TOGGLE,
  D_TIMER_OUTPUT " 7 " D_BUTTON_TOGGLE,
  D_TIMER_OUTPUT " 8 " D_BUTTON_TOGGLE,
  D_REPLY " " D_TEMPERATURE, // Reply Temperature
  D_REPLY " " D_HUMIDITY,    // Reply Humidity
  D_REPLY " " D_VOLTAGE           ,
  D_REPLY " " D_CURRENT           ,
  D_REPLY " " D_POWERUSAGE        ,
  D_REPLY " " D_POWER_FACTOR      ,
  D_REPLY " " D_ENERGY_TODAY      ,
  D_REPLY " " D_ENERGY_YESTERDAY  ,
  D_REPLY " " D_ENERGY_TOTAL      ,
  D_KNX_RX_SLOT   " 1",
  D_KNX_RX_SLOT   " 2",
  D_KNX_RX_SLOT   " 3",
  D_KNX_RX_SLOT   " 4",
  D_KNX_RX_SLOT   " 5",
  nullptr
};

// Commands
#define D_CMND_KNXTXCMND "KnxTx_Cmnd"
#define D_CMND_KNXTXVAL "KnxTx_Val"
#define D_CMND_KNX_ENABLED "Knx_Enabled"
#define D_CMND_KNX_ENHANCED "Knx_Enhanced"
#define D_CMND_KNX_PA "Knx_PA"
#define D_CMND_KNX_GA "Knx_GA"
#define D_CMND_KNX_CB "Knx_CB"
enum KnxCommands { CMND_KNXTXCMND, CMND_KNXTXVAL, CMND_KNX_ENABLED, CMND_KNX_ENHANCED, CMND_KNX_PA,
                   CMND_KNX_GA, CMND_KNX_CB } ;
const char kKnxCommands[] PROGMEM = D_CMND_KNXTXCMND "|" D_CMND_KNXTXVAL "|" D_CMND_KNX_ENABLED "|"
                   D_CMND_KNX_ENHANCED "|" D_CMND_KNX_PA "|" D_CMND_KNX_GA "|" D_CMND_KNX_CB ;

uint8_t KNX_GA_Search( uint8_t param, uint8_t start = 0 )
{
  for (uint8_t i = start; i < Settings.knx_GA_registered; ++i)
  {
    if ( Settings.knx_GA_param[i] == param )
    {
      if ( Settings.knx_GA_addr[i] != 0 ) // Relay has group address set? GA=0/0/0 can not be used as KNX address, so it is used here as a: not set value
      {
         if ( i >= start ) { return i; }
      }
    }
  }
  return KNX_Empty;
}


uint8_t KNX_CB_Search( uint8_t param, uint8_t start = 0 )
{
  for (uint8_t i = start; i < Settings.knx_CB_registered; ++i)
  {
    if ( Settings.knx_CB_param[i] == param )
    {
      if ( Settings.knx_CB_addr[i] != 0 )
      {
         if ( i >= start ) { return i; }
      }
    }
  }
  return KNX_Empty;
}


void KNX_ADD_GA( uint8_t GAop, uint8_t GA_FNUM, uint8_t GA_AREA, uint8_t GA_FDEF )
{
  // Check if all GA were assigned. If yes-> return
  if ( Settings.knx_GA_registered >= MAX_KNX_GA ) { return; }
  if ( GA_FNUM == 0 && GA_AREA == 0 && GA_FDEF == 0 ) { return; }

  // Assign a GA to that address
  Settings.knx_GA_param[Settings.knx_GA_registered] = GAop;
  KNX_addr.ga.area = GA_FNUM;
  KNX_addr.ga.line = GA_AREA;
  KNX_addr.ga.member = GA_FDEF;
  Settings.knx_GA_addr[Settings.knx_GA_registered] = KNX_addr.value;

  Settings.knx_GA_registered++;

  snprintf_P(log_data, sizeof(log_data), PSTR(D_LOG_KNX D_ADD " GA #%d: %s " D_TO " %d/%d/%d"),
   Settings.knx_GA_registered,
   device_param_ga[GAop-1],
   GA_FNUM, GA_AREA, GA_FDEF );
  AddLog(LOG_LEVEL_DEBUG);
}


void KNX_DEL_GA( uint8_t GAnum )
{

  uint8_t dest_offset = 0;
  uint8_t src_offset = 0;
  uint8_t len = 0;

  // Delete GA
  Settings.knx_GA_param[GAnum-1] = 0;

  if (GAnum == 1)
  {
    // start of array, so delete first entry
    src_offset = 1;
    // Settings.knx_GA_registered will be 1 in case of only one entry
    // Settings.knx_GA_registered will be 2 in case of two entries, etc..
    // so only copy anything, if there is it at least more then one element
    len = (Settings.knx_GA_registered - 1);
  }
  else if (GAnum == Settings.knx_GA_registered)
  {
    // last element, don't do anything, simply decrement counter
  }
  else
  {
    // somewhere in the middle
    // need to calc offsets

    // skip all prev elements
    dest_offset = GAnum -1 ; // GAnum -1 is equal to how many element are in front of it
    src_offset = dest_offset + 1; // start after the current element
    len = (Settings.knx_GA_registered - GAnum);
  }

  if (len > 0)
  {
    memmove(Settings.knx_GA_param + dest_offset, Settings.knx_GA_param + src_offset, len * sizeof(uint8_t));
    memmove(Settings.knx_GA_addr + dest_offset, Settings.knx_GA_addr + src_offset, len * sizeof(uint16_t));
  }

  Settings.knx_GA_registered--;

  snprintf_P(log_data, sizeof(log_data), PSTR(D_LOG_KNX D_DELETE " GA #%d"),
    GAnum );
  AddLog(LOG_LEVEL_DEBUG);
}


void KNX_ADD_CB( uint8_t CBop, uint8_t CB_FNUM, uint8_t CB_AREA, uint8_t CB_FDEF )
{
  // Check if all callbacks were assigned. If yes-> return
  if ( Settings.knx_CB_registered >= MAX_KNX_CB ) { return; }
  if ( CB_FNUM == 0 && CB_AREA == 0 && CB_FDEF == 0 ) { return; }

  // Check if a CB for CBop was registered on the ESP-KNX-IP Library
  if ( device_param[CBop-1].CB_id == KNX_Empty )
  {
    // if no, register the CB for CBop
    device_param[CBop-1].CB_id = knx.callback_register("", KNX_CB_Action, &device_param[CBop-1]);
      // KNX IP Library requires a parameter
      // to identify which action was requested on the KNX network
      // to be performed on this device (set relay, etc.)
      // Is going to be used device_param[j].type that stores the type number (1: relay 1, etc)
  }
  // Assign a callback to CB address
  Settings.knx_CB_param[Settings.knx_CB_registered] = CBop;
  KNX_addr.ga.area = CB_FNUM;
  KNX_addr.ga.line = CB_AREA;
  KNX_addr.ga.member = CB_FDEF;
  Settings.knx_CB_addr[Settings.knx_CB_registered] = KNX_addr.value;

  knx.callback_assign( device_param[CBop-1].CB_id, KNX_addr );

  Settings.knx_CB_registered++;

  snprintf_P(log_data, sizeof(log_data), PSTR(D_LOG_KNX D_ADD " CB #%d: %d/%d/%d " D_TO " %s"),
   Settings.knx_CB_registered,
   CB_FNUM, CB_AREA, CB_FDEF,
   device_param_cb[CBop-1] );
  AddLog(LOG_LEVEL_DEBUG);
}


void KNX_DEL_CB( uint8_t CBnum )
{
  uint8_t oldparam = Settings.knx_CB_param[CBnum-1];
  uint8_t dest_offset = 0;
  uint8_t src_offset = 0;
  uint8_t len = 0;

  // Delete assigment
  knx.callback_unassign(CBnum-1);
  Settings.knx_CB_param[CBnum-1] = 0;

  if (CBnum == 1)
  {
    // start of array, so delete first entry
    src_offset = 1;
    // Settings.knx_CB_registered will be 1 in case of only one entry
    // Settings.knx_CB_registered will be 2 in case of two entries, etc..
    // so only copy anything, if there is it at least more then one element
    len = (Settings.knx_CB_registered - 1);
  }
  else if (CBnum == Settings.knx_CB_registered)
  {
    // last element, don't do anything, simply decrement counter
  }
  else
  {
    // somewhere in the middle
    // need to calc offsets

    // skip all prev elements
    dest_offset = CBnum -1 ; // GAnum -1 is equal to how many element are in front of it
    src_offset = dest_offset + 1; // start after the current element
    len = (Settings.knx_CB_registered - CBnum);
  }

  if (len > 0)
  {
    memmove(Settings.knx_CB_param + dest_offset, Settings.knx_CB_param + src_offset, len * sizeof(uint8_t));
    memmove(Settings.knx_CB_addr + dest_offset, Settings.knx_CB_addr + src_offset, len * sizeof(uint16_t));
  }

  Settings.knx_CB_registered--;

  // Check if there is no other assigment to that callback. If there is not. delete that callback register
  if ( KNX_CB_Search( oldparam ) == KNX_Empty ) {
    knx.callback_deregister( device_param[oldparam-1].CB_id );
    device_param[oldparam-1].CB_id =  KNX_Empty;
  }

  snprintf_P(log_data, sizeof(log_data), PSTR(D_LOG_KNX D_DELETE " CB #%d"), CBnum );
  AddLog(LOG_LEVEL_DEBUG);
}


bool KNX_CONFIG_NOT_MATCH(void)
{
  // Check for configured parameters that the device does not have (module changed)
  for (uint8_t i = 0; i < KNX_MAX_device_param; ++i)
  {
    if ( !device_param[i].show ) { // device has this parameter ?
      // if not, search for all registered group address to this parameter for deletion

      // Checks all GA
      if ( KNX_GA_Search(i+1) != KNX_Empty ) { return true; }
      // Check all CB
      if ( i < 8 ) // check relays (i from 8 to 15 are toggle relays parameters)
      {
        if ( KNX_CB_Search(i+1) != KNX_Empty ) { return true; }
        if ( KNX_CB_Search(i+9) != KNX_Empty ) { return true; }
      }
      // check sensors and others
      if ( i > 15 )
      {
        if ( KNX_CB_Search(i+1) != KNX_Empty ) { return true; }
      }
    }
  }

  // Check for invalid or erroneous configuration (tasmota flashed without clearing the memory)
  for (uint8_t i = 0; i < Settings.knx_GA_registered; ++i)
  {
    if ( Settings.knx_GA_param[i] != 0 ) // the GA[i] have a parameter defined?
    {
      if ( Settings.knx_GA_addr[i] == 0 ) // the GA[i] with parameter have the 0/0/0 as address?
      {
         return true; // So, it is invalid. Reset KNX configuration
      }
    }
  }
  for (uint8_t i = 0; i < Settings.knx_CB_registered; ++i)
  {
    if ( Settings.knx_CB_param[i] != 0 ) // the CB[i] have a parameter defined?
    {
      if ( Settings.knx_CB_addr[i] == 0 ) // the CB[i] with parameter have the 0/0/0 as address?
      {
         return true; // So, it is invalid. Reset KNX configuration
      }
    }
  }

  return false;
}


void KNXStart(void)
{
  knx.start(nullptr);
  snprintf_P(log_data, sizeof(log_data), PSTR(D_LOG_KNX D_START));
  AddLog(LOG_LEVEL_DEBUG);
}


void KNX_INIT(void)
{
  // Check for incompatible config
  if (Settings.knx_GA_registered > MAX_KNX_GA) { Settings.knx_GA_registered = MAX_KNX_GA; }
  if (Settings.knx_CB_registered > MAX_KNX_CB) { Settings.knx_CB_registered = MAX_KNX_CB; }

  // Set Physical KNX Address of the device
  KNX_physs_addr.value = Settings.knx_physsical_addr;
  knx.physical_address_set( KNX_physs_addr );

  // Read Configuration
  //   Check which relays, buttons and sensors where configured for this device
  //   and activate options according to the hardware
  /*for (int i = GPIO_REL1; i < GPIO_REL8 + 1; ++i)
  {
    if (GetUsedInModule(i, my_module.io)) { device_param[i - GPIO_REL1].show = true; }
  }
  for (int i = GPIO_REL1_INV; i < GPIO_REL8_INV + 1; ++i)
  {
    if (GetUsedInModule(i, my_module.io)) { device_param[i - GPIO_REL1_INV].show = true; }
  }*/
  for (int i = 0; i < devices_present; ++i)
  {
    device_param[i].show = true;
  }
  for (int i = GPIO_SWT1; i < GPIO_SWT4 + 1; ++i)
  {
    if (GetUsedInModule(i, my_module.io)) { device_param[i - GPIO_SWT1 + 8].show = true; }
  }
  for (int i = GPIO_KEY1; i < GPIO_KEY4 + 1; ++i)
  {
    if (GetUsedInModule(i, my_module.io)) { device_param[i - GPIO_KEY1 + 8].show = true; }
  }
  for (int i = GPIO_SWT1_NP; i < GPIO_SWT4_NP + 1; ++i)
  {
    if (GetUsedInModule(i, my_module.io)) { device_param[i - GPIO_SWT1_NP + 8].show = true; }
  }
  for (int i = GPIO_KEY1_NP; i < GPIO_KEY4_NP + 1; ++i)
  {
    if (GetUsedInModule(i, my_module.io)) { device_param[i - GPIO_KEY1_NP + 8].show = true; }
  }
  if (GetUsedInModule(GPIO_DHT11, my_module.io)) { device_param[KNX_TEMPERATURE-1].show = true; }
  if (GetUsedInModule(GPIO_DHT22, my_module.io)) { device_param[KNX_TEMPERATURE-1].show = true; }
  if (GetUsedInModule(GPIO_SI7021, my_module.io)) { device_param[KNX_TEMPERATURE-1].show = true; }
  if (GetUsedInModule(GPIO_DSB, my_module.io)) { device_param[KNX_TEMPERATURE-1].show = true; }
  if (GetUsedInModule(GPIO_DHT11, my_module.io)) { device_param[KNX_HUMIDITY-1].show = true; }
  if (GetUsedInModule(GPIO_DHT22, my_module.io)) { device_param[KNX_HUMIDITY-1].show = true; }
  if (GetUsedInModule(GPIO_SI7021, my_module.io)) { device_param[KNX_HUMIDITY-1].show = true; }

  // Any device with a Power Monitoring
  if ( energy_flg != ENERGY_NONE ) {
    device_param[KNX_ENERGY_POWER-1].show = true;
    device_param[KNX_ENERGY_DAILY-1].show = true;
    device_param[KNX_ENERGY_START-1].show = true;
    device_param[KNX_ENERGY_TOTAL-1].show = true;
    device_param[KNX_ENERGY_VOLTAGE-1].show = true;
    device_param[KNX_ENERGY_CURRENT-1].show = true;
    device_param[KNX_ENERGY_POWERFACTOR-1].show = true;
  }

#ifdef USE_RULES
  device_param[KNX_SLOT1-1].show = true;
  device_param[KNX_SLOT2-1].show = true;
  device_param[KNX_SLOT3-1].show = true;
  device_param[KNX_SLOT4-1].show = true;
  device_param[KNX_SLOT5-1].show = true;
#endif

  // Delete from KNX settings all configuration is not anymore related to this device
  if (KNX_CONFIG_NOT_MATCH()) {
    Settings.knx_GA_registered = 0;
    Settings.knx_CB_registered = 0;
    snprintf_P(log_data, sizeof(log_data), PSTR(D_LOG_KNX D_DELETE " " D_KNX_PARAMETERS ));
    AddLog(LOG_LEVEL_DEBUG);
  }

  // Register Group Addresses to listen to
  //     Search on the settings if there is a group address set for receive KNX messages for the type: device_param[j].type
  //     If there is, register the group address on the KNX_IP Library to Receive data for Executing Callbacks
  uint8_t j;
  for (uint8_t i = 0; i < Settings.knx_CB_registered; ++i)
  {
    j = Settings.knx_CB_param[i];
    if ( j > 0 )
    {
      device_param[j-1].CB_id = knx.callback_register("", KNX_CB_Action, &device_param[j-1]); // KNX IP Library requires a parameter
                                                                                              // to identify which action was requested on the KNX network
                                                                                              // to be performed on this device (set relay, etc.)
                                                                                              // Is going to be used device_param[j].type that stores the type number (1: relay 1, etc)
      KNX_addr.value = Settings.knx_CB_addr[i];
      knx.callback_assign( device_param[j-1].CB_id, KNX_addr );
    }
  }
}


void KNX_CB_Action(message_t const &msg, void *arg)
{
  device_parameters_t *chan = (device_parameters_t *)arg;
  if (!(Settings.flag.knx_enabled)) { return; }

  char tempchar[33];

  if (msg.data_len == 1) {
    // COMMAND
    snprintf_P(log_data, sizeof(log_data), PSTR(D_LOG_KNX D_RECEIVED_FROM " %d.%d.%d " D_COMMAND " %s: %d " D_TO " %s"),
     msg.received_on.ga.area, msg.received_on.ga.line, msg.received_on.ga.member,
     (msg.ct == KNX_CT_WRITE) ? D_KNX_COMMAND_WRITE : (msg.ct == KNX_CT_READ) ? D_KNX_COMMAND_READ : D_KNX_COMMAND_OTHER,
     msg.data[0],
     device_param_cb[(chan->type)-1]);
  }  else  {
    // VALUE
    float tempvar = knx.data_to_2byte_float(msg.data);
    dtostrfd(tempvar,2,tempchar);
    snprintf_P(log_data, sizeof(log_data), PSTR(D_LOG_KNX D_RECEIVED_FROM " %d.%d.%d " D_COMMAND " %s: %s " D_TO " %s"),
     msg.received_on.ga.area, msg.received_on.ga.line, msg.received_on.ga.member,
     (msg.ct == KNX_CT_WRITE) ? D_KNX_COMMAND_WRITE : (msg.ct == KNX_CT_READ) ? D_KNX_COMMAND_READ : D_KNX_COMMAND_OTHER,
     tempchar,
     device_param_cb[(chan->type)-1]);
  }
  AddLog(LOG_LEVEL_INFO);

  switch (msg.ct)
  {
    case KNX_CT_WRITE:
      if (chan->type < 9) // Set Relays
      {
        ExecuteCommandPower(chan->type, msg.data[0], SRC_KNX);
      }
      else if (chan->type < 17) // Toggle Relays
      {
        if (!toggle_inhibit) {
          ExecuteCommandPower((chan->type) -8, 2, SRC_KNX);
          if (Settings.flag.knx_enable_enhancement) {
            toggle_inhibit = TOGGLE_INHIBIT_TIME;
          }
        }
      }
#ifdef USE_RULES
      else if ((chan->type >= KNX_SLOT1) && (chan->type <= KNX_SLOT5)) // KNX RX SLOTs (write command)
      {
        if (!toggle_inhibit) {
          char command[25];
          if (msg.data_len == 1) {
            // Command received
            snprintf_P(command, sizeof(command), PSTR("event KNXRX_CMND%d=%d"), ((chan->type) - KNX_SLOT1 + 1 ), msg.data[0]);
          } else {
            // Value received
            snprintf_P(command, sizeof(command), PSTR("event KNXRX_VAL%d=%s"), ((chan->type) - KNX_SLOT1 + 1 ), tempchar);
          }
          ExecuteCommand(command, SRC_KNX);
          if (Settings.flag.knx_enable_enhancement) {
            toggle_inhibit = TOGGLE_INHIBIT_TIME;
          }
        }
      }
#endif
      break;

    case KNX_CT_READ:
      if (chan->type < 9) // reply Relays status
      {
        knx.answer_1bit(msg.received_on, chan->last_state);
        if (Settings.flag.knx_enable_enhancement) {
          knx.answer_1bit(msg.received_on, chan->last_state);
          knx.answer_1bit(msg.received_on, chan->last_state);
        }
      }
      else if (chan->type == KNX_TEMPERATURE) // Reply Temperature
      {
        knx.answer_2byte_float(msg.received_on, last_temp);
        if (Settings.flag.knx_enable_enhancement) {
          knx.answer_2byte_float(msg.received_on, last_temp);
          knx.answer_2byte_float(msg.received_on, last_temp);
        }
      }
      else if (chan->type == KNX_HUMIDITY) // Reply Humidity
      {
        knx.answer_2byte_float(msg.received_on, last_hum);
        if (Settings.flag.knx_enable_enhancement) {
          knx.answer_2byte_float(msg.received_on, last_hum);
          knx.answer_2byte_float(msg.received_on, last_hum);
        }
      }
#ifdef USE_RULES
      else if ((chan->type >= KNX_SLOT1) && (chan->type <= KNX_SLOT5)) // KNX RX SLOTs (read command)
      {
        if (!toggle_inhibit) {
          char command[25];
          snprintf_P(command, sizeof(command), PSTR("event KNXRX_REQ%d"), ((chan->type) - KNX_SLOT1 + 1 ) );
          ExecuteCommand(command, SRC_KNX);
          if (Settings.flag.knx_enable_enhancement) {
            toggle_inhibit = TOGGLE_INHIBIT_TIME;
          }
        }
      }
#endif
      break;
  }
}


void KnxUpdatePowerState(uint8_t device, power_t state)
{
  if (!(Settings.flag.knx_enabled)) { return; }

  device_param[device -1].last_state = bitRead(state, device -1); // power state (on/off)

  // Search all the registered GA that has that output (variable: device) as parameter
  uint8_t i = KNX_GA_Search(device);
  while ( i != KNX_Empty ) {
    KNX_addr.value = Settings.knx_GA_addr[i];
    knx.write_1bit(KNX_addr, device_param[device -1].last_state);
    if (Settings.flag.knx_enable_enhancement) {
      knx.write_1bit(KNX_addr, device_param[device -1].last_state);
      knx.write_1bit(KNX_addr, device_param[device -1].last_state);
    }

    snprintf_P(log_data, sizeof(log_data), PSTR(D_LOG_KNX "%s = %d " D_SENT_TO " %d.%d.%d"),
     device_param_ga[device -1], device_param[device -1].last_state,
     KNX_addr.ga.area, KNX_addr.ga.line, KNX_addr.ga.member);
    AddLog(LOG_LEVEL_INFO);

    i = KNX_GA_Search(device, i + 1);
  }
}


void KnxSendButtonPower(uint8_t key, uint8_t device, uint8_t state)
{
// key 0 = button_topic
// key 1 = switch_topic
// state 0 = off
// state 1 = on
// state 2 = toggle
// state 3 = hold
// state 9 = clear retain flag
  if (!(Settings.flag.knx_enabled)) { return; }
//  if (key)
//  {

// Search all the registered GA that has that output (variable: device) as parameter
  uint8_t i = KNX_GA_Search(device + 8);
  while ( i != KNX_Empty ) {
    KNX_addr.value = Settings.knx_GA_addr[i];
    knx.write_1bit(KNX_addr, !(state == 0));
    if (Settings.flag.knx_enable_enhancement) {
      knx.write_1bit(KNX_addr, !(state == 0));
      knx.write_1bit(KNX_addr, !(state == 0));
    }

    snprintf_P(log_data, sizeof(log_data), PSTR(D_LOG_KNX "%s = %d " D_SENT_TO " %d.%d.%d"),
     device_param_ga[device + 7], !(state == 0),
     KNX_addr.ga.area, KNX_addr.ga.line, KNX_addr.ga.member);
    AddLog(LOG_LEVEL_INFO);

    i = KNX_GA_Search(device + 8, i + 1);
  }
//  }
}


void KnxSensor(uint8_t sensor_type, float value)
{
  if (sensor_type == KNX_TEMPERATURE)
  {
    last_temp = value;
  } else if (sensor_type == KNX_HUMIDITY)
  {
    last_hum = value;
  }

  if (!(Settings.flag.knx_enabled)) { return; }

  uint8_t i = KNX_GA_Search(sensor_type);
  while ( i != KNX_Empty ) {
    KNX_addr.value = Settings.knx_GA_addr[i];
    knx.write_2byte_float(KNX_addr, value);
    if (Settings.flag.knx_enable_enhancement) {
      knx.write_2byte_float(KNX_addr, value);
      knx.write_2byte_float(KNX_addr, value);
    }

    snprintf_P(log_data, sizeof(log_data), PSTR(D_LOG_KNX "%s " D_SENT_TO " %d.%d.%d "),
     device_param_ga[sensor_type -1],
     KNX_addr.ga.area, KNX_addr.ga.line, KNX_addr.ga.member);
    AddLog(LOG_LEVEL_INFO);

    i = KNX_GA_Search(sensor_type, i+1);
  }
}


/*********************************************************************************************\
 * Presentation
\*********************************************************************************************/

#ifdef USE_WEBSERVER
#ifdef USE_KNX_WEB_MENU
const char S_CONFIGURE_KNX[] PROGMEM = D_CONFIGURE_KNX;

const char HTTP_BTN_MENU_KNX[] PROGMEM =
  "<p><form action='kn' method='get'><button>" D_CONFIGURE_KNX "</button></form></p>";

const char HTTP_FORM_KNX[] PROGMEM =
  "<fieldset><legend style='text-align:left;'><b>&nbsp;" D_KNX_PARAMETERS "&nbsp;</b>"
  "</legend><form method='post' action='kn'>"
  "<br/><center>"
  "<b>" D_KNX_PHYSICAL_ADDRESS " </b>"
  "<input style='width:12%;' type='number' name='area' min='0' max='15' value='{kna'> . "
  "<input style='width:12%;' type='number' name='line' min='0' max='15' value='{knl'> . "
  "<input style='width:12%;' type='number' name='member' min='0' max='255' value='{knm'>"
  "<br/><br/>" D_KNX_PHYSICAL_ADDRESS_NOTE "<br/><br/>"
  "<input id='b1' name='b1' type='checkbox'";

const char HTTP_FORM_KNX1[] PROGMEM =
  "><b>" D_KNX_ENABLE "</b>&emsp;<input id='b2' name='b2' type='checkbox'";

const char HTTP_FORM_KNX2[] PROGMEM =
  "><b>" D_KNX_ENHANCEMENT "</b><br/></center><br/>"

  "<fieldset><center>"
  "<b>" D_KNX_GROUP_ADDRESS_TO_WRITE "</b><hr>"

  "<select name='GAop' style='width:25%;'>";

const char HTTP_FORM_KNX_OPT[] PROGMEM =
  "<option value='{vop}'>{nop}</option>";

const char HTTP_FORM_KNX_GA[] PROGMEM =
  "<input style='width:12%;' type='number' id='GAfnum' name='GAfnum' min='0' max='31' value='0'> / "
  "<input style='width:12%;' type='number' id='GAarea' name='GAarea' min='0' max='7' value='0'> / "
  "<input style='width:12%;' type='number' id='GAfdef' name='GAfdef' min='0' max='255' value='0'> ";

const char HTTP_FORM_KNX_ADD_BTN[] PROGMEM =
  "<button type='submit' onclick='fncbtnadd()' btndis name='btn_add' value='{btnval}' style='width:18%;'>" D_ADD "</button><br/><br/>"
  "<table style='width:80%; font-size: 14px;'><col width='250'><col width='30'>";

const char HTTP_FORM_KNX_ADD_TABLE_ROW[] PROGMEM =
  "<tr><td><b>{optex} -> GAfnum / GAarea / GAfdef </b></td>"
  "<td><button type='submit' name='btn_del_ga' value='{opval}' class='button bred'> " D_DELETE " </button></td></tr>";

const char HTTP_FORM_KNX3[] PROGMEM =
  "</table></center></fieldset><br/>"
  "<fieldset><form method='post' action='kn'><center>"
  "<b>" D_KNX_GROUP_ADDRESS_TO_READ "</b><hr>";

const char HTTP_FORM_KNX4[] PROGMEM =
  "-> <select name='CBop' style='width:25%;'>";

const char HTTP_FORM_KNX_ADD_TABLE_ROW2[] PROGMEM =
  "<tr><td><b>GAfnum / GAarea / GAfdef -> {optex}</b></td>"
  "<td><button type='submit' name='btn_del_cb' value='{opval}' class='button bred'> " D_DELETE " </button></td></tr>";

void HandleKNXConfiguration(void)
{
  if (!HttpCheckPriviledgedAccess()) { return; }

  AddLog_P(LOG_LEVEL_DEBUG, S_LOG_HTTP, S_CONFIGURE_KNX);

  char tmp[100];
  String stmp;

  if ( WebServer->hasArg("save") ) {
    KNX_Save_Settings();
    HandleConfiguration();
  }
  else
  {
    if ( WebServer->hasArg("btn_add") ) {
      if ( WebServer->arg("btn_add") == "1" ) {

        stmp = WebServer->arg("GAop"); //option selected
        uint8_t GAop = stmp.toInt();
        stmp = WebServer->arg("GA_FNUM");
        uint8_t GA_FNUM = stmp.toInt();
        stmp = WebServer->arg("GA_AREA");
        uint8_t GA_AREA = stmp.toInt();
        stmp = WebServer->arg("GA_FDEF");
        uint8_t GA_FDEF = stmp.toInt();

        if (GAop) {
          KNX_ADD_GA( GAop, GA_FNUM, GA_AREA, GA_FDEF );
        }
      }
      else
      {

        stmp = WebServer->arg("CBop"); //option selected
        uint8_t CBop = stmp.toInt();
        stmp = WebServer->arg("CB_FNUM");
        uint8_t CB_FNUM = stmp.toInt();
        stmp = WebServer->arg("CB_AREA");
        uint8_t CB_AREA = stmp.toInt();
        stmp = WebServer->arg("CB_FDEF");
        uint8_t CB_FDEF = stmp.toInt();

        if (CBop) {
          KNX_ADD_CB( CBop, CB_FNUM, CB_AREA, CB_FDEF );
        }
      }
    }
    else if ( WebServer->hasArg("btn_del_ga") )
    {

      stmp = WebServer->arg("btn_del_ga");
      uint8_t GA_NUM = stmp.toInt();

      KNX_DEL_GA(GA_NUM);

    }
    else if ( WebServer->hasArg("btn_del_cb") )
    {

      stmp = WebServer->arg("btn_del_cb");
      uint8_t CB_NUM = stmp.toInt();

      KNX_DEL_CB(CB_NUM);

    }

    String page = FPSTR(HTTP_HEAD);
    page.replace(F("{v}"), FPSTR(S_CONFIGURE_KNX));
    page += FPSTR(HTTP_HEAD_STYLE);
    page.replace(F("340px"), F("530px"));
    page += FPSTR(HTTP_FORM_KNX);
    KNX_physs_addr.value = Settings.knx_physsical_addr;
    page.replace(F("{kna"), String(KNX_physs_addr.pa.area));
    page.replace(F("{knl"), String(KNX_physs_addr.pa.line));
    page.replace(F("{knm"), String(KNX_physs_addr.pa.member));
    if ( Settings.flag.knx_enabled ) { page += F(" checked"); }
    page += FPSTR(HTTP_FORM_KNX1);
    if ( Settings.flag.knx_enable_enhancement ) { page += F(" checked"); }

    page += FPSTR(HTTP_FORM_KNX2);
    for (uint8_t i = 0; i < KNX_MAX_device_param ; i++)
    {
      if ( device_param[i].show )
      {
        page += FPSTR(HTTP_FORM_KNX_OPT);
        page.replace(F("{vop}"), String(device_param[i].type));
        page.replace(F("{nop}"), String(device_param_ga[i]));
      }
    }
    page += F("</select> -> ");
    page += FPSTR(HTTP_FORM_KNX_GA);
    page.replace(F("GAfnum"), F("GA_FNUM"));
    page.replace(F("GAarea"), F("GA_AREA"));
    page.replace(F("GAfdef"), F("GA_FDEF"));
    page.replace(F("GAfnum"), F("GA_FNUM"));
    page.replace(F("GAarea"), F("GA_AREA"));
    page.replace(F("GAfdef"), F("GA_FDEF"));
    page += FPSTR(HTTP_FORM_KNX_ADD_BTN);
    page.replace(F("{btnval}"), String(1));
    if (Settings.knx_GA_registered < MAX_KNX_GA) {
      page.replace(F("btndis"), F(" "));
    }
    else
    {
      page.replace(F("btndis"), F("disabled"));
    }
    page.replace(F("fncbtnadd"), F("GAwarning"));
    for (uint8_t i = 0; i < Settings.knx_GA_registered ; ++i)
    {
      if ( Settings.knx_GA_param[i] )
      {
        page += FPSTR(HTTP_FORM_KNX_ADD_TABLE_ROW);
        page.replace(F("{opval}"), String(i+1));
        page.replace(F("{optex}"), String(device_param_ga[Settings.knx_GA_param[i]-1]));
        KNX_addr.value = Settings.knx_GA_addr[i];
        page.replace(F("GAfnum"), String(KNX_addr.ga.area));
        page.replace(F("GAarea"), String(KNX_addr.ga.line));
        page.replace(F("GAfdef"), String(KNX_addr.ga.member));
      }
    }

    page += FPSTR(HTTP_FORM_KNX3);
    page += FPSTR(HTTP_FORM_KNX_GA);
    page.replace(F("GAfnum"), F("CB_FNUM"));
    page.replace(F("GAarea"), F("CB_AREA"));
    page.replace(F("GAfdef"), F("CB_FDEF"));
    page.replace(F("GAfnum"), F("CB_FNUM"));
    page.replace(F("GAarea"), F("CB_AREA"));
    page.replace(F("GAfdef"), F("CB_FDEF"));
    page += FPSTR(HTTP_FORM_KNX4);
    uint8_t j;
    for (uint8_t i = 0; i < KNX_MAX_device_param ; i++)
    {
      // Check How many Relays are available and add: RelayX and TogleRelayX
      if ( (i > 8) && (i < 16) ) { j=i-8; } else { j=i; }
      if ( i == 8 ) { j = 0; }
      if ( device_param[j].show )
      {
        page += FPSTR(HTTP_FORM_KNX_OPT);
        page.replace(F("{vop}"), String(device_param[i].type));
        page.replace(F("{nop}"), String(device_param_cb[i]));
      }
    }
    page += F("</select> ");
    page += FPSTR(HTTP_FORM_KNX_ADD_BTN);
    page.replace(F("{btnval}"), String(2));
    if (Settings.knx_CB_registered < MAX_KNX_CB) {
      page.replace(F("btndis"), F(" "));
    }
    else
    {
      page.replace(F("btndis"), F("disabled"));
    }
    page.replace(F("fncbtnadd"), F("CBwarning"));

    for (uint8_t i = 0; i < Settings.knx_CB_registered ; ++i)
    {
      if ( Settings.knx_CB_param[i] )
      {
        page += FPSTR(HTTP_FORM_KNX_ADD_TABLE_ROW2);
        page.replace(F("{opval}"), String(i+1));
        page.replace(F("{optex}"), String(device_param_cb[Settings.knx_CB_param[i]-1]));
        KNX_addr.value = Settings.knx_CB_addr[i];
        page.replace(F("GAfnum"), String(KNX_addr.ga.area));
        page.replace(F("GAarea"), String(KNX_addr.ga.line));
        page.replace(F("GAfdef"), String(KNX_addr.ga.member));
      }
    }
    page += F("</table></center></fieldset>");
    page += F("<br/><button name='save' type='submit' class='button bgrn'>" D_SAVE "</button></form></fieldset>");
    page += FPSTR(HTTP_BTN_CONF);

    page.replace( F("</script>"),
      F("function GAwarning()"
        "{"
          "var GA_FNUM = document.getElementById('GA_FNUM');"
          "var GA_AREA = document.getElementById('GA_AREA');"
          "var GA_FDEF = document.getElementById('GA_FDEF');"
          "if ( GA_FNUM != null && GA_FNUM.value == '0' && GA_AREA.value == '0' && GA_FDEF.value == '0' ) {"
            "alert('" D_KNX_WARNING "');"
          "}"
        "}"
        "function CBwarning()"
        "{"
          "var CB_FNUM = document.getElementById('CB_FNUM');"
          "var CB_AREA = document.getElementById('CB_AREA');"
          "var CB_FDEF = document.getElementById('CB_FDEF');"
          "if ( CB_FNUM != null && CB_FNUM.value == '0' && CB_AREA.value == '0' && CB_FDEF.value == '0' ) {"
            "alert('" D_KNX_WARNING "');"
          "}"
        "}"
      "</script>") );
    ShowPage(page);
  }

}


void KNX_Save_Settings(void)
{
  String stmp;
  address_t KNX_addr;

  Settings.flag.knx_enabled = WebServer->hasArg("b1");
  Settings.flag.knx_enable_enhancement = WebServer->hasArg("b2");
  snprintf_P(log_data, sizeof(log_data), PSTR(D_LOG_KNX D_ENABLED ": %d, " D_KNX_ENHANCEMENT ": %d"),
   Settings.flag.knx_enabled, Settings.flag.knx_enable_enhancement );
  AddLog(LOG_LEVEL_DEBUG);

  stmp = WebServer->arg("area");
  KNX_addr.pa.area = stmp.toInt();
  stmp = WebServer->arg("line");
  KNX_addr.pa.line = stmp.toInt();
  stmp = WebServer->arg("member");
  KNX_addr.pa.member = stmp.toInt();
  Settings.knx_physsical_addr = KNX_addr.value;
  knx.physical_address_set( KNX_addr ); // Set Physical KNX Address of the device
  snprintf_P(log_data, sizeof(log_data), PSTR(D_LOG_KNX D_KNX_PHYSICAL_ADDRESS ": %d.%d.%d "),
   KNX_addr.pa.area, KNX_addr.pa.line, KNX_addr.pa.member );
  AddLog(LOG_LEVEL_DEBUG);

  snprintf_P(log_data, sizeof(log_data), PSTR(D_LOG_KNX "GA: %d"),
   Settings.knx_GA_registered );
  AddLog(LOG_LEVEL_DEBUG);
  for (uint8_t i = 0; i < Settings.knx_GA_registered ; ++i)
  {
    KNX_addr.value = Settings.knx_GA_addr[i];
    snprintf_P(log_data, sizeof(log_data), PSTR(D_LOG_KNX "GA #%d: %s " D_TO " %d/%d/%d"),
     i+1, device_param_ga[Settings.knx_GA_param[i]-1],
     KNX_addr.ga.area, KNX_addr.ga.line, KNX_addr.ga.member );
    AddLog(LOG_LEVEL_DEBUG);
  }

  snprintf_P(log_data, sizeof(log_data), PSTR(D_LOG_KNX "CB: %d"),
   Settings.knx_CB_registered );
  AddLog(LOG_LEVEL_DEBUG);
  for (uint8_t i = 0; i < Settings.knx_CB_registered ; ++i)
  {
    KNX_addr.value = Settings.knx_CB_addr[i];
    snprintf_P(log_data, sizeof(log_data), PSTR(D_LOG_KNX "CB #%d: %d/%d/%d " D_TO " %s"),
     i+1,
     KNX_addr.ga.area, KNX_addr.ga.line, KNX_addr.ga.member,
     device_param_cb[Settings.knx_CB_param[i]-1] );
    AddLog(LOG_LEVEL_DEBUG);
  }
}

#endif  // USE_KNX_WEB_MENU
#endif  // USE_WEBSERVER


bool KnxCommand(void)
{
  char command[CMDSZ];
  uint8_t index = XdrvMailbox.index;
  int command_code = GetCommandCode(command, sizeof(command), XdrvMailbox.topic, kKnxCommands);

  if (-1 == command_code) { return false; } // Unknown command

  else if ((CMND_KNXTXCMND == command_code) && (index > 0) && (index <= MAX_KNXTX_CMNDS) && (XdrvMailbox.data_len > 0)) {
    // index <- KNX SLOT to use
    // XdrvMailbox.payload <- data to send
    if (!(Settings.flag.knx_enabled)) { return false; }
    // Search all the registered GA that has that output (variable: KNX SLOTx) as parameter
    uint8_t i = KNX_GA_Search(index + KNX_SLOT1 -1);
    while ( i != KNX_Empty ) {
      KNX_addr.value = Settings.knx_GA_addr[i];
      knx.write_1bit(KNX_addr, !(XdrvMailbox.payload == 0));
      if (Settings.flag.knx_enable_enhancement) {
        knx.write_1bit(KNX_addr, !(XdrvMailbox.payload == 0));
        knx.write_1bit(KNX_addr, !(XdrvMailbox.payload == 0));
      }

      snprintf_P(log_data, sizeof(log_data), PSTR(D_LOG_KNX "%s = %d " D_SENT_TO " %d.%d.%d"),
       device_param_ga[index + KNX_SLOT1 -2], !(XdrvMailbox.payload == 0),
       KNX_addr.ga.area, KNX_addr.ga.line, KNX_addr.ga.member);
      AddLog(LOG_LEVEL_INFO);

      i = KNX_GA_Search(index + KNX_SLOT1 -1, i + 1);
    }
    snprintf_P (mqtt_data, sizeof(mqtt_data), PSTR("{\"%s%d\":\"%s\"}"),
      command, index, XdrvMailbox.data );
  }

  else if ((CMND_KNXTXVAL == command_code) && (index > 0) && (index <= MAX_KNXTX_CMNDS) && (XdrvMailbox.data_len > 0)) {
    // index <- KNX SLOT to use
    // XdrvMailbox.payload <- data to send
    if (!(Settings.flag.knx_enabled)) { return false; }
    // Search all the registered GA that has that output (variable: KNX SLOTx) as parameter
    uint8_t i = KNX_GA_Search(index + KNX_SLOT1 -1);
    while ( i != KNX_Empty ) {
      KNX_addr.value = Settings.knx_GA_addr[i];

      float tempvar = CharToDouble(XdrvMailbox.data);
      dtostrfd(tempvar,2,XdrvMailbox.data);

      knx.write_2byte_float(KNX_addr, tempvar);
      if (Settings.flag.knx_enable_enhancement) {
        knx.write_2byte_float(KNX_addr, tempvar);
        knx.write_2byte_float(KNX_addr, tempvar);
      }

      snprintf_P(log_data, sizeof(log_data), PSTR(D_LOG_KNX "%s = %s " D_SENT_TO " %d.%d.%d"),
       device_param_ga[index + KNX_SLOT1 -2], XdrvMailbox.data,
       KNX_addr.ga.area, KNX_addr.ga.line, KNX_addr.ga.member);
      AddLog(LOG_LEVEL_INFO);

      i = KNX_GA_Search(index + KNX_SLOT1 -1, i + 1);
    }
    snprintf_P (mqtt_data, sizeof(mqtt_data), PSTR("{\"%s%d\":\"%s\"}"),
      command, index, XdrvMailbox.data );
  }

  else if (CMND_KNX_ENABLED == command_code) {
    if (!XdrvMailbox.data_len) {
      if (Settings.flag.knx_enabled) {
        snprintf_P(XdrvMailbox.data, sizeof(XdrvMailbox.data), PSTR("1"));
      } else {
        snprintf_P(XdrvMailbox.data, sizeof(XdrvMailbox.data), PSTR("0"));
      }
    } else {
      if (XdrvMailbox.payload == 1) {
        Settings.flag.knx_enabled = 1;
      } else if (XdrvMailbox.payload == 0) {
        Settings.flag.knx_enabled = 0;
      } else { return false; }  // Incomplete command
    }
    snprintf_P (mqtt_data, sizeof(mqtt_data), PSTR("{\"%s\":\"%s\"}"),
      command, XdrvMailbox.data );
  }

  else if (CMND_KNX_ENHANCED == command_code) {
    if (!XdrvMailbox.data_len) {
      if (Settings.flag.knx_enable_enhancement) {
        snprintf_P(XdrvMailbox.data, sizeof(XdrvMailbox.data), PSTR("1"));
      } else {
        snprintf_P(XdrvMailbox.data, sizeof(XdrvMailbox.data), PSTR("0"));
      }
    } else {
      if (XdrvMailbox.payload == 1) {
        Settings.flag.knx_enable_enhancement = 1;
      } else if (XdrvMailbox.payload == 0) {
        Settings.flag.knx_enable_enhancement = 0;
      } else { return false; }  // Incomplete command
    }
    snprintf_P (mqtt_data, sizeof(mqtt_data), PSTR("{\"%s\":\"%s\"}"),
      command, XdrvMailbox.data );
  }

  else if (CMND_KNX_PA == command_code) {
    if (XdrvMailbox.data_len) {
      if (strstr(XdrvMailbox.data, ".")) {     // Process parameter entry
        char sub_string[XdrvMailbox.data_len];

        int pa_area = atoi(subStr(sub_string, XdrvMailbox.data, ".", 1));
        int pa_line = atoi(subStr(sub_string, XdrvMailbox.data, ".", 2));
        int pa_member = atoi(subStr(sub_string, XdrvMailbox.data, ".", 3));

        if ( ((pa_area == 0) && (pa_line == 0) && (pa_member == 0))
             || (pa_area > 15) || (pa_line > 15) || (pa_member > 255) ) {
               snprintf_P (mqtt_data, sizeof(mqtt_data), PSTR("{\"%s\":\"" D_ERROR "\"}"),
                 command );
               return true;
        }  // Invalid command

        KNX_addr.pa.area = pa_area;
        KNX_addr.pa.line = pa_line;
        KNX_addr.pa.member = pa_member;
        Settings.knx_physsical_addr = KNX_addr.value;
      }
    }
    KNX_addr.value = Settings.knx_physsical_addr;
    snprintf_P (mqtt_data, sizeof(mqtt_data), PSTR("{\"%s\":\"%d.%d.%d\"}"),
      command, KNX_addr.pa.area, KNX_addr.pa.line, KNX_addr.pa.member );
  }

  else if ((CMND_KNX_GA == command_code) && (index > 0) && (index <= MAX_KNX_GA)) {
    if (XdrvMailbox.data_len) {
      if (strstr(XdrvMailbox.data, ",")) {     // Process parameter entry
        char sub_string[XdrvMailbox.data_len];

        int ga_option = atoi(subStr(sub_string, XdrvMailbox.data, ",", 1));
        int ga_area = atoi(subStr(sub_string, XdrvMailbox.data, ",", 2));
        int ga_line = atoi(subStr(sub_string, XdrvMailbox.data, ",", 3));
        int ga_member = atoi(subStr(sub_string, XdrvMailbox.data, ",", 4));

        if ( ((ga_area == 0) && (ga_line == 0) && (ga_member == 0))
          || (ga_area > 31) || (ga_line > 7) || (ga_member > 255)
          || (ga_option < 0) || ((ga_option > KNX_MAX_device_param ) && (ga_option != KNX_Empty))
          || (!device_param[ga_option-1].show) ) {
               snprintf_P (mqtt_data, sizeof(mqtt_data), PSTR("{\"%s\":\"" D_ERROR "\"}"), command );
               return true;
        }  // Invalid command

        KNX_addr.ga.area = ga_area;
        KNX_addr.ga.line = ga_line;
        KNX_addr.ga.member = ga_member;

        if ( index > Settings.knx_GA_registered ) {
          Settings.knx_GA_registered ++;
          index = Settings.knx_GA_registered;
        }

        Settings.knx_GA_addr[index -1] = KNX_addr.value;
        Settings.knx_GA_param[index -1] = ga_option;
      } else {
        if ( (XdrvMailbox.payload <= Settings.knx_GA_registered) && (XdrvMailbox.payload > 0) ) {
          index = XdrvMailbox.payload;
        } else {
          snprintf_P (mqtt_data, sizeof(mqtt_data), PSTR("{\"%s\":\"" D_ERROR "\"}"), command );
          return true;
        }
      }
      if ( index <= Settings.knx_GA_registered ) {
        KNX_addr.value = Settings.knx_GA_addr[index -1];
        snprintf_P (mqtt_data, sizeof(mqtt_data), PSTR("{\"%s%d\":\"%s, %d/%d/%d\"}"),
          command, index, device_param_ga[Settings.knx_GA_param[index-1]-1],
          KNX_addr.ga.area, KNX_addr.ga.line, KNX_addr.ga.member );
      }
    } else {
      snprintf_P (mqtt_data, sizeof(mqtt_data), PSTR("{\"%s\":\"%d\"}"),
        command, Settings.knx_GA_registered );
    }
  }

  else if ((CMND_KNX_CB == command_code) && (index > 0) && (index <= MAX_KNX_CB)) {
    if (XdrvMailbox.data_len) {
      if (strstr(XdrvMailbox.data, ",")) {     // Process parameter entry
        char sub_string[XdrvMailbox.data_len];

        int cb_option = atoi(subStr(sub_string, XdrvMailbox.data, ",", 1));
        int cb_area = atoi(subStr(sub_string, XdrvMailbox.data, ",", 2));
        int cb_line = atoi(subStr(sub_string, XdrvMailbox.data, ",", 3));
        int cb_member = atoi(subStr(sub_string, XdrvMailbox.data, ",", 4));

        if ( ((cb_area == 0) && (cb_line == 0) && (cb_member == 0))
          || (cb_area > 31) || (cb_line > 7) || (cb_member > 255)
          || (cb_option < 0) || ((cb_option > KNX_MAX_device_param ) && (cb_option != KNX_Empty))
          || (!device_param[cb_option-1].show) ) {
               snprintf_P (mqtt_data, sizeof(mqtt_data), PSTR("{\"%s\":\"" D_ERROR "\"}"), command );
               return true;
        }  // Invalid command

        KNX_addr.ga.area = cb_area;
        KNX_addr.ga.line = cb_line;
        KNX_addr.ga.member = cb_member;

        if ( index > Settings.knx_CB_registered ) {
          Settings.knx_CB_registered ++;
          index = Settings.knx_CB_registered;
        }

        Settings.knx_CB_addr[index -1] = KNX_addr.value;
        Settings.knx_CB_param[index -1] = cb_option;
      } else {
        if ( (XdrvMailbox.payload <= Settings.knx_CB_registered) && (XdrvMailbox.payload > 0) ) {
          index = XdrvMailbox.payload;
        } else {
          snprintf_P (mqtt_data, sizeof(mqtt_data), PSTR("{\"%s\":\"" D_ERROR "\"}"), command );
          return true;
        }
      }
      if ( index <= Settings.knx_CB_registered ) {
        KNX_addr.value = Settings.knx_CB_addr[index -1];
        snprintf_P (mqtt_data, sizeof(mqtt_data), PSTR("{\"%s%d\":\"%s, %d/%d/%d\"}"),
          command, index, device_param_cb[Settings.knx_CB_param[index-1]-1],
          KNX_addr.ga.area, KNX_addr.ga.line, KNX_addr.ga.member );
      }
    } else {
      snprintf_P (mqtt_data, sizeof(mqtt_data), PSTR("{\"%s\":\"%d\"}"),
        command, Settings.knx_CB_registered );
    }
  }

  else { return false; }  // Incomplete command

  return true;
}


/*********************************************************************************************\
 * Interface
\*********************************************************************************************/

bool Xdrv11(uint8_t function)
{
  bool result = false;
    switch (function) {
      case FUNC_PRE_INIT:
        KNX_INIT();
        break;
#ifdef USE_WEBSERVER
#ifdef USE_KNX_WEB_MENU
      case FUNC_WEB_ADD_BUTTON:
        strncat_P(mqtt_data, HTTP_BTN_MENU_KNX, sizeof(mqtt_data) - strlen(mqtt_data) -1);
        break;
      case FUNC_WEB_ADD_HANDLER:
        WebServer->on("/kn", HandleKNXConfiguration);
        break;
#endif // USE_KNX_WEB_MENU
#endif  // USE_WEBSERVER
      case FUNC_LOOP:
        if (!global_state.wifi_down) { knx.loop(); }  // Process knx events
        break;
      case FUNC_EVERY_50_MSECOND:
        if (toggle_inhibit) {
          toggle_inhibit--;
        }
        break;
      case FUNC_COMMAND:
        result = KnxCommand();
        break;
//      case FUNC_SET_POWER:
//        break;
    }
  return result;
}

#endif  // USE_KNX
