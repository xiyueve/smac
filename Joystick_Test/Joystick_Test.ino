#define JOY_X   32
#define JOY_Y   34  // try this instead of 33
#define JOY_BTN 15
//make sure to wire it up where vcc is 3.3v 

void setup() {
  Serial.begin(115200);
  pinMode(JOY_BTN, INPUT_PULLUP);
}

void loop() {
  int x = analogRead(JOY_X);
  int y = analogRead(JOY_Y);
  bool btn = !digitalRead(JOY_BTN);

  Serial.print("X: "); Serial.print(x);
  Serial.print(" Y: "); Serial.print(y);
  Serial.print(" BTN: "); Serial.println(btn);

  delay(200);
}