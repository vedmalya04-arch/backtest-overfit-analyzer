#pragma once

#include "Statistics.hpp"
#include <string>
#include <vector>
#include <random>
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <cmath>

namespace quant {

struct PriceBar {
    std::string date;
    double open = 0.0;
    double high = 0.0;
    double low = 0.0;
    double close = 0.0;
    double volume = 0.0;
};

struct TrialResult {
    size_t trial_index = 0;
    std::string rule_name;
    double annualized_return = 0.0;
    double annualized_volatility = 0.0;
    double sharpe_ratio = 0.0;
    std::vector<double> daily_returns;
};

class StrategyEngine {
public:
    // Load historical daily price series from CSV
    static std::vector<PriceBar> load_csv_prices(const std::string& filepath) {
        std::vector<PriceBar> prices;
        std::ifstream file(filepath);
        if (!file.is_open()) {
            return prices;
        }

        std::string line;
        bool header_skipped = false;
        while (std::getline(file, line)) {
            if (line.empty()) continue;
            if (!header_skipped) {
                // Check if line contains header words
                if (line.find("Date") != std::string::npos || line.find("date") != std::string::npos) {
                    header_skipped = true;
                    continue;
                }
                header_skipped = true;
            }

            std::stringstream ss(line);
            std::string date_str, open_str, high_str, low_str, close_str, vol_str;

            if (std::getline(ss, date_str, ',') &&
                std::getline(ss, open_str, ',') &&
                std::getline(ss, high_str, ',') &&
                std::getline(ss, low_str, ',') &&
                std::getline(ss, close_str, ',') &&
                std::getline(ss, vol_str, ',')) {
                
                try {
                    PriceBar bar;
                    bar.date = date_str;
                    bar.open = std::stod(open_str);
                    bar.high = std::stod(high_str);
                    bar.low = std::stod(low_str);
                    bar.close = std::stod(close_str);
                    bar.volume = std::stod(vol_str);
                    prices.push_back(bar);
                } catch (...) {
                    // Skip malformed rows
                }
            }
        }
        return prices;
    }

    // Compute simple discrete asset daily returns
    static std::vector<double> compute_asset_returns(const std::vector<PriceBar>& prices) {
        std::vector<double> ret;
        if (prices.size() < 2) return ret;
        ret.reserve(prices.size() - 1);
        for (size_t i = 1; i < prices.size(); ++i) {
            double prev = prices[i - 1].close;
            double curr = prices[i].close;
            if (prev > 0.0) {
                ret.push_back((curr - prev) / prev);
            } else {
                ret.push_back(0.0);
            }
        }
        return ret;
    }

    // Technical Rule 1: Moving Average Crossover (Fast MA / Slow MA)
    static std::vector<double> backtest_ma_crossover(const std::vector<PriceBar>& prices, size_t fast_period, size_t slow_period) {
        if (prices.size() <= slow_period || fast_period >= slow_period) {
            return {};
        }

        std::vector<double> strategy_returns;
        strategy_returns.reserve(prices.size() - slow_period);

        // Precompute moving averages
        std::vector<double> fast_ma(prices.size(), 0.0);
        std::vector<double> slow_ma(prices.size(), 0.0);

        double sum_fast = 0.0;
        for (size_t i = 0; i < prices.size(); ++i) {
            sum_fast += prices[i].close;
            if (i >= fast_period) sum_fast -= prices[i - fast_period].close;
            if (i >= fast_period - 1) fast_ma[i] = sum_fast / static_cast<double>(fast_period);
        }

        double sum_slow = 0.0;
        for (size_t i = 0; i < prices.size(); ++i) {
            sum_slow += prices[i].close;
            if (i >= slow_period) sum_slow -= prices[i - slow_period].close;
            if (i >= slow_period - 1) slow_ma[i] = sum_slow / static_cast<double>(slow_period);
        }

        int position = 0; // -1, 0, +1
        for (size_t i = slow_period; i < prices.size(); ++i) {
            double prev_close = prices[i - 1].close;
            double curr_close = prices[i].close;
            double asset_ret = (prev_close > 0.0) ? (curr_close - prev_close) / prev_close : 0.0;

            // Strategy return gained with position held from previous bar
            strategy_returns.push_back(static_cast<double>(position) * asset_ret);

            // Update signal for next bar
            if (fast_ma[i] > slow_ma[i]) {
                position = 1;
            } else if (fast_ma[i] < slow_ma[i]) {
                position = -1;
            }
        }

        return strategy_returns;
    }

    // Technical Rule 2: Momentum / Rate of Change
    static std::vector<double> backtest_momentum(const std::vector<PriceBar>& prices, size_t lookback, double threshold) {
        if (prices.size() <= lookback) return {};

        std::vector<double> strategy_returns;
        strategy_returns.reserve(prices.size() - lookback);

        int position = 0;
        for (size_t i = lookback; i < prices.size(); ++i) {
            double prev_close = prices[i - 1].close;
            double curr_close = prices[i].close;
            double asset_ret = (prev_close > 0.0) ? (curr_close - prev_close) / prev_close : 0.0;

            strategy_returns.push_back(static_cast<double>(position) * asset_ret);

            double past_close = prices[i - lookback].close;
            double roc = (past_close > 0.0) ? (curr_close - past_close) / past_close : 0.0;

            if (roc > threshold) {
                position = 1;
            } else if (roc < -threshold) {
                position = -1;
            } else {
                position = 0;
            }
        }
        return strategy_returns;
    }

    // Technical Rule 3: Mean Reversion / Relative Range Oscillator
    static std::vector<double> backtest_mean_reversion(const std::vector<PriceBar>& prices, size_t lookback, double z_thresh) {
        if (prices.size() <= lookback) return {};

        std::vector<double> strategy_returns;
        strategy_returns.reserve(prices.size() - lookback);

        int position = 0;
        for (size_t i = lookback; i < prices.size(); ++i) {
            double prev_close = prices[i - 1].close;
            double curr_close = prices[i].close;
            double asset_ret = (prev_close > 0.0) ? (curr_close - prev_close) / prev_close : 0.0;

            strategy_returns.push_back(static_cast<double>(position) * asset_ret);

            // Compute local mean & std dev
            double sum = 0.0;
            for (size_t j = i - lookback; j < i; ++j) {
                sum += prices[j].close;
            }
            double mean = sum / static_cast<double>(lookback);

            double sum_sq = 0.0;
            for (size_t j = i - lookback; j < i; ++j) {
                double diff = prices[j].close - mean;
                sum_sq += diff * diff;
            }
            double std_dev = std::sqrt(sum_sq / static_cast<double>(lookback));

            if (std_dev > 1e-6) {
                double z = (curr_close - mean) / std_dev;
                if (z < -z_thresh) {
                    position = 1;  // Oversold -> Buy
                } else if (z > z_thresh) {
                    position = -1; // Overbought -> Short
                } else {
                    position = 0;
                }
            }
        }
        return strategy_returns;
    }

    // Run trial parameter grid search over historical prices
    static std::vector<TrialResult> generate_technical_trials(const std::vector<PriceBar>& prices, size_t target_trials) {
        std::vector<TrialResult> results;
        results.reserve(target_trials);

        if (prices.size() < 30) return results;

        std::mt19937_64 rng(42);
        std::uniform_int_distribution<size_t> fast_dist(2, 25);
        std::uniform_int_distribution<size_t> slow_dist(26, 120);
        std::uniform_int_distribution<size_t> mom_lookback_dist(3, 60);
        std::uniform_real_distribution<double> mom_thresh_dist(0.005, 0.05);
        std::uniform_int_distribution<size_t> mr_lookback_dist(5, 50);
        std::uniform_real_distribution<double> mr_z_dist(0.8, 2.5);
        std::uniform_int_distribution<int> rule_type_dist(0, 2);

        for (size_t i = 0; i < target_trials; ++i) {
            TrialResult trial;
            trial.trial_index = i + 1;
            int r_type = rule_type_dist(rng);

            std::vector<double> ret;
            if (r_type == 0) {
                size_t f = fast_dist(rng);
                size_t s = slow_dist(rng);
                trial.rule_name = "SMA_Cross(" + std::to_string(f) + "," + std::to_string(s) + ")";
                ret = backtest_ma_crossover(prices, f, s);
            } else if (r_type == 1) {
                size_t lb = mom_lookback_dist(rng);
                double th = mom_thresh_dist(rng);
                trial.rule_name = "Momentum(" + std::to_string(lb) + "," + std::to_string(th).substr(0, 5) + ")";
                ret = backtest_momentum(prices, lb, th);
            } else {
                size_t lb = mr_lookback_dist(rng);
                double zt = mr_z_dist(rng);
                trial.rule_name = "MeanRevert(" + std::to_string(lb) + "," + std::to_string(zt).substr(0, 4) + ")";
                ret = backtest_mean_reversion(prices, lb, zt);
            }

            if (!ret.empty()) {
                ReturnMoments m = Statistics::compute_moments(ret);
                trial.annualized_return = m.annualized_return;
                trial.annualized_volatility = m.annualized_volatility;
                trial.sharpe_ratio = m.annualized_sharpe;
                trial.daily_returns = std::move(ret);
            } else {
                trial.annualized_return = 0.0;
                trial.annualized_volatility = 0.15;
                trial.sharpe_ratio = 0.0;
            }

            results.push_back(std::move(trial));
        }

        return results;
    }

    // High-performance Monte Carlo synthetic trials generator
    // Simulates N empirical trial return paths of length T under null hypothesis (H0: zero alpha)
    // with exact Cornish-Fisher adjustments for user skewness and excess kurtosis.
    static std::vector<TrialResult> generate_synthetic_trials(size_t total_trials,
                                                             size_t track_record_days,
                                                             double reported_sharpe,
                                                             double skewness = 0.0,
                                                             double kurtosis = 3.0,
                                                             unsigned int seed = 0) {
        (void)reported_sharpe;
        std::vector<TrialResult> trials;
        trials.reserve(total_trials);

        // Dynamic PRNG seed via hardware entropy if 0, else deterministic
        std::mt19937_64 rng(seed != 0 ? seed : std::random_device{}());
        std::normal_distribution<double> std_norm(0.0, 1.0);

        // Daily benchmark market volatility (16% annualized, typical equity baseline)
        const double daily_vol = 0.16 / std::sqrt(252.0);

        // Cornish-Fisher expansion coefficients for non-normal return moments
        double s = skewness;
        double excess_k = std::max(-1.5, std::min(kurtosis - 3.0, 15.0));

        size_t T = (track_record_days >= 2) ? track_record_days : 252;
        std::vector<double> path(T);

        for (size_t i = 0; i < total_trials; ++i) {
            TrialResult t;
            t.trial_index = i + 1;
            t.rule_name = "Permutation_" + std::to_string(i + 1);

            double sum_r = 0.0;
            for (size_t d = 0; d < T; ++d) {
                double z = std_norm(rng);
                double z2 = z * z;
                double z3 = z2 * z;
                // Cornish-Fisher expansion under H0 (mean = 0)
                double w = z + (s / 6.0) * (z2 - 1.0)
                             + (excess_k / 24.0) * (z3 - 3.0 * z)
                             - (s * s / 36.0) * (2.0 * z3 - 5.0 * z);
                double r = daily_vol * w;
                path[d] = r;
                sum_r += r;
            }

            double mean_r = sum_r / static_cast<double>(T);
            double sum_sq = 0.0;
            for (size_t d = 0; d < T; ++d) {
                double diff = path[d] - mean_r;
                sum_sq += diff * diff;
            }
            double var_r = sum_sq / static_cast<double>(T - 1);
            double std_r = (var_r > 1e-12) ? std::sqrt(var_r) : 1e-6;

            double trial_sr = (mean_r / std_r) * std::sqrt(252.0);
            double trial_ann_vol = std_r * std::sqrt(252.0);
            double trial_ann_ret = mean_r * 252.0;

            t.annualized_return = trial_ann_ret;
            t.annualized_volatility = trial_ann_vol;
            t.sharpe_ratio = trial_sr;
            t.daily_returns = path;
            trials.push_back(std::move(t));
        }

        return trials;
    }
};

} // namespace quant
