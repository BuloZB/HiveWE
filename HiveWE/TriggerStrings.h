#pragma once

class TriggerStrings {
	std::map<std::string, std::string> strings; // ToDo change back to unordered_map?
public:
	void load(BinaryReader& reader);
	void save() const;

	std::string string(const std::string& key) const;
	void set_string(const std::string& key, const std::string& value);
};