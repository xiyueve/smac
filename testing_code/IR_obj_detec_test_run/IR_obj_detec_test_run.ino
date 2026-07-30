// CONNECTIONS

// ESP 32      IR OBJ detector
// 3V3         VCC
// GND         GND
// OUT         D19


// Define the IR sensor input pin
const int IR_SENSOR_PIN = 19;

void setup() {
  // Set up Serial Monitor at 9600 baud
  Serial.begin(115200);
  
  // Set GPIO 19 as an INPUT pin
  pinMode(IR_SENSOR_PIN, INPUT);
  
  Serial.println("IR Obstacle Sensor Test Initialized.");
}

void loop() {
  // Read the state of the OUT pin (HIGH or LOW)
  int sensorState = digitalRead(IR_SENSOR_PIN);

  // Remember: LOW means obstacle detected!
  if (sensorState == LOW) {
    Serial.println("⚠️ OBSTACLE DETECTED!");
  } else {
    Serial.println("CLEAR - No obstacle.");
  }

  // Small delay so the Serial Monitor isn't spammed too quickly
  delay(200);
}
