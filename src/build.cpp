#include "build.hpp"
#include "agents/calibrated.hpp"
#include "agents/informed_trader.hpp"
#include "agents/market_maker.hpp"
#include "agents/noise_trader.hpp"
#include "agents/reactive.hpp"

#include <stdexcept>

std::unique_ptr<Market> build_market(const SimConfig& c) {
    auto m = std::make_unique<Market>(c.seed, c.fundamental_start, c.fundamental_vol);

    if (c.noise_model == "legacy") {
        for (int i = 1; i <= c.noise_count; ++i)
            m->add_agent(std::make_unique<NoiseTrader>(static_cast<uint64_t>(i), c.noise_rate,
                c.noise_price_spread, c.noise_max_qty, c.noise_lifetime));
    } else if (c.noise_model == "calibrated") {

        // Reactive providers take over a share of the orders placed at the
        // touch. Total order flow is unchanged: whatever they post is removed
        // from the ordinary traders, so the calibrated arrival rate still holds.
        std::vector<double> probs = c.placement_probs;
        double passive = c.passive_rate, reactive_rate = 0.0;
        if (c.reactive_share > 0.0 && probs.size() > 1) {
            const double p_touch = probs[0] + probs[1];
            reactive_rate = passive * p_touch * c.reactive_share;
            passive -= reactive_rate;
            probs[0] *= (1.0 - c.reactive_share);
            probs[1] *= (1.0 - c.reactive_share);
            double sum = 0.0;
            for (double x : probs) sum += x;
            for (double& x : probs) x /= sum;
        }

        auto pick = [&](const std::vector<double>& specific) {
            return Quantiles(specific.empty() ? c.lifetime_quantiles : specific);
        };
        for (int i = 1; i <= c.noise_count; ++i)
            m->add_agent(std::make_unique<CalibratedTrader>(static_cast<uint64_t>(i),
                (passive + c.aggressive_rate) / c.noise_count,
                (passive + c.aggressive_rate) > 0 ? c.aggressive_rate / (passive + c.aggressive_rate) : 0.0,
                Quantiles(c.size_quantiles),
                Quantiles(c.trade_size_quantiles), pick(c.lifetime_touch_quantiles),
                pick(c.lifetime_near_quantiles), pick(c.lifetime_deep_quantiles), probs));
        for (int i = 0; i < c.reactive_count && reactive_rate > 0.0; ++i)
            m->add_agent(std::make_unique<ReactiveTrader>(static_cast<uint64_t>(300 + i),
                c.reactive_wake_rate, reactive_rate / c.reactive_count, c.reactive_quote_life,
                Quantiles(c.size_quantiles)));
    } else {
        throw std::runtime_error("unknown noise_model " + c.noise_model);
    }

    if (c.maker == "naive")
        m->add_agent(std::make_unique<MarketMaker>(MAKER_ID, c.maker_rate, c.maker_half_spread,
                                                   c.maker_size, c.maker_max_inventory));
    else if (c.maker == "skewed")
        m->add_agent(std::make_unique<SkewedMarketMaker>(MAKER_ID, c.maker_rate, c.maker_half_spread,
                                                         c.maker_size, c.maker_max_inventory, c.maker_skew));
    else if (c.maker == "as")
        m->add_agent(std::make_unique<AvellanedaStoikovMaker>(MAKER_ID, c.maker_rate, c.maker_size,
            c.maker_max_inventory, c.as_gamma, c.fundamental_vol, c.as_k, c.duration));
    else if (c.maker != "none")
        throw std::runtime_error("unknown maker " + c.maker);
    if (c.maker != "none") { m->set_maker(MAKER_ID); m->track_agent(MAKER_ID); }

    if (c.informed_rate > 0.0) {
        if (c.informed_model == "legacy")
            m->add_agent(std::make_unique<InformedTrader>(INFORMED_ID, c.informed_rate,
                c.informed_threshold, c.informed_size, c.informed_cap));
        else if (c.informed_model == "calibrated")
            m->add_agent(std::make_unique<CalibratedInformed>(INFORMED_ID, c.informed_rate,
                c.informed_threshold, Quantiles(c.trade_size_quantiles), c.informed_cap));
        else
            throw std::runtime_error("unknown informed_model " + c.informed_model);
        m->set_informed(INFORMED_ID);
    }
    return m;
}
