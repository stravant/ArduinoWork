
#include "stdint.h"
#include <iostream>

uint32_t pow_mod(uint32_t base, uint32_t exponent, uint32_t modulus) {
	int64_t result = 1;
	for (int place = 0; place < 32; ++place) {
		if ((exponent>>place) & 0x1) {
			//this place should be calculated
			int64_t factor = base;
			for (int i = 0; i < place; ++i)
				factor = (factor*factor) % modulus;
			//multiply the place to the result
			result = (result*factor) % modulus;
		}
	}
	return result;
}

int main() {
	while (true) {
		std::cout << "> ";
		int32_t base, ex, mod;
		std::cin >> base;
		std::cin >> ex;
		std::cin >> mod;
		std::cout << "\n";
		std::cout << "Res: " << pow_mod(base, ex, mod) << "\n";
	}
}