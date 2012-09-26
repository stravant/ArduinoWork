

class MersenneTwister {
public:
	MersenneTwister() {
		seed(0xDEADB08F);
	}
	//
	void seed(uint32_t seed) {
		MT[0] = seed;
		for (int i = 1; i < 624; ++i) {
			uint64_t v = MT[i-1];
			MT[i] = (0x6C078965*(v ^ (v>>30)) + i);
		}
	}
	//
	uint32_t nextUInt32() {
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

MersenneTwister random;

int main() {
	while (true) {
		std::cin.get();
		for (int i = 0; i < 10; ++i) {
			std::cout << random.nextUInt32() << "\n";
		}
	}
	return 0;
}