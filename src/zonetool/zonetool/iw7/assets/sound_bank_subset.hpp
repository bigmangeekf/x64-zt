#pragma once

#include "sound_bank_subset_rules.hpp"
#include "sound_bank_subset_report.hpp"
#include <utils/cryptography.hpp>

// Cloning preserves retail SAB addresses. The donor remains owned by the DB.
// This path never dumps audio, creates a SAB, or copies a donor map's mixers.
namespace zonetool::iw7::sound_bank_subset
{
	inline SndBank* parse(const std::string& name, zone_memory* mem)
	{
		auto file = filesystem::file("soundbank\\" + name + ".subset.json");
		if (!file.exists()) return nullptr;
		if (name.empty() || name.find_first_of("/\\:") != std::string::npos || name == "." || name == "..")
			throw std::runtime_error("Invalid soundbank subset name");
		if (filesystem::file("soundbank\\" + name + ".json").exists())
			throw std::runtime_error("Soundbank has both a subset and an ordinary source: " + name);
		if (file.open("rb") != 0) throw std::runtime_error("Could not open soundbank subset source");
		std::vector<uint8_t> source_bytes(file.size());
		if (source_bytes.empty() || file.read(source_bytes.data(), 1, source_bytes.size()) != source_bytes.size() ||
			fgetc(file.get_fp()) != EOF || ferror(file.get_fp()))
			throw std::runtime_error("Could not read exact soundbank subset source bytes");
		if (file.close() != 0) throw std::runtime_error("Could not close soundbank subset source");
		const auto config = read_configuration(json::parse(source_bytes), name);
		const auto& donor_name = config.donor;
		auto* donor = db_find_x_asset_header_safe(ASSET_TYPE_SOUND_BANK, donor_name.c_str()).soundBank;
		if (!donor || DB_IsXAssetDefault(ASSET_TYPE_SOUND_BANK, donor_name.c_str()) ||
			!donor->alias || !donor->aliasCount || !donor->zone || !donor->gameLanguage || !donor->soundLanguage ||
			(donor->duckCount && !donor->ducks))
			throw std::runtime_error("Soundbank subset donor is missing or incomplete: " + donor_name);

		const auto& prefixes = config.prefixes;
		const auto& roots = config.roots;
		const auto& required = config.required;
		const auto& allowed = config.allowed;
		const auto& external = config.external;
		const std::unordered_set<std::string> root_names(roots.begin(), roots.end()), allowed_names(allowed.begin(), allowed.end());
		std::unordered_map<std::string, unsigned int> by_name;
		std::unordered_map<SndStringHash, unsigned int> by_id;
		for (unsigned int i = 0; i < donor->aliasCount; ++i)
		{
			const auto& list = donor->alias[i];
			if (!list.aliasName || !*list.aliasName || !list.head || list.count <= 0 ||
				list.id != snd_hash_name(list.aliasName) || !by_name.emplace(list.aliasName, i).second ||
				!by_id.emplace(list.id, i).second)
				throw std::runtime_error("Invalid or ambiguous donor alias list");
			for (int h = 0; h < list.count; ++h)
				if (!list.head[h].aliasName || std::string(list.head[h].aliasName) != list.aliasName || list.head[h].id != list.id)
					throw std::runtime_error("Donor alias head/list identity mismatch");
		}
		for (const auto& root : roots)
			if (!by_name.contains(root)) throw std::runtime_error("Missing subset root: " + root);
		for (const auto& prefix : prefixes)
			if (std::none_of(by_name.begin(), by_name.end(), [&](const auto& entry) { return entry.first.starts_with(prefix); }))
				throw std::runtime_error("Subset prefix matches no aliases: " + prefix);
		const auto is_root = [&](const std::string& alias)
		{
			return root_names.contains(alias) || std::any_of(prefixes.begin(), prefixes.end(),
				[&](const auto& prefix) { return alias.starts_with(prefix); });
		};
		std::unordered_map<SndStringHash, std::string> external_by_id;
		for (const auto& alias : external)
			if (by_name.contains(alias) || !external_by_id.emplace(snd_hash_name(alias.c_str()), alias).second)
				throw std::runtime_error("External alias is local or has an ambiguous hash: " + alias);
		const auto& external_ducks = config.external_ducks;
		const std::unordered_set<SndStringHash> permitted_ducks(external_ducks.begin(), external_ducks.end());
		std::unordered_map<SndStringHash, const SndDuck*> by_duck;
		for (unsigned int i = 0; i < donor->duckCount; ++i)
			if (!by_duck.emplace(donor->ducks[i].id, &donor->ducks[i]).second)
				throw std::runtime_error("Ambiguous donor duck hash");
		std::vector<unsigned int> selected;
		std::unordered_set<unsigned int> included;
		for (unsigned int i = 0; i < donor->aliasCount; ++i)
			if (is_root(donor->alias[i].aliasName)) { included.insert(i); selected.push_back(i); }
		if (selected.empty()) throw std::runtime_error("Empty soundbank subset");
		std::unordered_set<SndStringHash> used_external, required_ducks;
		const auto dependency = [&](const char* alias_name, SndStringHash id)
		{
			if ((!alias_name || !*alias_name) && !id) return;
			const auto named = alias_name && *alias_name;
			const auto resolved_id = named ? snd_hash_name(alias_name) : id;
			if (id && id != resolved_id) throw std::runtime_error("Alias dependency name/hash mismatch");
			if (const auto found = by_id.find(resolved_id); found != by_id.end())
			{
				const auto index = found->second;
				const std::string alias = donor->alias[index].aliasName;
				if (named && alias != alias_name) throw std::runtime_error("Alias dependency hash collision");
				if (!is_root(alias) && !allowed_names.contains(alias))
					throw std::runtime_error("Subset dependency requires explicit review: " + alias);
				if (included.insert(index).second) selected.push_back(index);
			}
			else
			{
				const auto external_found = external_by_id.find(resolved_id);
				if (external_found == external_by_id.end() || (named && external_found->second != alias_name))
					throw std::runtime_error("Unreviewed dependency outside donor bank: " + std::to_string(resolved_id));
				used_external.insert(resolved_id);
			}
		};
		for (size_t p = 0; p < selected.size(); ++p)
		{
			const auto& list = donor->alias[selected[p]];
			for (int h = 0; h < list.count; ++h)
			{
				const auto& alias = list.head[h];
				dependency(alias.secondaryAliasName, alias.secondaryId);
				dependency(alias.stopAliasName, alias.stopAliasID);
				if (!alias.duck) continue;
				required_ducks.insert(alias.duck);
				if (const auto duck = by_duck.find(alias.duck); duck != by_duck.end()) dependency(nullptr, duck->second->duckAlias);
				else if (!permitted_ducks.contains(alias.duck))
					throw std::runtime_error("External duck requires explicit review: " + std::to_string(alias.duck));
			}
		}
		for (const auto& alias : required)
			if (!by_name.contains(alias) || !included.contains(by_name.at(alias)))
				throw std::runtime_error("Required alias missing from subset: " + alias);
		constexpr auto empty = std::numeric_limits<unsigned short>::max();
		if (selected.size() >= empty) throw std::runtime_error("Soundbank subset exceeds index capacity");
		auto* bank = mem->allocate<SndBank>();
		*bank = *donor;
		bank->name = mem->duplicate_string(name);
		bank->aliasCount = static_cast<unsigned int>(selected.size());
		bank->alias = mem->allocate<SndAliasList>(bank->aliasCount);
		for (unsigned int i = 0; i < bank->aliasCount; ++i)
		{
			bank->alias[i] = donor->alias[selected[i]];
			bank->alias[i].head = mem->allocate<SndAlias>(bank->alias[i].count);
			memcpy(bank->alias[i].head, donor->alias[selected[i]].head, sizeof(SndAlias) * bank->alias[i].count);
		}
		std::vector<SndStringHash> alias_ids;
		for (unsigned int i = 0; i < bank->aliasCount; ++i) alias_ids.push_back(bank->alias[i].id);
		const auto alias_index = build_alias_index<SndIndexEntry>(alias_ids);
		bank->aliasIndex = mem->allocate<SndIndexEntry>(bank->aliasCount);
		memcpy(bank->aliasIndex, alias_index.data(), sizeof(SndIndexEntry) * bank->aliasCount);
		std::vector<SndDuck> ducks;
		for (unsigned int i = 0; i < donor->duckCount; ++i)
			if (required_ducks.contains(donor->ducks[i].id)) ducks.push_back(donor->ducks[i]);
		bank->duckCount = static_cast<unsigned int>(ducks.size());
		bank->ducks = ducks.empty() ? nullptr : mem->allocate<SndDuck>(ducks.size());
		if (!ducks.empty()) memcpy(bank->ducks, ducks.data(), sizeof(SndDuck) * ducks.size());
		bank->soundTable = {};
		bank->sendEffectCount = 0; bank->sendEffects = nullptr;
		bank->musicSetCount = 0; bank->musicSets = nullptr;

		ordered_json report{{"version", 1}, {"stage", "subset-parse"},
			{"sourceSHA256", utils::cryptography::sha256::compute(source_bytes.data(), source_bytes.size(), true)},
			{"bank", name}, {"donor", donor_name}, {"donorAliasCount", donor->aliasCount},
			{"aliasCount", bank->aliasCount}, {"duckCount", bank->duckCount}, {"zone", bank->zone},
			{"gameLanguage", bank->gameLanguage}, {"soundLanguage", bank->soundLanguage}, {"aliases", ordered_json::array()},
			{"externalAliases", ordered_json::array()}, {"externalDuckIds", ordered_json::array()}, {"indexVerified", true}};
		for (unsigned int i = 0; i < bank->aliasCount; ++i)
		{
			const auto& list = bank->alias[i];
			ordered_json heads = ordered_json::array();
			for (int h = 0; h < list.count; ++h)
				heads.push_back({{"assetId", list.head[h].assetId}, {"secondaryId", list.head[h].secondaryId},
					{"stopAliasId", list.head[h].stopAliasID}, {"duck", list.head[h].duck}});
			report["aliases"].push_back({{"name", list.aliasName}, {"id", list.id}, {"heads", heads}});
		}
		for (const auto id : used_external) report["externalAliases"].push_back(external_by_id.at(id));
		for (const auto id : required_ducks) if (!by_duck.contains(id)) report["externalDuckIds"].push_back(id);
		publish_report("soundbank-subsets", name, report.dump(2));
		ZONETOOL_INFO("Soundbank subset %s: %u/%u alias lists; rebuilt index verified", name.c_str(), bank->aliasCount, donor->aliasCount);
		return bank;
	}
}
