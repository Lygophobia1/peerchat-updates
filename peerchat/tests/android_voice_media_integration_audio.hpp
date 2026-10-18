#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace voice_integration_audio {
struct Snapshot {
    std::uint64_t id=0,starts=0,captures=0,pulls=0,supplied=0,energy=0;
    double tone330=0,tone660=0;
    std::string owner;
    bool active=false,muted=false;
};
class OwnerScope {
    std::string previous_;
public:
    explicit OwnerScope(const std::string& owner);
    ~OwnerScope();
};
void step();
std::vector<Snapshot> snapshots();
}
