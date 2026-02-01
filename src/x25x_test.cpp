// Simple X25X algorithm test
// Compile: g++ -o x25x_test x25x_test.cpp -I. -I../src -std=c++20

#include <iostream>
#include <cstring>
#include <cstdint>
#include <iomanip>

// Simple test using the raw hash functions
extern "C" {
#include "crypto/sphlib/x11.h"
}

void print_hash(const char* name, const unsigned char* hash, size_t len) {
    std::cout << name << ": ";
    for (size_t i = 0; i < len; i++) {
        std::cout << std::hex << std::setfill('0') << std::setw(2) << (int)hash[i];
    }
    std::cout << std::dec << std::endl;
}

int main() {
    std::cout << "=== X25X Algorithm Test ===" << std::endl << std::endl;

    const char* test_data = "WATTx X25X Multi-Algorithm Mining Test";
    size_t data_len = strlen(test_data);

    std::cout << "Input: \"" << test_data << "\"" << std::endl;
    std::cout << "Length: " << data_len << " bytes" << std::endl << std::endl;

    // Test X11
    unsigned char x11_hash[32];
    x11_hash(test_data, data_len, x11_hash);
    print_hash("X11", x11_hash, 32);

    // Test consistency - hash same input again
    unsigned char x11_hash2[32];
    x11_hash(test_data, data_len, x11_hash2);

    bool x11_consistent = (memcmp(x11_hash, x11_hash2, 32) == 0);
    std::cout << "X11 Consistency: " << (x11_consistent ? "PASS" : "FAIL") << std::endl;

    // Test that output is not all zeros
    bool x11_nonzero = false;
    for (int i = 0; i < 32; i++) {
        if (x11_hash[i] != 0) {
            x11_nonzero = true;
            break;
        }
    }
    std::cout << "X11 Non-zero: " << (x11_nonzero ? "PASS" : "FAIL") << std::endl;

    // Test different input produces different output
    const char* test_data2 = "Different input data";
    unsigned char x11_hash3[32];
    x11_hash(test_data2, strlen(test_data2), x11_hash3);

    bool x11_different = (memcmp(x11_hash, x11_hash3, 32) != 0);
    std::cout << "X11 Different outputs: " << (x11_different ? "PASS" : "FAIL") << std::endl;

    std::cout << std::endl;
    std::cout << "=== All X11 Tests: " <<
        ((x11_consistent && x11_nonzero && x11_different) ? "PASSED" : "FAILED")
        << " ===" << std::endl;

    return (x11_consistent && x11_nonzero && x11_different) ? 0 : 1;
}
