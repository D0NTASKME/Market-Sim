#include "config.hpp"

#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace {
std::vector<double> read_list(std::istringstream& in) {
    std::vector<double> v; double x;
    while (in >> x) v.push_back(x);
    return v;
}
void write_list(std::ostream& out, const char* key, const std::vector<double>& v) {
    if (v.empty()) return;
    out << key;
    for (double x : v) out << ' ' << x;
    out << '\n';
}
}  // namespace

SimConfig SimConfig::load(const std::string& path) {
    std::ifstream f(path);
    if (!f) throw std::runtime_error("cannot open config " + path);
    return parse(f, path);
}

SimConfig SimConfig::parse(std::istream& f, const std::string& path) {
    SimConfig c;
    std::string line;
    int lineno = 0;
    while (std::getline(f, line)) {
        ++lineno;
        if (auto h = line.find('#'); h != std::string::npos) line.erase(h);
        std::istringstream in(line);
        std::string k;
        if (!(in >> k)) continue;
        if      (k == "seed") in >> c.seed;
        else if (k == "duration") in >> c.duration;
        else if (k == "fundamental_start") in >> c.fundamental_start;
        else if (k == "fundamental_vol") in >> c.fundamental_vol;
        else if (k == "noise_model") in >> c.noise_model;
        else if (k == "noise_count") in >> c.noise_count;
        else if (k == "noise_rate") in >> c.noise_rate;
        else if (k == "noise_price_spread") in >> c.noise_price_spread;
        else if (k == "noise_max_qty") in >> c.noise_max_qty;
        else if (k == "noise_lifetime") in >> c.noise_lifetime;
        else if (k == "passive_rate") in >> c.passive_rate;
        else if (k == "aggressive_rate") in >> c.aggressive_rate;
        else if (k == "size_quantiles") c.size_quantiles = read_list(in);
        else if (k == "trade_size_quantiles") c.trade_size_quantiles = read_list(in);
        else if (k == "lifetime_quantiles") c.lifetime_quantiles = read_list(in);
        else if (k == "lifetime_touch_quantiles") c.lifetime_touch_quantiles = read_list(in);
        else if (k == "lifetime_near_quantiles") c.lifetime_near_quantiles = read_list(in);
        else if (k == "lifetime_deep_quantiles") c.lifetime_deep_quantiles = read_list(in);
        else if (k == "placement_probs") c.placement_probs = read_list(in);
        else if (k == "reactive_share") in >> c.reactive_share;
        else if (k == "reactive_count") in >> c.reactive_count;
        else if (k == "reactive_wake_rate") in >> c.reactive_wake_rate;
        else if (k == "reactive_quote_life") in >> c.reactive_quote_life;
        else if (k == "maker") in >> c.maker;
        else if (k == "maker_rate") in >> c.maker_rate;
        else if (k == "maker_half_spread") in >> c.maker_half_spread;
        else if (k == "maker_size") in >> c.maker_size;
        else if (k == "maker_max_inventory") in >> c.maker_max_inventory;
        else if (k == "maker_skew") in >> c.maker_skew;
        else if (k == "as_gamma") in >> c.as_gamma;
        else if (k == "as_k") in >> c.as_k;
        else if (k == "informed_model") in >> c.informed_model;
        else if (k == "informed_rate") in >> c.informed_rate;
        else if (k == "informed_threshold") in >> c.informed_threshold;
        else if (k == "informed_size") in >> c.informed_size;
        else if (k == "informed_cap") in >> c.informed_cap;
        // An unknown key is almost always a typo that would otherwise be
        // silently ignored, leaving a default in place without anyone noticing.
        else throw std::runtime_error(path + ":" + std::to_string(lineno) + ": unknown key '" + k + "'");
        if (in.fail() && !in.eof()) throw std::runtime_error(path + ":" + std::to_string(lineno) + ": bad value for '" + k + "'");
    }
    return c;
}

void SimConfig::save(const std::string& path) const {
    std::ofstream o(path);
    o << std::setprecision(10);
    o << "seed " << seed << "\nduration " << duration
      << "\nfundamental_start " << fundamental_start << "\nfundamental_vol " << fundamental_vol
      << "\nnoise_model " << noise_model << "\nnoise_count " << noise_count
      << "\nnoise_rate " << noise_rate << "\nnoise_price_spread " << noise_price_spread
      << "\nnoise_max_qty " << noise_max_qty << "\nnoise_lifetime " << noise_lifetime
      << "\npassive_rate " << passive_rate << "\naggressive_rate " << aggressive_rate << '\n';
    write_list(o, "size_quantiles", size_quantiles);
    write_list(o, "trade_size_quantiles", trade_size_quantiles);
    write_list(o, "lifetime_quantiles", lifetime_quantiles);
    write_list(o, "lifetime_touch_quantiles", lifetime_touch_quantiles);
    write_list(o, "lifetime_near_quantiles", lifetime_near_quantiles);
    write_list(o, "lifetime_deep_quantiles", lifetime_deep_quantiles);
    write_list(o, "placement_probs", placement_probs);
    o << "reactive_share " << reactive_share << "\nreactive_count " << reactive_count
      << "\nreactive_wake_rate " << reactive_wake_rate
      << "\nreactive_quote_life " << reactive_quote_life << '\n';
    o << "maker " << maker << "\nmaker_rate " << maker_rate << "\nmaker_half_spread " << maker_half_spread
      << "\nmaker_size " << maker_size << "\nmaker_max_inventory " << maker_max_inventory
      << "\nmaker_skew " << maker_skew << "\nas_gamma " << as_gamma << "\nas_k " << as_k
      << "\ninformed_model " << informed_model << "\ninformed_rate " << informed_rate
      << "\ninformed_threshold " << informed_threshold << "\ninformed_size " << informed_size
      << "\ninformed_cap " << informed_cap << '\n';
}
