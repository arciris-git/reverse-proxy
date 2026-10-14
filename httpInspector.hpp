#pragma once

#include <string_view>
#include <algorithm>
#include <array>

using namespace std;

class HttpInspector {
public:
    // Blocklist covering standard and URL-encoded SQLi patterns & bad bots
    static constexpr array<string_view, 5> maliciousPatterns = {
        "UNION SELECT",
        "UNION%20SELECT",
        "UNION+SELECT",
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