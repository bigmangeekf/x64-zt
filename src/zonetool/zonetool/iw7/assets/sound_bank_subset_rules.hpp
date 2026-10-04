#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>
#include <json.hpp>

// Asset-free rules shared by the importer and its synthetic regression tests.
namespace zonetool::iw7::sound_bank_subset
{
	inline bool is_catalog_donor(const std::string_view name)
	{
		return name == "cp_zmb" || name == "cp_rave" || name == "cp_disco" ||
			name == "cp_town" || name == "cp_final";
	}

	inline uint32_t uint32_field(const nlohmann::json& value, const char* field)
	{
		// get<uint32_t>() alone silently narrows signed, fractional and large numbers.
		if (!value.is_number_integer() ||
			(!value.is_number_unsigned() && value.get<int64_t>() < 0) ||
			value.get<uint64_t>() > std::numeric_limits<uint32_t>::max())
			throw std::runtime_error(std::string(field) + " must be an integer in [0, 4294967295]");
		return value.get<uint32_t>();
	}

	struct configuration
	{
		std::string donor;
		std::vector<std::string> prefixes, roots, required, allowed, external;
		std::vector<uint32_t> external_ducks;
	};

	inline configuration read_configuration(const nlohmann::json& data, const std::string& name)
	{
		const std::unordered_set<std::string> keys{"version", "donor", "prefixes", "aliases",
			"requiredAliases", "allowedDependencies", "externalAliases", "externalDuckIds"};
		if (!data.is_object()) throw std::runtime_error("Soundbank subset must be an object");
		for (const auto& item : data.items())
			if (!keys.contains(item.key())) throw std::runtime_error("Unknown soundbank subset field: " + item.key());
		if (uint32_field(data.at("version"), "version") != 1)
			throw std::runtime_error("Unsupported soundbank subset version");
		configuration result;
		result.donor = data.at("donor").get<std::string>();
		if (result.donor.empty() || result.donor == name)
			throw std::runtime_error("Subset needs a distinct donor bank");
		const auto strings = [&](const char* key)
		{
			auto values = data.value(key, std::vector<std::string>{});
			std::unordered_set<std::string> unique;
			for (const auto& value : values)
				if (value.empty() || !unique.insert(value).second)
					throw std::runtime_error("Empty or repeated subset selector in " + std::string(key));
			return values;
		};
		result.prefixes = strings("prefixes"); result.roots = strings("aliases");
		result.required = strings("requiredAliases"); result.allowed = strings("allowedDependencies");
		result.external = strings("externalAliases");
		if (result.prefixes.empty() && result.roots.empty()) throw std::runtime_error("Soundbank subset has no roots");
		if (data.contains("externalDuckIds"))
		{
			const auto& ducks = data.at("externalDuckIds");
			if (!ducks.is_array()) throw std::runtime_error("externalDuckIds must be an array");
			std::unordered_set<uint32_t> unique;
			for (const auto& duck : ducks)
			{
				const auto id = uint32_field(duck, "externalDuckIds entry");
				if (!unique.insert(id).second) throw std::runtime_error("Repeated externalDuckIds entry");
				result.external_ducks.push_back(id);
			}
		}
		return result;
	}

	template <typename Entry>
	inline std::vector<Entry> build_alias_index(const std::vector<uint32_t>& ids)
	{
		constexpr auto empty = std::numeric_limits<unsigned short>::max();
		if (ids.empty() || ids.size() >= empty) throw std::runtime_error("Soundbank subset index capacity invalid");
		if (std::unordered_set<uint32_t>(ids.begin(), ids.end()).size() != ids.size())
			throw std::runtime_error("Soundbank subset has duplicate alias hashes");
		std::vector<Entry> entries(ids.size());
		for (auto& entry : entries) { entry.value = empty; entry.next = empty; }
		std::vector<bool> indexed(ids.size(), false);
		// Reserve all home buckets before assigning overflow slots.
		for (unsigned short i = 0; i < ids.size(); ++i)
		{
			const auto bucket = ids[i] % ids.size();
			if (entries[bucket].value == empty) { entries[bucket].value = i; indexed[i] = true; }
		}
		for (unsigned short i = 0; i < ids.size(); ++i)
		{
			if (indexed[i]) continue;
			auto tail = static_cast<unsigned short>(ids[i] % ids.size());
			while (entries[tail].next != empty) tail = entries[tail].next;
			unsigned short free_slot = empty;
			for (unsigned int j = 0; j < ids.size(); ++j)
				if (entries[j].value == empty) { free_slot = static_cast<unsigned short>(j); break; }
			if (free_slot == empty) throw std::runtime_error("Soundbank subset index exhausted");
			entries[tail].next = free_slot;
			entries[free_slot].value = i;
		}
		for (unsigned short i = 0; i < ids.size(); ++i)
		{
			auto slot = static_cast<unsigned short>(ids[i] % ids.size());
			unsigned int steps = 0;
			while (slot != empty && entries[slot].value != i && steps++ < ids.size()) slot = entries[slot].next;
			if (slot == empty || steps >= ids.size()) throw std::runtime_error("Soundbank subset index verification failed");
		}
		return entries;
	}
}
