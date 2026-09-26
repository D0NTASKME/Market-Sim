#pragma once
#include "config.hpp"
#include "market.hpp"
#include <memory>

// Agent ids used throughout: noise traders 1..N, maker 100, informed 200.
constexpr uint64_t MAKER_ID = 100;
constexpr uint64_t INFORMED_ID = 200;

std::unique_ptr<Market> build_market(const SimConfig& c);
