// =========================================================================
// Toilet roll monitor - V1 PCB.
// Apache V2 License.
// Author: Stephen Harrison
// =========================================================================

// Each sensor has a small range of values between roll present and absent, 
// a value above the adcThresholds[] value for the sensor sets the roll as 
// not being present. A lower ADC value (more reflectance) indicates a roll present.
// 
// Each sensor is individually calibrated first (cal1) with no roll present to get a
// no reflection value, then with a roll present (cal2) to get the value with a roll present.
// The ADC threshold is set as the midpoint between the cal1 and cal2 value 
//
// **************************************************************************
// On first run, no rolls should be fitted and the baseline is taken.
// Rolls should then be fitted and a Cal2 triggered (call the particle function "calibrate" with the argument value "2")
// Cal 1 can be repeated by calling the "calibrate" function with argument value "1" first if this is desired.
// both Cal1 and Cal2 need to be run to determine the correct threshold for each sensor.
// Cal2 MUST be run after a Cal1.
// **************************************************************************
//
// ADC values and roll count are stored in EEPROM.
// When the roll count changes a senml message is published with the roll count.
// 

// The maximum number of sensors on the board. values 1-4 acceptable.
uint8_t maxSensors = 3;

// Default initial debugging setting to publish ADC values as they are read.
bool publishAdcValues = true;

// Initialise to defaults but calibrated values are loaded 
// from EEPROM if the value is stored.

// Empty/full threshold value. 
uint16_t adcThresholds[] =  {3990, 3990, 3990, 3990};

// Cal 1 ADC values for no roll present
uint16_t adcEmpty[] =  {4096, 4096, 4096, 4096}; 

// CAL 2 ADC values with roll present.
uint16_t adcFull[] =  {3967, 3967, 3967, 3967}; 

// 0: Not claibrated. 
// 1: Cal 1 (no roll)
// 2: Cal 2 (roll present)
uint8_t calibrationState = 0; 

// The minimum number of rolls. When the roll count hits this value
// the low roll warning is triggered.
uint8_t minimumRollCount = 1;

// The number of loo rolls detected.
uint8_t looRollCount = -1;

// Individual sensor values for the rolls.
bool hasRoll[] = {false, false, false, false};
// The ADC values read for the rolls.
int adcValues[] = {0,0,0,0};

// Mapping for roll index to ADC/DIO pins.
int sensors[] = {A0, A1, A2, A3};
// V1 PCB, all sensors are using D2 for LED drive.
int leds[] = {D2, D2, D2, D2};

// "debug" led behind the OSH Logo.
int oshLogoLedPin = A4;

// Water sense pin.
int waterSensePin = WKP; // A7 - used also for water sense trigger.
// If water has been sensed.
int waterSensed = false;

// if water was sensed in the interrupt.
volatile int waterSenseTriggered = false;

// If the water sensed alert has been published
int waterSensedPublished = false;

// Battery
// Battery voltage sense pin.
int vBattSensePin = DAC; // A6
int vBattAdc = 0;
int vBattAdcThreshold = 2048;
bool lowBattery = false;
double batteryVoltage = 0;

//////////////////////////////////////////////////////////////////////////
// Particle methods
//////////////////////////////////////////////////////////////////////////
int setMinRolls(String args);
int getCount(String args);
int calibrate(String args);

//////////////////////////////////////////////////////////////////////////
// Setup
//////////////////////////////////////////////////////////////////////////
void setup() {
    RGB.control(true);
    RGB.color(0, 0, 255);
    
    for (int i=0; i<maxSensors; i++) {
        pinMode(sensors[i], INPUT);
        pinMode(leds[i], OUTPUT);
        digitalWrite(leds[i], LOW);
    }
    
    // Water sensor
    pinMode(A5, INPUT);
    pinMode(D6, OUTPUT);
    digitalWrite(D6, LOW);
    
    // OSH Logo LED
    pinMode(oshLogoLedPin, OUTPUT);
    digitalWrite(oshLogoLedPin, LOW);
    
    // Water sense pin
    // This is used as the wake up pin
    pinMode(waterSensePin, INPUT_PULLUP);
    
    // Battery sense
    pinMode(vBattSensePin, INPUT);
    
    // Debug
    pinMode(D7, OUTPUT);
    
    // Particle functions
    Particle.function("setMinRolls", setMinRolls);
    Particle.function("getCount", getCount);
    Particle.function("calibrate", calibrate);
    publishStatus("Loo Roll Monitor V0.4.00");
    //Particle.publish("status", "Loo Roll Monitor V0.3.11");
    
    // Blue to indicate set-up 
    // and not cleash with red/green of status
    RGB.color(0, 0, 255);
    
    // Load in the ADC thresholds for the sensors and bits.
    readSettings();
    
    // Allow time to send status.
    delay(2000);
    
    // Now attach the water sense interrupt.
    // only interested in falling edge to indicate water present
    // allow natural polling to clear it
    attachInterrupt(waterSensePin, waterSenseIsr, FALLING);
}

void loop() {
    // Whilst in High power mode keep the OSH LED on to indicate high power.
    // recovering from sleep mode indication...
    digitalWrite(oshLogoLedPin, HIGH);
    RGB.brightness(25);
    
    checkVBatt();
    checkLooRoll();
    checkWaterSense();
    publishStatus();
    
        // If more than 0 then set RGB LED to green
    if (looRollCount > minimumRollCount && !waterSensedPublished) {
        RGB.color(0, 255, 0);
    } else {
        // If loo roll count low, or water sensed then show red.
        RGB.color(255, 0, 0);
    }
    
    sleep();
}

// Sleep the Photon, depending on how long it's been awake
// either heavy sleep, or light sleep to help debugging.
void sleep() {
    // millis not affected by sleep.
    unsigned long t = millis();
    
    // If more than n minutes since the last reset
    // then go into Low power mode
    if (t > (60 * 1000)) {
        //Particle.publish("status", "Going to sleep...");
        //delay(1000);
        
        // Indivate low power mode with a bright LED!
        digitalWrite(oshLogoLedPin, LOW);
        RGB.brightness(5);
        
        
        // Sleep WiFi (but still runs user code so
        // still runs high current (40mA))
        //System.sleep(30);
        //delay(30000);
        
        // Deep sleep, wake after n seconds or falling edge on water sense pin.
        // This sleep mode keeps memory preserved and allows for falling signal
        // on interrupt pin (WKP here) to wake up.
        // however it uses ca. 1-2mA
        System.sleep(waterSensePin, FALLING, 30);
        
        //Particle.publish("status","Awake!");
        //delay(5000);

        publishAdcValues = false;
        
    } else {
        // Otherwise check every 10 seconds and don't go into low power
        // Handy for debug or initial power up + firmware updates
        for (int i=0; i<10; i++) {
            delay(1000);
            // If water sensed then exit out of the delay
            // and allow the loop to process the status.
            if (waterSenseTriggered) {
                return;
            }
        }
        publishAdcValues = true;
    }
}

void publishStatus() {
    // Publish the number of rolls, water sense, 
    // publish this every wake cycle regardless to keep
    // monitor informed. 
    publishSenML("{e:[{'n':'rollCount','v':'" + String(looRollCount) + "'},{'n':'WaterSensed','v':'" + String(waterSensed) + "'},{'n':'VBatt','v':'" + String(batteryVoltage) + "'} ]}");
        // Don't forget Temperature and humidity if they are measured.
}


//////////////////////////////////////////////////////////////////////////////
// Roll detection
//////////////////////////////////////////////////////////////////////////////
void checkLooRoll() {
    
    int count = 0;
    
    // Work up the sensors from the lowest (A0)
    // to the highest (A3 or ...)
    for (int i=0; i<maxSensors; i++) {
        hasRoll[i] = hasLooRoll(i);
        if (hasRoll[i]) {
            count++;
        } 
    }
    
    // Check to see if the number of rolls has changed since we 
    // last measured
    if (count != looRollCount || publishAdcValues) {
        // Globally store the loo roll count
        // TODO: Write to EEPROM???
        looRollCount = count;
        
        notifyLooRollCountChanged();
    }
    
    if (publishAdcValues) {
        publishSenML("{e:[{'n':'A0','v':'" + String(adcValues[0]) + "'},{'n':'A1','v':'" + String(adcValues[1]) + "'},{'n':'A2','v':'" + String(adcValues[2]) + "'},{'n':'A3','v':'" + String(adcValues[3]) + "'} ]}");
        //Particle.publish("senml", "{e:[{'n':'A0','v':'" + String(adcValues[0]) + "'},{'n':'A1','v':'" + String(adcValues[1]) + "'},{'n':'A2','v':'" + String(adcValues[2]) + "'},{'n':'A3','v':'" + String(adcValues[3]) + "'} ]}", 60, PRIVATE);
        delay(1000);
    }
}

void notifyLooRollCountChanged() {
    // TODO: Allow user to define minimum roll count.
    if (looRollCount <= minimumRollCount) {
        // Ensure we have a good delay for sending the message.
        publishStatus("Loo roll Low!!! Rolls remaining: " + String(looRollCount));
        //Particle.publish("status", "Loo roll Low!!! Rolls remaining: " + String(looRollCount));
        delay(1000);
    }
        
    //Particle.publish("status", "Their are now " + String(looRollCount) + " loo rolls on the holder.");
    
    //Particle.publish("senml", "{e:[{'n':'rollCount','v':'" + String(looRollCount) + "'} ]}", 60, PRIVATE);
    publishSenML("{e:[{'n':'rollCount','v':'" + String(looRollCount) + "'} ]}");
    
    // Ensure we have a good delay for sending the message.
    // as we might sleep immediatly after and risk the 
    // message not having been sent.
    delay(2000);
}

// Read the IR sensor ADC value.
int readSensor(int sensorId) {
    int ledChannel = leds[sensorId];
    digitalWrite(ledChannel, HIGH);
    delay(20);
    
    int adc = analogRead(sensors[sensorId]);
    
    digitalWrite(ledChannel, LOW);
    return adc;
}

// Determine if a loo roll is present at the sensor position.
// 0 is top, 3 is bottom (if fitted)
bool hasLooRoll(int sensorId) {

    adcValues[sensorId] = readSensor(sensorId);
    
    // If ADC reading below the threshold then
    // their is a roll there reflecting.
    return adcValues[sensorId] < adcThresholds[sensorId];
}



//////////////////////////////////////////////////////////////////////////////
// Water leak detection
//////////////////////////////////////////////////////////////////////////////
void checkWaterSense() {
    // Pin reads low if water is present.
    waterSensed = !digitalRead(waterSensePin);
    
    // trigger once only.
    if (waterSensed && !waterSensedPublished) {
        publishStatus("WATER LEAK DETECTED!");
        //Particle.publish("status", "WATER LEAK DETECTED!");
        publishSenML("{e:[{'n':'WaterSensed','v':'" + String(waterSensed) + "'} ]}");
        //Particle.publish("senml", "{e:[{'n':'WaterSensed','v':'" + String(waterSensed) + "'} ]}", 60, PRIVATE);
        waterSensedPublished = true;
    }
    
    // If we have published water sense detected but it is not now.
    // then clear the warning
    if (!waterSensed && waterSensedPublished) {
        publishStatus("Water leak cleared.");
        //Particle.publish("status", "Water leak cleared.");
        publishSenML("{e:[{'n':'WaterSensed','v':'" + String(waterSensed) + "'} ]}");
        //Particle.publish("senml", "{e:[{'n':'WaterSensed','v':'" + String(waterSensed) + "'} ]}", 60, PRIVATE);
        waterSensedPublished = false;
    }
    
    // Don't do any action on waterSenseTriggered
    // but use it to bring the photon out of low power mode.
    // clear it hear if it had been set.
    waterSenseTriggered = false;
}

//////////////////////////////////////////////////////////////////////////////
// Battery monitor
//////////////////////////////////////////////////////////////////////////////
void checkVBatt() {
    vBattAdc = analogRead(vBattSensePin);
    
    // VBatt is split by voltage divider 2:1
    batteryVoltage = ((double)vBattAdc * 0.0008) * (double)2; 
    
    // trigger once only.
    if (!lowBattery && vBattAdc < vBattAdcThreshold) {
        lowBattery = true;
        Particle.publish("status", "Battery Low!");
        Particle.publish("senml", "{e:[{'n':'VBattAdc','v':'" + String(vBattAdc) + "'} ]}", 60, PRIVATE);
        delay(1000);
      //  waterSenseTriggered = true;
    } 
    
    if (publishAdcValues) {
        Particle.publish("senml", "{e:[{'n':'VBattAdc','v':'" + String(vBattAdc) + "'},{'n':'VBatt','v':'" + String(batteryVoltage) + "'} ]}", 60, PRIVATE);
        delay(1000);
    }
}

//////////////////////////////////////////////////////////////////////////////
// Particle methods
//////////////////////////////////////////////////////////////////////////////

// Set the minimum number of rolls acceptable
int setMinRolls(String args) {
    int minimumRollCount = args.toInt();
    writeSettings();
    return minimumRollCount;
}

// Checks the rolls present and returns the count.
int getCount(String args) {
    checkLooRoll();
    return looRollCount;
}

// Calibrate the IR sensors.
// 1 = Cal 1 - No rolls.
// 2 = Cal 2 - All rolls populated.
int calibrate(String args) {

    switch (args.toInt()) {
        case 1:
            doRollEmptyCalibration();
            return 1;
        case 2:
            doRollPresentCalibration();
            return 2;
        default:
            return 0;
    }
    
}

//////////////////////////////////////////////////////////////////////////////
// Sensor Calibration
//////////////////////////////////////////////////////////////////////////////

// Calibrate the sensors with no rolls on the stand
void doRollEmptyCalibration() {
    
    Particle.publish("status", "Performing roll absent calibration.");
    delay(1000);
        
    for (int i=0; i<maxSensors; i++) {
        adcEmpty[i] = (uint16_t)readSensor(i);
        
        Particle.publish("status", "Empty Cal. Roll: " + String(i) + " ADC: "+ String(adcEmpty[i]));
        delay(1000);
    }
    
    computeAdcThresholds();
    
    // Cal 1 performed. Forces cal2 to be redone but assumes values reasobable.
    calibrationState = 1; 
    writeSettings();
}

// Calibrate the sensors fully fitted with rolls.
// roll present  cal assumes that empty cal has already been performed.
// ADC values for roll present calibration are stored in adcFull at offset 20.
// This assumes cal 1 (empty cal), has been performed.
void doRollPresentCalibration() {
    
    Particle.publish("status", "Performing roll present calibration.");
    delay(1000);
    
    for (int i=0; i<maxSensors; i++) {
        adcFull[i] = (uint16_t)readSensor(i);
    
        Particle.publish("status", "Present Cal. Roll: " + String(i) + " ADC: "+ String(adcFull[i]));
        delay(1000);
    }

    computeAdcThresholds();
    
    // Cal 2 performed (assumed cal 1 performed)
    calibrationState = 2; 
    writeSettings();
}

void computeAdcThresholds() {
    for (int i=0; i<maxSensors; i++) {
        uint16_t empty = adcEmpty[i]; //  higher value (e. 4086)
        uint16_t full = adcFull[i];   // lower value (e.g. 3984)
        
        if (empty > full) {
            
            // e.g. (4086 - 3984)/2 == 51.
            // Compute the midpoint between the full and empty value to determine 
            // the threshold point for the 
            uint16_t halfDifference = (empty - full) / 2;
            
            // take the lower value (full) and add the half difference
            // to give the threshold for detection of the roll.
            // e.g. 3984 + 51 = 4035
            adcThresholds[i] = full + halfDifference;
        } else {
            // Calibration error for this channel
            Particle.publish("status", "Calibration error for channel: " + String(i) + " ADC for empty < present.");
        }
    }
    
    // Now publish the thresholds
    Particle.publish("senml", "{e:[{'n':'A0-Threshold','v':'" + String(adcThresholds[0]) + "'},{'n':'A1-Threshold','v':'" + String(adcThresholds[1]) + "'},{'n':'A2-Threshold','v':'" + String(adcThresholds[2]) + "'},{'n':'A3-Threshold','v':'" + String(adcThresholds[3]) + "'} ]}", 60, PRIVATE);
    
    // Publish cal 1 and cal 2 values as well to help debug, with a small delay to prevent rate limiting.
    delay(1000);
    Particle.publish("senml", "{e:[{'n':'A0','v':'" + String(adcEmpty[0]) + "'},{'n':'A1','v':'" + String(adcEmpty[1]) + "'},{'n':'A2','v':'" + String(adcEmpty[2]) + "'},{'n':'A3','v':'" + String(adcEmpty[3]) + "'},{'n':'Tags','sv':'Cal1,Empty'} ]}", 60, PRIVATE);
    delay(1000);
    Particle.publish("senml", "{e:[{'n':'A0','v':'" + String(adcFull[0]) + "'},{'n':'A1','v':'" + String(adcFull[1]) + "'},{'n':'A2','v':'" + String(adcFull[2]) + "'},{'n':'A3','v':'" + String(adcFull[3]) + "'},{'n':'Tags','sv':'Cal2,Full'} ]}", 60, PRIVATE);
}

//////////////////////////////////////////////////////////////////////////////
// Settings storage
//////////////////////////////////////////////////////////////////////////////
// Memory Map...
// byte    Contenat
// 0       Version
// 1       0: Uncalibdated, 1: Cal1, 2: Cal 2 done.
// 2       Cal 1 valid
// 10      Cal 0 4x 2 array
// 20      Cal 1 4x 2 array.
// 30      ADC threshold values derived from Cal 0 / Cal 1. This determins if a roll is present or not.
// 40      ...
//////////////////////////////////////////////////////////////////////////////

void writeSettings() {
     // Version 1
    EEPROM.write(0, 1);
    // Calibration Stage. 0 = Uncalibrated. 1 = cal1 (empty), 2 = Cal2 (empty), 255 = eeprom default - uncalibrated.
    EEPROM.write(1, calibrationState); 
    EEPROM.write(2, maxSensors); 
    EEPROM.write(3, looRollCount); 
    EEPROM.write(4, minimumRollCount);
    EEPROM.put(10, adcEmpty);
    EEPROM.put(20, adcFull);
    EEPROM.put(30, adcThresholds);
}

void readSettings() {
     
    // read version info.
    uint8_t version = EEPROM.read(0);
    
    if (version == 1) {
        Particle.publish("status","Loading config");
        calibrationState = EEPROM.read(1);
        maxSensors = EEPROM.read(2);
        looRollCount = EEPROM.read(3);
        minimumRollCount = EEPROM.read(4);
        EEPROM.get(10, adcEmpty);
        EEPROM.get(20, adcFull);
        EEPROM.get(30, adcThresholds);
    } else {
        Particle.publish("status","No valid config");
    }
    
}

//////////////////////////////////////////////////////////////////////////////
// Publishing helpers
//////////////////////////////////////////////////////////////////////////////
void publishStatus(String message) {
    // TODO: Connect to Particle.io if needed
    // wait for connection to establish
    Particle.publish("status", message, 60, PRIVATE);
    delay(1000);
}

void publishSenML(String message) {
    // TODO: Connect to Particle.io if needed
    // wait for connection to establish
    Particle.publish("senml", message, 60, PRIVATE);
    delay(1000);
}

//////////////////////////////////////////////////////////////////////////////
// Interrupt handing
//////////////////////////////////////////////////////////////////////////////

void waterSenseIsr() {
    waterSenseTriggered = true;
}