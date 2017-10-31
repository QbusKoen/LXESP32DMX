/**************************************************************************/
/*!
    @file     rdmTest.ino
    @author   Claude Heintz
    @license  BSD (see LXESP32DMX LICENSE)
    @copyright 2017 by Claude Heintz

    Print changes in the levels of three slots in DMX serial input
    @section  HISTORY

    v1.00 - First release
    v1.1  - Adds PWM output
*/
/**************************************************************************/
#include <LXESP32DMX.h>
#include <UID.h>
#include <TOD.h>
#include <rdm_utility.h>



// the addresses of the slots to observe
int test_slotA = 10;
int test_slotB = 1;
int test_slotC = 512;

// the levels of those slots
uint8_t test_levelA = 0;
uint8_t test_levelB = 0;
uint8_t test_levelC = 0;

//pins for PWM output
uint8_t led_pinA = 5;
uint8_t led_pinB = 18;
uint8_t led_pinC = 19;

//ledc channels (set to zero to disable)
uint8_t led_channelA = 1;
uint8_t led_channelB = 2;
uint8_t led_channelC = 3;

uint8_t testLevel = 0;
uint8_t loopDivider = 0;
uint8_t identifyFlag = 1;
uint8_t tableChangedFlag = 0;

TOD tableOfDevices(0);
TOD discoveryTree(0);

UID lower(0,0,0,0,0,0);
UID upper(0,0,0,0,0,0);
UID mid(0,0,0,0,0,0);
UID found(0,0,0,0,0,0);

#define DISC_STATE_SEARCH 0
#define DISC_STATE_TBL_CK 1
uint8_t discovery_state = DISC_STATE_TBL_CK;
uint8_t discovery_tbl_ck_index = 0;

/************************************************************************
	attach a pin to a channel and configure PWM output
*************************************************************************/
void setupPWMChannel(uint8_t pin, uint8_t channel) {
	if ( channel ) {
		ledcAttachPin(pin, channel);
		ledcSetup(channel, 12000, 16); // 12 kHz PWM, 8-bit resolution
	}
}

/************************************************************************
	gamma corrected write to a PWM channel
*************************************************************************/
void gammaCorrectedWrite(uint8_t channel, uint8_t level) {
	if ( channel ) {
		ledcWrite(channel, level*level);
	}
}

/************************************************************************
	callback for when DMX frame is received
	Note:  called from receive task
*************************************************************************/

void receiveCallback(int slots) {
	if ( slots ) {
		if ( test_levelA != ESP32DMX.getSlot(test_slotA) ) {
			test_levelA = ESP32DMX.getSlot(test_slotA);
			Serial.print(test_slotA);
			Serial.print(" => ");
			Serial.println(test_levelA);
			gammaCorrectedWrite(led_channelA, test_levelA);
		}
		if ( test_levelB != ESP32DMX.getSlot(test_slotB) ) {
			test_levelB = ESP32DMX.getSlot(test_slotB);
			Serial.print(test_slotB);
			Serial.print(" => ");
			Serial.println(test_levelB);
			gammaCorrectedWrite(led_channelB, test_levelB);
		}
		if ( test_levelC != ESP32DMX.getSlot(test_slotC) ) {
			test_levelC = ESP32DMX.getSlot(test_slotC);
			Serial.print(test_slotC);
			Serial.print(" => ");
			Serial.println(test_levelC);
			gammaCorrectedWrite(led_channelC, test_levelC);
		}
    if ( ESP32DMX.getSlot(test_slotA) == 0 ) {
      if ( slots != 512 ) {
        Serial.println("slots = ");
        Serial.println(slots);
      }
    }
	}
}

//***************** discovery functions

//#define PRINT_DIAGNOSTIC_MSGS

void checkDeviceFound(UID found) {
#if defined PRINT_DIAGNOSTIC_MSGS
    Serial.print("check device-> ");
    Serial.print(found);
#endif
  if ( testMute(found) ) {
#if defined PRINT_DIAGNOSTIC_MSGS
    Serial.println(" found!");
#endif
    tableOfDevices.add(found);
    tableChangedFlag = 1;
  }
#if defined PRINT_DIAGNOSTIC_MSGS
    else {
    Serial.println(" failed.");
  }
#endif
}

uint8_t testMute(UID u) {
   // try three times to get response when sending a mute message
   if ( ESP32DMX.sendRDMDiscoveryMute(u, RDM_DISC_MUTE) ) {
     return 1;
   }
   if ( ESP32DMX.sendRDMDiscoveryMute(u, RDM_DISC_MUTE) ) {
     return 1;
   }
   if ( ESP32DMX.sendRDMDiscoveryMute(u, RDM_DISC_MUTE) ) {
     return 1;
   }
   return 0;
}

uint8_t checkTable(uint8_t ck_index) {
  if ( ck_index == 0 ) {
    ESP32DMX.sendRDMDiscoveryMute(BROADCAST_ALL_DEVICES_ID, RDM_DISC_UNMUTE);
  }

  if ( tableOfDevices.getUIDAt(ck_index, &found) )  {
    if ( testMute(found) ) {
      // device confirmed
      return ck_index += 6;
    }
    
#if defined PRINT_DIAGNOSTIC_MSGS
    Serial.print("device disconnected ");
    Serial.print(found);
#endif
    // device not found
    tableOfDevices.removeUIDAt(ck_index);
    tableChangedFlag = 1;
    return ck_index;
  }
  // index invalid
  return 0;
}

void identifyEach() {
  Serial.println("_______________ Identifying Devices _______________");
  int i = 0;
  uint8_t notDone = 1;
  while ( notDone ) {
    i = tableOfDevices.getNextUID(i, &found);
    if ( i < 0 ) {
      notDone = 0;
    } else {
      //uint16_t data;  //for DMX address and identify device on/off
      uint8_t data[2];
      if ( ESP32DMX.sendRDMGetCommand(found, RDM_DEVICE_START_ADDR, data, 2) ) {
        uint16_t addr = (data[0] << 8) | data[1];
        Serial.print(found);
        Serial.print(" address=> ");
        Serial.println(addr);
        if ( addr == 0x0F ) {
          data[0] = 0x00;
          data[1] = 0x01;
          ESP32DMX.sendRDMSetCommand(found, RDM_DEVICE_START_ADDR, (uint8_t*)data, 2);
        }
  
        data[0] = 0x01;
        ESP32DMX.sendRDMSetCommand(found, RDM_IDENTIFY_DEVICE, (uint8_t*)data, 1);
        delay(2000);
        data[0] = 0x00;
        ESP32DMX.sendRDMSetCommand(found, RDM_IDENTIFY_DEVICE, (uint8_t*)data, 1);
      }
    }
  }
}

//called when range responded, so divide into sub ranges push them on stack to be further checked
void pushActiveBranch(UID lower, UID upper) {
  if ( mid.becomeMidpoint(lower, upper) ) {
    discoveryTree.push(lower);
    discoveryTree.push(mid);
    discoveryTree.push(mid);
    discoveryTree.push(upper);
  } else {
    // No midpoint possible:  lower and upper are equal or a 1 apart
    checkDeviceFound(lower);
    checkDeviceFound(upper);
  }
}

void pushInitialBranch() {
  lower.setBytes(0);
  upper.setBytes(BROADCAST_ALL_DEVICES_ID);
  discoveryTree.push(lower);
  discoveryTree.push(upper);

  //ETC devices seem to only respond with wildcard or exact manufacturer ID
  lower.setBytes(0x657400000000);
  upper.setBytes(0x6574FFFFFFFF);
  discoveryTree.push(lower);
  discoveryTree.push(upper);
}

uint8_t checkNextRange() {
  if ( discoveryTree.pop(&upper) ) {
    if ( discoveryTree.pop(&lower) ) {
      if ( lower == upper ) {
        checkDeviceFound(lower);
      } else {        //not leaf so, check range lower->upper
        uint8_t result = ESP32DMX.sendRDMDiscoveryPacket(lower, upper, &found);
        if ( result ) {
#if defined PRINT_DIAGNOSTIC_MSGS
          if ( result == 2 ) {    //single device returned (don't depend on this!)
            Serial.println(found);  
          }
#endif
          //this range responded, so divide into sub ranges push them on stack to be further checked
          pushActiveBranch(lower, upper);
           
        } else if ( ESP32DMX.sendRDMDiscoveryPacket(lower, upper, &found) ) {
            pushActiveBranch(lower, upper); //if discovery fails, try a second time
        }
      }         // end check range
      return 1; // UID ranges may be remaining to test
    }           // end valid pop
  }             // end valid pop  
  return 0;     // none left to pop
}



void testRDMDiscovery() {
  if ( discovery_state ) {
    // check the table of devices
    discovery_tbl_ck_index = checkTable(discovery_tbl_ck_index);
    if ( discovery_tbl_ck_index == 0 ) {
      // done with table check
      discovery_state = DISC_STATE_SEARCH;
      pushInitialBranch();

      if ( identifyFlag ) {   //once per cycle identify each device
        identifyEach();       //this is just to demonstrate GET device address
        identifyFlag = 0;     //and SET identify device
      }
      
      if ( tableChangedFlag ) {   //if the table has changed...
        tableChangedFlag = 0;

        // if this were an Art-Net application, you would send an 
        // ArtTOD packet here, because the device table has changed.
        // for this test, we just print the list of devices
        Serial.println("_______________ Table Of Devices _______________");
        tableOfDevices.printTOD();
      }
 #if defined PRINT_DIAGNOSTIC_MSGS
      Serial.println("table check complete");
#endif
    }
  } else {    // search for devices in range popped from discoveryTree
    if ( checkNextRange() == 0 ) {
      // done with search
      discovery_tbl_ck_index = 0;
      discovery_state = DISC_STATE_TBL_CK;
 #if defined PRINT_DIAGNOSTIC_MSGS
      Serial.println("search complete");
 #endif
    }
  } 
}

/************************************************************************
	setup
*************************************************************************/
void setup() {
  Serial.begin(115200);
  Serial.print("setup... ");
  
  setupPWMChannel(led_pinA, led_channelA);
  setupPWMChannel(led_pinB, led_channelB);
  setupPWMChannel(led_pinC, led_channelC);
  pinMode(15, OUTPUT);
  
  ESP32DMX.setDataReceivedCallback(receiveCallback);
  ESP32DMX.startRDM(4);
  Serial.println("setup complete");
}


/************************************************************************

  The main loop checks to see if the level of the designated slot has changed
  and prints the new level to the serial monitor.  If a PWM channel is assigned,
  it also sets the output level.
  
*************************************************************************/

void loop() {
#if defined PRINT_DIAGNOSTIC_MSGS
  Serial.print("loop ");
  Serial.println(testLevel);
#endif
  delay(2);
  testRDMDiscovery();
  
  ESP32DMX.setSlot(7,testLevel);
  ESP32DMX.setSlot(8,255);
  ESP32DMX.setSlot(371,testLevel);
  ESP32DMX.setSlot(22,255);
  loopDivider++;
  if ( loopDivider == 4 ) {
    testLevel++;
    loopDivider = 0;
  }
  if ( testLevel == 1 ) {
    delay(500);
    identifyFlag = 1;
  }
}
