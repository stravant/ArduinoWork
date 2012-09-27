
#include <iostream>

///////////////////////////////////////////////////////////////////////////////
//
// Mersenne Twister random number generator implementation
//
///////////////////////////////////////////////////////////////////////////////

class MersenneTwister {
public:
	MersenneTwister() {
		seed(0xDEADB08F);
	}
	//
	void seed(uint32_t seed) {
		index = 0;
		MT[0] = seed;
		for (int i = 1; i < 624; ++i) {
			uint64_t v = MT[i-1];
			MT[i] = (0x6C078965*(v ^ (v>>30)) + i);
		}
	}
	//
	uint32_t next_uint32() {
		if (index == 0)
			update();
		//
		uint64_t y = MT[index];
		y ^= (y>>11);
		y ^= ((y<<11) & 0x9D2C5680);
		y ^= ((y<<15) & 0xEFC60000);
		y ^= (y>>18);
		//
		index = (index+1)%624;
		return y;
	}
	
private:
	void update() {
		for (int i = 0; i < 624; ++i) {
			int32_t y = (MT[i]>>31) + (0x8FFFFFFF & MT[(i+1)%624]);
			MT[i] = MT[(i+397)%624] ^ (y>>1);
			if (y%2 != 0) {
				MT[i] ^= 0x9908B0DF;
			}
		}
	}

	uint32_t MT[624];
	uint32_t index = 0;
};

MersenneTwister Random;



///////////////////////////////////////////////////////////////////////////////
//
// Utility to read random noise off of serial ports.
//
///////////////////////////////////////////////////////////////////////////////

uint16_t analog_noise() {
	//won't overflow, 16 values * 1024 => 14 bits needed
	uint16_t total = 0;
	for (int i = 0; i < 16; ++i) {
		total += analogRead(i);
	}
	return total;
}



///////////////////////////////////////////////////////////////////////////////
//
// Utility to calculate the power of large numbers, mod a modulus.
//
///////////////////////////////////////////////////////////////////////////////

uint32_t pow_mod(uint32_t base, uint32_t exponent, uint32_t modulus) {
	uint64_t result = 1;

	//this will underflow when exponent=0, but using the post-increment it will 
	//correctly halt before that becomes a problem.
	while (exponent--) {
		result = (result*base) % modulus;
	}
}



///////////////////////////////////////////////////////////////////////////////
//
// Main state tracking for the encrypted communications
//
///////////////////////////////////////////////////////////////////////////////

enum EncryptStatus {
	NeedInit,
	SentKey,
	Ready,
	Failed,
};
class EncryptState {
public:
	static const uint32_t PrimeMod = 19211;
	static const uint32_t Generator = 6;
	
	//Diffie Helman key exchange info
	static uint32_t MyPublicKey = 0;
	static uint32_t OtherPublicKey = 0;
	static uint32_t SecretKey = 0; //shared secret key
	static uint32_t MyKey = 0; //my secret key
	
	//Pseudo-random number generator 
	static MersenneTwister MyRandomGen;
	static MersenneTwister OtherRandomGen; 
	
	//what is my status? Shows whether we still need to initialize a key
	//exchange or are ready to communicate.
	static EncryptStatus Status = NeedInit;
	
	//message indicies, for future use.
	static uint8_t MyMessageIndex = 0;
	static uint8_t OtherMessageIndex = 0;

	//sets us up for communications with the current private key that is set.
	static void start_session() {
		//seed out both generators with the secret key
		MyRandomGen.seed(SecretKey);
		OtherRandomGen.seed(SecretKey);

		//and then set our status to ready
		Status = Ready;

		//and set the message index back to 0
		MyMessageIndex = 0;
		OtherMessageIndex = 0;
	}
};



///////////////////////////////////////////////////////////////////////////////
//
// Data serialization code, to send keys and messages
// Note, we send the generator and prime modulus too even though they are not
// dynamically chosen currently.
//
///////////////////////////////////////////////////////////////////////////////

void send_key() {
	Serial1.print("KEY");
	// send prime modulus
	Serial1.write((EncryptState::PrimeMod>>24) & 0xFF);
	Serial1.write((EncryptState::PrimeMod>>16) & 0xFF);
	Serial1.write((EncryptState::PrimeMod>>8 ) & 0xFF);
	Serial1.write((EncryptState::PrimeMod    ) & 0xFF);
	// send generator
	Serial1.write((EncryptState::Generator>>24) & 0xFF);
	Serial1.write((EncryptState::Generator>>16) & 0xFF);
	Serial1.write((EncryptState::Generator>>8 ) & 0xFF);
	Serial1.write((EncryptState::Generator    ) & 0xFF);
	// send public key
	Serial1.write((EncryptState::MyPublicKey>>24) & 0xFF);
	Serial1.write((EncryptState::MyPublicKey>>16) & 0xFF);
	Serial1.write((EncryptState::MyPublicKey>>8 ) & 0xFF);
	Serial1.write((EncryptState::MyPublicKey    ) & 0xFF);
	Serial1.print(";");
}

void send_key_response() {
	Serial1.print("RSP");
	Serial1.write((EncryptState::MyPublicKey>>24) & 0xFF);
	Serial1.write((EncryptState::MyPublicKey>>16) & 0xFF);
	Serial1.write((EncryptState::MyPublicKey>>8 ) & 0xFF);
	Serial1.write((EncryptState::MyPublicKey    ) & 0xFF);
	Serial1.print(";");
}

void send_character(char c) {
	Serial1.print("MSG");
	Serial1.write(c);
	Serial1.print(";");
}



///////////////////////////////////////////////////////////////////////////////
//
// Data deserialization code
// Contains a main function |process_incomming_messages| that incrementally
// gathers serial input, deserializes it, and dispatches it to the rec_* 
// functions, which corrospond to the send_* seriazilation functions.
// The rec_* functions then take actual actions on the program state.
//
///////////////////////////////////////////////////////////////////////////////

void rec_key() {
	// generate and send our own response secret key
	// generate
	int32_t val = analog_noise();
	int32_t seed = val | (((uint32_t)val)<<16);
	Random.seed(seed);
	EncryptStatus::MyKey = Random.next_uint32();

	// calculate my public key, and the shared secret, since
	// we do have the other's info to work with at this point.
	EncryptState::MyPublicKey = 
		pow_mod(EncryptState::Generator, EncryptState::MyKey, EncryptState::PrimeMod);
	EncryptState::SecretKey = 
		pow_mod(EncryptState::OtherPublicKey, EncryptState::MyKey, EncryptState::PrimeMod)

	// send. This will send my public key
	send_key_response()

	// and we already have the secret key, so start the session
	EncryptState::start_session();
}

void rec_key_response() {
	// find out the shared secret key
	EncryptState::SecretKey =
		pow_mod(EncryptState::OtherPublicKey, EncryptState::MyKey, EncryptState::PrimeMod);

	// and start the session
	EncryptState::start_session();
}

void rec_character(uint8_t ch) {
	// decrypt the character using the partner's random generator
	ch ^= (EncryptState::OtherRandomGen.next_uint32() & 0xFF);

	// and put it to our ouput
	Serial.write(ch);
}


//
// More complex logic. We have to wait for serial input and detect when we've
// gotten a valid message... but we can't block waiting for data to become
// available, so we use a state machine to track what we're expecting as the
// next byte that we recieve.
// Also we can't just use 1 character codes or the noise could be confused
// with messages too easily.
//
enum SerialState {
	Ready,
	WaitKey_E,
	WaitKey_Y,
	WaitMsg_S,
	WaitMsg_G,
	WaitRsp_S,
	WaitRsp_P
}
SerialState CurrentReadState = Ready;
uint8_t rec_byte_blocking() {
	// simple function to wait for and return a byte from the serial1
	while (!Serial1.available());
	return Serial1.read() & 0xFF;
}
uint32_t rec_int32_blocking() {
	// simple function to deserialize and return a 32bit integer
	return (((int32_t)rec_byte_blocking()) << 24) |
	       (((int32_t)rec_byte_blocking()) << 16) |
	       (((int32_t)rec_byte_blocking()) << 8 ) |
	       (((int32_t)rec_byte_blocking())      );
}
void process_incomming_messages() {
	while (Serial1.available()) {
		uint8_t val = Serial1.read() & 0xFF;
		if (val == 'K' && CurrentReadState == Ready) {
			CurrentReadState = WaitKey_E
		} else if (val == 'E' && CurrentReadState == WaitKey_E) {
			CurrentReadState = WaitKey_Y
		} else if (val == 'Y' && CurrentReadState == WaitKey_Y) {
			CurrentReadState = Ready;

			// Do the main KEY message decoding.
			// we do do this all in one single chunk, since it's probably
			// a valid message if we've gotten this far, or at least all
			// of the bytes of the message are here or on their way even
			// if they got corrupted.
			EncryptState::PrimeMod = rec_int32_blocking();
			EncryptState::Generator = rec_int32_blocking();
			EncryptState::OtherPublicKey = rec_int32_blocking();
			EncryptState::MessageIndex = 0;
			EncryptState::Status = SentKey;
			//
			if (rec_byte_blocking() != ';') {
				//failed message integrity check
				EncryptState::Status = Failed;
			} else {
				rec_key();
			}
			


		} else if (val == 'R' && CurrentReadState == Ready) {
			CurrentReadState = WaitRsp_S;
		} else if (val == 'S' && CurrentReadState == WaitRsp_S) {
			CurrentReadState = WaitRsp_P;
		} else if (val == 'P' && CurrentReadState == WaitRsp_P) {
			CurrentReadState = Ready;

			// Do the main RSP message decoding.
			// Get the other's public key
			EncryptState::OtherPublicKey = rec_int32_blocking();
			//
			if (rec_byte_blocking() != ';') {
				//failed message integrity check
				EncryptState::Status = Failed;
			} else {
				rec_key_response();
			}


		} else if (val == 'M' && CurrentReadState == Ready) {
			CurrentReadState = WaitMsg_S;
		} else if (val == 'S' && CurrentReadState == WaitMsg_S) {
			CurrentReadState = WaitMsg_G;
		} else if (val == 'G' && CurrentReadState == WaitMsg_G) {
			CurrentReadState = Ready;

			// Do the main MSG message decoding.
			// get the data character
			int8_t ch = rec_byte_blocking();
			//
			if (rec_byte_blocking() != ';') {
				// failed message integrity check
				EncryptState::Status = Failed;
			} else {
				rec_character(ch);
			}

		} else {
			CurrentReadState = Ready;
		}
	}
}



///////////////////////////////////////////////////////////////////////////////
//
// The main output functions that the program setup calls on. These functions
// build the data for the send_* functions to serialize.
//
///////////////////////////////////////////////////////////////////////////////

// Initiate a new secure session for this any any connected client.
void set_session_key() {
	// get a seed as analog noise
	uint16_t noise = analog_noise();

	// Mersenne works best when the bits in the seed are close to random to
	// start with, so fill all 32 bits with something, rather than just 16
	uint32_t seed = noise | (((uint32_t)noise) << 16);

	// generate the key / public key for me
	Random.seed(seed);
	EncryptState::MyKey = Random.next_uint32();
	EncryptState::MyPublicKey =
		pow_mod(EncryptState::Generator, EncryptState::MyKey, EncryptState::PrimeMod);

	// set us to waiting for key response
	EncryptState::Status = 

	// and send them off
	send_key();
}

// Encrypt and send a character
void output_character(uint8_t ch) {
	// encrypt the character with my random generator
	ch ^= (EncryptState::MyRandomGen)

	// send the character
	send_character(c);
}



///////////////////////////////////////////////////////////////////////////////
//
// The main setup and loop
//
///////////////////////////////////////////////////////////////////////////////

void setup() {
	// open the serial communications that I need
	Serial.begin(9600);
	Serial1.begin(9600);
}

void loop() {
	// let the incomming message processing do its work.
	process_incomming_messages()

	// check for user input
	if (Serial.available()) {
		// we got user input, see if we're initialized
		if (EncryptState::Status == NeedInit) {
			// we still need to do init, fire off the init
			set_session_key();

		} else if (EncryptState::Status == Ready) {
			// we're ready, send the input
			output_character(Serial.read());

		} else if (EncryptState::Status == Failed) {
			// we failed, let the user know
			Serial.println("Failed to send.")

			// and clear out the buffer so we don't spam failure messages
			while (Serial.available()) 
				Serial.read();
		} else if (EncryptState::Status == SentKey) {
			// nothing to do, we're waiting for the other's public key to get-
			// to us, keep the input in the buffer for now.
		}
	}
}