#pragma once

#include <vector>
#include <cmath>
#include <cstddef>
#include <string>

namespace quant {

struct ReturnMoments {
    double mean = 0.0;
    double variance = 0.0;
    double std_dev = 0.0;
    double skewness = 0.0;
    double kurtosis = 3.0;
    double annualized_return = 0.0;
    double annualized_volatility = 0.0;
    double annualized_sharpe = 0.0;
    size_t sample_size = 0;
};

struct HistogramData {
    double min_val = 0.0;
    double max_val = 0.0;
    double bin_width = 0.0;
    std::vector<int> counts;
    std::vector<double> bin_edges;
};

class Statistics {
public:
    static constexpr double EULER_MASCHERONI = 0.5772156649015328606065;
    static constexpr double E_CONST = 2.718281828459045235360287;
    static constexpr double VARIANCE_EPSILON = 1e-12;

    static ReturnMoments compute_moments(const double* returns, size_t size, double annualization_factor = 252.0);
    static ReturnMoments compute_moments(const std::vector<double>& returns, double annualization_factor = 252.0);

    static double normal_cdf(double z);

    static double normal_quantile(double p);

    static double sample_variance(const double* values, size_t size, double* out_mean = nullptr);
    static double sample_variance(const std::vector<double>& values, double* out_mean = nullptr);

    static double expected_max_sharpe(size_t n_trials, double trial_sharpe_variance);

    static double compute_dsr_z(double reported_sharpe,
                                double expected_max_sharpe,
                                size_t track_record_days,
                                double skewness,
                                double kurtosis,
                                double annualization_factor = 252.0);

    static double compute_dsr(double reported_sharpe,
                              double expected_max_sharpe,
                              size_t track_record_days,
                              double skewness,
                              double kurtosis,
                              double annualization_factor = 252.0);

    static HistogramData compute_histogram(const std::vector<double>& data, size_t num_bins = 8);
};

}
