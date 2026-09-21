// Exposes the simulator to JavaScript through Embind.
//
// Nothing here re-implements the model: it builds the same agents with the
// same parameters as main.cpp and hands the page a handle to step and read.

#include "market.hpp"
#include "agents/informed_trader.hpp"
#include "agents/market_maker.hpp"
#include "agents/noise_trader.hpp"

#include <emscripten/bind.h>
#include <emscripten/val.h>

#include <memory>
#include <random>
#include <string>

using emscripten::val;

// The slider changes the informed trader's rate while the market runs. A
// Poisson process can't simply be rescheduled mid-wait, so this wakes at a
// fixed maximum rate and acts with probability rate / max_rate. That is
// Poisson thinning: statistically identical to arrivals at `rate`, and the
// original InformedTrader is left untouched for the experiments.
class TunableInformedTrader : public InformedTrader {
public:
    TunableInformedTrader(uint64_t id, double max_rate, double threshold,
                          uint32_t qty, int64_t max_position, double rate)
        : InformedTrader(id, max_rate, threshold, qty, max_position),
          max_rate_(max_rate), rate_(rate) {}

    void set_rate(double r) { rate_ = r; }

    void act(Market& market) override {
        std::uniform_real_distribution<double> u(0.0, 1.0);
        if (u(market.rng()) < rate_ / max_rate_) InformedTrader::act(market);
    }

private:
    double max_rate_;
    double rate_;
};

class WebSim {
public:
    static constexpr uint64_t MAKER = 100;
    static constexpr uint64_t INFORMED = 200;

    WebSim(int seed, const std::string& maker, double informed_rate)
        : market_(static_cast<uint32_t>(seed), 10100.0, 2.0), rate_(informed_rate) {
        for (uint64_t i = 1; i <= 10; ++i)
            market_.add_agent(std::make_unique<NoiseTrader>(i, 2.0, 5, 100));

        if (maker == "naive")
            market_.add_agent(std::make_unique<MarketMaker>(MAKER, 20.0, 1, 50, 2000));
        else if (maker == "as")
            market_.add_agent(std::make_unique<AvellanedaStoikovMaker>(
                MAKER, 20.0, 50, 2000, 5e-6, 2.0, 1.0, 300.0));
        else
            market_.add_agent(std::make_unique<SkewedMarketMaker>(MAKER, 20.0, 1, 50, 2000, 0.005));
        market_.set_maker(MAKER);
        market_.track_agent(MAKER);

        auto inf = std::make_unique<TunableInformedTrader>(INFORMED, 20.0, 3.0, 200, 5000, rate_);
        informed_ = inf.get();
        market_.add_agent(std::move(inf));
        market_.set_informed(INFORMED);
    }

    void run_to(double t) {
        if (t <= t_) return;
        rate_time_ += rate_ * (t - t_);   // for the episode's average rate
        market_.run_until(t);
        t_ = t;
    }

    void set_informed_rate(double r) { rate_ = r; informed_->set_rate(r); }

    double time() const          { return t_; }
    double average_rate() const  { return t_ > 0 ? rate_time_ / t_ : rate_; }
    double mid()                 { return market_.mid_price(); }
    double fair() const          { return market_.fundamental(); }
    double edge_noise() const    { return market_.maker_flow_vs_noise(); }
    double edge_informed() const { return market_.maker_flow_vs_informed(); }
    double inventory() const     { return static_cast<double>(market_.position(MAKER).inventory); }
    double pnl() const {
        const Position& p = market_.position(MAKER);
        return p.cash + p.inventory * market_.fundamental();
    }
    double best_bid() const { auto b = market_.best_bid(); return b ? static_cast<double>(*b) : NAN; }
    double best_ask() const { auto a = market_.best_ask(); return a ? static_cast<double>(*a) : NAN; }

    // Returns a JS array of {price, qty, mine}.
    val depth(bool bids, int n) const {
        val out = val::array();
        for (const DepthLevel& l : market_.depth(bids ? Side::Buy : Side::Sell, static_cast<size_t>(n))) {
            val o = val::object();
            o.set("price", static_cast<double>(l.price));
            o.set("qty", static_cast<double>(l.qty));
            o.set("mine", static_cast<double>(l.mine));
            out.call<void>("push", o);
        }
        return out;
    }

private:
    Market market_;
    TunableInformedTrader* informed_ = nullptr;   // owned by market_
    double rate_;
    double t_ = 0.0;
    double rate_time_ = 0.0;
};

EMSCRIPTEN_BINDINGS(market_sim) {
    emscripten::class_<WebSim>("WebSim")
        .constructor<int, const std::string&, double>()
        .function("runTo", &WebSim::run_to)
        .function("setInformedRate", &WebSim::set_informed_rate)
        .function("time", &WebSim::time)
        .function("averageRate", &WebSim::average_rate)
        .function("mid", &WebSim::mid)
        .function("fair", &WebSim::fair)
        .function("edgeNoise", &WebSim::edge_noise)
        .function("edgeInformed", &WebSim::edge_informed)
        .function("inventory", &WebSim::inventory)
        .function("pnl", &WebSim::pnl)
        .function("bestBid", &WebSim::best_bid)
        .function("bestAsk", &WebSim::best_ask)
        .function("depth", &WebSim::depth);
}
