void setup() {
  Serial.begin(9600);
  while (!Serial);  // ✅ Wait for Serial Monitor to open (very important for Leonardo)
  
  Serial1.begin(9600);  // SIM900 on pins 0/1
  Serial.println("SIM900 test ready! Type AT commands:");
}

void loop() {
  if (Serial.available())
    Serial1.write(Serial.read());
  if (Serial1.available())
    Serial.write(Serial1.read());
}
