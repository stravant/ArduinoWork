
#include <iostream>
#include "stdint.h"

//add with modulus, with no overflow
uint32_t add_mod(uint32_t a, uint32_t b, uint32_t mod) {
	a = a%mod; 
	b = b%mod;
	if (a > 0xFFFFFFFFull-b) {
		return add_mod(0xFFFFFFFFull % mod, (a+b+1ull) % mod, mod);
	} else {
		return (a+b) % mod;
	}
}

//multiply a which is at most 2^(n-1) by 2^pow2, taking the mod
uint32_t mulpow2_mod(uint32_t a, uint8_t pow2, uint32_t mod) {
	for (uint8_t i = 0; i < pow2; ++i) {
		a = add_mod(a, a, mod);
	}
	return a;
}

uint32_t mul_mod(uint32_t a, uint32_t b, uint32_t mod) {
	uint32_t sum = 0;
	for (uint8_t i = 0; i < 32; ++i) {
		for (uint8_t j = 0; j < 32; ++j) {
			if (((a >> i) & 1) && ((b >> j) & 1)) {
				//a has a 1 in the i'th place
				//b has a 1 it the j'th place
				sum = add_mod(sum, mulpow2_mod(1<<i, j, mod), mod);
			} 
		}
	}
	return sum;
}

int main() {
	uint32_t p = 0xefffffffull;
	uint32_t q = 0xffffffffull;
	std::cout << (p%q) << "\n";
	while (true) {
		uint32_t a,b,m;
		std::cin >> std::hex >> a;
		std::cin >> b;
		std::cin >> m;
		uint32_t c = mul_mod(a, b, m);
		std::cout << " > " << c << "\n";
	}
}
