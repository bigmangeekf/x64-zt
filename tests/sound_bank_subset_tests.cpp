#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../src/zonetool/zonetool/iw7/assets/sound_bank_subset_rules.hpp"
#include "../src/zonetool/zonetool/iw7/assets/sound_bank_subset_report.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
#include <random>

namespace subset = zonetool::iw7::sound_bank_subset;
using json = nlohmann::json;

void require(bool condition, const char* message)
{
	if (!condition) throw std::runtime_error(message);
}

template <typename Function> void rejects(Function&& operation, const char* message)
{
	bool rejected = false;
	try { operation(); } catch (const std::exception&) { rejected = true; }
	require(rejected, message);
}

json fixture()
{
	return {{"version", 1}, {"donor", "synthetic.all"}, {"prefixes", {"effect_"}}};
}

void schema_tests()
{
	const auto valid = fixture();
	require(subset::read_configuration(valid, "subset.all").donor == "synthetic.all", "Valid configuration rejected");
	for (const auto* literal : {"1.9", "1.0", "1e0", "-1", "0", "2", "4294967297", "18446744073709551616", "true", "null", "\"1\""})
	{
		auto candidate = valid;
		candidate["version"] = json::parse(literal);
		rejects([&] { subset::read_configuration(candidate, "subset.all"); }, "Invalid version accepted");
	}
	for (const auto* literal : {"-1", "0.5", "1.0", "1e0", "4294967296", "18446744073709551615", "18446744073709551616", "true", "null", "\"123\""})
	{
		auto candidate = valid;
		candidate["externalDuckIds"] = json::array({json::parse(literal)});
		rejects([&] { subset::read_configuration(candidate, "subset.all"); }, "Invalid duck hash accepted");
	}
	auto boundary = valid;
	boundary["externalDuckIds"] = json::parse("[0, 1, 2147483648, 4294967295]");
	const auto parsed = subset::read_configuration(boundary, "subset.all");
	require(parsed.external_ducks == std::vector<uint32_t>({0, 1, 2147483648u, 4294967295u}), "Duck hash boundary changed");
	for (const auto* literal : {"null", "{}", "1", "[1, 1]"})
	{
		auto candidate = valid;
		candidate["externalDuckIds"] = json::parse(literal);
		rejects([&] { subset::read_configuration(candidate, "subset.all"); }, "Invalid duck array accepted");
	}
	for (const auto* key : {"prefixes", "aliases", "requiredAliases", "allowedDependencies", "externalAliases"})
		for (const auto* literal : {"null", "{}", "\"effect_\"", "[1]", "[\"\"]", "[\"same\",\"same\"]"})
		{
			auto candidate = valid;
			candidate[key] = json::parse(literal);
			rejects([&] { subset::read_configuration(candidate, "subset.all"); }, "Invalid string selector accepted");
		}
	auto candidate = valid;
	candidate["unexpected"] = true;
	rejects([&] { subset::read_configuration(candidate, "subset.all"); }, "Unknown key accepted");
	candidate = valid;
	candidate.erase("version");
	rejects([&] { subset::read_configuration(candidate, "subset.all"); }, "Missing version accepted");
	candidate = valid;
	candidate["prefixes"] = json::array();
	rejects([&] { subset::read_configuration(candidate, "subset.all"); }, "Rootless configuration accepted");
	rejects([&] { subset::read_configuration(valid, "synthetic.all"); }, "Self donor accepted");
	for (const auto* donor : {"cp_zmb", "cp_rave", "cp_disco", "cp_town", "cp_final"})
		require(subset::is_catalog_donor(donor), "Allowed catalog donor rejected");
	for (const auto* donor : {"", "cp_other", "CP_ZMB", "../cp_zmb", "cp_final_boss", "cp_zmb,cp_rave"})
		require(!subset::is_catalog_donor(donor), "Unreviewed catalog donor accepted");
}

struct index_entry { unsigned short value, next; };

// Simulate the consuming hash lookup and compare results with an independent
// linear scan, including hashes absent from the table. No serializer internals.
void check_index(const std::vector<uint32_t>& ids)
{
	const auto entries = subset::build_alias_index<index_entry>(ids);
	const auto lookup = [&](uint32_t id)
	{
		auto slot = static_cast<size_t>(id % ids.size());
		for (size_t hops = 0; hops < ids.size(); ++hops)
		{
			require(slot < entries.size(), "Out-of-range index link");
			const auto& entry = entries[slot];
			if (entry.value == 65535) return ids.size();
			require(entry.value < ids.size(), "Out-of-range alias value");
			if (ids[entry.value] == id) return static_cast<size_t>(entry.value);
			if (entry.next == 65535) return ids.size();
			slot = entry.next;
		}
		throw std::runtime_error("Alias index contains a cycle");
	};
	for (const auto id : ids)
		require(lookup(id) == static_cast<size_t>(std::find(ids.begin(), ids.end(), id) - ids.begin()), "Hash lookup differs from linear scan");
	for (uint32_t id = 100000; id < 100064; ++id)
		require(lookup(id) == static_cast<size_t>(std::find(ids.begin(), ids.end(), id) - ids.begin()), "Missing hash lookup differs from linear scan");
}

void index_tests()
{
	check_index({0}); check_index({4294967295u});
	check_index({0, 4, 1, 8}); // Overflow must not occupy a later alias's home bucket.
	check_index({8, 1, 4, 0});
	std::vector<uint32_t> collisions;
	for (uint32_t i = 0; i < 64; ++i) collisions.push_back(i * 64);
	check_index(collisions);
	std::mt19937 random(0x51A5u);
	for (unsigned int iteration = 0; iteration < 128; ++iteration)
	{
		std::vector<uint32_t> ids;
		for (uint32_t i = 0; i <= iteration; ++i) ids.push_back(i * (iteration + 1) + i % 7);
		std::shuffle(ids.begin(), ids.end(), random);
		check_index(ids);
	}
	rejects([] { subset::build_alias_index<index_entry>({}); }, "Empty index accepted");
	rejects([] { subset::build_alias_index<index_entry>({1, 1}); }, "Duplicate hash accepted");
	rejects([] { subset::build_alias_index<index_entry>(std::vector<uint32_t>(65535)); }, "Sentinel-sized index accepted");
	std::vector<uint32_t> maximum;
	for (uint32_t id = 0; id < 65534; ++id) maximum.push_back(id);
	const auto entries = subset::build_alias_index<index_entry>(maximum);
	require(entries.size() == 65534 && entries.back().value == 65533, "Largest supported index failed");
}

std::string read_file(const std::filesystem::path& path)
{
	std::ifstream stream(path, std::ios::binary);
	return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

void publication_tests()
{
	// Create an owned empty directory; never reuse or remove an existing path.
	const auto directory = std::filesystem::temp_directory_path() /
		("zt-subset-tests-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
	require(std::filesystem::create_directory(directory), "Test directory already exists");
	try
	{
		const auto report = directory / "synthetic.json";
		subset::publish_report(directory, "synthetic", "first report");
		require(read_file(report) == "first report", "Initial report mismatch");
		subset::publish_report(directory, "synthetic", std::string("replacement\0bytes", 17));
		require(read_file(report) == std::string("replacement\0bytes", 17), "Repeated report did not replace exact bytes");
		const auto held = CreateFileW(report.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		require(held != INVALID_HANDLE_VALUE, "Cannot lock test report");
		bool blocked = false;
		try { subset::publish_report(directory, "synthetic", "must not appear"); }
		catch (const std::exception&) { blocked = true; }
		require(CloseHandle(held) != 0, "Cannot close test report");
		require(blocked, "Locked destination did not fail publication");
		require(read_file(report) == std::string("replacement\0bytes", 17), "Failed publication damaged prior report");
		rejects([&] { subset::publish_report(report, "synthetic", "invalid"); }, "File used as report directory");
		rejects([&] { subset::publish_report(directory, "../escape", "invalid"); }, "Path traversal accepted");
		const auto blocked_destination = directory / "directory.json";
		require(std::filesystem::create_directory(blocked_destination), "Cannot create blocked destination");
		rejects([&] { subset::publish_report(directory, "directory", "invalid"); }, "Directory used as report file");
		for (const auto& entry : std::filesystem::directory_iterator(directory))
			require(entry.path().extension() == ".json", "Failed publication left a temporary file");
		std::filesystem::remove(blocked_destination);
		std::filesystem::remove(report);
		std::filesystem::remove(directory);
	}
	catch (...)
	{
		std::cerr << "Test files retained for diagnosis: " << directory << '\n';
		throw;
	}
}

int main()
{
	try
	{
		schema_tests(); index_tests(); publication_tests();
		std::cout << "Soundbank subset schema, index and publication tests passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
