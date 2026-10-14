#pragma once

#include <string_view>
#include <algorithm>
#include <array>

using namespace std;

class HttpInspector {
public:
    // Simple blocklist for malicious signatures (e.g., SQLi patterns or bad User-Agents)
    static constexpr array<string_view, 3> maliciousPatterns = {
        "UNION SELECT",
        "1=1",
        "BadBot/1.0"
    };

    static bool isRequestSafe(string_view rawRequest) {
        for (const auto& pattern : maliciousPatterns) {
            if (rawRequest.find(pattern) != string_view::npos) {
                return false; // Malicious pattern detected!
            }
        }
        return true;
    }
};