#include "legacy_engine_preflight.h"
#include <cstdlib>
#include <iostream>
#include <vector>

void require(bool condition, const char* message) {
    if (!condition) { std::cerr << message << '\n'; std::exit(1); }
}

int main() {
    require(!w3vr::legacy_engine_layout_matches({}), "Empty image accepted");
    std::vector<uint8_t> image(w3vr::legacy_head_callback_rva +
        w3vr::legacy_head_callback_entry.size(), 0);
    require(!w3vr::legacy_engine_layout_matches(image), "Unknown image accepted");
    std::copy(w3vr::legacy_head_callback_entry.begin(),
        w3vr::legacy_head_callback_entry.end(),
        image.begin() + w3vr::legacy_head_callback_rva);
    require(!w3vr::legacy_engine_layout_matches(image), "One callback accepted");
    std::copy(w3vr::legacy_matrix_callback_entry.begin(),
        w3vr::legacy_matrix_callback_entry.end(),
        image.begin() + w3vr::legacy_matrix_callback_rva);
    require(w3vr::legacy_engine_layout_matches(image), "Legacy layout rejected");
    image[w3vr::legacy_matrix_callback_rva + 3] ^= 1;
    require(!w3vr::legacy_engine_layout_matches(image), "Changed matrix callback accepted");
    image[w3vr::legacy_matrix_callback_rva + 3] ^= 1;
    image[w3vr::legacy_head_callback_rva + 1] ^= 1;
    require(!w3vr::legacy_engine_layout_matches(image), "Changed head callback accepted");
    image[w3vr::legacy_head_callback_rva + 1] ^= 1;
    image.pop_back();
    require(!w3vr::legacy_engine_layout_matches(image), "Truncated image accepted");
    std::cout << "Legacy preflight rejects changed and truncated layouts.\n";
}
