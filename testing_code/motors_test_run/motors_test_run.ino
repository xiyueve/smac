// Connections:
// ESP 32    Motor driver
// D25       A-1A
// D26       A-1B
// RX2       B-1A
// TX2       B-2A
// GND       GND
//           VCC to external battery
// GND              external battery ground

// Define Motor Control Pins
const int LEFT_MOTOR_A = 25; // A-1A
const int LEFT_MOTOR_B = 26; // A-1B
const int RIGHT_MOTOR_A = 16; // B-1A
const int RIGHT_MOTOR_B = 17; // B-2A
const int fullSpeed = 255; // full speed at which motors can operate

void setup() {
  // Set all motor control pins as outputs
  pinMode(LEFT_MOTOR_A, OUTPUT);
  pinMode(LEFT_MOTOR_B, OUTPUT);
  pinMode(RIGHT_MOTOR_A, OUTPUT);
  pinMode(RIGHT_MOTOR_B, OUTPUT);
  
  // Ensure everything starts turned off
  stopMotors();
}

void loop() {
  // 1. Move Forward at roughly 70% speed (180 out of 255)
  // (Lower speeds on USB power help prevent drawing too much current)
  // Move A forward
  analogWrite(LEFT_MOTOR_A, 180);
  analogWrite(RIGHT_MOTOR_A, 0);
  analogWrite(LEFT_MOTOR_B, 0);
  analogWrite(RIGHT_MOTOR_B, 0);
  
  delay(2000);
  
  // Move B forward
  analogWrite(LEFT_MOTOR_A, 0);
  analogWrite(RIGHT_MOTOR_A, 0);
  analogWrite(LEFT_MOTOR_B, 0);
  analogWrite(RIGHT_MOTOR_B, 180);

  delay(2000); 
  // 2. Move Backward
  //analogWrite(LEFT_MOTOR_A, 180);
  //analogWrite(LEFT_MOTOR_B, 0);
  //analogWrite(RIGHT_MOTOR_A, 0);
  //analogWrite(RIGHT_MOTOR_B, 180);
  //delay(2000);

  // 3. Stop
  stopMotors();
  delay(2000);
}

void stopMotors() {
  digitalWrite(LEFT_MOTOR_A, LOW);
  digitalWrite(LEFT_MOTOR_B, LOW);
  digitalWrite(RIGHT_MOTOR_A, LOW);
  digitalWrite(RIGHT_MOTOR_B, LOW);
}
