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

// ESP 32         Ultra Dist Sensor
// VIN            Vcc         (VIN of ESP 32 is it's 5V pin, power source for other components)
// D5             Trig
// D18            Echo pin   Connected to through a voltage divider, actually connected to 1Kohm resistor, other end of resistor connected to D18, and D18 connected to ground through 2Kohm resistor)
// GND            GND

// ESP 32      IR OBJ detector
// 3V3         VCC
// GND         GND
// OUT         D19

// Define Motor Control Pins
const int RIGHT_MOTOR_A = 25; // A-1A
const int RIGHT_MOTOR_B = 26; // A-1B
const int LEFT_MOTOR_A = 16; // B-1A
const int LEFT_MOTOR_B = 17; // B-2A
const int fullSpeed = 255; // full speed at which motors can operate

const float slowSpeed = (0.65)*fullSpeed;  // 25% speed
const float midSpeed = (0.85)*fullSpeed;   // 50% speed
const float fastSpeed = (0.75)*fullSpeed;  // 75% speed
const float turnSpeed = (0.55)*fullSpeed;  // 75% speed

float operatingSpeed;  //present speed of motors
// Calibration factors (adjust between 0.8 and 1.0)
float leftMotorTrim  = 1.00; 
float rightMotorTrim = 1.00;

// Define Sensor Pins
const int TRIG_PIN = 5;
const int ECHO_PIN = 18;
float frontDist;   // distance of object from front

// Variables for calculation
long duration;
float distanceCm;

// Define the IR sensor input pin
const int IR_SENSOR_PIN = 19;
bool irObstacle;   // true if obstacle present else false
unsigned long lastActionTime = 0; // tracks the last time any movement command what executed

// --- ESCAPE / TURNING LOGIC VARIABLES ---
bool turningRight = true;              // Preference: turn right first
unsigned long turnStartTime = 0;       // Timestamp when turning started
const unsigned long MAX_TURN_TIME = 1000; // If turning for > 1000ms (1 sec), try turning left


void setup() {
  
  // Set all motor control pins as outputs
  pinMode(LEFT_MOTOR_A, OUTPUT);
  pinMode(LEFT_MOTOR_B, OUTPUT);
  pinMode(RIGHT_MOTOR_A, OUTPUT);
  pinMode(RIGHT_MOTOR_B, OUTPUT);
  
  // Ensure everything starts turned off
  stopMotors();

  Serial.begin(115200); // Initialize serial communication and debugging
  
  pinMode(TRIG_PIN, OUTPUT); // Sets the TrigPin as an Output
  pinMode(ECHO_PIN, INPUT);  // Sets the EchoPin as an Input

  pinMode(IR_SENSOR_PIN, INPUT); // Set GPIO 19 as an INPUT pin

}

void loop() {

  // Need to connect it to Run's code to start running once alarm rings

  //reading for front
  frontDist = readUltrasonicDistance();
  //detecting if object present at back
  irObstacle = (digitalRead(IR_SENSOR_PIN) == LOW); // LOW means object detected

  Serial.print("Front obj distance is: ");
  Serial.println(frontDist);


  // Setting up operating speeds depending on any object behind the clock
  if (irObstacle) {
    // object is detected 4 inch away from clock at the back
    // motor set to forward and at next highest speed
    operatingSpeed = fastSpeed;
  } 
  else {
    operatingSpeed = slowSpeed;
  }

  //MOVEMENT DECISON LOGIC decides whether to turn (turn left or right) or whether to move forward
  // CASE 1: if object there in the front and it's within 17.78 cm , i.e., 7 inches

  if (frontDist<=17.78 || frontDist>270){
    // If we JUST started turning on this loop, mark the start time
    if (turnStartTime == 0) {
      turnStartTime = millis(); 
    }

    // Check if we've been stuck turning in one direction for too long (> 1000ms)
    if (millis() - turnStartTime > MAX_TURN_TIME) {
      turningRight = !turningRight; // Swap direction preference (Right -> Left or Left -> Right)
      turnStartTime = millis();    // Reset timer for the new direction
    }

    // Perform the turn based on current preference
    if (turningRight) {
      Serial.println("Turning right");
      turnRight();
    } 
    else {
      Serial.println("Turning left");
      turnLeft();
    }

  } 
  // CASE B: Coast is clear ahead!
  else {
    turnStartTime = 0;   // Reset turn timer since path is open
    turningRight = true; // Reset default preference back to right
    moveForward();
  }

  delay(20); // Small 20ms pause for ultrasonic wave dissipation

}

// Function to make distance measurements
float readUltrasonicDistance() {
  //send the ping
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  //read for time taken by the signal
  long duration = pulseIn(ECHO_PIN, HIGH);
  //that time into speed of sound
  return (duration * 0.034) / 2.0;
}

// Function for basic forward movement in case of no significant event required (turn left, turn right or speed up)
// Use same function for speed up but set operatingSpeed == midSpeed or fastSpeed (after testing)
void moveForward() {
  analogWrite(LEFT_MOTOR_A, operatingSpeed*leftMotorTrim);
  analogWrite(LEFT_MOTOR_B, 0);
  analogWrite(RIGHT_MOTOR_A, 0);
  analogWrite(RIGHT_MOTOR_B, operatingSpeed*rightMotorTrim);
}

// Function to turn right
void turnRight() {
  analogWrite(LEFT_MOTOR_A, operatingSpeed);
  analogWrite(LEFT_MOTOR_B, 0);
  analogWrite(RIGHT_MOTOR_A, operatingSpeed);
  analogWrite(RIGHT_MOTOR_B, 0);
}

void turnLeft() {
  analogWrite(LEFT_MOTOR_A, 0);
  analogWrite(LEFT_MOTOR_B, operatingSpeed);
  analogWrite(RIGHT_MOTOR_A, 0);
  analogWrite(RIGHT_MOTOR_B, operatingSpeed);
}

// Stop the motors
void stopMotors() {
  analogWrite(LEFT_MOTOR_A, 0);
  analogWrite(LEFT_MOTOR_B, 0);
  analogWrite(RIGHT_MOTOR_A, 0);
  analogWrite(RIGHT_MOTOR_B, 0);
}
