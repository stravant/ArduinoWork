
/*
OUTPUT INPUT HIGH LOW
digitalWrite digitalRead, analogRead, analogWrite, pinMode, delay, delayMicroseconds
*/

int periodAMicros = 2500; // 400 Hz
int periodBMicros = 200;   // 20000 Hz

bool speakerState = false;

void interruptHandler() {
    speakerState = !speakerState;
}

void setup() {
    pinMode(0, OUTPUT);
    pinMode(1, INPUT);
    attachInterrupt(1, interruptHandler, FALLING);
}

void loop() {
    int voltage1 = analogRead(0);
    int voltage2 = analogRead(1);
    if (speakerState) {
        int period = map(voltage1+voltage2, 0, 2046, periodAMicros, periodBMicros);
        int halfPeriod = period/2;
        for (int i = 0; i < 100000/period; ++i) {
            delayMicroseconds(halfPeriod);
            digitalWrite(0, HIGH);
            delayMicroseconds(halfPeriod);
            digitalWrite(0, LOW);
        }
    }
}

