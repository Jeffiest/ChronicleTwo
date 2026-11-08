#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <optional>
#include <random>
#include <ranges>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <exception>

#include "moonmod/api.hpp"

namespace moonmod {

// Uniform [0, 1). Not std::rand(): the Chronicle port replaces rand() with the PS2 version, whose
// range is 0..2^31-1, not RAND_MAX, so rand()/RAND_MAX multiplied damage thousands of times.
inline double FormulaRandom01() {
    static std::mt19937 engine{std::random_device{}()};
    return std::uniform_real_distribution<double>(0.0, 1.0)(engine);
}

// A bounded modifier registry shared by both game adapters. Native hit
// pipelines call Apply() only after their own defense, elemental, and damage-block rules.
class CombatDamageFormulaRegistry {
public:
    ApiCallResult Register(std::string_view owner, const Json &arguments) {
        if (!IsValidModId(owner) || !arguments.is_array() || arguments.size() != 1 ||
            !arguments[0].is_object()) {
            return Invalid("expected one formula object owned by the calling mod");
        }

        const Json &request = arguments[0];
        for (auto field = request.begin(); field != request.end(); ++field) {
            if (field.key() != "id" && field.key() != "operation" && field.key() != "value" &&
                field.key() != "priority" && field.key() != "chance" && field.key() != "min_value" && field.key() != "max_value" &&
                field.key() != "weapon_id" && field.key() != "native_model_id" &&
                field.key() != "rate_per_second" && field.key() != "reset_after_frames" &&
                field.key() != "character_id" && field.key() != "target_hp_below_percent") {
                return Invalid("formula contains an unsupported field");
            }
        }
        if (!request.contains("id") || !request["id"].is_string() ||
            !request.contains("operation") || !request["operation"].is_string() ||
            !request.contains("value") || !request["value"].is_number() ||
            (request.contains("priority") && !request["priority"].is_number_integer())) {
            return Invalid("expected {id, operation, value, priority = optional integer}");
        }

        const std::string local_id = request["id"].get<std::string>();
        if (local_id.empty() || local_id.size() > 64 || local_id.find(':') != std::string::npos ||
            !std::all_of(local_id.begin(), local_id.end(), [](unsigned char character) {
                return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
                       (character >= '0' && character <= '9') || character == '.' || character == '_' ||
                       character == '-';
            })) {
            return Invalid("formula id must be a local identifier of 1-64 ASCII letters, digits, '.', '_' or '-'");
        }

        Formula formula;
        formula.owner = std::string(owner);
        formula.id = local_id;
        formula.operation = request["operation"].get<std::string>();
        try {
            formula.value = request["value"].get<double>();
        } catch (const std::exception &) {
            return Invalid("formula value is outside the numeric range");
        }
        if (request.contains("priority")) {
            try {
                const std::int64_t priority = request["priority"].get<std::int64_t>();
                if (priority < -1000 || priority > 1000) return Invalid("priority must be in [-1000, 1000]");
                formula.priority = static_cast<int>(priority);
            } catch (const std::exception &) {
                return Invalid("priority is outside the integer range");
            }
        }
        if (!std::isfinite(formula.value)) return Invalid("formula value must be finite");
        if (formula.operation == "ramp_multiply") {
            if (request.contains("chance") || request.contains("min_value") || request.contains("max_value") ||
                !request.contains("rate_per_second") || !request["rate_per_second"].is_number() ||
                !request.contains("reset_after_frames") || !request["reset_after_frames"].is_number_integer()) {
                return Invalid("ramp_multiply requires rate_per_second and integer reset_after_frames, and does not accept chance or random bounds");
            }
            try {
                formula.rate_per_second = request["rate_per_second"].get<double>();
                const std::int64_t reset_after = request["reset_after_frames"].get<std::int64_t>();
                if (reset_after < 1 || reset_after > 36000) return Invalid("reset_after_frames must be in [1, 36000]");
                formula.reset_after_frames = static_cast<std::uint64_t>(reset_after);
            } catch (const std::exception &) {
                return Invalid("ramp_multiply parameters are outside their numeric range");
            }
            if (!std::isfinite(formula.rate_per_second) || formula.rate_per_second < 0.0 || formula.rate_per_second > 10.0 ||
                formula.value < 1.0 || formula.value > 100.0) {
                return Invalid("ramp_multiply requires 1 <= value <= 100 and rate_per_second in [0, 10]");
            }
        } else if (request.contains("rate_per_second") || request.contains("reset_after_frames")) {
            return Invalid("rate_per_second and reset_after_frames are only valid with operation = 'ramp_multiply'");
        }
        if (formula.operation == "random_multiply") {
            if (request.contains("chance") || !request.contains("min_value") || !request.contains("max_value") ||
                !request["min_value"].is_number() || !request["max_value"].is_number()) {
                return Invalid("random_multiply requires min_value and max_value, and does not accept chance");
            }
            try {
                formula.min_value = request["min_value"].get<double>();
                formula.max_value = request["max_value"].get<double>();
            } catch (const std::exception &) {
                return Invalid("random_multiply bounds must be finite numbers");
            }
            if (!std::isfinite(formula.min_value) || !std::isfinite(formula.max_value) ||
                formula.min_value < 0.0 || formula.max_value > 100.0 || formula.min_value > formula.max_value) {
                return Invalid("random_multiply requires 0 <= min_value <= max_value <= 100");
            }
        } else if (formula.operation == "chance_multiply") {
            if (!request.contains("chance") || !request["chance"].is_number()) {
                return Invalid("chance_multiply requires chance = 0..100");
            }
            try {
                formula.chance = request["chance"].get<double>();
            } catch (const std::exception &) {
                return Invalid("chance must be a finite number from 0 to 100");
            }
            if (!std::isfinite(formula.chance) || formula.chance < 0.0 || formula.chance > 100.0 ||
                formula.value < 0.0 || formula.value > 100.0) {
                return Invalid("chance_multiply requires chance = 0..100 and value = 0..100");
            }
        } else if (request.contains("chance") || request.contains("min_value") || request.contains("max_value")) {
            return Invalid("chance is only valid with operation = 'chance_multiply'");
        }
        for (const auto field : {std::string_view("weapon_id"), std::string_view("native_model_id"),
                                 std::string_view("character_id")}) {
            const std::string name(field);
            if (request.contains(name) && (!request.at(name).is_number_integer() ||
                request.at(name).get<std::int64_t>() < 0 || request.at(name).get<std::int64_t>() > 1000000)) {
                return Invalid(std::string(field) + " must be an integer from 0 to 1000000");
            }
        }
        if (request.contains("weapon_id")) formula.weapon_id = request.at("weapon_id").get<int>();
        if (request.contains("native_model_id")) formula.native_model_id = request.at("native_model_id").get<int>();
        if (request.contains("character_id")) formula.character_id = request.at("character_id").get<int>();
        if (request.contains("target_hp_below_percent")) {
            const auto &threshold = request.at("target_hp_below_percent");
            if (!threshold.is_number()) return Invalid("target_hp_below_percent must be between 0 and 100");
            try {
                formula.target_hp_below_percent = threshold.get<double>();
            } catch (const std::exception &) {
                return Invalid("target_hp_below_percent must be finite and between 0 and 100");
            }
            if (!std::isfinite(*formula.target_hp_below_percent) || *formula.target_hp_below_percent < 0.0 ||
                *formula.target_hp_below_percent > 100.0) {
                return Invalid("target_hp_below_percent must be finite and between 0 and 100");
            }
        }
        if (formula.operation == "add") {
            if (formula.value < -1000000.0 || formula.value > 1000000.0) {
                return Invalid("add value must be in [-1000000, 1000000]");
            }
        } else if (formula.operation == "multiply") {
            if (formula.value < 0.0 || formula.value > 100.0) {
                return Invalid("multiply value must be in [0, 100]");
            }
        } else if (formula.operation == "set") {
            if (formula.value < 0.0 || formula.value > 1000000.0) {
                return Invalid("set value must be in [0, 1000000]");
            }
        } else if (formula.operation == "ramp_multiply") {
            // The charge factor is calculated from matching hit-frame timestamps in Apply().
        } else if (formula.operation != "chance_multiply" && formula.operation != "random_multiply") {
            return Invalid("operation must be 'add', 'multiply', 'set', 'chance_multiply', 'random_multiply', or 'ramp_multiply'");
        }

        const auto existing = std::ranges::find_if(formulas_, [&formula](const auto &entry) {
            return entry.second.owner == formula.owner && entry.second.id == formula.id;
        });
        if (existing != formulas_.end()) {
            if (existing->second.SameDefinition(formula)) {
                return {ApiCallStatus::Ok,
                        {{"id", formula.owner + ":" + formula.id}, {"operation", formula.operation},
                         {"value", formula.value}, {"priority", formula.priority}, {"idempotent", true}}, {}};
            }
            existing->second = formula;
            return {ApiCallStatus::Ok,
                    {{"id", formula.owner + ":" + formula.id}, {"operation", formula.operation},
                     {"value", formula.value}, {"priority", formula.priority}, {"idempotent", false}}, {}};
        }

        const std::size_t owner_count = std::ranges::count_if(formulas_, [&formula](const auto &entry) {
            return entry.second.owner == formula.owner;
        });
        if (owner_count >= 64 || formulas_.size() >= 512) {
            return Invalid(owner_count >= 64 ? "a mod may register at most 64 combat formulas"
                                             : "the game supports at most 512 combat formulas");
        }

        const auto key = std::make_tuple(formula.priority, formula.owner, formula.id);
        formulas_.emplace(key, formula);
        return {ApiCallStatus::Ok,
                {{"id", formula.owner + ":" + formula.id}, {"operation", formula.operation},
                 {"value", formula.value}, {"priority", formula.priority}, {"idempotent", false}}, {}};
    }

    float Apply(float damage, int weapon_id = -1, int native_model_id = -1, int character_id = -1,
                int target_hp = -1, int target_max_hp = -1, std::uint64_t frame = 0) const {
        if (!std::isfinite(damage) || damage <= 0.0f) return 0.0f;
        double result = std::min<double>(damage, 1000000.0);
        for (const auto &[key, formula] : formulas_) {
            (void)key;
            if (formula.weapon_id && *formula.weapon_id != weapon_id) continue;
            if (formula.native_model_id && *formula.native_model_id != native_model_id) continue;
            if (formula.character_id && *formula.character_id != character_id) continue;
            if (formula.target_hp_below_percent) {
                if (target_hp < 0 || target_max_hp <= 0 ||
                    100.0 * target_hp >= *formula.target_hp_below_percent * target_max_hp) continue;
            }
            if (formula.operation == "chance_multiply") {
                const double roll = 100.0 * FormulaRandom01();
                if (roll >= formula.chance) continue;
                result *= formula.value;
            } else if (formula.operation == "random_multiply") {
                const double unit = FormulaRandom01();
                result *= formula.min_value + (formula.max_value - formula.min_value) * unit;
            } else if (formula.operation == "ramp_multiply") {
                if (!formula.ramp_started || frame < formula.ramp_last_frame ||
                    frame - formula.ramp_last_frame > formula.reset_after_frames) {
                    formula.ramp_start_frame = frame;
                    formula.ramp_started = true;
                }
                formula.ramp_last_frame = frame;
                const double elapsed_seconds = static_cast<double>(frame - formula.ramp_start_frame) / 60.0;
                result *= std::min(formula.value, 1.0 + formula.rate_per_second * elapsed_seconds);
            } else if (formula.operation == "add") result += formula.value;
            else if (formula.operation == "multiply") result *= formula.value;
            else result = formula.value;
            if (!std::isfinite(result) || result <= 0.0) return 0.0f;
            result = std::min(result, 1000000.0);
        }
        return static_cast<float>(result);
    }

    void RemoveOwner(std::string_view owner) {
        std::erase_if(formulas_, [owner](const auto &entry) { return entry.second.owner == owner; });
    }

    void Clear() { formulas_.clear(); }

private:
    struct Formula {
        std::string owner;
        std::string id;
        std::string operation;
        double value = 0.0;
        double chance = 100.0;
        double min_value = 1.0;
        double max_value = 1.0;
        double rate_per_second = 0.0;
        std::uint64_t reset_after_frames = 0;
        mutable std::uint64_t ramp_start_frame = 0;
        mutable std::uint64_t ramp_last_frame = 0;
        mutable bool ramp_started = false;
        int priority = 0;
        std::optional<int> weapon_id;
        std::optional<int> native_model_id;
        std::optional<int> character_id;
        std::optional<double> target_hp_below_percent;

        bool SameDefinition(const Formula &other) const {
            return owner == other.owner && id == other.id && operation == other.operation &&
                   value == other.value && chance == other.chance && min_value == other.min_value && max_value == other.max_value &&
                   priority == other.priority && weapon_id == other.weapon_id &&
                   native_model_id == other.native_model_id && character_id == other.character_id &&
                   target_hp_below_percent == other.target_hp_below_percent &&
                   rate_per_second == other.rate_per_second && reset_after_frames == other.reset_after_frames;
        }
    };

    static ApiCallResult Invalid(std::string message) {
        return {ApiCallStatus::InvalidArgument, nullptr, std::move(message)};
    }

    static bool IsValidModId(std::string_view id) {
        if (id.empty() || id.size() > 64) return false;
        return std::all_of(id.begin(), id.end(), [](unsigned char character) {
            return (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') ||
                   character == '.' || character == '_' || character == '-';
        });
    }

    mutable std::map<std::tuple<int, std::string, std::string>, Formula> formulas_;
};

inline float ApplyCombatDamageBonuses(float damage, const Json &bonuses) {
    if (!std::isfinite(damage) || damage <= 0.0f) return 0.0f;
    double result = std::min<double>(damage, 1000000.0);
    if (!bonuses.is_array()) return static_cast<float>(result);
    for (const Json &bonus : bonuses) {
        if (!bonus.is_object() || !bonus.contains("rank") || !bonus["rank"].is_number_integer() ||
            !bonus.contains("per_rank") || !bonus["per_rank"].is_number()) {
            continue;
        }
        const std::int64_t rank = bonus["rank"].get<std::int64_t>();
        const double per_rank = bonus["per_rank"].get<double>();
        if (rank <= 0 || rank > 99 || !std::isfinite(per_rank) || per_rank <= 0.0 || per_rank > 1.0) continue;
        result *= 1.0 + per_rank * static_cast<double>(rank);
        if (!std::isfinite(result) || result >= 1000000.0) {
            result = 1000000.0;
            break;
        }
    }
    return static_cast<float>(result);
}

} // namespace moonmod
