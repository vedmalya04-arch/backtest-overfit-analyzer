#pragma once

#include "DSRDetector.hpp"
#include "Strategy.hpp"
#include <libpq-fe.h>
#include <string>
#include <vector>
#include <mutex>
#include <memory>
#include <stdexcept>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <iomanip>

namespace quant {

struct AuditSummaryRow {
    std::string audit_id;
    std::string strategy_id;
    std::string strategy_name;
    int total_trials = 0;
    int sample_length_days = 0;
    double reported_sharpe = 0.0;
    double expected_max_sharpe = 0.0;
    double dsr_p_value = 0.0;
    int is_overfitted = 1;
    double skewness = 0.0;
    double kurtosis = 0.0;
    double trial_sharpe_variance = 0.0;
    std::string executed_at;
};

struct QueryResult {
    bool success = true;
    std::string error_message;
    std::vector<std::string> columns;
    std::vector<std::vector<std::string>> rows;
};

class Database {
private:
    PGconn* conn_ = nullptr;
    mutable std::recursive_mutex db_mutex_;
    std::string conn_info_;

public:
    explicit Database(const std::string& conn_info = "host=localhost port=5432 user=postgres password=Asdfgh@040907 dbname=boa")
        : conn_info_(conn_info) {
        connect();
        init_schema();
    }

    ~Database() {
        disconnect();
    }

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    void connect() {
        if (conn_) {
            PQfinish(conn_);
            conn_ = nullptr;
        }

        conn_ = PQconnectdb(conn_info_.c_str());
        if (PQstatus(conn_) != CONNECTION_OK) {
            std::string err = PQerrorMessage(conn_);
            PQfinish(conn_);
            conn_ = nullptr;
            throw std::runtime_error("Failed to connect to PostgreSQL: " + err);
        }
    }

    void disconnect() {
        if (conn_) {
            PQfinish(conn_);
            conn_ = nullptr;
        }
    }

    void ensure_connected() {
        if (!conn_ || PQstatus(conn_) != CONNECTION_OK) {
            connect();
        }
    }

    std::string escape_string(const std::string& str) {
        if (!conn_) return str;
        char* escaped = PQescapeLiteral(conn_, str.c_str(), str.length());
        if (!escaped) return "''";
        std::string res(escaped);
        PQfreemem(escaped);
        return res;
    }

    void execute_raw(const std::string& sql) {
        std::lock_guard<std::recursive_mutex> lock(db_mutex_);
        ensure_connected();

        PGresult* res = PQexec(conn_, sql.c_str());
        ExecStatusType status = PQresultStatus(res);
        if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK) {
            std::string err = PQerrorMessage(conn_);
            PQclear(res);
            throw std::runtime_error("PostgreSQL execute_raw error: " + err);
        }
        PQclear(res);
    }

    void init_schema() {
        const char* ddl = R"SQL(
            CREATE TABLE IF NOT EXISTS strategies (
                strategy_id VARCHAR(128) PRIMARY KEY,
                name VARCHAR(255) NOT NULL UNIQUE,
                asset_class VARCHAR(64) NOT NULL DEFAULT 'EQUITIES',
                author VARCHAR(128) NOT NULL DEFAULT 'Quantitative Research',
                created_at TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP
            );

            CREATE TABLE IF NOT EXISTS audit_runs (
                audit_id VARCHAR(128) PRIMARY KEY,
                strategy_id VARCHAR(128) NOT NULL,
                total_trials INTEGER NOT NULL,
                sample_length_days INTEGER NOT NULL,
                reported_sharpe DOUBLE PRECISION NOT NULL,
                expected_max_sharpe DOUBLE PRECISION NOT NULL,
                dsr_p_value DOUBLE PRECISION NOT NULL,
                is_overfitted INTEGER NOT NULL,
                skewness DOUBLE PRECISION NOT NULL,
                kurtosis DOUBLE PRECISION NOT NULL,
                trial_sharpe_variance DOUBLE PRECISION NOT NULL,
                executed_at TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP,
                FOREIGN KEY (strategy_id) REFERENCES strategies(strategy_id) ON DELETE CASCADE
            );

            CREATE TABLE IF NOT EXISTS trial_metrics (
                trial_id VARCHAR(128) PRIMARY KEY,
                audit_id VARCHAR(128) NOT NULL,
                trial_index INTEGER NOT NULL,
                annualized_return DOUBLE PRECISION NOT NULL,
                annualized_volatility DOUBLE PRECISION NOT NULL,
                sharpe_ratio DOUBLE PRECISION NOT NULL,
                FOREIGN KEY (audit_id) REFERENCES audit_runs(audit_id) ON DELETE CASCADE
            );

            CREATE TABLE IF NOT EXISTS benchmark_returns (
                id SERIAL PRIMARY KEY,
                date DATE NOT NULL UNIQUE,
                asset_ticker VARCHAR(32) NOT NULL DEFAULT 'SPY',
                close_price DOUBLE PRECISION NOT NULL,
                daily_return DOUBLE PRECISION NOT NULL
            );

            CREATE INDEX IF NOT EXISTS idx_audit_runs_strategy ON audit_runs(strategy_id);
            CREATE INDEX IF NOT EXISTS idx_audit_runs_executed ON audit_runs(executed_at);
            CREATE INDEX IF NOT EXISTS idx_trial_metrics_audit ON trial_metrics(audit_id);
        )SQL";

        execute_raw(ddl);
    }

    std::string get_or_create_strategy(const std::string& name,
                                       const std::string& asset_class = "EQUITIES",
                                       const std::string& author = "Quantitative Research") {
        std::lock_guard<std::recursive_mutex> lock(db_mutex_);
        ensure_connected();

        const char* select_sql = "SELECT strategy_id FROM strategies WHERE name = $1;";
        const char* params[1] = { name.c_str() };
        PGresult* res = PQexecParams(conn_, select_sql, 1, nullptr, params, nullptr, nullptr, 0);

        if (PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) > 0) {
            std::string existing_id = PQgetvalue(res, 0, 0);
            PQclear(res);
            return existing_id;
        }
        PQclear(res);

        std::string new_id = "strat_" + name;
        for (char& c : new_id) {
            if (!std::isalnum(static_cast<unsigned char>(c))) c = '_';
        }

        const char* insert_sql = R"SQL(
            INSERT INTO strategies (strategy_id, name, asset_class, author)
            VALUES ($1, $2, $3, $4)
            ON CONFLICT (name) DO UPDATE SET name = EXCLUDED.name
            RETURNING strategy_id;
        )SQL";

        const char* ins_params[4] = { new_id.c_str(), name.c_str(), asset_class.c_str(), author.c_str() };
        res = PQexecParams(conn_, insert_sql, 4, nullptr, ins_params, nullptr, nullptr, 0);
        if (PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) > 0) {
            std::string final_id = PQgetvalue(res, 0, 0);
            PQclear(res);
            return final_id;
        }
        PQclear(res);

        return new_id;
    }

    void record_audit_run(const AuditReport& report, const std::vector<TrialResult>& trials) {
        std::lock_guard<std::recursive_mutex> lock(db_mutex_);
        ensure_connected();

        execute_raw("BEGIN;");

        const char* audit_sql = R"SQL(
            INSERT INTO audit_runs (
                audit_id, strategy_id, total_trials, sample_length_days,
                reported_sharpe, expected_max_sharpe, dsr_p_value, is_overfitted,
                skewness, kurtosis, trial_sharpe_variance, executed_at
            ) VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, CURRENT_TIMESTAMP)
            ON CONFLICT (audit_id) DO UPDATE SET
                reported_sharpe = EXCLUDED.reported_sharpe,
                expected_max_sharpe = EXCLUDED.expected_max_sharpe,
                dsr_p_value = EXCLUDED.dsr_p_value,
                is_overfitted = EXCLUDED.is_overfitted;
        )SQL";

        std::string s_total = std::to_string(report.metrics.total_trials);
        std::string s_days = std::to_string(report.metrics.track_record_days);
        std::string s_rep_sr = std::to_string(report.metrics.reported_sharpe);
        std::string s_exp_sr = std::to_string(report.metrics.expected_max_noise_sharpe);
        std::string s_dsr_p = std::to_string(report.metrics.dsr_p_value);
        std::string s_overfit = std::to_string(report.metrics.is_overfitted ? 1 : 0);
        std::string s_skew = std::to_string(report.metrics.sample_skewness);
        std::string s_kurt = std::to_string(report.metrics.sample_kurtosis);
        std::string s_var = std::to_string(report.metrics.trial_variance);

        const char* audit_params[11] = {
            report.audit_id.c_str(),
            report.strategy_id.c_str(),
            s_total.c_str(),
            s_days.c_str(),
            s_rep_sr.c_str(),
            s_exp_sr.c_str(),
            s_dsr_p.c_str(),
            s_overfit.c_str(),
            s_skew.c_str(),
            s_kurt.c_str(),
            s_var.c_str()
        };

        PGresult* res = PQexecParams(conn_, audit_sql, 11, nullptr, audit_params, nullptr, nullptr, 0);
        if (PQresultStatus(res) != PGRES_COMMAND_OK) {
            std::string err = PQerrorMessage(conn_);
            PQclear(res);
            PQexec(conn_, "ROLLBACK;");
            throw std::runtime_error("Error inserting audit_runs into PostgreSQL: " + err);
        }
        PQclear(res);

        size_t insert_count = std::min(trials.size(), size_t(500));
        if (insert_count > 0) {
            std::ostringstream batch_ss;
            batch_ss << "INSERT INTO trial_metrics (trial_id, audit_id, trial_index, annualized_return, annualized_volatility, sharpe_ratio) VALUES ";

            for (size_t i = 0; i < insert_count; ++i) {
                const auto& t = trials[i];
                std::string tid = report.audit_id + "_t" + std::to_string(t.trial_index);

                batch_ss << "('" << tid << "', '" << report.audit_id << "', " << t.trial_index
                         << ", " << t.annualized_return << ", " << t.annualized_volatility
                         << ", " << t.sharpe_ratio << ")";

                if (i + 1 < insert_count) {
                    batch_ss << ", ";
                }
            }
            batch_ss << " ON CONFLICT (trial_id) DO NOTHING;";

            std::string batch_query = batch_ss.str();
            res = PQexec(conn_, batch_query.c_str());
            if (PQresultStatus(res) != PGRES_COMMAND_OK) {
                std::string err = PQerrorMessage(conn_);
                PQclear(res);
                PQexec(conn_, "ROLLBACK;");
                throw std::runtime_error("Error inserting trial_metrics into PostgreSQL: " + err);
            }
            PQclear(res);
        }

        execute_raw("COMMIT;");
    }

    std::vector<AuditSummaryRow> get_audits(int limit = 20, int offset = 0, int* out_total = nullptr) {
        std::lock_guard<std::recursive_mutex> lock(db_mutex_);
        ensure_connected();
        std::vector<AuditSummaryRow> list;

        if (out_total) {
            PGresult* count_res = PQexec(conn_, "SELECT COUNT(*) FROM audit_runs;");
            if (PQresultStatus(count_res) == PGRES_TUPLES_OK && PQntuples(count_res) > 0) {
                *out_total = std::stoi(PQgetvalue(count_res, 0, 0));
            }
            PQclear(count_res);
        }

        const char* sql = R"SQL(
            SELECT a.audit_id, a.strategy_id, s.name, a.total_trials, a.sample_length_days,
                   a.reported_sharpe, a.expected_max_sharpe, a.dsr_p_value, a.is_overfitted,
                   a.skewness, a.kurtosis, a.trial_sharpe_variance, a.executed_at::text
            FROM audit_runs a
            JOIN strategies s ON a.strategy_id = s.strategy_id
            ORDER BY a.executed_at DESC
            LIMIT $1 OFFSET $2;
        )SQL";

        std::string s_lim = std::to_string(limit);
        std::string s_off = std::to_string(offset);
        const char* params[2] = { s_lim.c_str(), s_off.c_str() };

        PGresult* res = PQexecParams(conn_, sql, 2, nullptr, params, nullptr, nullptr, 0);
        if (PQresultStatus(res) == PGRES_TUPLES_OK) {
            int rows = PQntuples(res);
            list.reserve(rows);

            for (int i = 0; i < rows; ++i) {
                AuditSummaryRow row;
                row.audit_id = PQgetvalue(res, i, 0);
                row.strategy_id = PQgetvalue(res, i, 1);
                row.strategy_name = PQgetvalue(res, i, 2);
                row.total_trials = std::stoi(PQgetvalue(res, i, 3));
                row.sample_length_days = std::stoi(PQgetvalue(res, i, 4));
                row.reported_sharpe = std::stod(PQgetvalue(res, i, 5));
                row.expected_max_sharpe = std::stod(PQgetvalue(res, i, 6));
                row.dsr_p_value = std::stod(PQgetvalue(res, i, 7));
                row.is_overfitted = std::stoi(PQgetvalue(res, i, 8));
                row.skewness = std::stod(PQgetvalue(res, i, 9));
                row.kurtosis = std::stod(PQgetvalue(res, i, 10));
                row.trial_sharpe_variance = std::stod(PQgetvalue(res, i, 11));
                row.executed_at = PQgetvalue(res, i, 12);
                list.push_back(std::move(row));
            }
        }
        PQclear(res);

        return list;
    }

    bool get_audit_by_id(const std::string& audit_id, AuditSummaryRow& out_row, std::vector<double>& out_sharpes) {
        std::lock_guard<std::recursive_mutex> lock(db_mutex_);
        ensure_connected();

        const char* sql = R"SQL(
            SELECT a.audit_id, a.strategy_id, s.name, a.total_trials, a.sample_length_days,
                   a.reported_sharpe, a.expected_max_sharpe, a.dsr_p_value, a.is_overfitted,
                   a.skewness, a.kurtosis, a.trial_sharpe_variance, a.executed_at::text
            FROM audit_runs a
            JOIN strategies s ON a.strategy_id = s.strategy_id
            WHERE a.audit_id = $1;
        )SQL";

        const char* params[1] = { audit_id.c_str() };
        PGresult* res = PQexecParams(conn_, sql, 1, nullptr, params, nullptr, nullptr, 0);
        bool found = false;

        if (PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) > 0) {
            out_row.audit_id = PQgetvalue(res, 0, 0);
            out_row.strategy_id = PQgetvalue(res, 0, 1);
            out_row.strategy_name = PQgetvalue(res, 0, 2);
            out_row.total_trials = std::stoi(PQgetvalue(res, 0, 3));
            out_row.sample_length_days = std::stoi(PQgetvalue(res, 0, 4));
            out_row.reported_sharpe = std::stod(PQgetvalue(res, 0, 5));
            out_row.expected_max_sharpe = std::stod(PQgetvalue(res, 0, 6));
            out_row.dsr_p_value = std::stod(PQgetvalue(res, 0, 7));
            out_row.is_overfitted = std::stoi(PQgetvalue(res, 0, 8));
            out_row.skewness = std::stod(PQgetvalue(res, 0, 9));
            out_row.kurtosis = std::stod(PQgetvalue(res, 0, 10));
            out_row.trial_sharpe_variance = std::stod(PQgetvalue(res, 0, 11));
            out_row.executed_at = PQgetvalue(res, 0, 12);
            found = true;
        }
        PQclear(res);

        if (found) {
            const char* trials_sql = "SELECT sharpe_ratio FROM trial_metrics WHERE audit_id = $1 ORDER BY trial_index ASC LIMIT 2000;";
            res = PQexecParams(conn_, trials_sql, 1, nullptr, params, nullptr, nullptr, 0);
            if (PQresultStatus(res) == PGRES_TUPLES_OK) {
                int rows = PQntuples(res);
                out_sharpes.reserve(rows);
                for (int i = 0; i < rows; ++i) {
                    out_sharpes.push_back(std::stod(PQgetvalue(res, i, 0)));
                }
            }
            PQclear(res);
        }

        return found;
    }

    QueryResult run_readonly_query(const std::string& sql) {
        std::lock_guard<std::recursive_mutex> lock(db_mutex_);
        ensure_connected();
        QueryResult res_out;

        std::string trimmed = sql;
        trimmed.erase(0, trimmed.find_first_not_of(" \t\n\r"));
        std::string upper = trimmed;
        std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);

        if (upper.rfind("SELECT", 0) != 0 && upper.rfind("WITH", 0) != 0) {
            res_out.success = false;
            res_out.error_message = "Security guard: only read-only queries (SELECT or WITH) are permitted.";
            return res_out;
        }

        const std::vector<std::string> forbidden = {
            "INSERT", "UPDATE", "DELETE", "DROP", "ALTER", "CREATE", "ATTACH", "DETACH", "TRUNCATE", "GRANT", "REVOKE"
        };
        for (const auto& word : forbidden) {
            size_t pos = 0;
            while ((pos = upper.find(word, pos)) != std::string::npos) {
                bool pre_ok = (pos == 0 || !std::isalnum(static_cast<unsigned char>(upper[pos - 1])));
                bool post_ok = (pos + word.length() >= upper.length() || !std::isalnum(static_cast<unsigned char>(upper[pos + word.length()])));
                if (pre_ok && post_ok) {
                    res_out.success = false;
                    res_out.error_message = "Security guard: mutation keyword '" + word + "' is not permitted in read-only queries.";
                    return res_out;
                }
                pos += word.length();
            }
        }

        PGresult* res = PQexec(conn_, sql.c_str());
        if (PQresultStatus(res) != PGRES_TUPLES_OK) {
            res_out.success = false;
            res_out.error_message = PQerrorMessage(conn_);
            PQclear(res);
            return res_out;
        }

        int col_count = PQnfields(res);
        for (int i = 0; i < col_count; ++i) {
            const char* col_name = PQfname(res, i);
            res_out.columns.push_back(col_name ? col_name : "col_" + std::to_string(i));
        }

        int row_count = PQntuples(res);
        int limit = std::min(row_count, 500);
        res_out.rows.reserve(limit);

        for (int r = 0; r < limit; ++r) {
            std::vector<std::string> row;
            row.reserve(col_count);
            for (int c = 0; c < col_count; ++c) {
                if (PQgetisnull(res, r, c)) {
                    row.push_back("NULL");
                } else {
                    row.push_back(PQgetvalue(res, r, c));
                }
            }
            res_out.rows.push_back(std::move(row));
        }

        PQclear(res);
        return res_out;
    }
};

}
