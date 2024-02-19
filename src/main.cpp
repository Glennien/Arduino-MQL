#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <AccelStepper.h>
#include <EEPROM.h>


const int POTENTIOMETER_PIN = A1;
const int CALIBRATION_ADDR = 0; // EEPROM address
const int MOTOR_STEP_PIN = 5;
const int MOTOR_DIR_PIN = 6;
const int STEPS_PER_REVOLUTION = 400; // Update this value if using microstepping

// Initialize the stepper library
AccelStepper stepper(AccelStepper::DRIVER, MOTOR_STEP_PIN, MOTOR_DIR_PIN);

// Initialize the LCD
LiquidCrystal_I2C lcd(0x27, 16, 2); // Adjust the address and size




const unsigned long DEBOUNCE_TIME = 50;    // 50 ms debounce period
const unsigned long LONG_PRESS_TIME = 5000; // 5 seconds for long press
const unsigned long FAST_PRESS_TIME = 1500; // 1.5 seconds for fast press
const int BUTTON_PIN = 2;
unsigned long buttonPressStartTime = 0;
bool isButtonPressed = false;

// Function prototypes
void handleIdleState();
void handleCalibrationMenuState();
void handleCalibratingState();
void handlePurgingState();
void handleRunningState();
void handleCanceledState();
void centerTextOnLCD(const String &text, int row);

enum SystemState {
    Idle,
    CalibrationMenu,
    Calibrating,
    Purging,
    Running,
    Canceled
};
SystemState currentState = Idle; // Always idle on startup
SystemState previousState = Idle;

void runCalibrationMotor(int totalRevolutions) {
    long totalSteps = totalRevolutions * STEPS_PER_REVOLUTION;

    stepper.setMaxSpeed(400); // 400 steps per second (1 revolution per second)
    stepper.setSpeed(400);
    stepper.move(totalSteps);

    centerTextOnLCD("CALIBRATION", 0);

    while (stepper.distanceToGo() != 0) {
        stepper.runSpeed(); // Run the motor
    }
}

void displayCalibrationProgress(int progressPercent) {
    const int totalBlocks = 16; // Total number of blocks in the progress bar (16x2 LCD)
    int filledBlocks = (progressPercent * totalBlocks) / 100; // Calculate the number of filled blocks

    lcd.setCursor(0, 1);
    for (int i = 0; i < filledBlocks; ++i) {
        lcd.write((byte)255); // Display a filled block
    }
    for (int i = filledBlocks; i < totalBlocks; ++i) {
        lcd.write('_'); // Display an empty space for unfilled blocks
    }
}


int queryForMeasuredLiquid() {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Set liquid vol.");

    int measuredLiquid = 0;
    while (true) {
        measuredLiquid = map(analogRead(POTENTIOMETER_PIN), 0, 1023, 1, 20);
        lcd.setCursor(0, 1);
        lcd.print(measuredLiquid);
        lcd.print(" ml   ");

        if (digitalRead(BUTTON_PIN) == LOW) {
            delay(50); // Debounce delay
            break; // Exit when button is pressed
        }
    }
    return measuredLiquid;
}

void storeCalibrationValue(int measuredLiquid, int totalRevolutions) {
    float revolutionsPerML = (float)totalRevolutions / measuredLiquid;
    EEPROM.update(CALIBRATION_ADDR, revolutionsPerML);
}



void handleIdleState() {
    // Center "Idle" text on the first line
    String idleText = "Idle";
    int startPos = (16 - idleText.length()) / 2;
    lcd.setCursor(startPos, 0);
    lcd.print(idleText);

    // Display "Cal:" and the calibration value on the second line
    lcd.setCursor(0, 1);
    lcd.print("Cal:");
    lcd.print("0"); // Replace 'calibrationValue' with your variable
}

void handleCalibrationMenuState() {
    static bool isWaitingForButtonRelease = true;
    static unsigned long buttonPressStartTime = 0;

    // Wait for the button to be released if it was pressed
    if (isWaitingForButtonRelease && digitalRead(BUTTON_PIN) == HIGH) {
        isWaitingForButtonRelease = false; // Button released, ready to detect next press
    }

    // Display calibration menu options
    String calibText = "Press: Calib";
    String purgeText = "Hold: Purge";
    centerTextOnLCD(calibText, 0);
    centerTextOnLCD(purgeText, 1);

    // Detect button press and duration
    if (!isWaitingForButtonRelease) {
        if (digitalRead(BUTTON_PIN) == LOW) {
            // Button pressed, start timing
            if (buttonPressStartTime == 0) {
                buttonPressStartTime = millis();
            }
        } else if (buttonPressStartTime > 0) {
            // Button released, check duration
            unsigned long pressDuration = millis() - buttonPressStartTime;
            buttonPressStartTime = 0; // Reset timer for next press

            if (pressDuration >= 50 && pressDuration < 2000) {
                // Short press detected, enter calibration mode
                currentState = Calibrating;
            } else if (pressDuration >= 2000) {
                // Long press detected, enter purge mode
                currentState = Purging;
            }
        }
    }
}


void centerTextOnLCD(const String &text, int row) {
    int startPos = (16 - text.length()) / 2;
    lcd.setCursor(startPos, row);
    lcd.print(text);
}

void handleCalibratingState() {
    const int totalRevolutions = 10; // Define the total number of revolutions for calibration

    runCalibrationMotor(totalRevolutions); // Run the motor for calibration
    int measuredLiquid = queryForMeasuredLiquid(); // Query for measured liquid after motor run
    storeCalibrationValue(measuredLiquid, totalRevolutions); // Store the calibration value

    currentState = Idle; // Go back to Idle state or next appropriate state
}

void handlePurgingState() {
    static bool isPurging = false;
    static unsigned long purgeEndTime = 0;
    const unsigned long purgeDelay = 2000; // 2 seconds delay

    if (!isPurging) {
        // Display "Hold purge" centered when first entering purging mode
        centerTextOnLCD("Hold purge", 0);
        lcd.setCursor(0, 1); // Clear the second line
        lcd.print("                ");

        // Check for button press to start purging
        if (digitalRead(BUTTON_PIN) == LOW) {
            delay(50); // Debounce delay
            isPurging = true; // Start purging
            centerTextOnLCD("Purging..", 0); // Update display to show "Purging.."
            purgeEndTime = 0; // Reset the purge end time
        }
    } else {
        // Purging logic here
        // This is where you would activate the motor or other purging actions

        // Check if the button is released to stop purging
        if (digitalRead(BUTTON_PIN) == HIGH) {
            if (purgeEndTime == 0) { // First detection of button release
                purgeEndTime = millis(); // Mark the time of button release
            } else if (millis() - purgeEndTime > purgeDelay) {
                // Wait for 2 seconds after button release
                isPurging = false;
                currentState = Idle; // Transition back to idle state
                centerTextOnLCD("Idle", 0); // Update display for idle state
            }
        } else {
            purgeEndTime = 0; // Reset if button is pressed again
        }
    }
}


void handleRunningState() {
    // Center "Run" text on the first line
    String runText = "Run";
    int startPos = (16 - runText.length()) / 2;
    lcd.setCursor(startPos, 0);
    lcd.print(runText);

    // Additional logic for the running state
    // This might include updating the second line of the LCD with relevant information
}


void handleCanceledState(){

}

void handleButtonPress() {
    if (isButtonPressed) {
        unsigned long pressDuration = millis() - buttonPressStartTime;

        if (pressDuration >= DEBOUNCE_TIME) {
            if (pressDuration >= LONG_PRESS_TIME) {
                // Long press detected
                currentState = CalibrationMenu;
            } else if (pressDuration <= FAST_PRESS_TIME) {
                // Fast press detected
                if (currentState == Idle) {
                    currentState = Running; // Toggle to operational state
                } else if (currentState == Running) {
                    currentState = Idle; // Toggle to idle state
                }
                // Add logic here if fast press should confirm user inputs in other states
            }
        }
        isButtonPressed = false; // Reset the button press state
    }
}

void buttonPressISR() {
    if (digitalRead(BUTTON_PIN) == LOW) {
        // Button pressed
        if (!isButtonPressed) {
            buttonPressStartTime = millis(); // Start timing
            isButtonPressed = true;
        }
    } else {
        // Button released
        if (isButtonPressed) {
            handleButtonPress(); // Handle the button press
        }
    }
}


void setup() {
    // Initialize serial communication, LCD, stepper motor, etc.
    Serial.begin(9600);
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), buttonPressISR, CHANGE);
    lcd.init();
    lcd.backlight();
    stepper.setMaxSpeed(6000); // Set a high max speed
    stepper.setAcceleration(800); // Set a reasonable acceleration

    // Optional: Display a welcome message or clear the display
    lcd.clear();
}

void loop() {

    if (currentState != previousState) {
        // State has changed, clear the LCD
        lcd.clear();
        previousState = currentState; // Update the previous state
    }

    switch (currentState) {
        case Idle:
            handleIdleState();
            break;
        case CalibrationMenu:
            handleCalibrationMenuState();
            break;
        case Calibrating:
            handleCalibratingState();
            break;
        case Purging:
            handlePurgingState();
            break;
        case Running:
            handleRunningState();
            break;
        case Canceled:
            handleCanceledState();
            break;
    }

    // Handle common tasks here (if any)
}