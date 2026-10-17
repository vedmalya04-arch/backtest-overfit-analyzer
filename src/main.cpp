#include "DSRDetector.hpp"
#include "Database.hpp"
#include "HttpServer.hpp"
#include "Statistics.hpp"
#include "Strategy.hpp"


#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <vector>


using namespace quant;

void print_banner() {
  std::cout << "\n"
            << "==============================================================="
               "=========\n"
            << "       BACKTEST OVERFITTING DETECTOR & AUDIT ENGINE "
               "(C++20/C++17)       \n"
            << "  Methodology: David Bailey & Marcos Lopez de Prado (Deflated "
               "Sharpe)   \n"
            << "==============================================================="
               "=========\n"
            << std::endl;
}

void run_cli_audit(Database &db, const std::string &strategy_name,
                   size_t total_trials, size_t track_record_days,
                   double reported_sharpe, bool use_csv) {
  std::cout << "[Audit Engine] Initializing audit for strategy: "
            << strategy_name << "\n";
  std::cout << "[Audit Engine] Parameters: Trials=" << total_trials
            << ", Days=" << track_record_days
            << ", Reported Sharpe=" << std::fixed << std::setprecision(2)
            << reported_sharpe << "\n";

  std::vector<TrialResult> trials;
  std::vector<double> trial_sharpes;
  trial_sharpes.reserve(total_trials);

  if (use_csv) {
    std::cout << "[Audit Engine] Ingesting asset prices from "
                 "data/historical_prices.csv...\n";
    auto prices = StrategyEngine::load_csv_prices("data/historical_prices.csv");
    if (prices.size() >= 30) {
      std::cout << "[Audit Engine] Loaded " << prices.size()
                << " historical price bars. Running technical trials...\n";
      trials = StrategyEngine::generate_technical_trials(prices, total_trials);
      for (const auto &t : trials) {
        trial_sharpes.push_back(t.sharpe_ratio);
      }
    } else {
      std::cout << "[Audit Engine] Warning: CSV contains insufficient data. "
                   "Falling back to synthetic distribution.\n";
      trials = StrategyEngine::generate_synthetic_trials(
          total_trials, track_record_days, reported_sharpe);
      for (const auto &t : trials) {
        trial_sharpes.push_back(t.sharpe_ratio);
      }
    }
  } else {
    std::cout << "[Audit Engine] Generating synthetic multi-trial noise "
                 "distribution...\n";
    trials = StrategyEngine::generate_synthetic_trials(
        total_trials, track_record_days, reported_sharpe);
    for (const auto &t : trials) {
      trial_sharpes.push_back(t.sharpe_ratio);
    }
  }

  std::string audit_id =
      "aud_" + std::to_string(
                   std::chrono::system_clock::now().time_since_epoch().count() %
                   100000000);
  std::string strategy_id = db.get_or_create_strategy(strategy_name);

  std::cout
      << "[Audit Engine] Executing Deflated Sharpe Ratio calculation...\n";
  AuditReport report = DSRDetector::evaluate(
      audit_id, strategy_id, strategy_name, reported_sharpe, total_trials,
      track_record_days, trial_sharpes);

  db.record_audit_run(report, trials);

  std::cout << "\n---------------------------- AUDIT RESULTS "
               "-----------------------------\n";
  std::cout << " Audit ID:                  " << report.audit_id << "\n";
  std::cout << " Strategy:                  " << report.strategy_name << " ("
            << strategy_id << ")\n";
  std::cout << " Total Trials (N):          " << report.metrics.total_trials
            << "\n";
  std::cout << " Sample Length (T Days):    "
            << report.metrics.track_record_days << "\n";
  std::cout << " Reported Sharpe (SR*):     " << std::fixed
            << std::setprecision(4) << report.metrics.reported_sharpe << "\n";
  std::cout << " Expected Max Noise Sharpe: "
            << report.metrics.expected_max_noise_sharpe << "\n";
  std::cout << " Trial Sharpe Variance:     " << report.metrics.trial_variance
            << "\n";
  std::cout << " Sample Skewness:           " << report.metrics.sample_skewness
            << "\n";
  std::cout << " Sample Kurtosis:           " << report.metrics.sample_kurtosis
            << "\n";
  std::cout << " DSR z-Score:               " << report.metrics.dsr_z_score
            << "\n";
  std::cout << " DSR p-Value:               " << report.metrics.dsr_p_value
            << "\n";
  std::cout << " Execution Latency:         " << report.execution_time_ms
            << " ms\n";
  std::cout << " CLASSIFICATION:            ";
  if (report.metrics.is_overfitted) {
    std::cout << "[REJECT / LIKELY OVERFITTED] (DSR < 0.95)\n";
  } else {
    std::cout << "[PASS / STATISTICALLY SIGNIFICANT] (DSR >= 0.95)\n";
  }
  std::cout << "---------------------------------------------------------------"
               "---------\n\n";
}

int main(int argc, char *argv[]) {
  print_banner();

  bool server_mode = true;
  int port = 8088;
  const char* env_port = std::getenv("PORT");
  if (env_port && *env_port) {
    try {
      port = std::stoi(env_port);
    } catch (...) {}
  }

  std::string strategy_name = "Momentum_Trend_Alpha_v1";
  size_t total_trials = 1000;
  size_t track_record_days = 756;
  double reported_sharpe = 1.85;
  bool use_csv = false;

  std::string conn_info = "host=localhost port=5432 user=postgres password=Asdfgh@040907 dbname=boa";
  const char* env_db_url = std::getenv("DATABASE_URL");
  if (env_db_url && *env_db_url) {
    conn_info = env_db_url;
  } else {
    const char* env_conn = std::getenv("CONN_INFO");
    if (env_conn && *env_conn) {
      conn_info = env_conn;
    }
  }

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--cli") {
      server_mode = false;
    } else if (arg == "--server" || arg == "--serve") {
      server_mode = true;
    } else if (arg == "--port" && i + 1 < argc) {
      port = std::stoi(argv[++i]);
    } else if (arg == "--conn" && i + 1 < argc) {
      conn_info = argv[++i];
    } else if (arg == "--strategy" && i + 1 < argc) {
      strategy_name = argv[++i];
    } else if (arg == "--trials" && i + 1 < argc) {
      total_trials = static_cast<size_t>(std::stoul(argv[++i]));
    } else if (arg == "--days" && i + 1 < argc) {
      track_record_days = static_cast<size_t>(std::stoul(argv[++i]));
    } else if (arg == "--sharpe" && i + 1 < argc) {
      reported_sharpe = std::stod(argv[++i]);
    } else if (arg == "--csv") {
      use_csv = true;
    } else if (arg == "--help" || arg == "-h") {
      std::cout
          << "Usage: dsr_engine [OPTIONS]\n"
          << "  --server             Run in REST API Server mode (default)\n"
          << "  --cli                Run single audit in CLI mode\n"
          << "  --port <PORT>        HTTP port (default: 8088)\n"
          << "  --conn <CONNINFO>    PostgreSQL connection string\n"
          << "  --strategy <NAME>    Strategy name (default: "
             "Momentum_Trend_Alpha_v1)\n"
          << "  --trials <N>         Number of trials (default: 1000)\n"
          << "  --days <T>           Track record sample length in days "
             "(default: 756)\n"
          << "  --sharpe <SR>        Reported Sharpe ratio (default: 1.85)\n"
          << "  --csv                Ingest historical prices from "
             "data/historical_prices.csv\n";
      return 0;
    }
  }

  try {
    std::cout << "[Init] Connecting to PostgreSQL database (boa)...\n";
    auto db = std::make_shared<Database>(conn_info);
    std::cout << "[Init] PostgreSQL connected successfully.\n";

    int existing_count = 0;
    db->get_audits(1, 0, &existing_count);
    std::cout << "[Init] PostgreSQL connected. Audit database ready (" << existing_count << " existing records).\n";

    if (!server_mode) {
      run_cli_audit(*db, strategy_name, total_trials, track_record_days,
                    reported_sharpe, use_csv);
      return 0;
    }

    HttpServer server(port, db);
    server.start();

  } catch (const std::exception &e) {
    std::cerr << "[Fatal Error] " << e.what() << std::endl;
    return 1;
  }

  return 0;
}
