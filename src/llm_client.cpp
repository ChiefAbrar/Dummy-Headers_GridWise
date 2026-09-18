#include "llm_client.hpp"
#include "validation.hpp"
#include <boost/json.hpp>
#include <algorithm>
#include <curl/curl.h>
#include <chrono>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace gridwise {
namespace {

size_t curl_write(void* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* s = static_cast<std::string*>(userdata);
    const std::size_t n = size * nmemb;
    if (s->size() + n > 2 * 1024 * 1024) return 0;
    s->append(static_cast<const char*>(ptr), n);
    return n;
}

std::string join_url(const std::string& base, const std::string& path) {
    if (base.empty()) return path;
    if (base.back() == '/' && !path.empty() && path.front() == '/') return base.substr(0, base.size() - 1) + path;
    if (base.back() != '/' && !path.empty() && path.front() != '/') return base + "/" + path;
    return base + path;
}

boost::json::value parse_json_text(const std::string& text) {
    boost::system::error_code ec;
    auto v = boost::json::parse(text, ec);
    if (ec) throw std::runtime_error("invalid JSON from model");
    return v;
}

}

LlmClient::LlmClient(const Config& config) : config_(config), llm_slots_(std::make_unique<std::counting_semaphore<64>>(static_cast<ptrdiff_t>(std::min<std::size_t>(config.llm_concurrency, 64)))) {
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

std::string LlmClient::make_cache_key(const Scenario& s) const {
    std::ostringstream oss;
    oss << s.battery.capacity_kwh << '|' << s.battery.minimum_energy_kwh << '|';
    for (const auto& note : s.operator_notes) oss << note.size() << ':' << note << '|';
    return oss.str();
}

std::string LlmClient::system_prompt() const {
    if (!config_.llm_system.empty()) return config_.llm_system;
    return R"PROMPT(You are the operator-note interpreter for the GridWise energy optimizer.
Interpret every operator note independently and return ONLY JSON matching this exact shape:
{"interpretations":[{"note_index":0,"applies":true,"directive_type":"solar_reduction","structured_adjustment":{"hours":[13,14],"factor":0.2},"explanation":"..."}]}

Allowed directive_type values are exactly:
- solar_reduction: usable solar fraction remaining during listed whole hours; adjustment {hours, factor}.
- minimum_battery_reserve: minimum battery energy after each listed hour; adjustment {hours, minimum_energy_kwh}.
- no_charge_window: charging forbidden during listed hours; adjustment {hours}.
- no_discharge_window: discharging forbidden during listed hours; adjustment {hours}.
- max_grid_window: grid import cap in listed hours; adjustment {hours, max_grid_kwh}.
- no_op: note has no effect on this 24-hour energy schedule; adjustment null and applies=false.

Rules:
1. Return exactly one interpretation for every note, in note_index order 0..N-1.
2. A non-no_op directive must have applies=true. no_op must have applies=false and null adjustment.
3. Hours use whole-hour start-inclusive/end-exclusive semantics. Example: 1 PM to 3 PM => [13,14].
4. For solar reduction, the factor is what remains. "80% reduction" => factor 0.2; "20% of normal" => factor 0.2.
5. For percentage battery reserves, calculate the percentage of the supplied battery capacity.
6. Do not invent demand, solar, tariffs, battery limits, or unsupported directive types.
7. A distractor or administrative note unrelated to the 24-hour energy schedule is no_op.
8. Numeric values must be ordinary finite numbers. Do not use null, NaN, infinity, or text for numeric fields.
9. Do not explain your reasoning outside the JSON object.)PROMPT";
}

boost::json::value LlmClient::interpret_json(const Scenario& scenario) {
    const std::string key = make_cache_key(scenario);
    if (config_.llm_cache_entries > 0) {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        auto it = cache_.find(key);
        if (it != cache_.end()) {
            lru_.erase(it->second.second);
            lru_.push_front(key);
            it->second.second = lru_.begin();
            return parse_json_text(it->second.first);
        }
    }

    boost::json::object payload;
    payload["model"] = config_.llm_model;
    payload["temperature"] = 0.0;
    boost::json::array messages;
    messages.push_back(boost::json::object{{"role", "system"}, {"content", system_prompt()}});
    boost::json::object user;
    user["battery_capacity_kwh"] = scenario.battery.capacity_kwh;
    user["base_minimum_energy_kwh"] = scenario.battery.minimum_energy_kwh;
    boost::json::array notes;
    for (std::size_t i = 0; i < scenario.operator_notes.size(); ++i) {
        boost::json::object n;
        n["note_index"] = static_cast<std::int64_t>(i);
        n["operator_note"] = scenario.operator_notes[i];
        notes.push_back(std::move(n));
    }
    user["operator_notes"] = std::move(notes);
    messages.push_back(boost::json::object{{"role", "user"}, {"content", boost::json::serialize(user)}});
    payload["messages"] = std::move(messages);
    payload["response_format"] = boost::json::object{{"type", "json_object"}};

    const std::string body = boost::json::serialize(payload);
    const std::string url = join_url(config_.llm_base_url, config_.llm_chat_path);

    llm_slots_->acquire();
    struct SlotGuard { std::counting_semaphore<64>* s; ~SlotGuard() { s->release(); } } slot_guard{llm_slots_.get()};

    std::string response_body;
    long http_code = 0;
    CURL* curl = curl_easy_init();
    if (!curl) throw std::runtime_error("llm client init failed");

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    if (!config_.llm_api_key.empty()) {
        const std::string h = "Authorization: Bearer " + config_.llm_api_key;
        headers = curl_slist_append(headers, h.c_str());
    }

    auto cleanup = [&]() {
        if (headers) curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    };

    CURLcode rc = CURLE_FAILED_INIT;
    for (int attempt = 0; attempt <= config_.llm_retries; ++attempt) {
        response_body.clear();
        curl_easy_reset(curl);
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.data());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, config_.llm_connect_timeout_ms);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, config_.llm_timeout_ms);
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
        rc = curl_easy_perform(curl);
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        if (rc == CURLE_OK && http_code >= 200 && http_code < 300) break;
        if (attempt < config_.llm_retries) std::this_thread::sleep_for(std::chrono::milliseconds(75 * (attempt + 1)));
    }
    cleanup();

    if (rc != CURLE_OK || http_code < 200 || http_code >= 300) {
        throw std::runtime_error("llm provider request failed");
    }

    const auto outer = parse_json_text(response_body);
    if (!outer.is_object()) throw std::runtime_error("llm provider response invalid");
    const auto* choices = outer.as_object().if_contains("choices");
    if (!choices || !choices->is_array() || choices->as_array().empty()) throw std::runtime_error("llm provider response missing choices");
    const auto* choice = choices->as_array()[0].is_object() ? &choices->as_array()[0].as_object() : nullptr;
    if (!choice) throw std::runtime_error("llm provider choice invalid");
    const auto* message = choice->if_contains("message");
    if (!message || !message->is_object()) throw std::runtime_error("llm provider response missing message");
    const auto* content = message->as_object().if_contains("content");
    if (!content || !content->is_string()) throw std::runtime_error("llm provider response missing content");

    const auto parsed = parse_json_text(std::string(content->as_string()));

    if (config_.llm_cache_entries > 0) {
        const std::string serialized = boost::json::serialize(parsed);
        std::lock_guard<std::mutex> lock(cache_mutex_);
        lru_.push_front(key);
        cache_[key] = {serialized, lru_.begin()};
        while (cache_.size() > config_.llm_cache_entries) {
            const std::string old = lru_.back();
            lru_.pop_back();
            cache_.erase(old);
        }
    }
    return parsed;
}

InterpretationResult LlmClient::interpret(const Scenario& scenario) {
    const std::string key = make_cache_key(scenario);
    for (int attempt = 0; attempt <= config_.llm_retries; ++attempt) {
        try {
            auto raw = interpret_json(scenario);
            InterpretationResult result;
            const auto check = validate_directives(scenario, raw, result);
            if (!check.ok) throw std::runtime_error("model output failed guardrails");
            return result;
        } catch (const std::exception&) {
            if (config_.llm_cache_entries > 0) {
                std::lock_guard<std::mutex> lock(cache_mutex_);
                auto it = cache_.find(key);
                if (it != cache_.end()) {
                    lru_.erase(it->second.second);
                    cache_.erase(it);
                }
            }
            if (attempt >= config_.llm_retries) throw;
        }
    }
    throw std::runtime_error("unreachable");
}

}