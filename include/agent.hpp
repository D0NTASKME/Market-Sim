#pragma once

#include <cstdint>
#include <random>

class Market;   // only need the name, not the definition

class Agent {
public:
    explicit Agent(uint64_t id) : id_(id) {}
    virtual ~Agent() = default;

    virtual void act(Market& market) = 0;
    virtual double next_delay(std::mt19937& rng) = 0;

    uint64_t id() const { return id_; }

protected:
    uint64_t id_;
};
