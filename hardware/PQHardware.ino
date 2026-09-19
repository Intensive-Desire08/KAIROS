/*
  Dual Photodiode True Random Number Generator (TRNG)

  - Uses two independent analog entropy sources (photodiodes)
  - Amplified using LM358 op-amp
  - Extracts least significant bit (LSB) from ADC readings
  - Applies Von Neumann whitening to remove bias
  - Converts the bits into bytes
  - Outputs unbiased random bytes over Serial
*/

int greenLED = 4;
int redLED = 5;
const int PIN1 = 34;
const int PIN2 = 35;
unsigned long lastLEDTime = 0;
const int ledDelay = 100; // 0.1 sec

int getBit(){
  // Von Neumann extractor:
  // Ensures unbiased randomness by discarding equal pairs (00, 11)
  while(true){
    int b1 = analogRead(PIN1) & 1;  // LSB from channel 1

    delayMicroseconds(300);  // Small delay to reduce temporal correlation

    int b2 = analogRead(PIN2) & 1;  // LSB from channel 2

    if (b1 == 0 && b2 == 1) return 0;
    if (b1 == 1 && b2 == 0) return 1;

    // Ignore 00 and 11 (biased or correlated cases)
  }
}

int getByte(){
  // Converts the bit values to bytes
  // For ease of access
  int value = 0;

  for(int i = 0; i < 8; i++){
    value = (value << 1) | getBit();  // build byte
  }

  return value;
}

void setup() {
  Serial.begin(115200);

  pinMode(greenLED, OUTPUT);
  pinMode(redLED, OUTPUT);
}

void loop(){

  // Raw analog readings (useful for debugging / analysis)
  //int val1 = analogRead(PIN1);
  //int val2 = analogRead(PIN2);

  // Extract final unbiased random bit
  int finalByte = getByte();

  // Output random bit stream
  Serial.write(0xAA);       // sync marker
  Serial.write(finalByte);

  //int b1 = analogRead(PIN1) & 1;
  //int b2 = analogRead(PIN2) & 1;

  // LED visualization
  if(millis() - lastLEDTime >= ledDelay){ // Time for updation
    lastLEDTime = millis();

    int bit = getBit();  // independent entropy sample

    if(bit == 1){
      digitalWrite(greenLED, HIGH);
      digitalWrite(redLED, LOW);
    } else {
      digitalWrite(greenLED, LOW);
      digitalWrite(redLED, HIGH);
    }
  }

  //Serial.println(b1);
  //Serial.println(val1);
  //Serial.println(finalByte);
}
