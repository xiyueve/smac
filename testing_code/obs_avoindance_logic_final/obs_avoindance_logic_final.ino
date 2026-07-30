// Connections:
// ESP 32    Motor driver
// D25       A-1A
// D26       A-1B
// RX2       B-1A
// TX2       B-2A
// GND       GND
// VIN       VCC   (indirectly to external battery)
// GND              external battery ground
// VIN to ext battery

// ESP 32      IR OBJ detector
// 3V3         VCC
// GND         GND
// OUT         D19

// Define Motor Control Pins
const int LEFT_MOTOR_A = 25; // A-1A
const int RIGHT_MOTOR_A = 16; // B-1A
const int LEFT_MOTOR_B = 26; // A-1B
const int RIGHT_MOTOR_B = 17; // B-2A
const int fullSpeed = 255; // full speed at which motors can operate

const float midSpeed = (0.85)*fullSpeed;   // 50% speed

float operatingSpeed = midSpeed;  //present speed of motors
// Calibration factors (adjust between 0.8 and 1.0)
float aMotorTrim  = 0.80; 
float bMotorTrim = 1.00;

// Define the IR sensor input pin
const int IR_SENSOR_PIN = 19;
bool irObstacle;   // true if obstacle present else false
unsigned long lastActionTime = 0; // tracks the last time any movement command what executed

void setup() {
  
  // Set all motor control pins as outputs
  pinMode(LEFT_MOTOR_A, OUTPUT);
  pinMode(LEFT_MOTOR_B, OUTPUT);
  pinMode(RIGHT_MOTOR_A, OUTPUT);
  pinMode(RIGHT_MOTOR_B, OUTPUT);
  
  // Ensure everything starts turned off
  stopMotors();

  Serial.begin(115200); // Initialize serial communication and debugging

  pinMode(IR_SENSOR_PIN, INPUT); // Set GPIO 19 as an INPUT pin

}

void loop() {

  //detecting if object present at back
  irObstacle = (digitalRead(IR_SENSOR_PIN) == LOW); // LOW means object detected

  if (irObstacle) {
    Serial.println("Object detected!");
  }


  // Setting up operating speeds depending on any object behind the clock
  if (irObstacle) {
    Serial.println("moving backward");
    moveBackward();
    delay(500);

    Serial.println("turning right");
    turnRight();
    delay(500);
  } 
  else {
    Serial.println("moving forward");
    moveForward();
  }

}

// Function for basic forward movement in case of no significant event required (turn left, turn right or speed up)
// Use same function for speed up but set operatingSpeed == midSpeed or fastSpeed (after testing)
void moveForward() {
  //Left motor
  analogWrite(RIGHT_MOTOR_A, 0);
  analogWrite(RIGHT_MOTOR_B, operatingSpeed*aMotorTrim);
  //Right motor
  analogWrite(LEFT_MOTOR_A, operatingSpeed*bMotorTrim);
  analogWrite(LEFT_MOTOR_B, 0);
  
}

void moveBackward() {
  //Left Motor
  analogWrite(RIGHT_MOTOR_A, operatingSpeed*aMotorTrim);
  analogWrite(RIGHT_MOTOR_B, 0);
  //Right Motor
  analogWrite(LEFT_MOTOR_A, 0);
  analogWrite(LEFT_MOTOR_B, operatingSpeed*bMotorTrim);
  
}

// Function to turn right
void turnRight() {
  analogWrite(RIGHT_MOTOR_A, 0);
  analogWrite(RIGHT_MOTOR_B, operatingSpeed);

  analogWrite(LEFT_MOTOR_A, 0);
  analogWrite(LEFT_MOTOR_B, operatingSpeed);
  
}


// Stop the motors
void stopMotors() {
  analogWrite(LEFT_MOTOR_A, 0);
  analogWrite(RIGHT_MOTOR_A, 0);
  analogWrite(LEFT_MOTOR_B, 0);
  analogWrite(RIGHT_MOTOR_B, 0);
}
