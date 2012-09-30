///////////////////////////////////////////////////////////////////////////////
//
// CMPUT 296 -- Assignment 1
//
// Written by:
// 	Mark Langen
// 	Michael Blouin
//
///////////////////////////////////////////////////////////////////////////////


///////////////////////////////////////////////////////////////////////////////
//
// Utility to read random noise off of serial ports.
//
///////////////////////////////////////////////////////////////////////////////

uint16_t analog_noise() {
	//won't overflow, 16 values * 1024 => 14 bits needed
	uint16_t total = 0;
	for (int i = 0; i < 16; ++i)
		total += analogRead(i);

	return total;
}

///////////////////////////////////////////////////////////////////////////////
//
// Utility to calculate the power of large numbers, mod a modulus.
//
///////////////////////////////////////////////////////////////////////////////

uint32_t pow_mod(uint16_t base, uint32_t exponent, uint32_t modulus) {
	//
	// Idea: 
	// Outer loop: b^e %c => Product[i: 0->32]( b^(Bit[e,i] * 2^i) %c ) %c
	// For the bits which have Bit[e,i] = 1 :
	//   Inner loop: b^(2^i) => b^2 iteratively for i times 
	// 
	uint32_t shift;
	uint32_t result = 1;
	for (uint8_t place = 0; shift = exponent>>place; ++place) {
		// if we have a 1 bit, we need to calculate b^(2^place) and multiply
		// it to the product
		if (shift & 0x1) {
			uint64_t factor = base;
			// square base iteratively to get the factor
			for (uint8_t i = 0; i < place; ++i)
				factor = (factor*factor) % modulus;
			// multiply the place to the result
			result = (result*factor) % modulus;
		}
	}
	return result;
}

///////////////////////////////////////////////////////////////////////////////
//
// Utilities to read fixed amounts of data off of the Serial port
//
///////////////////////////////////////////////////////////////////////////////

/* Read a long off of the serial port and return. Does not distinguish between failing to parse an int and reading 0 */
int32_t readlong(uint8_t len) {
  char s[len+1];
  readline(s, len);
  s[len] = '\0';
  return atol(s);
}

/* Read a line (up to maxlen characters) off of the serial port. */
void readline(char *s, int maxlen) {
  uint8_t i = 0;

  while(1) {
    while (Serial.available() == 0);

    s[i] = Serial.read();
    if (s[i] == '\0' || s[i] == '\n' || i == maxlen) break;
    i++;
  }
}

///////////////////////////////////////////////////////////////////////////////
//
// Encrypt state class handles all respects of creating/computing encryption keys.
//
///////////////////////////////////////////////////////////////////////////////
enum EncryptStatus {
	NeedInit,
	Ready,
	Failed,
};
class EncryptState {
public:
	EncryptState(): PrimeMod(19211), Generator(6),
					InitialSeed(0xDEADB08F), MyPublicKey(0), 
					OtherPublicKey(0), SecretKey(0), MyKey(0),
	                Status(NeedInit), MaxKeySize(3) {}

	// The prime to use as a modulus
	uint32_t PrimeMod;

	// The Diffie-Hellman generator for the prime
	uint32_t Generator;

	// An initial seed to use on for random number generation
	uint32_t InitialSeed;

	// The max length (in chars) for keys. Change if the key variable types change
	uint8_t MaxKeySize;
	
	// Diffie Hellman key exchange info
	// Note: The below keys may be changed as low as 8 bits unsigned or as high as 
	// 		 32 bits unsigned and everything will work fine. Although you may need to change the MaxKeySize
	uint8_t MyPublicKey;
	uint8_t OtherPublicKey;
	uint8_t SecretKey; //shared secret key
	uint8_t MyKey; //my secret key

	//what is my status? Shows whether we still need to initialize a key
	//exchange or are ready to communicate.
	EncryptStatus Status;

	// Encrypts or decrypts the character
	uint8_t encrypt_decrypt(uint8_t ch) {
		return ch ^ SecretKey;
	}

	// Generates a random private key and then sets us up for communication
	void start_session() {
		// Seed the random number generator with some noise and an initial seed
		randomSeed(InitialSeed ^ (analog_noise()<<16 | analog_noise()));

		// Calculate the private key
		MyKey = random();

		// Calculate the public key to share
		MyPublicKey = pow_mod(Generator, MyKey, PrimeMod);

		// Show the user our shared index
		Serial.println("===========================");
		Serial.print("|| My Key: ");
		Serial.println(MyPublicKey);
		Serial.println("===========================");

		// Ask the user for the other's public key
		Serial.println("Please enter the other public key (number > 0) to continue...");

		// Wait for the key
		while ( !Serial.available() ) {};

		// We have data in the serial monitor
		if ( OtherPublicKey = readlong(MaxKeySize) ) {
			// Compute the shared secret
			SecretKey = pow_mod(OtherPublicKey, MyKey, PrimeMod);

			//and then set our status to ready
			Status = Ready;

			// Tell the user that we're a go
			Serial.println("===========================");
			Serial.println("|| Encryption configuration successful");
			Serial.print("|| My key: ");
			Serial.println(MyPublicKey);
			Serial.print("|| Other's Key: ");
			Serial.println(OtherPublicKey);
			Serial.println("===========================");
		} else {
			// Something funny happened with the serial.
			// The user probably entered chars
			Serial.println("Assertation failure.");

			Status = Failed;

			// Clear the serial buffer
			Serial.println("Dumping bad serial data");
			while (Serial.available()) Serial.read();

			// Reset encryption
			Serial.println("Resetting encryption");
			start_session();
		}
	}
};
EncryptState Encrypt;

///////////////////////////////////////////////////////////////////////////////
//
// The main setup and loop
//
///////////////////////////////////////////////////////////////////////////////

void setup() {
	// open the serial communications
	Serial.begin(9600);
	Serial1.begin(9600);

	// Start the encryption setup process
	Encrypt.start_session();
}

void loop() {
	// check for user input
	if (Serial.available()) {
		// we got user input, see if we're initialized
		if (Encrypt.Status == Ready) {
			// Read the char from the Serial Monitors' input, encrypt it and send it
			// to the other device via Serial1
			Serial1.write(Encrypt.encrypt_decrypt(Serial.read()));
       
		} else if (Encrypt.Status == Failed) {
			// we failed, let the user know
			Serial.println("Failed to send message because encryption failed.");

			// Tell  the user
			Serial.println("Attempting to restart encryption session...");

			// Restart the encryption session
			Encrypt.start_session();

		} else {
			Serial.println("Assertation failure.");
			while (Serial.available()) Serial.read();
		}
	}

	// Check for data on the other serial port
	if (Serial1.available()) {
		if (Encrypt.Status == Ready) {
			// Read the char, encrypt it and output it onto the Serial Monitor
			Serial.write(Encrypt.encrypt_decrypt(Serial1.read()));
       
		} else if (Encrypt.Status == Failed) {
			// we failed, let the user know
			Serial.println("Failed to receive message because encryption failed.");

			// Dump the message
			Serial.println("Dropping message...");
			while ( Serial1.available() ) Serial1.read();

			// Tell  the user
			Serial.println("Attempting to restart encryption session...");

			// Restart the encryption session
			Encrypt.start_session();

		} else {
			Serial1.println("Assertation failure.");
			while (Serial1.available()) Serial1.read();
		}
	}
}