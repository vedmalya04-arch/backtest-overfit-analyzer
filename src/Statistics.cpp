#include "Statistics.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <limits>

namespace quant {

ReturnMoments Statistics::compute_moments(const double* returns, size_t size, double annualization_factor) {
    ReturnMoments m;
    m.sample_size = size;

    if (size == 0) {
        return m;
    }

    if (size == 1) {
        m.mean = returns[0];
        m.annualized_return = m.mean * annualization_factor;
        m.kurtosis = 3.0;
        return m;
    }

    double sum = 0.0;
    for (size_t i = 0; i < size; ++i) {
        sum += returns[i];
    }
    m.mean = sum / static_cast<double>(size);

    double sum_sq = 0.0;
    double sum_cube = 0.0;
    double sum_quad = 0.0;

    for (size_t i = 0; i < size; ++i) {
        double diff = returns[i] - m.mean;
        double diff_sq = diff * diff;
        sum_sq += diff_sq;
        sum_cube += diff_sq * diff;
        sum_quad += diff_sq * diff_sq;
    }

    m.variance = sum_sq / static_cast<double>(size - 1);

    if (m.variance < VARIANCE_EPSILON) {
        m.variance = VARIANCE_EPSILON;
    }

    m.std_dev = std::sqrt(m.variance);

    double n = static_cast<double>(size);
    double sigma3 = m.variance * m.std_dev;
    double sigma4 = m.variance * m.variance;

    m.skewness = (sum_cube / n) / (sigma3 > VARIANCE_EPSILON ? sigma3 : VARIANCE_EPSILON);
    m.kurtosis = (sum_quad / n) / (sigma4 > VARIANCE_EPSILON ? sigma4 : VARIANCE_EPSILON);

    double sqrt_factor = std::sqrt(annualization_factor);
    m.annualized_return = m.mean * annualization_factor;
    m.annualized_volatility = m.std_dev * sqrt_factor;

    if (m.std_dev > 1e-9) {
        m.annualized_sharpe = (m.mean / m.std_dev) * sqrt_factor;
    } else {
        m.annualized_sharpe = 0.0;
    }

    return m;
}

ReturnMoments Statistics::compute_moments(const std::vector<double>& returns, double annualization_factor) {
    return compute_moments(returns.data(), returns.size(), annualization_factor);
}

double Statistics::normal_cdf(double z) {
    if (std::isnan(z)) return 0.5;

    return 0.5 * (1.0 + std::erf(z / std::sqrt(2.0)));
}

double Statistics::normal_quantile(double p) {

    static const double a[] = {
        -3.969683028665376e+01,
         2.209460984245205e+02,
        -2.759285104469687e+02,
         1.383577518672690e+02,
        -3.066479864014738e+01,
         2.506628277459239e+00
    };

    static const double b[] = {
        -5.447609879822406e+01,
         1.615858368580409e+02,
        -1.556989798598866e+02,
         6.680131188771972e+01,
        -1.328068155288572e+01
    };

    static const double c[] = {
        -7.784894002430293e-03,
        -3.223964580411365e-01,
        -2.400758277161838e+00,
        -2.549732539343734e+00,
         4.374664141464968e+00,
         2.938163982698783e+00
    };

    static const double d[] = {
         7.784695709041462e-03,
         3.224671290700398e-01,
         2.445134137142996e+00,
         3.754408661907416e+00
    };

    const double p_low = 0.02425;
    const double p_high = 1.0 - p_low;

    if (p <= 0.0) return -std::numeric_limits<double>::infinity();
    if (p >= 1.0) return std::numeric_limits<double>::infinity();

    double q, r;

    if (p < p_low) {
        q = std::sqrt(-2.0 * std::log(p));
        return (((((c[0]*q + c[1])*q + c[2])*q + c[3])*q + c[4])*q + c[5]) /
               ((((d[0]*q + d[1])*q + d[2])*q + d[3])*q + 1.0);
    }

    if (p > p_high) {
        q = std::sqrt(-2.0 * std::log(1.0 - p));
        return -(((((c[0]*q + c[1])*q + c[2])*q + c[3])*q + c[4])*q + c[5]) /
                ((((d[0]*q + d[1])*q + d[2])*q + d[3])*q + 1.0);
    }

    q = p - 0.5;
    r = q * q;
    return (((((a[0]*r + a[1])*r + a[2])*r + a[3])*r + a[4])*r + a[5]) * q /
           (((((b[0]*r + b[1])*r + b[2])*r + b[3])*r + b[4])*r + 1.0);
}

double Statistics::sample_variance(const double* values, size_t size, double* out_mean) {
    if (size < 2) {
        if (out_mean && size == 1) *out_mean = values[0];
        return 0.0;
    }

    double sum = 0.0;
    for (size_t i = 0; i < size; ++i) {
        sum += values[i];
    }
    double mean = sum / static_cast<double>(size);
    if (out_mean) *out_mean = mean;

    double sum_sq = 0.0;
    for (size_t i = 0; i < size; ++i) {
        double d = values[i] - mean;
        sum_sq += d * d;
    }

    return sum_sq / static_cast<double>(size - 1);
}

double Statistics::sample_variance(const std::vector<double>& values, double* out_mean) {
    return sample_variance(values.data(), values.size(), out_mean);
}

double Statistics::expected_max_sharpe(size_t n_trials, double trial_sharpe_variance) {
    if (n_trials <= 1) {
        return 0.0;
    }

    if (trial_sharpe_variance < VARIANCE_EPSILON) {
        trial_sharpe_variance = VARIANCE_EPSILON;
    }

    double n = static_cast<double>(n_trials);
    double p1 = 1.0 - (1.0 / n);
    double p2 = 1.0 - (1.0 / (n * E_CONST));

    p1 = std::clamp(p1, 1e-15, 1.0 - 1e-15);
    p2 = std::clamp(p2, 1e-15, 1.0 - 1e-15);

    double q1 = normal_quantile(p1);
    double q2 = normal_quantile(p2);

    double evt_factor = (1.0 - EULER_MASCHERONI) * q1 + EULER_MASCHERONI * q2;
    double expected_max = std::sqrt(trial_sharpe_variance) * evt_factor;

    return expected_max;
}

double Statistics::compute_dsr_z(double reported_sharpe,
                                double expected_max_sharpe,
                                size_t track_record_days,
                                double skewness,
                                double kurtosis,
                                double annualization_factor) {
    double t = static_cast<double>(track_record_days > 1 ? track_record_days : 2);
    double sqrt_factor = (annualization_factor > 0.0) ? std::sqrt(annualization_factor) : 1.0;

    // Convert annualized Sharpe ratios to per-period (daily) sampling frequency
    double sr_period = reported_sharpe / sqrt_factor;
    double sr0_period = expected_max_sharpe / sqrt_factor;

    double term_skew = skewness * sr_period;
    double term_kurt = ((kurtosis - 1.0) / 4.0) * (sr_period * sr_period);
    double denom_sq = 1.0 - term_skew + term_kurt;

    if (denom_sq < VARIANCE_EPSILON) {
        denom_sq = VARIANCE_EPSILON;
    }

    double denom = std::sqrt(denom_sq);
    double z = ((sr_period - sr0_period) * std::sqrt(t - 1.0)) / denom;

    return z;
}

double Statistics::compute_dsr(double reported_sharpe,
                              double expected_max_sharpe,
                              size_t track_record_days,
                              double skewness,
                              double kurtosis,
                              double annualization_factor) {
    double z = compute_dsr_z(reported_sharpe, expected_max_sharpe, track_record_days, skewness, kurtosis, annualization_factor);
    return normal_cdf(z);
}

HistogramData Statistics::compute_histogram(const std::vector<double>& data, size_t num_bins) {
    HistogramData hist;
    if (data.empty()) return hist;
    if (num_bins == 0) num_bins = 8;

    double min_v = data[0];
    double max_v = data[0];
    for (double v : data) {
        if (v < min_v) min_v = v;
        if (v > max_v) max_v = v;
    }

    if (std::abs(max_v - min_v) < 1e-9) {
        min_v -= 0.5;
        max_v += 0.5;
    }

    hist.min_val = min_v;
    hist.max_val = max_v;
    hist.bin_width = (max_v - min_v) / static_cast<double>(num_bins);
    hist.counts.assign(num_bins, 0);
    hist.bin_edges.resize(num_bins + 1);

    for (size_t i = 0; i <= num_bins; ++i) {
        hist.bin_edges[i] = min_v + static_cast<double>(i) * hist.bin_width;
    }

    for (double v : data) {
        size_t bin = static_cast<size_t>((v - min_v) / hist.bin_width);
        if (bin >= num_bins) bin = num_bins - 1;
        hist.counts[bin]++;
    }

    return hist;
}

}
