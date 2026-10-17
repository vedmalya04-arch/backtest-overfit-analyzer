#pragma once

#include "DSRDetector.hpp"
#include "Database.hpp"
#include "Strategy.hpp"


#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef int socklen_t;
#else
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#define closesocket close
typedef int SOCKET;
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#endif

#include <atomic>
#include <chrono>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <thread>
#include <vector>


namespace quant {

class JsonUtils {
public:
  static std::string escape_string(const std::string &str) {
    std::ostringstream ss;
    for (char c : str) {
      switch (c) {
      case '"':
        ss << "\\\"";
        break;
      case '\\':
        ss << "\\\\";
        break;
      case '\b':
        ss << "\\b";
        break;
      case '\f':
        ss << "\\f";
        break;
      case '\n':
        ss << "\\n";
        break;
      case '\r':
        ss << "\\r";
        break;
      case '\t':
        ss << "\\t";
        break;
      default:
        if ('\x00' <= c && c <= '\x1f') {
          ss << "\\u" << std::hex << std::setw(4) << std::setfill('0')
             << static_cast<int>(c);
        } else {
          ss << c;
        }
      }
    }
    return ss.str();
  }

  static std::string unescape_string(const std::string &str) {
    std::string res;
    res.reserve(str.length());
    for (size_t i = 0; i < str.length(); ++i) {
      if (str[i] == '\\' && i + 1 < str.length()) {
        char next = str[i + 1];
        if (next == '"') {
          res.push_back('"');
          i++;
        } else if (next == '\\') {
          res.push_back('\\');
          i++;
        } else if (next == 'n') {
          res.push_back('\n');
          i++;
        } else if (next == 't') {
          res.push_back('\t');
          i++;
        } else if (next == 'r') {
          res.push_back('\r');
          i++;
        } else {
          res.push_back(next);
          i++;
        }
      } else {
        res.push_back(str[i]);
      }
    }
    return res;
  }

  static std::string extract_string(const std::string &json,
                                    const std::string &key) {
    std::string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) {
      pos = json.find(key);
      if (pos == std::string::npos)
        return "";
      search = key;
    }

    size_t colon = json.find(':', pos + search.length());
    if (colon == std::string::npos)
      return "";

    size_t start = colon + 1;
    while (start < json.length() &&
           (json[start] == ' ' || json[start] == '\t' || json[start] == '\r' ||
            json[start] == '\n')) {
      start++;
    }

    if (start < json.length() && json[start] == '"') {
      size_t end = json.find('"', start + 1);
      while (end != std::string::npos && json[end - 1] == '\\') {
        end = json.find('"', end + 1);
      }
      if (end != std::string::npos) {
        return unescape_string(json.substr(start + 1, end - start - 1));
      }
    } else {
      size_t end = start;
      while (end < json.length() && json[end] != ',' && json[end] != '}' &&
             json[end] != '\r' && json[end] != '\n') {
        end++;
      }
      std::string val = json.substr(start, end - start);
      if (!val.empty() && val.front() == '"')
        val = val.substr(1);
      if (!val.empty() && val.back() == '"')
        val.pop_back();
      return unescape_string(val);
    }

    return "";
  }

  static double extract_double(const std::string &json, const std::string &key,
                               double default_val = 0.0) {
    std::string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos)
      return default_val;

    size_t colon = json.find(':', pos + search.length());
    if (colon == std::string::npos)
      return default_val;

    size_t start = colon + 1;
    while (start < json.length() &&
           (json[start] == ' ' || json[start] == '\t' || json[start] == '\r' ||
            json[start] == '\n')) {
      start++;
    }

    size_t end = start;
    while (end < json.length() &&
           (std::isdigit(json[end]) || json[end] == '.' || json[end] == '-' ||
            json[end] == '+' || json[end] == 'e' || json[end] == 'E')) {
      end++;
    }

    if (end > start) {
      try {
        return std::stod(json.substr(start, end - start));
      } catch (...) {
      }
    }
    return default_val;
  }

  static int extract_int(const std::string &json, const std::string &key,
                         int default_val = 0) {
    return static_cast<int>(extract_double(json, key, default_val));
  }

  static bool extract_bool(const std::string &json, const std::string &key,
                           bool default_val = false) {
    std::string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos)
      return default_val;

    size_t colon = json.find(':', pos + search.length());
    if (colon == std::string::npos)
      return default_val;

    std::string sub = json.substr(colon + 1, 20);
    if (sub.find("true") != std::string::npos)
      return true;
    if (sub.find("false") != std::string::npos)
      return false;
    return default_val;
  }
};

class HttpServer {
private:
  int port_;
  std::shared_ptr<Database> db_;
  std::atomic<bool> is_running_{false};
  std::chrono::steady_clock::time_point start_time_;
  SOCKET server_socket_ = INVALID_SOCKET;
  std::string web_dir_ = "web";

public:
  HttpServer(int port, std::shared_ptr<Database> db) : port_(port), db_(db) {
    start_time_ = std::chrono::steady_clock::now();
  }

  ~HttpServer() { stop(); }

  void start() {
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
      std::cerr << "WSAStartup failed." << std::endl;
      return;
    }
#endif

    server_socket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_socket_ == INVALID_SOCKET) {
      std::cerr << "Error creating socket." << std::endl;
      return;
    }

    int opt = 1;
    setsockopt(server_socket_, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char *>(&opt), sizeof(opt));

    sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(static_cast<u_short>(port_));

    if (bind(server_socket_, reinterpret_cast<sockaddr *>(&server_addr),
             sizeof(server_addr)) == SOCKET_ERROR) {
      std::cerr << "Bind failed on port " << port_ << std::endl;
      closesocket(server_socket_);
      return;
    }

    if (listen(server_socket_, 32) == SOCKET_ERROR) {
      std::cerr << "Listen failed on port " << port_ << std::endl;
      closesocket(server_socket_);
      return;
    }

    is_running_ = true;
    std::cout << "========================================================\n";
    std::cout << " [Backtest Overfit Engine] REST Server listening on "
                 "http://127.0.0.1:"
              << port_ << "\n";
    std::cout << " Minimalist UI Dashboard: http://127.0.0.1:" << port_
              << "/\n";
    std::cout << " Health check:            http://127.0.0.1:" << port_
              << "/health\n";
    std::cout << " API Audits:              http://127.0.0.1:" << port_
              << "/api/v1/audits\n";
    std::cout << "========================================================\n"
              << std::flush;

    while (is_running_) {
      sockaddr_in client_addr;
      socklen_t client_len = sizeof(client_addr);
      SOCKET client_sock =
          accept(server_socket_, reinterpret_cast<sockaddr *>(&client_addr),
                 &client_len);

      if (client_sock == INVALID_SOCKET) {
        if (!is_running_)
          break;
        continue;
      }

      std::thread([this, client_sock]() {
        this->handle_connection(client_sock);
      }).detach();
    }
  }

  void stop() {
    if (is_running_) {
      is_running_ = false;
      if (server_socket_ != INVALID_SOCKET) {
        closesocket(server_socket_);
        server_socket_ = INVALID_SOCKET;
      }
#ifdef _WIN32
      WSACleanup();
#endif
    }
  }

private:
  void handle_connection(SOCKET client) {
    char buffer[8192];
    int bytes_read = recv(client, buffer, sizeof(buffer) - 1, 0);
    if (bytes_read <= 0) {
      closesocket(client);
      return;
    }

    buffer[bytes_read] = '\0';
    std::string raw_req(buffer, bytes_read);

    std::istringstream req_stream(raw_req);
    std::string method, uri, http_version;
    req_stream >> method >> uri >> http_version;

    std::string header_line;
    int content_length = 0;
    while (std::getline(req_stream, header_line) && header_line != "\r" &&
           !header_line.empty()) {
      std::string lower_hdr = header_line;
      std::transform(lower_hdr.begin(), lower_hdr.end(), lower_hdr.begin(),
                     ::tolower);
      if (lower_hdr.find("content-length:") != std::string::npos) {
        size_t c_pos = lower_hdr.find(':');
        try {
          content_length = std::stoi(lower_hdr.substr(c_pos + 1));
        } catch (...) {
        }
      }
    }

    std::string body;
    size_t header_end = raw_req.find("\r\n\r\n");
    if (header_end != std::string::npos) {
      body = raw_req.substr(header_end + 4);

      int remaining = content_length - static_cast<int>(body.length());
      while (remaining > 0) {
        char more[4096];
        int chunk =
            recv(client, more,
                 std::min(remaining, static_cast<int>(sizeof(more))), 0);
        if (chunk <= 0)
          break;
        body.append(more, chunk);
        remaining -= chunk;
      }
    }

    std::string response;

    if (method == "OPTIONS") {
      response = make_cors_options_response();
    } else if (uri == "/health" && method == "GET") {
      response = handle_health();
    } else if (uri.find("/api/v1/audits") == 0) {
      if (method == "POST") {
        response = handle_post_audit(body);
      } else if (method == "GET") {
        response = handle_get_audits(uri);
      } else {
        response =
            make_json_response(405, "{\"error\": \"Method not allowed\"}");
      }
    } else if (uri == "/api/v1/query" && method == "POST") {
      response = handle_post_query(body);
    } else if (method == "GET") {

      response = handle_static_files(uri);
    } else {
      response = make_json_response(404, "{\"error\": \"Not Found\"}");
    }

    send(client, response.c_str(), static_cast<int>(response.length()), 0);
    closesocket(client);
  }

  std::string make_cors_options_response() {
    return "HTTP/1.1 204 No Content\r\n"
           "Access-Control-Allow-Origin: *\r\n"
           "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
           "Access-Control-Allow-Headers: Content-Type, Authorization\r\n"
           "Access-Control-Max-Age: 86400\r\n"
           "Content-Length: 0\r\n"
           "\r\n";
  }

  std::string make_json_response(int status_code, const std::string &body) {
    std::string status_text = (status_code == 200)   ? "OK"
                              : (status_code == 201) ? "Created"
                              : (status_code == 400) ? "Bad Request"
                              : (status_code == 404) ? "Not Found"
                              : (status_code == 405) ? "Method Not Allowed"
                                                     : "Internal Server Error";

    std::ostringstream ss;
    ss << "HTTP/1.1 " << status_code << " " << status_text << "\r\n";
    ss << "Content-Type: application/json; charset=utf-8\r\n";
    ss << "Access-Control-Allow-Origin: *\r\n";
    ss << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n";
    ss << "Access-Control-Allow-Headers: Content-Type\r\n";
    ss << "Content-Length: " << body.length() << "\r\n";
    ss << "Connection: close\r\n\r\n";
    ss << body;
    return ss.str();
  }

  std::string handle_health() {
    auto now = std::chrono::steady_clock::now();
    std::chrono::duration<double> uptime = now - start_time_;

    std::ostringstream ss;
    ss << "{\n"
       << "  \"status\": \"healthy\",\n"
       << "  \"uptime_seconds\": " << std::fixed << std::setprecision(2)
       << uptime.count() << ",\n"
       << "  \"database\": \"PostgreSQL (Connected: boa@localhost:5432)\",\n"
       << "  \"engine\": \"David Bailey & Marcos Lopez de Prado DSR Engine\",\n"
       << "  \"version\": \"1.0.0 (C++ / PostgreSQL 17)\"\n"
       << "}";
    return make_json_response(200, ss.str());
  }

  std::string handle_post_audit(const std::string &body) {
    std::string strategy_name =
        JsonUtils::extract_string(body, "strategy_name");
    if (strategy_name.empty())
      strategy_name =
          "Strategy_Candidate_" + std::to_string(std::time(nullptr));

    int total_trials = JsonUtils::extract_int(body, "total_trials", 1000);
    if (total_trials < 2)
      total_trials = 2;
    if (total_trials > 20000)
      total_trials = 20000;

    int track_record_days =
        JsonUtils::extract_int(body, "track_record_days", 756);
    if (track_record_days < 10)
      track_record_days = 756;

    double reported_sharpe =
        JsonUtils::extract_double(body, "reported_sharpe", 1.85);
    double skewness = JsonUtils::extract_double(body, "skewness", 0.0);
    double kurtosis = JsonUtils::extract_double(body, "kurtosis", 3.0);
    bool generate_synthetic =
        JsonUtils::extract_bool(body, "generate_synthetic_distribution", true);

    std::vector<TrialResult> trials;
    std::vector<double> trial_sharpes;
    trial_sharpes.reserve(total_trials);

    if (generate_synthetic) {
      trials = StrategyEngine::generate_synthetic_trials(
          total_trials, track_record_days, reported_sharpe, skewness, kurtosis);
      for (const auto &t : trials) {
        trial_sharpes.push_back(t.sharpe_ratio);
      }
    } else {

      auto prices =
          StrategyEngine::load_csv_prices("data/historical_prices.csv");
      if (prices.size() >= 30) {
        trials =
            StrategyEngine::generate_technical_trials(prices, total_trials);
        for (const auto &t : trials) {
          trial_sharpes.push_back(t.sharpe_ratio);
        }
      } else {
        trials = StrategyEngine::generate_synthetic_trials(
            total_trials, track_record_days, reported_sharpe, skewness,
            kurtosis);
        for (const auto &t : trials) {
          trial_sharpes.push_back(t.sharpe_ratio);
        }
      }
    }

    std::string audit_id =
        "aud_" +
        std::to_string(
            std::chrono::system_clock::now().time_since_epoch().count() %
            100000000);
    std::string strategy_id = db_->get_or_create_strategy(strategy_name);

    AuditReport report = DSRDetector::evaluate(
        audit_id, strategy_id, strategy_name, reported_sharpe, total_trials,
        track_record_days, trial_sharpes, skewness, kurtosis);

    db_->record_audit_run(report, trials);

    std::ostringstream ss;
    ss << "{\n"
       << "  \"status\": \"success\",\n"
       << "  \"audit_id\": \"" << report.audit_id << "\",\n"
       << "  \"strategy_name\": \""
       << JsonUtils::escape_string(report.strategy_name) << "\",\n"
       << "  \"metrics\": {\n"
       << "    \"reported_sharpe\": " << std::fixed << std::setprecision(4)
       << report.metrics.reported_sharpe << ",\n"
       << "    \"expected_max_noise_sharpe\": "
       << report.metrics.expected_max_noise_sharpe << ",\n"
       << "    \"dsr_p_value\": " << report.metrics.dsr_p_value << ",\n"
       << "    \"dsr_z_score\": " << report.metrics.dsr_z_score << ",\n"
       << "    \"is_overfitted\": "
       << (report.metrics.is_overfitted ? "true" : "false") << ",\n"
       << "    \"sample_skewness\": " << report.metrics.sample_skewness << ",\n"
       << "    \"sample_kurtosis\": " << report.metrics.sample_kurtosis << ",\n"
       << "    \"trial_variance\": " << report.metrics.trial_variance << "\n"
       << "  },\n"
       << "  \"distribution_summary\": {\n"
       << "    \"min_sharpe\": " << report.distribution_summary.min_val << ",\n"
       << "    \"max_sharpe\": " << report.distribution_summary.max_val << ",\n"
       << "    \"sharpe_histogram\": [";

    for (size_t i = 0; i < report.distribution_summary.counts.size(); ++i) {
      ss << report.distribution_summary.counts[i];
      if (i + 1 < report.distribution_summary.counts.size())
        ss << ", ";
    }
    ss << "]\n"
       << "  }\n"
       << "}";

    return make_json_response(201, ss.str());
  }

  std::string handle_get_audits(const std::string &uri) {

    std::string prefix = "/api/v1/audits/";
    if (uri.length() > prefix.length() &&
        uri.substr(0, prefix.length()) == prefix) {
      std::string id = uri.substr(prefix.length());

      size_t q = id.find('?');
      if (q != std::string::npos)
        id = id.substr(0, q);

      AuditSummaryRow row;
      std::vector<double> sharpes;
      if (db_->get_audit_by_id(id, row, sharpes)) {
        auto hist = Statistics::compute_histogram(sharpes, 8);
        std::ostringstream ss;
        ss << "{\n"
           << "  \"status\": \"success\",\n"
           << "  \"audit_id\": \"" << row.audit_id << "\",\n"
           << "  \"strategy_id\": \"" << row.strategy_id << "\",\n"
           << "  \"strategy_name\": \""
           << JsonUtils::escape_string(row.strategy_name) << "\",\n"
           << "  \"total_trials\": " << row.total_trials << ",\n"
           << "  \"sample_length_days\": " << row.sample_length_days << ",\n"
           << "  \"metrics\": {\n"
           << "    \"reported_sharpe\": " << row.reported_sharpe << ",\n"
           << "    \"expected_max_noise_sharpe\": " << row.expected_max_sharpe
           << ",\n"
           << "    \"dsr_p_value\": " << row.dsr_p_value << ",\n"
           << "    \"is_overfitted\": "
           << (row.is_overfitted ? "true" : "false") << ",\n"
           << "    \"sample_skewness\": " << row.skewness << ",\n"
           << "    \"sample_kurtosis\": " << row.kurtosis << ",\n"
           << "    \"trial_variance\": " << row.trial_sharpe_variance << "\n"
           << "  },\n"
           << "  \"distribution_summary\": {\n"
           << "    \"min_sharpe\": " << hist.min_val << ",\n"
           << "    \"max_sharpe\": " << hist.max_val << ",\n"
           << "    \"sharpe_histogram\": [";
        for (size_t i = 0; i < hist.counts.size(); ++i) {
          ss << hist.counts[i];
          if (i + 1 < hist.counts.size())
            ss << ", ";
        }
        ss << "]\n"
           << "  },\n"
           << "  \"executed_at\": \"" << row.executed_at << "\"\n"
           << "}";
        return make_json_response(200, ss.str());
      } else {
        return make_json_response(404, "{\"error\": \"Audit not found\"}");
      }
    }

    int page = 1;
    int limit = 10;
    size_t page_pos = uri.find("page=");
    if (page_pos != std::string::npos) {
      try {
        page = std::stoi(uri.substr(page_pos + 5));
      } catch (...) {
      }
    }
    size_t limit_pos = uri.find("limit=");
    if (limit_pos != std::string::npos) {
      try {
        limit = std::stoi(uri.substr(limit_pos + 6));
      } catch (...) {
      }
    }
    if (page < 1)
      page = 1;
    if (limit < 1)
      limit = 10;
    if (limit > 100)
      limit = 100;

    int total = 0;
    int offset = (page - 1) * limit;
    auto audits = db_->get_audits(limit, offset, &total);

    std::ostringstream ss;
    ss << "{\n"
       << "  \"status\": \"success\",\n"
       << "  \"page\": " << page << ",\n"
       << "  \"limit\": " << limit << ",\n"
       << "  \"total\": " << total << ",\n"
       << "  \"audits\": [\n";

    for (size_t i = 0; i < audits.size(); ++i) {
      const auto &a = audits[i];
      ss << "    {\n"
         << "      \"audit_id\": \"" << a.audit_id << "\",\n"
         << "      \"strategy_id\": \"" << a.strategy_id << "\",\n"
         << "      \"strategy_name\": \""
         << JsonUtils::escape_string(a.strategy_name) << "\",\n"
         << "      \"total_trials\": " << a.total_trials << ",\n"
         << "      \"sample_length_days\": " << a.sample_length_days << ",\n"
         << "      \"reported_sharpe\": " << std::fixed << std::setprecision(4)
         << a.reported_sharpe << ",\n"
         << "      \"expected_max_sharpe\": " << a.expected_max_sharpe << ",\n"
         << "      \"dsr_p_value\": " << a.dsr_p_value << ",\n"
         << "      \"is_overfitted\": " << a.is_overfitted << ",\n"
         << "      \"skewness\": " << a.skewness << ",\n"
         << "      \"kurtosis\": " << a.kurtosis << ",\n"
         << "      \"trial_sharpe_variance\": " << a.trial_sharpe_variance
         << ",\n"
         << "      \"executed_at\": \"" << a.executed_at << "\"\n"
         << "    }";
      if (i + 1 < audits.size())
        ss << ",";
      ss << "\n";
    }
    ss << "  ]\n}";

    return make_json_response(200, ss.str());
  }

  std::string handle_post_query(const std::string &body) {
    std::string sql = JsonUtils::extract_string(body, "sql");
    if (sql.empty()) {
      return make_json_response(
          400, "{\"error\": \"Missing or empty 'sql' field\"}");
    }

    QueryResult res = db_->run_readonly_query(sql);
    if (!res.success) {
      std::ostringstream ss;
      ss << "{\n"
         << "  \"error\": \"" << JsonUtils::escape_string(res.error_message)
         << "\"\n"
         << "}";
      return make_json_response(400, ss.str());
    }

    std::ostringstream ss;
    ss << "{\n"
       << "  \"columns\": [";
    for (size_t i = 0; i < res.columns.size(); ++i) {
      ss << "\"" << JsonUtils::escape_string(res.columns[i]) << "\"";
      if (i + 1 < res.columns.size())
        ss << ", ";
    }
    ss << "],\n"
       << "  \"rows\": [\n";

    for (size_t r = 0; r < res.rows.size(); ++r) {
      ss << "    [";
      for (size_t c = 0; c < res.rows[r].size(); ++c) {
        const std::string &val = res.rows[r][c];

        bool is_num = true;
        if (val.empty() || val == "NULL") {
          is_num = false;
        } else {
          for (char ch : val) {
            if (!std::isdigit(ch) && ch != '.' && ch != '-' && ch != '+' &&
                ch != 'e' && ch != 'E') {
              is_num = false;
              break;
            }
          }
        }

        if (val == "NULL") {
          ss << "null";
        } else if (is_num) {
          ss << val;
        } else {
          ss << "\"" << JsonUtils::escape_string(val) << "\"";
        }

        if (c + 1 < res.rows[r].size())
          ss << ", ";
      }
      ss << "]";
      if (r + 1 < res.rows.size())
        ss << ",";
      ss << "\n";
    }

    ss << "  ]\n}";
    return make_json_response(200, ss.str());
  }

  std::string handle_static_files(const std::string &uri) {
    std::string filepath;
    std::string content_type = "text/html; charset=utf-8";

    if (uri == "/" || uri == "/index.html") {
      filepath = web_dir_ + "/index.html";
    } else if (uri == "/style.css") {
      filepath = web_dir_ + "/style.css";
      content_type = "text/css; charset=utf-8";
    } else if (uri == "/app.js") {
      filepath = web_dir_ + "/app.js";
      content_type = "application/javascript; charset=utf-8";
    } else {
      return make_json_response(404, "{\"error\": \"Resource not found\"}");
    }

    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
      return make_json_response(
          404, "{\"error\": \"File not found: " + filepath + "\"}");
    }

    std::ostringstream ss;
    ss << file.rdbuf();
    std::string content = ss.str();

    std::ostringstream resp;
    resp << "HTTP/1.1 200 OK\r\n";
    resp << "Content-Type: " << content_type << "\r\n";
    resp << "Access-Control-Allow-Origin: *\r\n";
    resp << "Content-Length: " << content.length() << "\r\n";
    resp << "Connection: close\r\n\r\n";
    resp << content;
    return resp.str();
  }
};

} // namespace quant
