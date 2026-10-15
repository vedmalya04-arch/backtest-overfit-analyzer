-- PostgreSQL DDL Schema for Backtest Overfitting Detector
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
    is_overfitted INTEGER NOT NULL, -- 0 = Pass, 1 = Overfitted
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
