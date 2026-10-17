#pragma once

#include "Statistics.hpp"
#include <string>
#include <vector>
#include <chrono>

namespace quant {

struct AuditMetrics {
    double reported_sharpe = 0.0;
    double expected_max_noise_sharpe = 0.0;
    double dsr_p_value = 0.0;
    double dsr_z_score = 0.0;
    bool is_overfitted = true;
    double sample_skewness = 0.0;
    double sample_kurtosis = 3.0;
    double trial_variance = 0.0;
    size_t total_trials = 0;
    size_t track_record_days = 0;
};

struct AuditReport {
    std::string audit_id;
    std::string strategy_id;
    std::string strategy_name;
    AuditMetrics metrics;
    HistogramData distribution_summary;
    std::vector<double> trial_sharpe_distribution;
    double execution_time_ms = 0.0;
    std::string executed_at;
};

class DSRDetector {
public:
    static constexpr double DSR_SIGNIFICANCE_THRESHOLD = 0.95;

    static AuditReport evaluate(const std::string& audit_id,
                                const std::string& strategy_id,
                                const std::string& strategy_name,
                                double reported_sharpe,
                                size_t total_trials,
                                size_t track_record_days,
                                const std::vector<double>& trial_sharpes,
                                double skewness = 0.0,
                                double kurtosis = 3.0,
                                double annualization_factor = 252.0) {
        auto start_clock = std::chrono::high_resolution_clock::now();

        AuditReport report;
        report.audit_id = audit_id;
        report.strategy_id = strategy_id;
        report.strategy_name = strategy_name;
        report.trial_sharpe_distribution = trial_sharpes;

        double sharpe_mean = 0.0;
        double trial_variance = 0.0;
        if (trial_sharpes.size() > 1) {
            trial_variance = Statistics::sample_variance(trial_sharpes, &sharpe_mean);
        } else {
            // Asymptotic theoretical variance of Sharpe ratio under H0: Var[SR] = q / (T - 1)
            double t = static_cast<double>(track_record_days > 1 ? track_record_days : 2);
            trial_variance = annualization_factor / (t - 1.0);
        }

        if (trial_variance < Statistics::VARIANCE_EPSILON) {
            trial_variance = Statistics::VARIANCE_EPSILON;
        }

        double exp_max = Statistics::expected_max_sharpe(total_trials, trial_variance);

        double z_score = Statistics::compute_dsr_z(reported_sharpe, exp_max, track_record_days, skewness, kurtosis, annualization_factor);
        double dsr_p = Statistics::normal_cdf(z_score);

        bool overfitted = (dsr_p < DSR_SIGNIFICANCE_THRESHOLD);

        report.metrics.reported_sharpe = reported_sharpe;
        report.metrics.expected_max_noise_sharpe = exp_max;
        report.metrics.dsr_p_value = dsr_p;
        report.metrics.dsr_z_score = z_score;
        report.metrics.is_overfitted = overfitted;
        report.metrics.sample_skewness = skewness;
        report.metrics.sample_kurtosis = kurtosis;
        report.metrics.trial_variance = trial_variance;
        report.metrics.total_trials = total_trials;
        report.metrics.track_record_days = track_record_days;

        if (!trial_sharpes.empty()) {
            report.distribution_summary = Statistics::compute_histogram(trial_sharpes, 8);
        } else {

            report.distribution_summary = Statistics::compute_histogram({reported_sharpe}, 8);
        }

        auto end_clock = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> elapsed = end_clock - start_clock;
        report.execution_time_ms = elapsed.count();

        return report;
    }
};

}
