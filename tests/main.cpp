#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>

#include "TestFramework.h"

namespace mfxtest {
std::vector<TestCase>& registry() { static std::vector<TestCase> r; return r; }
int gChecks = 0;
static std::vector<std::string> gRecords;
static std::string gCurrent;
void record(const std::string& key, double value, const std::string& unit) {
    std::ostringstream os;
    os << "    {\"test\": \"" << gCurrent << "\", \"key\": \"" << key << "\", \"value\": " << (std::isfinite(value) ? value : -1e300)
       << ", \"unit\": \"" << unit << "\"}";
    gRecords.push_back(os.str());
}
} // namespace mfxtest

int main(int argc, char** argv) {
    using namespace mfxtest;
    std::string group, filter, jsonOut;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--group") && i + 1 < argc) group = argv[++i];
        else if (!std::strcmp(argv[i], "--filter") && i + 1 < argc) filter = argv[++i];
        else if (!std::strcmp(argv[i], "--json") && i + 1 < argc) jsonOut = argv[++i];
    }
    int pass = 0, fail = 0;
    std::vector<std::string> results;
    for (auto& t : registry()) {
        if (!group.empty() && t.group != group) continue;
        const std::string full = t.group + "." + t.name;
        if (!filter.empty() && full.find(filter) == std::string::npos) continue;
        gCurrent = full;
        const auto t0 = std::chrono::steady_clock::now();
        std::string status = "PASS", msg;
        try { t.fn(); }
        catch (const Failure& f) { status = "FAIL"; msg = f.msg; }
        catch (const std::exception& e) { status = "FAIL"; msg = std::string("exception: ") + e.what(); }
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        (status == "PASS" ? pass : fail)++;
        std::printf("[%s] %-55s %8.1f ms %s\n", status.c_str(), full.c_str(), ms, msg.c_str());
        std::ostringstream os;
        std::string esc;
        for (char c : msg) { if (c == '"' || c == '\\') esc += '\\'; esc += c; }
        os << "    {\"test\": \"" << full << "\", \"status\": \"" << status << "\", \"ms\": " << ms << ", \"message\": \"" << esc << "\"}";
        results.push_back(os.str());
    }
    std::printf("\n%d passed, %d failed, %d checks\n", pass, fail, gChecks);
    if (!jsonOut.empty()) {
        std::ofstream f(jsonOut);
        f << "{\n  \"passed\": " << pass << ",\n  \"failed\": " << fail << ",\n  \"checks\": " << gChecks << ",\n  \"tests\": [\n";
        for (size_t i = 0; i < results.size(); ++i) f << results[i] << (i + 1 < results.size() ? ",\n" : "\n");
        f << "  ],\n  \"measurements\": [\n";
        for (size_t i = 0; i < gRecords.size(); ++i) f << gRecords[i] << (i + 1 < gRecords.size() ? ",\n" : "\n");
        f << "  ]\n}\n";
    }
    return fail == 0 ? 0 : 1;
}
