// SerialRandomDemo — sends a random value to the Serial Monitor every 500 ms.
// Open the Serial Monitor at 115200 baud to see the output.

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    // Portenta uses native USB: wait until the PC has opened the port,
    // otherwise the first messages are lost.
  }
  randomSeed(analogRead(A0)); // without a seed, random() repeats the same sequence every boot
}

void loop() {
  int value = random(0, 100); // 0 to 99

  Serial.print("random value = ");
  Serial.println(value);

  delay(500);
}
