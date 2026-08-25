#pragma once

#include <cstdlib>
#include <iostream>

inline void require(bool cond, const char* msg) {
    if (!cond) {
        std::cerr << "FAIL: " << msg << "\n";
        std::exit(1);
    }
}
