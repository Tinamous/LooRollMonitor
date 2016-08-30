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

STARTUP(System.enableFeature(FEATURE_RETAINED_MEMORY));

// How long to sleep for when entering deep sleep.
// Development: 30s...
//int deepSleepTimeSeconds = 60;
// Production: 3600 (1 hour) or more is usable
int deepSleepTimeSeconds = 3600;

int debugSleepSeconds = 10;

// The maximum number of sensors on the board. values 1-4 acceptable.
uint8_t maxSensors = 3;

// Default initial debugging setting to publish ADC values as they are read.
bool publishAdcValues = false;

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
// Stored in backupd ram so will be kept between
// deep sleeps
retained uint8_t looRollCount = -1;

// Individual sensor values for the rolls.
bool hasRoll[] = {false, false, false, false};

// The ADC values read for the rolls.
int adcValues[] = {0,0,0,0};

// Mapping for roll index to ADC/DIO pins.
int sensors[] = {A0, A1, A2, A3};
// V1 PCB, all sensors are using D2 for LED drive.
int leds[] = {D2, D2, D2, D2};
// V1 hardware has single drive pin
int ledPin = D2;

// "debug" led behind the OSH Logo.
int oshLogoLedPin = A4;

// Water sense pin.
int waterSensePin = WKP; // A7 - used also for water sense trigger.
// If water has been sensed.
int waterSensed = false;

// if water was sensed in the interrupt.
volatile int waterSenseTriggered = false;

// If the water sensed alert has been published
// Stored in backupd ram so will be kept between
// deep sleeps
retained int waterSensedPublished = false;

// Battery
// Battery voltage sense pin.
int vBattSensePin = DAC; // A6
int vBattAdc = 0;
int vBattThreshold = 3.8; // 3.8V cutoff for low battery
bool lowBattery = false;
double batteryVoltage = 0;

bool calibrating = false;
bool messagePublishedWithNoDelay = false;

retained bool isWakeFromPowerSave = false;

// how long between reset and set-up completing (i.e. connect)
unsigned long timeToConnect = 0;

// how long from reset to sleep (i.e. running on full power)
retained unsigned long timeToSleep = 0;

//////////////////////////////////////////////////////////////////////////
// Particle methods
//////////////////////////////////////////////////////////////////////////
int setMinRolls(String args);
int getCount(String args);
int calibrate(String args);
int getCalibration(String args);

//////////////////////////////////////////////////////////////////////////
// Setup
//////////////////////////////////////////////////////////////////////////
void setup() {
    RGB.control(true);
    RGB.brightness(25);
    // Blue to indicate set-up and not cleash with red/green of status
    RGB.color(0, 0, 255);
    
    for (int i=0; i<maxSensors; i++) {
        pinMode(sensors[i], INPUT);
        pinMode(leds[i], OUTPUT);
        digitalWrite(leds[i], LOW);
    }
    
    // OSH Logo (High power mode) LED
    pinMode(oshLogoLedPin, OUTPUT);
    digitalWrite(oshLogoLedPin, HIGH);
    
    // Water sense pin
    // This is used as the wake up pin
    // Active low digital input.
    // pinMode(waterSensePin, INPUT_PULLUP);
    
    // Battery sense. Analog input.
    pinMode(vBattSensePin, INPUT);
    
    // Debug
    pinMode(D7, OUTPUT);
    
    // Particle functions
    Particle.function("setMinRolls", setMinRolls);
    Particle.function("getCount", getCount);
    Particle.function("calibrate", calibrate);
    Particle.function("getCal", getCalibration);
    
    // Publish version info on first start only
    if (!isWakeFromPowerSave) {
        publishStatus("Loo Roll Monitor V0.5.15", false);
        
    }
    
    // Load in the ADC thresholds for the sensors and bits.
    readSettings();
    
    // Now attach the water sense interrupt.
    // only interested in falling edge to indicate water present
    // allow natural polling to clear it
    //attachInterrupt(waterSensePin, waterSenseIsr, FALLING);
    
    timeToConnect = millis();
}

void loop() {
    // Whilst in High power mode keep the OSH LED on to indicate high power.
    // recovering from sleep mode indication...
    digitalWrite(oshLogoLedPin, HIGH);
    RGB.brightness(25);
    
    if (!calibrating) {
        checkVBatt();
        checkWaterSense();
        
        // If the battery is low then hold off checking the rolls
        // as it may kill the battery.
        if (!lowBattery) {
            checkLooRoll();
        }
        
        // Show a warning (red) led if their is a problem.    
        // Do this before publishing so it's shown for a little bit longer.
        if (shouldShowWarning()) {
            RGB.color(255, 0, 0);
        } else {
            RGB.color(0, 255, 0);
        }
        
        if (!lowBattery) {
            publishSystemState();
        }
    }
    
    sleep();
}

// Determine if a warning LED should be shown
// e.g. Battery low, water sensed or loo roll count is low.
bool shouldShowWarning() {
    if (looRollCount <= minimumRollCount) {
        return true;
    }
        
    if (waterSensedPublished) {
        return true;
    }
            
    if (lowBattery) {
        return true;            
    }
    
    return false;
}

// Sleep the Photon, depending on how long it's been awake
// either heavy sleep, or light sleep to help debugging.
void sleep() {
    // millis not affected by sleep.
    unsigned long t = millis();
    
    // If more than n minutes since the power on
    // then go into Low power mode, or if the power-up
    // was caused by a deep sleep power down.
    if (isWakeFromPowerSave || t > (120 * 1000)) {
        
        // Small delay to ensure any messages that needed to be 
        // pushed to Particle are gone before sleeping.
        if (messagePublishedWithNoDelay) {
            delay(2000);
        }
        
        digitalWrite(oshLogoLedPin, LOW);
        
        // Next power-up will not be the initial one
        // so the delay before sleeping can be ignored.
        //isInitialPowerUp = false;
        isWakeFromPowerSave = true;
        timeToSleep = millis();
        
        // Deep sleep: wake after n seconds or rising edge on WKP
        // (water sense pin) - however water sense is active low so wkp 
        // will only trigger 
        // NB: Need pull-down for LED drive to ensure LEDs are not
        // turned on when in deep sleep.
        // WKP being high because of water sense appears to break
        // this being pulled out of deep sleep.
        System.sleep(SLEEP_MODE_DEEP, deepSleepTimeSeconds);
        
        // Stop mode: wake after n seconds or falling edge on water sense pin.
        // This sleep mode keeps memory preserved and allows for falling signal
        // on interrupt pin (WKP here) to wake up.
        // however it uses ca. 1-2mA
        //System.sleep(waterSensePin, FALLING, deepSleepTimeSeconds);
        
        // Don't publish debug ADC values whilst in low power mode.
        publishAdcValues = false;
        
    } else {
        // Debug mode:
        // Check every 10 seconds and don't go into low power
        // Handy for debug or initial power up + firmware updates
        for (int i=0; i<debugSleepSeconds; i++) {
            delay(1000);
            // If water sensed then exit out of the delay
            // and allow the loop to process the status.
            if (waterSenseTriggered) {
                return;
            }
        }
        
        // Set true to help debugging.
        publishAdcValues = false;
    }
}

// Publish the system status (roll count, water sensed, battery level etc.)
void publishSystemState() {
    
    String senmlFields = "{'n':'rollCount','v':'" + String(looRollCount) + "'}";
    senmlFields += ",{'n':'WaterSensed','v':'" + String(waterSensed) + "'}";
    senmlFields += ",{'n':'VBatt','v':'" + String(batteryVoltage) + "'}";
    // Debug fields
    senmlFields += ",{'n':'A0','v':'" + String(adcValues[0]) + "'}";
    senmlFields += ",{'n':'A1','v':'" + String(adcValues[1]) + "'}";
    senmlFields += ",{'n':'A2','v':'" + String(adcValues[2]) + "'}";
    senmlFields += ",{'n':'TConn','v':'" + String(timeToConnect) + "'}";
    senmlFields += ",{'n':'TTS','v':'" + String(timeToSleep) + "'}";
    
    // Don't forget Temperature and humidity if they are measured.
    
    // Publish the number of rolls, water sense, 
    // publish this every wake cycle regardless to keep
    // monitor informed. 
    publishSenML("{e:[" + senmlFields +  "]}");
}

//////////////////////////////////////////////////////////////////////////////
// Roll detection
//////////////////////////////////////////////////////////////////////////////
void checkLooRoll() {
    
    int count = 0;
    
    // Work up the sensors from the lowest (A0)
    // to the highest (A3 or ...)
    // This is not the most optimal use of power but is easy...
    // It is also ignores gravity defying errors (e.g. base roll
    // may appear to be absent but rolls above present)
    for (int i=0; i<maxSensors; i++) {
        hasRoll[i] = hasLooRoll(i);
        if (hasRoll[i]) {
            count++;
        } 
    }
    
    // Check to see if the number of rolls has changed since we 
    // last measured and 
    if (count != looRollCount || publishAdcValues) {
        looRollCount = count;
        
        notifyLooRollCountChanged();
        
        // Store in EEPROM the count
        writeLooRollCount();
    }
}

void notifyLooRollCountChanged() {
    
    if (looRollCount <= minimumRollCount) {
        // Ensure we have a good delay for sending the message.
        publishStatus("Loo roll Low!!! Rolls remaining: " + String(looRollCount), true);
    }
        
    publishStatus("Rolls remaining: " + String(looRollCount), false);
}

// Read the IR sensor ADC value.
int readSensor(int sensorId) {
    int ledChannel = leds[sensorId];
    
    // Compute how much the ADC input is decreased due to background IR
    int adcBefore = analogRead(sensors[sensorId]);
    int background = 4096 - adcBefore; 
    
    // Enable the IR LED and measure the reflectance level
    // A lower ADC value means more reflectance.
    digitalWrite(ledChannel, HIGH);
    delay(400);
    int adc = analogRead(sensors[sensorId]);
    digitalWrite(ledChannel, LOW);
    delay(500);
    
    if (background > 10) {
        // Debug to catch possible background noise. Should not be 
        // sent under normal operation.
        publishStatus("background: " + String(background), false);
    }
    

    // Add the background value to the adc measured value to  compensate
    return adc + background;
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
        publishStatus("WATER LEAK DETECTED!", true);
        publishSenML("{e:[{'n':'WaterSensed','v':'" + String(waterSensed) + "'} ]}");
        waterSensedPublished = true;
        
        // Small delay to allow critical message to be published.
        delay(2000);
    }
    
    // If we have published water sense detected but it is not now.
    // then clear the warning
    if (!waterSensed && waterSensedPublished) {
        publishStatus("Water leak cleared.", false);
        publishSenML("{e:[{'n':'WaterSensed','v':'" + String(waterSensed) + "'} ]}");
        waterSensedPublished = false;
    }
    
    // Don't do any action on waterSenseTriggered
    // but use it to bring the photon out of low power mode.
    // clear it hear if it had been set by the interrupt
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
    if (!lowBattery && batteryVoltage < vBattThreshold) {
        lowBattery = true;
        publishStatus("Battery Low!", true);
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

// Request to publish calibration details as status post.
// 1: Cal1
// 2: Cal2
// 3: Thresholds.
int getCalibration(String args) {
     switch (args.toInt()) {
        case 1:
            publishStatus("A0:" + String(adcEmpty[0]) + ", A1:" + String(adcEmpty[1]) + ", A2:" + String(adcEmpty[2]) + ", A3:" + String(adcEmpty[3]) + " #Cal1 #RollsAbsent", true);
            return 1;
        case 2:
            publishStatus("A0:" + String(adcFull[0]) + ", A1:" + String(adcFull[1]) + ", A2:" + String(adcFull[2]) + ", A3:" + String(adcFull[3]) + " #Cal2 #RollsPresent", true);
            return 2;
        case 3:
            publishStatus("A0:" + String(adcThresholds[0]) + ", A1:" + String(adcThresholds[1]) + ", A2:" + String(adcThresholds[2]) + ", A3:" + String(adcThresholds[3]) + " #Threshold", true);
            return 3;
        default:
            return 0;
    }
    
}

//////////////////////////////////////////////////////////////////////////////
// Sensor Calibration
//////////////////////////////////////////////////////////////////////////////

// Calibrate the sensors with no rolls on the stand
void doRollEmptyCalibration() {
    
    calibrating = true;
    publishStatus("Performing roll absent calibration.", true);
        
    for (int i=0; i<maxSensors; i++) {
        adcEmpty[i] = (uint16_t)readSensor(i);
    }
    
    publishSenML("{e:[{'n':'A0-Absent','v':'" + String(adcEmpty[0]) + "'},{'n':'A1-Absent','v':'" + String(adcEmpty[1]) + "'},{'n':'A2-Absent','v':'" + String(adcEmpty[2]) + "'},{'n':'A3-Absent','v':'" + String(adcEmpty[3]) + "'},{'n':'Tags','sv':'Cal1,Empty'} ]}");
    
    // Cal 1 performed. Forces cal2 to be redone but assumes values reasobable.
    calibrationState = 1; 
    calibrating = false;
}

// Calibrate the sensors fully fitted with rolls.
// roll present  cal assumes that empty cal has already been performed.
// ADC values for roll present calibration are stored in adcFull at offset 20.
// This assumes cal 1 (empty cal), has been performed.
void doRollPresentCalibration() {
    
    calibrating = true;
    publishStatus("Performing roll present calibration.", true);

    for (int i=0; i<maxSensors; i++) {
        adcFull[i] = (uint16_t)readSensor(i);
    }
    
    publishSenML("{e:[{'n':'A0-Present','v':'" + String(adcFull[0]) + "'},{'n':'A1-Present','v':'" + String(adcFull[1]) + "'},{'n':'A2-Present','v':'" + String(adcFull[2]) + "'},{'n':'A3-Present','v':'" + String(adcFull[3]) + "'},{'n':'Tags','sv':'Cal2,Full'} ]}");

    computeAdcThresholds();
    
    // Cal 2 performed (assumed cal 1 performed)
    calibrationState = 2; 
    writeSettings();
    calibrating = false;
    
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
            publishStatus("Calibration error for channel: " + String(i) + " ADC for empty < present.", true);
        }
    }
    
    // Now publish the thresholds (small delay to prevent rate limiting @ particle as a few messages are published during calibration)
    delay(2000);
    publishSenML("{e:[{'n':'A0-Threshold','v':'" + String(adcThresholds[0]) + "'},{'n':'A1-Threshold','v':'" + String(adcThresholds[1]) + "'},{'n':'A2-Threshold','v':'" + String(adcThresholds[2]) + "'},{'n':'A3-Threshold','v':'" + String(adcThresholds[3]) + "'} ]}");
    
    // Publish cal 1 and cal 2 values as well to help debug, with a small delay to prevent rate limiting.
    //delay(1000);
    //Particle.publish("senml", "{e:[{'n':'A0','v':'" + String(adcEmpty[0]) + "'},{'n':'A1','v':'" + String(adcEmpty[1]) + "'},{'n':'A2','v':'" + String(adcEmpty[2]) + "'},{'n':'A3','v':'" + String(adcEmpty[3]) + "'},{'n':'Tags','sv':'Cal1,Empty'} ]}", 60, PRIVATE);
    //delay(1000);
    //Particle.publish("senml", "{e:[{'n':'A0','v':'" + String(adcFull[0]) + "'},{'n':'A1','v':'" + String(adcFull[1]) + "'},{'n':'A2','v':'" + String(adcFull[2]) + "'},{'n':'A3','v':'" + String(adcFull[3]) + "'},{'n':'Tags','sv':'Cal2,Full'} ]}", 60, PRIVATE);
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
    //writeLooRollCount();
    EEPROM.write(4, minimumRollCount);
    EEPROM.put(10, adcEmpty);
    EEPROM.put(20, adcFull);
    EEPROM.put(30, adcThresholds);
}

// This will change frequently so allow it to be 
// set by itself rather than write all the settings 
void writeLooRollCount() {
    //EEPROM.write(3, looRollCount); 
}

void readSettings() {
     
    // read version info.
    uint8_t version = EEPROM.read(0);
    
    if (version == 1) {
        calibrationState = EEPROM.read(1);
        maxSensors = EEPROM.read(2);
       // looRollCount = EEPROM.read(3);
        minimumRollCount = EEPROM.read(4);
        EEPROM.get(10, adcEmpty);
        EEPROM.get(20, adcFull);
        EEPROM.get(30, adcThresholds);
    } else {
        publishStatus("Using default values. Calibration required", true);
    }
}

//////////////////////////////////////////////////////////////////////////////
// Publishing helpers
//////////////////////////////////////////////////////////////////////////////
void publishStatus(String message, bool isCritical) {
    // TODO: Connect to Particle.io if needed
    // wait for connection to establish
    
    Particle.publish("status", message, 60, PRIVATE);
    
    if (isCritical) {
        // Delay to ensure critical messages are sent
        // and don't sit in the buffer
        delay(2000);
    } else {
        // Message has been published without a delay
        // may need a delay to ensure that the message
        // is actually sent.
        messagePublishedWithNoDelay = true;
    }
}

void publishSenML(String message) {
    // TODO: Connect to Particle.io if needed
    // wait for connection to establish
    Particle.publish("senml", message, 60, PRIVATE);
    delay(1000);
    
    //messagePublishedWithNoDelay = true;
}

//////////////////////////////////////////////////////////////////////////////
// Interrupt handing
//////////////////////////////////////////////////////////////////////////////

void waterSenseIsr() {
    waterSenseTriggered = true;
}