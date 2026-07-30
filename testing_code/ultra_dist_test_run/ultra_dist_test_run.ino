// Connections
// ESP 32         Ultra Dist Sensor
// VIN            Vcc         (VIN of ESP 32 is it's 5V pin, power source for other components)
// D5             Trig
// D18            Echo pin   Connected to through a voltage divider, actually connected to 1Kohm resistor, other end of resistor connected to D18, and D18 connected to ground through 2Kohm resistor)
// GND            GND

// Define Sensor Pins
const int TRIG_PIN = 5;
const int ECHO_PIN = 18;

// Variables for calculation
long duration;
float distanceCm;

void setup() {
  Serial.begin(115200); // Initialize serial communication
  
  pinMode(TRIG_PIN, OUTPUT); // Sets the TrigPin as an Output
  pinMode(ECHO_PIN, INPUT);  // Sets the EchoPin as an Input
}

void loop() {
  // Clear the trigger pin
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  
  // Sets the trigger pin HIGH for 10 micro seconds
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  
  // Reads the echo pin, returns the sound wave travel time in microseconds
  duration = pulseIn(ECHO_PIN, HIGH);
  
  // Calculate the distance (Speed of sound is ~340m/s or 0.034 cm/us)
  distanceCm = duration * 0.034 / 2;
  
  // Print the distance to the Serial Monitor
  Serial.print("Distance: ");
  Serial.print(distanceCm);
  Serial.println(" cm");
  
  delay(500); // Wait half a second before measuring again
}

