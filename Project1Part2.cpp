
//#include "stdint.h"

// int16_t analogRead(int p);
// class SerialH {
// public:
// 	int8_t read();
// 	void write(int8_t v);
// 	void print(const char* c);
// 	void println(const char* c);
// 	bool available();
// 	void begin(int);
// };
// SerialH Serial1;
// SerialH Serial;

// Debug var
bool Debugging = true;

///////////////////////////////////////////////////////////////////////////////
//
// Mersenne Twister random number generator implemented from Wikipedia
//
///////////////////////////////////////////////////////////////////////////////

class MersenneTwister {
public:
	MersenneTwister(): index(0) {
		seed(0xDEADB08F);
	}
	//
	void seed(uint32_t seed) {
		index = 0;
		MT[0] = seed;
		for (uint16_t i = 1; i < 624; ++i) {
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
		for (uint16_t i = 0; i < 624; ++i) {
			int32_t y = (MT[i]>>31) + (0x8FFFFFFF & MT[(i+1)%624]);
			MT[i] = MT[(i+397)%624] ^ (y>>1);
			if (y%2 != 0) {
				MT[i] ^= 0x9908B0DF;
			}
		}
	}

	uint32_t MT[624];
	uint32_t index;
};



///////////////////////////////////////////////////////////////////////////////
//
// Utility to read random noise off of serial ports.
//
///////////////////////////////////////////////////////////////////////////////

uint16_t analog_noise() {
	//won't overflow, 16 values * 1024 => 14 bits needed
	uint16_t total = 0;
	for (uint8_t i = 0; i < 16; ++i) {
		total += analogRead(i);
	}
	return total;
}



///////////////////////////////////////////////////////////////////////////////
//
// Utilities for dealing with large numerical operations
//
///////////////////////////////////////////////////////////////////////////////

uint32_t pow_mod(uint16_t base, uint32_t exponent, uint64_t modulus) {
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

// mod from Mark's quiz #2
int mod(int a, int b) {
	if (b < 0) {
		//if b &lt; 0 then we use the -a % -b = -c fact that we posed, and
		//reduce the problem to dealing with only a negative a:
		return -mod(-a, -b);
	} else if (b == 0) {
		//special case for our definition of a % 0 as 0
		return 0;
	} else {
		//for this case, we have b > 0, we need to do more work
		if (a >= 0) {
			//for a greater than 0, simply do the mod
			return a % b;
		} else {
			//for negative, use -mod + divisor, which gives us a negative result
			int c = -(-a % b);
			if (c == 0) {
				//handle 0 edge case. We don't want to add the divisor here, since 0
				//is "in" the positive space already. 
				return 0;
			} else {
				//take it into the negative result into the positive space by adding b
				return c + b;
			}
		}
	}
}



///////////////////////////////////////////////////////////////////////////////
//
// Utility for creating a 32 bit number from the first 8 bits of 4 numbers
//
///////////////////////////////////////////////////////////////////////////////

uint32_t to_uint32( uint8_t b1, uint8_t b2, uint8_t b3, uint8_t b4 ) {
	return ((((uint32_t) b1) << 24) | 
			(((uint32_t) b2) << 16) |
			(((uint32_t) b3) <<  8) |
			 ((uint32_t) b4)      );
}



///////////////////////////////////////////////////////////////////////////////
//
// Struct for keeping track of all message keys and handlers
//
///////////////////////////////////////////////////////////////////////////////

struct KeyAndHandler {
	// The the digit message prefix
	const char *Key;

	// The number of bytes the function expects
	uint8_t DataLen;

	// The handler function
	void (*Handler)( uint8_t * );
};

KeyAndHandler MessageHandlers[] = {
	{ "KEY", 12, &KeyHandler },
	{ "MSG", 32, &MsgHandler },
	{ "RSP",  4, &RspHandler },
};



///////////////////////////////////////////////////////////////////////////////
//
// Implementation of a ring buffer for receiving transmitted data
//
///////////////////////////////////////////////////////////////////////////////

class RingBuffer {
	public: 
		RingBuffer(): BufferLen(131), BufferPosition(0) {
			Buffer = (uint8_t*) malloc(BufferLen * sizeof(uint8_t));
			if ( Buffer == 0 ) Serial.println("Memory exception allocating ring buffer!");
		};

		// The length of the buffer
		const uint8_t BufferLen;

		// @debug check if the uint8_t conflicts with pointers
		uint8_t *peek( int8_t offset = 0 ) {
			return &(Buffer[mod(BufferPosition + offset, BufferLen)]);
		}

		// Adds a value to the next position in the ring buffer
		void push( uint8_t val ) {
			BufferPosition++;
			Buffer[mod(BufferPosition, BufferLen)] = val;
		}

	private: 
		int8_t BufferPosition;

		// Buffer contains the RingBuffer's data
		uint8_t *Buffer;
};



////////	///////////////////////////////////////////////////////////////////////
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
	EncryptState(): PrimeMod(0x7FFFFFFF), Generator(16807),
	                MyPublicKey(0), OtherPublicKey(0),
	                SecretKey(0), MyKey(0),
	                Status(NeedInit), 
	                MyMessageIndex(0), OtherMessageIndex(0) {

	}

	uint32_t PrimeMod;
	uint32_t Generator;
	
	//Diffie Helman key exchange info
	uint32_t MyPublicKey;
	uint32_t OtherPublicKey;
	uint32_t SecretKey; //shared secret key
	uint32_t MyKey; //my secret key
	
	//Pseudo-random number generator 
	MersenneTwister MyRandomGen;
	MersenneTwister OtherRandomGen; 
	
	//what is my status? Shows whether we still need to initialize a key
	//exchange or are ready to communicate.
	EncryptStatus Status;
	
	//message indicies, for future use.
	uint8_t MyMessageIndex;
	uint8_t OtherMessageIndex;

	// Encrypts the character with my random generator
	uint8_t encrypt( uint8_t ch ) {
		return ch ^ MyRandomGen.next_uint32();
	}

	// Encrypts the character with the others' random generator
	uint8_t decrypt( uint8_t ch ) {
		return ch ^ OtherRandomGen.next_uint32();
	}

	//sets us up for communications with the current private key that is set.
	void start_session() {
		//seed out both generators with the secret key
		MyRandomGen.seed(SecretKey);
		OtherRandomGen.seed(SecretKey);

		//and then set our status to ready
		Status = Ready;

		//and set the message index back to 0
		MyMessageIndex = 0;
		OtherMessageIndex = 0;
	}

	// Initiate a new secure session for this any any connected client.
	void set_session_key() {
		Serial.println("=========================\n|| Start Session");
		// get a seed as analog noise
		uint16_t noise = analog_noise();

		// Mersenne works best when the bits in the seed are close to random to
		// start with, so fill all 32 bits with something, rather than just 16
		uint32_t seed = noise | (((uint32_t) noise) << 16);

		// generate the key / public key for me
		MyRandomGen.seed(seed);
		MyKey = MyRandomGen.next_uint32();
		MyPublicKey = pow_mod(Generator, MyKey, PrimeMod);

		Serial.print("|| Sent Public key: ");
		Serial.println(MyPublicKey, HEX);
		Serial.print("|| My private key: ");
		Serial.println(MyKey, HEX);
		Serial.println("=========================");
	}
};
EncryptState Encrypt;



///////////////////////////////////////////////////////////////////////////////
//
// Communication class handles all serial i/o between the Arduino devices
//
///////////////////////////////////////////////////////////////////////////////

enum SerialState {
	SerialReady,

	ReceivingKey,
	ReceivingMessage
};
// Encryption handshakes need a timeout
class Communication {
	public:
		Communication(): CurrentReadState(SerialReady), ReceivedDataLen(0) {};

		// Manage the current state of serial send/receive
		SerialState CurrentReadState;

		// Key buffer contains the key that indicates the type of message that is being received
		//char KeyBuffer[3];

		// The maximum transmission size is 32 8 bit ints
		//uint8_t DataBuffer[35];
		RingBuffer DataBuffer;

		///////////////////////////////////////////////////////////////////////////////
		//
		// Data serialization code, to send keys and messages
		// Note, we send the generator and prime modulus too even though they are not
		// dynamically chosen currently.
		//
		///////////////////////////////////////////////////////////////////////////////

		void send_key() {
			Debugging && Serial.println("Sending key");
			// set us to waiting for key response
			Encrypt.Status = SentKey;

			Serial1.print("KEY");
			// send prime modulus
			send_32bit(Encrypt.PrimeMod);

			// send generator
			send_32bit(Encrypt.Generator);

			// send public key
			send_32bit(Encrypt.MyPublicKey);

			// The termination character
			Serial1.print('\0');
		}

		void send_key_response() {
			Debugging && Serial.println("Sending key response");
			Serial1.print("RSP");

			// Output my public key
			send_32bit(Encrypt.MyPublicKey);

			Serial1.print('\0');
		}

		void send_character(char c) {
			Debugging && Serial.println("Sending Char");
			Serial1.print("MSG");
			Serial1.write(c);
			Serial1.print('\0');
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
			Serial.println("===========================");
			Serial.print("|| Other's Key: ");
			Serial.println(Encrypt.OtherPublicKey, HEX);
			Serial.println("===========================");
			// generate and send our own response secret key
			// generate
			int32_t val = analog_noise();
			int32_t seed = val | (((uint32_t)val)<<16);
			Encrypt.MyRandomGen.seed(seed);
			Encrypt.MyKey = Encrypt.MyRandomGen.next_uint32();

			// calculate my public key, and the shared secret, since
			// we do have the other's info to work with at this point.
			Encrypt.MyPublicKey = pow_mod(Encrypt.Generator, Encrypt.MyKey, Encrypt.PrimeMod);
			Encrypt.SecretKey = pow_mod(Encrypt.OtherPublicKey, Encrypt.MyKey, Encrypt.PrimeMod);

			// send. This will send my public key
			send_key_response();

			// and we already have the secret key, so start the session
			Encrypt.start_session();
		}

		void rec_key_response() {
			Debugging && Serial.println("Receiving Key Response");
			// find out the shared secret key
			Encrypt.SecretKey = pow_mod(Encrypt.OtherPublicKey, Encrypt.MyKey, Encrypt.PrimeMod);

			// and start the session
			Encrypt.start_session();
		}

		// Reads data from the serial port and places it into the key buffer and data buffer
		void process_incomming_messages() {
			// Check if there is data in the serial buffer
			while (Serial1.available()) {
				// Add the new data from the serial monitor to the ring buffer
				DataBuffer.push(Serial1.read());
				if ( CurrentReadState == ReceivingKey || CurrentReadState == SerialReady ) {
					// If we are receiving a key, the first 3 receieved characters form the key
					CurrentReadState = ReceivingKey;

					CurrentKey[0] = *(DataBuffer.peek( -2 ));
					CurrentKey[1] = *(DataBuffer.peek( -1 ));
					CurrentKey[2] = *(DataBuffer.peek(  0 ));

					Serial.println("=====");
					Serial.println(CurrentKey);

					// If these characters form a valid key, we can move on to processing the message.
					// sizeof messagehandlers may not work
					for ( uint8_t i = 0; i < 3; i++ ) {
						// Debugging && Serial.println(MessageHandlers[i].Key);
						// Debugging && Serial.println(strcmp(MessageHandlers[i].Key, CurrentKey));
						if ( !strcmp(MessageHandlers[i].Key, CurrentKey) ) {
							Serial.print("Key Matched:");
							Serial.println(CurrentKey);
							// This is the last char in a key,
							// If this message is not KEY and we haven't setup encryption, 
							// we need to tell the other to reinit
							if ( Encrypt.Status != Ready && strcmp(CurrentKey, "KEY") ) {
								// We are receiving something other than a KEY, but encryption has not been initialized
								Serial.print("Resetting encryption");
								Encrypt.set_session_key();
								send_key();
								CurrentReadState = SerialReady;
								return;
							}

							// Mark that we have the key
							CurrentReadState = ReceivingMessage;
							CurrentMessageHandler = MessageHandlers[i];

							// Reset received data length
							ReceivedDataLen = 0;
							return;
						}
					}
				} else if ( CurrentReadState == ReceivingMessage ) {
					// A message (general data) is being received
					ReceivedDataLen++;

					// Check if the message should be done, apply sanity check and call the handler
					if ( ReceivedDataLen > CurrentMessageHandler.DataLen && *(DataBuffer.peek()) == '\0' ) {
						// Technically sane!
						uint8_t *data = (uint8_t*) malloc(CurrentMessageHandler.DataLen * sizeof(uint8_t));
						if ( data == 0 ) Serial.println("Error allocating data array");

						for ( uint8_t i = 0; i <= CurrentMessageHandler.DataLen; i++ )
							data[i] = *DataBuffer.peek( i - CurrentMessageHandler.DataLen );

						CurrentMessageHandler.Handler( data );
						CurrentReadState = SerialReady;
					} else if ( ReceivedDataLen > CurrentMessageHandler.DataLen ) {
						// The data we received is bad because it is not terminated properly -- drop the message
						Serial.println("Dumping buffer data");
						CurrentReadState = SerialReady;

					}
				} else {
					CurrentReadState = SerialReady;
				}
			}
		}
	private: 
		char CurrentKey[3];
		uint8_t ReceivedDataLen;
		KeyAndHandler CurrentMessageHandler;

		void send_32bit(uint32_t num) {
			Serial1.write((num>>24) & 0xFF);
			Serial1.write((num>>16) & 0xFF);
			Serial1.write((num>>8 ) & 0xFF);
			Serial1.write((num    ) & 0xFF);
		}
};
Communication Comms;



///////////////////////////////////////////////////////////////////////////////
//
// Message Handlers are passed data after the Communication class has finished 
// receiving the data and checking validity.
//
///////////////////////////////////////////////////////////////////////////////

// Sets up the EncryptStatus class with all the numbers we need
void KeyHandler( uint8_t data[12] ) {
	Debugging && Serial.println("KeyHandler Called");

	// Give Encrypt the base data that it needs
	Encrypt.PrimeMod = to_uint32(data[0], data[1], data[2], data[3]);
	Encrypt.Generator = to_uint32(data[4], data[5], data[6], data[7]);
	Encrypt.OtherPublicKey = to_uint32(data[8], data[9], data[10], data[11]);
	Encrypt.Status = SentKey;

	// Let Encrypt handle the rest of the key setup
	Comms.rec_key();
}

// Decrypts all characters and prints them in the users' serial monitor
void MsgHandler( uint8_t data[32] ) {
	Debugging && Serial.println("MsgHandler Called");

	for ( uint8_t i = 0; i < 32; i++ ) {
		// Null character symbolizes end of useful data
		if ( data[i] == '\0' )
			break;

		Serial.write(Encrypt.decrypt(data[i]));
	}
}

// RSP message is receieved after we send a KEY message. It will contain the other
// devices' public key
void RspHandler( uint8_t data[4] ) {
	Debugging && Serial.println("RspHandler Called");

	Encrypt.OtherPublicKey = to_uint32(data[0], data[1], data[2], data[3]);

	// find out the shared secret key
	Encrypt.SecretKey = pow_mod(Encrypt.OtherPublicKey, Encrypt.MyKey, Encrypt.PrimeMod);

	// and start the session
	Encrypt.start_session();
}



///////////////////////////////////////////////////////////////////////////////
//
// The main output functions that the program setup calls on. These functions
// build the data for the send_* functions to serialize.
//
///////////////////////////////////////////////////////////////////////////////

// Encrypt and send a character
void output_character(uint8_t ch) {
	// encrypt the character with my random generator
	ch ^= (Encrypt.MyRandomGen.next_uint32() & 0xFF);

	// send the character
	Comms.send_character(ch);
}



///////////////////////////////////////////////////////////////////////////////
//
// The main setup and loop
//
///////////////////////////////////////////////////////////////////////////////
RingBuffer InBuffer;
void setup() {
	// open the serial communications that I need
	Serial.begin(9600);
	Serial1.begin(9600);
}

void loop() {
	// let the incomming message processing do its work.
	Comms.process_incomming_messages();

	// check for user input
	if (Serial.available()) {
		// we got user input, see if we're initialized
		if (Encrypt.Status == NeedInit) {
			// we still need to do init, fire off the init
			Encrypt.set_session_key();
			Comms.send_key();

		} else if (Encrypt.Status == Ready) {
			// we're ready, send the input
			output_character(*InBuffer.peek());

		} else if (Encrypt.Status == Failed) {
			// we failed, let the user know
			Serial.println("Failed to send.");

			// and clear out the buffer so we don't spam failure messages
			while (Serial.available()) Serial.read();
		} else if (Encrypt.Status == SentKey) {
			// nothing to do, we're waiting for the other's public key to get-
			// to us, keep the input in the buffer for now.
		} else {
			Serial.println("Assertation failure.");
			while (Serial.available()) Serial.read();
		}
	}
}