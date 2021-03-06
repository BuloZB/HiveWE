#include "stdafx.h"


// Initial version of map script generation
// Not very readable

void Triggers::load(BinaryReader& reader) {
	trigger_strings.load("UI/TriggerStrings.txt");
	trigger_data.load("UI/TriggerData.txt");
	trigger_data.substitute(world_edit_strings, "WorldEditStrings");

	// Manual fixes
	trigger_data.set_whole_data("TriggerTypeDefaults", "string", "\"\"");

	for (auto&& section : { "TriggerActions"s, "TriggerEvents"s, "TriggerConditions"s, "TriggerCalls"s }) {
		for (auto&&[key, value] : trigger_data.section(section)) {
			if (key.front() == '_') {
				continue;
			}

			int arguments = 0;
			for (auto&& j : value) {
				arguments += !j.empty() && !is_number(j) && j!= "nothing";
			}

			if (section == "TriggerCalls") {
				--arguments;
			}

			argument_counts[key] = arguments;
		}
	}

	std::string magic_number = reader.read_string(4);
	if (magic_number != "WTG!") {
		std::cout << "Unknown magic number for war3map.wtg " << magic_number << "\n";
		return;
	}

	int version = reader.read<uint32_t>();

	categories.resize(reader.read<uint32_t>());
	for (auto&& i : categories) {
		i.id = reader.read<uint32_t>();
		i.name = reader.read_c_string();
		if (version == 7) {
			i.is_comment = reader.read<uint32_t>();
		}
	}

	reader.advance(4); // Unknown always 0

	int variable_count = reader.read<uint32_t>();
	for (int i = 0; i < variable_count; i++) {
		std::string name = reader.read_c_string();
		TriggerVariable variable;
		variable.type = reader.read_c_string();
		reader.advance(4); // Unknown always 1
		variable.is_array = reader.read<uint32_t>();
		if (version == 7) {
			variable.array_size = reader.read<uint32_t>();
		}
		variable.is_initialized = reader.read<uint32_t>();
		if (variable.is_initialized) {
			std::cout << "init\n";
		}
		variable.initial_value = reader.read_c_string();
		variables[name] = variable;
	}

	//variables.resize(reader.read<uint32_t>());
	//for (auto&& i : variables) {
	//	i.name = reader.read_c_string();
	//	i.type = reader.read_c_string();
	//	reader.advance(4); // Unknown always 1
	//	i.is_array = reader.read<uint32_t>();
	//	if (version == 7) {
	//		i.array_size = reader.read<uint32_t>();
	//	}
	//	i.is_initialized = reader.read<uint32_t>();
	//	i.initial_value = reader.read_c_string();
	//}

	std::function<void(TriggerParameter&)> parse_parameter_structure = [&](TriggerParameter& parameter) {
		parameter.type = static_cast<TriggerParameter::Type>(reader.read<uint32_t>());
		parameter.value = reader.read_c_string();
		parameter.has_sub_parameter = reader.read<uint32_t>();
		if (parameter.has_sub_parameter) {
			parameter.sub_parameter.type = static_cast<TriggerSubParameter::Type>(reader.read<uint32_t>());
			parameter.sub_parameter.name = reader.read_c_string();
			parameter.sub_parameter.begin_parameters = reader.read<uint32_t>();
			if (parameter.sub_parameter.begin_parameters) {
				parameter.sub_parameter.parameters.resize(argument_counts[parameter.sub_parameter.name]);
				for (auto&& i : parameter.sub_parameter.parameters) {
					parse_parameter_structure(i);
				}
			}
		}
		if (version == 4) {
			if (parameter.type == TriggerParameter::Type::function) {
				reader.advance(4); // Unknown always 0
			} else {
				parameter.is_array = reader.read<uint32_t>();
			}
		} else {
			if (parameter.has_sub_parameter) {
				reader.advance(4); // Unknown always 0
			}
			parameter.is_array = reader.read<uint32_t>();
		}
		if (parameter.is_array) {
			parameter.parameters.resize(1);
			parse_parameter_structure(parameter.parameters.front());
		}
	};


	std::function<void(ECA&, bool)> parse_eca_structure = [&](ECA& eca, bool is_child) {
		eca.type = static_cast<ECA::Type>(reader.read<uint32_t>());
		if (is_child) {
			eca.group = reader.read<uint32_t>();
		}
		eca.name = reader.read_c_string();
		eca.enabled = reader.read<uint32_t>();
		eca.parameters.resize(argument_counts[eca.name]);
		for (auto&& i : eca.parameters) {
			parse_parameter_structure(i);
		}
		if (version == 7) {
			eca.ecas.resize(reader.read<uint32_t>());
			for (auto&& i : eca.ecas) {
				parse_eca_structure(i, true);
			}
		}
	};

	triggers.resize(reader.read<uint32_t>());
	for (auto&& i : triggers) {
		i.id = next_id++;
		i.name = reader.read_c_string();
		i.description = reader.read_c_string();
		if (version == 7) {
			i.is_comment = reader.read<uint32_t>();
		}
		i.is_enabled = reader.read<uint32_t>();
		reader.advance(4); // is_custom
		i.initally_off = reader.read<uint32_t>();
		i.run_on_initialization = reader.read<uint32_t>();
		i.category_id = reader.read<uint32_t>();
		i.lines.resize(reader.read<uint32_t>());
		for (auto&& j : i.lines) {
			parse_eca_structure(j, false);
		}
	}
}

void Triggers::load_jass(BinaryReader& reader) {
	const int version = reader.read<uint32_t>();

	if (version == 1) {
		global_jass_comment = reader.read_c_string();
		global_jass = reader.read_string(reader.read<uint32_t>());
	}

	reader.advance(4);
	for (auto&& i : triggers) {
		const int size = reader.read<uint32_t>();
		if (size > 0) {
			i.custom_text = reader.read_string(size);
		}
	}
}

void Triggers::save() const {

}

void Triggers::save_jass() const {

}

void Triggers::generate_map_script() {
	std::map<std::string, std::string> unit_variables; // creation_number, unit_id
	std::map<std::string, std::string> destructable_variables; // creation_number, destructable_id
	std::vector<std::string> initialization_triggers;


	std::string trigger_script;
	for (const auto& i : triggers) {
		if (!i.custom_text.empty()) {
			trigger_script += i.custom_text;
		} else {
			trigger_script += convert_gui_to_jass(i, initialization_triggers);
		}
	}

	// Search the trigger script for global unit/destructible definitons
	size_t pos = trigger_script.find("gg_unit", 0);
	while (pos != std::string::npos) {
		std::string type = trigger_script.substr(pos + 8, 4);
		std::string creation_number = trigger_script.substr(pos + 13, 4);
		unit_variables[creation_number] = type;
		pos = trigger_script.find("gg_unit", pos + 17);
	}

	pos = trigger_script.find("gg_dest", 0);
	while (pos != std::string::npos) {	
		std::string type = trigger_script.substr(pos + 8, 4);
		std::string creation_number = trigger_script.substr(pos + 13, 4);
		destructable_variables[creation_number] = type;
		pos = trigger_script.find("gg_dest", pos + 17);
	}

	// Write the results to a buffer
	BinaryWriter writer;

	writer.write_string(seperator);
	writer.write_string("//*\n");
	writer.write_string("//*  Global variables\n");
	writer.write_string("//*\n");
	writer.write_string(seperator);

	writer.write_string("globals\n");

	for (const auto&[name, variable] : variables) {
		if (variable.is_array) {
			writer.write_string("\t" + variable.type + " array udg_" + name + "\n");
		} else {
			std::string base_type = trigger_data.data("TriggerTypes", variable.type, 4);
			std::string type = base_type.empty() ? variable.type : base_type;
			std::string default_value = trigger_data.data("TriggerTypeDefaults", type);

			if (default_value.empty()) { // handle?
				default_value = "null";
			}

			writer.write_string("\t" + variable.type + " udg_" + name + " = " + default_value + "\n");
		}
	}

	for (const auto& i : map->regions.regions) {
		std::string region_name = i.name;
		// Replace spaces by underscores
		std::replace(region_name.begin(), region_name.end(), ' ', '_');
		writer.write_string("\trect gg_rct_" + region_name + " = null\n");
	}

	for (const auto& i : map->cameras.cameras) {
		std::string camera_name = i.name;
		// Replace spaces by underscores
		std::replace(camera_name.begin(), camera_name.end(), ' ', '_');
		writer.write_string("\tcamerasetup gg_cam_" + camera_name + " = null\n");
	}

	for (const auto& i : map->sounds.sounds) {
		std::string sound_name = i.name;
		// Replace spaces by underscores
		std::replace(sound_name.begin(), sound_name.end(), ' ', '_');
		writer.write_string("\tsound " + sound_name + " = null\n");
	}

	for (const auto& i : triggers) {
		if (i.is_comment) {
			continue;
		}

		std::string trigger_name = i.name;
		// Replace spaces by underscores
		std::replace(trigger_name.begin(), trigger_name.end(), ' ', '_');

		writer.write_string("\ttrigger gg_trg_" + trigger_name + " = null\n");
	}

	for (const auto&[creation_number, type] : unit_variables) {
		writer.write_string("\tunit gg_unit_" + type + "_" + creation_number + " = null\n");
	}

	for (const auto&[creation_number, type] : destructable_variables) {
		writer.write_string("\tdestructable gg_dest_" + type + "_" + creation_number + " = null\n");
	}

	writer.write_string("endglobals\n\n");

	// init globals
	writer.write_string("function InitGlobals takes nothing returns nothing\n");
	writer.write_string("\tlocal integer i = 0\n");
	for (const auto&[name, variable] : variables) {
		if (variable.is_array) {
			const std::string base_type = trigger_data.data("TriggerTypes", variable.type, 4);
			const std::string type = base_type.empty() ? variable.type : base_type;
			std::string default_value = trigger_data.data("TriggerTypeDefaults", type);
			
			if (!variable.is_initialized && default_value.empty()) {
				continue;
			}
			writer.write_string("\tset i = 0\n");
			writer.write_string("\tloop\n");
			writer.write_string("\t\texitwhen(i > " + std::to_string(variable.array_size) + ")\n");

			if (variable.is_initialized) {
				writer.write_string("\t\tset udg_" + name + "[i] = " + variable.initial_value + "\n");
			} else {
				writer.write_string("\t\tset udg_" + name + "[i] = " + default_value + "\n");
			}
			writer.write_string("\t\tset i = i + 1\n");
			writer.write_string("\tendloop\n");
			continue;
		}

		if (!variable.is_initialized) {
			continue;
		}
		writer.write_string("\tset udg_" + name + " = " + variable.initial_value + "\n");

	}
	writer.write_string("endfunction\n\n");

	writer.write_string(seperator);
	writer.write_string("//*\n");
	writer.write_string("//*  Unit Item Tables\n");
	writer.write_string("//*\n");
	writer.write_string(seperator);

	writer.write_string(seperator);
	writer.write_string("//*\n");
	writer.write_string("//*  Sounds\n");
	writer.write_string("//*\n");
	writer.write_string(seperator);

	writer.write_string("function InitSounds takes nothing returns nothing\n");

	for (const auto& i : map->sounds.sounds) {
		
		std::string sound_name = i.name;
		// Replace spaces by underscores
		std::replace(sound_name.begin(), sound_name.end(), ' ', '_');
		writer.write_string("\tset " + sound_name + " = CreateSound(" + 
			i.file + ", " + 
			(i.looping ? "true" : "false") + ", " + 
			(i.is_3d ? "true" : "false") + ", " +
			(i.stop_out_of_range ? "true" : "false") + ", " +
			std::to_string(i.fade_in_rate) + ", " +
			std::to_string(i.fade_out_rate) + ", " +
			i.eax_effect +
			")\n");


		writer.write_string("call SetSoundChannel(" + sound_name + ", " + std::to_string(i.channel) + ")\n");
		writer.write_string("call SetSoundVolume(" + sound_name + ", " + std::to_string(i.volume) + ")\n");
		writer.write_string("call SetSoundPitch(" + sound_name + ", " + std::to_string(i.pitch) + ")\n");
	}

	//string_replaced(parameter.value, "\\", "\\\\")

	writer.write_string("endfunction\n");

	writer.write_string(seperator);
	writer.write_string("//*\n");
	writer.write_string("//*  Destructable Objects\n");
	writer.write_string("//*\n");
	writer.write_string(seperator);

	writer.write_string("function CreateAllDestructables takes nothing returns nothing\n");
	writer.write_string("\tlocal destructable d\n");
	writer.write_string("\tlocal trigger t\n");
	writer.write_string("\tlocal real life\n");

	for (const auto& i : map->doodads.doodads) {
		if (destructable_variables.find(std::to_string(i.editor_id)) != destructable_variables.end()) {
			writer.write_string("\tset gg_dest_" + i.id + "_" + std::to_string(i.editor_id) + " = CreateDestructable(" +
				i.id + ", " +
				std::to_string(i.position.x) + ", " +
				std::to_string(i.position.y) + ", " +
				std::to_string(i.angle) + ", " +
				std::to_string(i.scale.x) + ", " +
				std::to_string(i.variation) + ")\n");
		}
	}

	writer.write_string("endfunction\n");

	writer.write_string(seperator);
	writer.write_string("//*\n");
	writer.write_string("//*  Unit Creation\n");
	writer.write_string("//*\n");
	writer.write_string(seperator);

	writer.write_string("function CreateUnits takes nothing returns nothing\n");

	writer.write_string("\tlocal unit u\n");
	writer.write_string("\tlocal integer unitID\n");
	writer.write_string("\tlocal trigger t\n");
	writer.write_string("\tlocal real life\n");

	for (const auto& i : map->units.units) {
		if (i.id == "sloc") {
			continue;
		}

		std::string unit_reference = "u";
		if (unit_variables.find(std::to_string(i.creation_number)) != unit_variables.end()) {
			unit_reference = "gg_unit_" + i.id + "_" + std::to_string(i.creation_number);
		}

		writer.write_string("\tset " + unit_reference + " = CreateUnit(Player(" + 
			std::to_string(i.player) + "), " +
			"\'" + i.id + "\', " +
			std::to_string(i.position.x * 128.f + map->terrain.offset.x) + ", " +
			std::to_string(i.position.y * 128.f + map->terrain.offset.y) + ", " +
			std::to_string(glm::degrees(i.angle)) + ")\n");

		if (i.health != -1) {
			writer.write_string("\tset life = GetUnitState(" + unit_reference + ", UNIT_STATE_LIFE)\n");
			writer.write_string("\tcall SetUnitState(" + unit_reference + ", UNIT_STATE_LIFE, 0.50 * life)\n");
		}

		if (i.mana != -1) {
			writer.write_string("\tcall SetUnitState(" + unit_reference + ", UNIT_STATE_MANA, " + std::to_string(i.mana) + ")\n");
		}

		if (i.level != 1) {
			writer.write_string("\tcall SetHeroLevel(" + unit_reference + ", " + std::to_string(i.level) + ", false)\n");
		}

		if (i.strength != 0) {
			writer.write_string("\tcall SetHeroStr(" + unit_reference + ", " + std::to_string(i.strength) + ", true)\n");
		}

		if (i.agility != 0) {
			writer.write_string("\tcall SetHeroAgi(" + unit_reference + ", " + std::to_string(i.agility) + ", true)\n");
		}

		if (i.intelligence != 0) {
			writer.write_string("\tcall SetHeroInt(" + unit_reference + ", " + std::to_string(i.intelligence) + ", true)\n");
		}

		float range;
		if (i.target_acquisition != -1.f) {
			if (i.target_acquisition == -2.f) {
				range = 200.f;
			} else {
				range = i.target_acquisition;
			}
			writer.write_string("\tcall SetUnitAcquireRange(" + unit_reference + ", " + std::to_string(range) + ")\n");
		}

		for (const auto& j : i.abilities) {
			for (int k = 0; k < std::get<2>(j); k++) {
				writer.write_string("\tcall SelectHeroSkill(" + unit_reference + ", \'" + std::get<0>(j) + "\')\n");
			}

			if (std::get<1>(j)) {
				std::string order_on = abilities_slk.data("Orderon", std::get<0>(j));
				if (order_on.empty()) {
					order_on = abilities_slk.data("Order", std::get<0>(j));
				}
				writer.write_string("\tcall IssueImmediateOrder(" + unit_reference + ", \"" + order_on + "\")\n");
			} else {
				std::string order_off = abilities_slk.data("Orderoff", std::get<0>(j));
				if (!order_off.empty()) {
					writer.write_string("\tcall IssueImmediateOrder(" + unit_reference + ", \"" + order_off + "\")\n");
				}
			}
			
		}

		for (const auto& j : i.items) {
			writer.write_string("\tcall UnitAddItemToSlotById(" + unit_reference + ", " + j.second + ", " + std::to_string(j.first) + ")\n");
		}
	}

	writer.write_string("endfunction\n");

	writer.write_string(seperator);
	writer.write_string("//*\n");
	writer.write_string("//*  Custom Script Code\n");
	writer.write_string("//*\n");
	writer.write_string(seperator);

	writer.write_string(global_jass);

	writer.write_string(seperator);
	writer.write_string("//*\n");
	writer.write_string("//*  Triggers\n");
	writer.write_string("//*\n");
	writer.write_string(seperator);

	writer.write_string(trigger_script);

	writer.write_string(seperator);

	writer.write_string("function InitCustomTriggers takes nothing returns nothing\n");
	for (const auto& i : triggers) {
		std::string trigger_name = i.name;
		// Replace spaces by underscores
		std::replace(trigger_name.begin(), trigger_name.end(), ' ', '_');

		writer.write_string("\tcall InitTrig_" + trigger_name + "()\n");
	}
	writer.write_string("endfunction\n");

	writer.write_string(seperator);
	writer.write_string("function RunInitializationTriggers takes nothing returns nothing\n");
	for (const auto& i : initialization_triggers) {
		writer.write_string("\tcall ConditionalTriggerExecute(" + i + ")\n");
	}
	writer.write_string("endfunction\n");

	writer.write_string(seperator);
	writer.write_string("//*\n");
	writer.write_string("//*  Players\n");
	writer.write_string("//*\n");
	writer.write_string(seperator);

	writer.write_string("function InitCustomPlayerSlots takes nothing returns nothing\n");

	const std::vector<std::string> players = { "MAP_CONTROL_USER", "MAP_CONTROL_COMPUTER", "MAP_CONTROL_NEUTRAL", "MAP_CONTROL_RESCUABLE" };
	const std::vector<std::string> races = { "RACE_PREF_RANDOM", "RACE_PREF_HUMAN", "RACE_PREF_ORC", "RACE_PREF_UNDEAD", "RACE_PREF_NIGHTELF" };

	int index = 0;
	for (const auto& i : map->info.players) {
		std::string player = "Player(" + std::to_string(i.internal_number) + "), ";
		writer.write_string("\tcall SetPlayerStartLocation(" + player + std::to_string(index) + ")\n");
		if (i.fixed_start_position || i.race == PlayerRace::selectable) {
			writer.write_string("\tcall ForcePlayerStartLocation(" + player + std::to_string(index) + ")\n");
		}

		writer.write_string("\tcall SetPlayerColor(" + player + "ConvertPlayerColor(" + std::to_string(i.internal_number) + "))\n");
		writer.write_string("\tcall SetPlayerRacePreference(" + player + races[static_cast<int>(i.race)] + ")\n");
		writer.write_string("\tcall SetPlayerRaceSelectable(" + player + "true" + ")\n");
		writer.write_string("\tcall SetPlayerController(" + player + players[static_cast<int>(i.type)] + ")\n");

		for (const auto& j : map->info.players) {
			if (i.type == PlayerType::rescuable && j.type == PlayerType::human) {
				writer.write_string("\tcall SetPlayerAlliance(" + player + "Player(" + std::to_string(j.internal_number) + "), ALLIANCE_RESCUABLE, true)\n");
			}
		}

		writer.write_string("\n");
		index++;

	}

	writer.write_string("endfunction\n\n");

	writer.write_string("function InitCustomTeams takes nothing returns nothing\n");

	int current_force = 0;
	for (const auto& i : map->info.forces) {
		writer.write_string("\t// Force: " + i.name + "\n");

		std::string post_state;

		for (const auto& j : map->info.players) {
			if (i.player_masks & (1 << j.internal_number)) {
				writer.write_string("\tcall SetPlayerTeam(Player(" + std::to_string(j.internal_number) + "), " + std::to_string(current_force) + ")\n");

				if (i.allied_victory) {
					writer.write_string("\tcall SetPlayerState(Player(" + std::to_string(j.internal_number) + "), PLAYER_STATE_ALLIED_VICTORY, 1)\n");
				}

				for (const auto& k : map->info.players) {
					if (i.player_masks & (1 << k.internal_number) && j.internal_number != k.internal_number) {
						if (i.allied) {
							post_state += "\tcall SetPlayerAllianceStateAllyBJ(Player(" + std::to_string(j.internal_number) + "), Player(" + std::to_string(k.internal_number) + "), true)\n";
						}
						if (i.share_vision) {
							post_state += "\tcall SetPlayerAllianceStateVisionBJ(Player(" + std::to_string(j.internal_number) + "), Player(" + std::to_string(k.internal_number) + "), true)\n";
						}
						if (i.share_unit_control) {
							post_state += "\tcall SetPlayerAllianceStateControlBJ(Player(" + std::to_string(j.internal_number) + "), Player(" + std::to_string(k.internal_number) + "), true)\n";
						}
						if (i.share_advanced_unit_control) {
							post_state += "\tcall SetPlayerAllianceStateFullControlBJ(Player(" + std::to_string(j.internal_number) + "), Player(" + std::to_string(k.internal_number) + "), true)\n";
						}
					}
				}
			}
		}

		if (!post_state.empty()) {
			writer.write_string(post_state);
		}

		writer.write_string("\n");
		current_force++;
	}
	writer.write_string("endfunction\n");

	writer.write_string("function InitAllyPriorities takes nothing returns nothing\n");

	std::map<int, int> player_to_startloc;

	int current_player = 0;
	for (const auto& i : map->info.players) {
		player_to_startloc[i.internal_number] = current_player;
		current_player++;
	}


	current_player = 0;
	for (const auto& i : map->info.players) {
		std::string player_text;
		
		int current_index = 0;
		for (const auto& j : map->info.players) {
			if (i.ally_low_priorities_flags & (1 << j.internal_number) && i.internal_number != j.internal_number) {
				player_text += "\tcall SetStartLocPrio(" + std::to_string(current_player) + ", " + std::to_string(current_index) + ", " + std::to_string(player_to_startloc[j.internal_number]) + ", MAP_LOC_PRIO_LOW)\n";
				current_index++;
			} else if (i.ally_high_priorities_flags & (1 << j.internal_number) && i.internal_number != j.internal_number) {
				player_text += "\tcall SetStartLocPrio(" + std::to_string(current_player) + ", " + std::to_string(current_index) + ", " + std::to_string(player_to_startloc[j.internal_number]) + ", MAP_LOC_PRIO_HIGH)\n";
				current_index++;
			}
		}

		player_text = "\tcall SetStartLocPrioCount(" + std::to_string(current_player) + ", " + std::to_string(current_index) + ")\n" + player_text;
		writer.write_string(player_text);
		current_player++;
	}
	writer.write_string("endfunction\n");

	writer.write_string(seperator);
	writer.write_string("//*\n");
	writer.write_string("//*  Main Initialization\n");
	writer.write_string("//*\n");
	writer.write_string(seperator);

	writer.write_string("function main takes nothing returns nothing\n");


	//std::string soto = "call SetCameraBounds("
	//	std::to_string( "-3328.0 + GetCameraMargin(CAMERA_MARGIN_LEFT),"
	//	"-3584.0 + GetCameraMargin(CAMERA_MARGIN_BOTTOM),"
	//	"3328.0 - GetCameraMargin(CAMERA_MARGIN_RIGHT)," 
	//	"3072.0 - GetCameraMargin(CAMERA_MARGIN_TOP),"
	//	"-3328.0 + GetCameraMargin(CAMERA_MARGIN_LEFT),"
	//	"3072.0 - GetCameraMargin(CAMERA_MARGIN_TOP),"
	//	"3328.0 - GetCameraMargin(CAMERA_MARGIN_RIGHT),"
	//	"-3584.0 + GetCameraMargin(CAMERA_MARGIN_BOTTOM))";


	//writer.write_string(std::to_string(map->info.camera_bottom_left.x) + " " + std::to_string(map->info.camera_bottom_left.y) + "\n");
	//writer.write_string(std::to_string(map->info.camera_bottom_right.x) + " " + std::to_string(map->info.camera_bottom_right.y) + "\n");
	//writer.write_string(std::to_string(map->info.camera_top_left.x) + " " + std::to_string(map->info.camera_top_left.y) + "\n");
	//writer.write_string(std::to_string(map->info.camera_top_right.x) + " " + std::to_string(map->info.camera_top_right.y) + "\n");

	//writer.write_string(std::to_string(map->info.camera_complements.x) + " " + std::to_string(map->info.camera_complements.y) + "\n");
	//writer.write_string(std::to_string(map->info.camera_complements.z) + " " + std::to_string(map->info.camera_complements.w) + "\n");

	writer.write_string("call CreateAllDestructables()");
	writer.write_string("call CreateUnits()");

	writer.write_string("endfunction\n");

	std::ofstream output("C:/Users/User/Desktop/testy.txt");
	output.write((char*)writer.buffer.data(), writer.buffer.size());

	//hierarchy.map.file_write("war3map.test", writer.buffer);
}

std::string Triggers::convert_eca_to_jass(const ECA& eca, std::string& pre_actions, const std::string& trigger_name, bool nested) const {
	std::string output;

	if (eca.name == "WaitForCondition") {
		output += "loop\n";
		output += "exitwhen (" + resolve_parameter(eca.parameters[0], trigger_name, pre_actions, get_type(eca.name, 0)) + ")\n";
		output += "call TriggerSleepAction(RMaxBJ(bj_WAIT_FOR_COND_MIN_INTERVAL, " + resolve_parameter(eca.parameters[1], trigger_name, pre_actions, get_type(eca.name, 1)) + "))\n";
		output += "endloop";
		return output;
	}
	if (eca.name == "ForLoopAMultiple" || eca.name == "ForLoopBMultiple") {
		std::string loop_index = eca.name == "ForLoopAMultiple" ? "bj_forLoopAIndex" : "bj_forLoopBIndex";
		std::string loop_index_end = eca.name == "ForLoopAMultiple" ? "bj_forLoopAIndexEnd" : "bj_forLoopBIndexEnd";

		output += "set " + loop_index + "=" + resolve_parameter(eca.parameters[0], trigger_name, pre_actions, get_type(eca.name, 0)) + "\n";
		output += "set " + loop_index_end + "=" + resolve_parameter(eca.parameters[1], trigger_name, pre_actions, get_type(eca.name, 1)) + "\n";
		output += "loop\n";
		output += "exitwhen " + loop_index + " > " + loop_index_end + "\n";
		for (const auto& i : eca.ecas) {
			output += convert_eca_to_jass(i, pre_actions, trigger_name, false) + "\n";
		}
		output += "set " + loop_index + " = " + loop_index + " + 1\n";
		output += "endloop\n";
		return output;
	}

	if (eca.name == "ForLoopVarMultiple") {
		std::string variable = "udg_" + resolve_parameter(eca.parameters[0], trigger_name, pre_actions, "integer");
		output += "set " + variable + " = ";
		output += resolve_parameter(eca.parameters[1], trigger_name, pre_actions, get_type(eca.name, 1)) + "\n";
		output += "loop\n";
		output += "exitwhen " + variable + " > " + resolve_parameter(eca.parameters[2], trigger_name, pre_actions, get_type(eca.name, 2)) + "\n";
		for (const auto& i : eca.ecas) {
			output += convert_eca_to_jass(i, pre_actions, trigger_name, false) + "\n";
		}
		output += "set " + variable + " = " + variable + " + 1\n";
		output += "endloop\n";
		return output;
	}

	if (eca.name == "IfThenElseMultiple") {
		std::string iftext;
		std::string thentext;
		std::string elsetext;

		std::string function_name = generate_function_name(trigger_name);
		iftext += "function " + function_name + " takes nothing returns boolean\n";

		for (const auto& i : eca.ecas) {
			if (i.type == ECA::Type::condition) {
				iftext += "\tif (not (" + convert_eca_to_jass(i, pre_actions, trigger_name, true) + ")) then\n";
				iftext += "\t\treturn false\n";
				iftext += "\tendif\n";
			} else if (i.type == ECA::Type::action) {
				if (i.group == 1) {
					thentext += convert_eca_to_jass(i, pre_actions, trigger_name, false) + "\n";
				} else {
					elsetext += convert_eca_to_jass(i, pre_actions, trigger_name, false) + "\n";
				}
			}
		}
		iftext += "\treturn true\n";
		iftext += "endfunction\n";
		pre_actions += iftext;

		return "if (" + function_name + "()) then\n" + thentext + "\telse\n" + elsetext + "\tendif";
	} 
	
	if (eca.name == "ForForceMultiple" || eca.name == "ForGroupMultiple") {
		const std::string function_name = generate_function_name(trigger_name);

		// Remove multiple
		output += "call " + eca.name.substr(0, 8) + "(" + resolve_parameter(eca.parameters[0], trigger_name, pre_actions, get_type(eca.name, 0)) + ", function " + function_name + ")\n";

		std::string toto;
		for (const auto& i : eca.ecas) {
			toto += "\t" + convert_eca_to_jass(i, pre_actions, trigger_name, false);
		}
		pre_actions += "function " + function_name + " takes nothing returns nothing\n";
		pre_actions += toto;
		pre_actions += "\nendfunction\n";

		return output;
	} 
	
	if (eca.name == "AndMultiple") {
		const std::string function_name = generate_function_name(trigger_name);

		std::string iftext = "function " + function_name + " takes nothing returns boolean\n";
		for (const auto& i : eca.ecas) {
			iftext += "\tif (not (" + convert_eca_to_jass(i, pre_actions, trigger_name, true) + ")) then\n";
			iftext += "\t\treturn false\n";
			iftext += "\tendif\n";
		}
		iftext += "\treturn true\n";
		iftext += "endfunction\n";
		pre_actions += iftext;

		return function_name + "()";
	} 

	if (eca.name == "OrMultiple") {
		const std::string function_name = generate_function_name(trigger_name);

		std::string iftext = "function " + function_name + " takes nothing returns boolean\n";
		for (const auto& i : eca.ecas) {
			iftext += "\tif (" + convert_eca_to_jass(i, pre_actions, trigger_name, true) + ") then\n";
			iftext += "\t\treturn true\n";
			iftext += "\tendif\n";
		}
		iftext += "\treturn false\n";
		iftext += "endfunction\n";
		pre_actions += iftext;

		return function_name + "()";
	}
	
	if (eca.name == "SetVariable") {
		const std::string first = resolve_parameter(eca.parameters[0], trigger_name, pre_actions, get_type(eca.name, 0));
		const std::string second = resolve_parameter(eca.parameters[1], trigger_name, pre_actions, get_type(eca.name, 1));
		return "set udg_" + first + " = " + second;
	}

	return testt(trigger_name, eca.name, eca.parameters, pre_actions, !nested);
}


std::string Triggers::testt(const std::string& trigger_name, const std::string& parent_name, const std::vector<TriggerParameter>& parameters, std::string& pre_actions, bool add_call) const {
	std::string output;

	if (parent_name == "CommentString") {
		return "//" + resolve_parameter(parameters[0], trigger_name, pre_actions, "");
	}

	if (parent_name == "CustomScriptCode") {
		return resolve_parameter(parameters[0], trigger_name, pre_actions, "");
	}


	if (parent_name.substr(0, 15) == "OperatorCompare") {
		output += resolve_parameter(parameters[0], trigger_name, pre_actions, get_type(parent_name, 0));
		output += " " + resolve_parameter(parameters[1], trigger_name, pre_actions, get_type(parent_name, 1)) + " ";
		output += resolve_parameter(parameters[2], trigger_name, pre_actions, get_type(parent_name, 2));
		return output;
	}

	if (parent_name == "ForLoopVar") {
		std::string variable = "udg_" + resolve_parameter(parameters[0], trigger_name, pre_actions, "integer");
		output += "set " + variable + " = ";
		output += resolve_parameter(parameters[1], trigger_name, pre_actions, get_type(parent_name, 1)) + "\n";
		output += "loop\n";
		output += "exitwhen " + variable + " > " + resolve_parameter(parameters[2], trigger_name, pre_actions, get_type(parent_name, 2)) + "\n";
		output += resolve_parameter(parameters[3], trigger_name, pre_actions, get_type(parent_name, 3), true) + "\n";
		output += "set " + variable + " = " + variable + " + 1\n";
		output += "endloop\n";
		return output;
	}

	if (parent_name == "IfThenElse") {
		std::string thentext;
		std::string elsetext;

		std::string function_name = generate_function_name(trigger_name);
		std::string tttt = resolve_parameter(parameters[0], trigger_name, pre_actions, get_type(parent_name, 0));

		output += "if (" + function_name + "()) then\n";
		output += resolve_parameter(parameters[1], trigger_name, pre_actions, get_type(parent_name, 1), true) + "\n";
		output += "else\n";
		output += resolve_parameter(parameters[2], trigger_name, pre_actions, get_type(parent_name, 2), true) + "\n";
		output += "endif";

		pre_actions += "function " + function_name + " takes nothing returns boolean\n";
		pre_actions += "return " + tttt + "\n";
		pre_actions += "endfunction\n";
		return output;
	}

	if (parent_name == "ForForce") {
		std::string function_name = generate_function_name(trigger_name);

		std::string tttt = resolve_parameter(parameters[1], trigger_name, pre_actions, get_type(parent_name, 1));

		output += "ForForce(";
		output += resolve_parameter(parameters[0], trigger_name, pre_actions, get_type(parent_name, 0));
		output += ", function " + function_name;
		output += ")";

		pre_actions += "function " + function_name + " takes nothing returns nothing\n";
		pre_actions += "\tcall " + tttt + "\n";
		pre_actions += "endfunction\n\n";
		return (add_call ? "call " : "") + output;
	}

	if (parent_name == "GetBooleanAnd") {
		std::string first_parameter = resolve_parameter(parameters[0], trigger_name, pre_actions, get_type(parent_name, 0));
		std::string second_parameter = resolve_parameter(parameters[1], trigger_name, pre_actions, get_type(parent_name, 1));

		std::string function_name = generate_function_name(trigger_name);
		output += "GetBooleanAnd(" + function_name + "(), ";
		pre_actions += "function " + function_name + " takes nothing returns boolean\n";
		pre_actions += "\t return ( " + first_parameter + ")\n";
		pre_actions += "endfunction\n\n";

		function_name = generate_function_name(trigger_name);
		output += function_name + "())";
		pre_actions += "function " + function_name + " takes nothing returns boolean\n";
		pre_actions += "\t return ( " + second_parameter + ")\n";
		pre_actions += "endfunction\n\n";

		return (add_call ? "call " : "") + output;
	}

	if (parent_name == "GetBooleanOr") {
		std::string first_parameter = resolve_parameter(parameters[0], trigger_name, pre_actions, get_type(parent_name, 0));
		std::string second_parameter = resolve_parameter(parameters[1], trigger_name, pre_actions, get_type(parent_name, 1));

		std::string function_name = generate_function_name(trigger_name);
		output += "GetBooleanOr(" + function_name + "(), ";
		pre_actions += "function " + function_name + " takes nothing returns boolean\n";
		pre_actions += "\t return ( " + first_parameter + ")\n";
		pre_actions += "endfunction\n\n";

		function_name = generate_function_name(trigger_name);
		output += function_name + "())";
		pre_actions += "function " + function_name + " takes nothing returns boolean\n";
		pre_actions += "\t return ( " + second_parameter + ")\n";
		pre_actions += "endfunction\n\n";

		return (add_call ? "call " : "") + output;
	}

	if (parent_name == "OperatorInt" || parent_name == "OperatorReal") {
		output += "(" + resolve_parameter(parameters[0], trigger_name, pre_actions, get_type(parent_name, 0));
		output += " " + resolve_parameter(parameters[1], trigger_name, pre_actions, get_type(parent_name, 1)) + " ";
		output += resolve_parameter(parameters[2], trigger_name, pre_actions, get_type(parent_name, 2)) + ")";
		return output;
	}

	for (int k = 0; k < parameters.size(); k++) {
		const auto& i = parameters[k];

		output += resolve_parameter(i, trigger_name, pre_actions, get_type(parent_name, k));

		if (k < parameters.size() - 1) {
			output += ", ";
		}
	}
	return (add_call ? "call " : "") + parent_name + "(" + output + ")";
}

std::string Triggers::resolve_parameter(const TriggerParameter& parameter, const std::string& trigger_name, std::string& pre_actions, const std::string& type, bool add_call) const {
	if (parameter.has_sub_parameter) {
		return testt(trigger_name, parameter.sub_parameter.name, parameter.sub_parameter.parameters, pre_actions, add_call);
	} else {
		switch (parameter.type) {
			case TriggerParameter::Type::invalid:
				std::cout << "Invalid parameter type\n";
				return "";
			case TriggerParameter::Type::preset:
				return trigger_data.data("TriggerParams", parameter.value, 2);
			case TriggerParameter::Type::function:
				return parameter.value + "()";
			case TriggerParameter::Type::variable: {
				std::string output = parameter.value;
				if (parameter.is_array) {
					output += "[" + resolve_parameter(parameter.parameters[0], trigger_name, pre_actions, "integer") + "]";
				}
				return output;
			}
			case TriggerParameter::Type::string:
				std::string base_type = trigger_data.data("TriggerTypes", type, 4);
				std::string import_type = trigger_data.data("TriggerTypes", type, 5);

				if (not import_type.empty()) {
					return "\"" + string_replaced(parameter.value, "\\", "\\\\") + "\"";
				} else if (base_type == "string") {
					return "\"" + parameter.value + "\"";
				} else {
					return parameter.value;
				}
		}
	}
}

std::string Triggers::get_type(const std::string& function_name, int parameter) const {
	std::string type;

	if (trigger_data.key_exists("TriggerActions", function_name)) {
		type = trigger_data.data("TriggerActions", function_name, 1 + parameter);
	} else if (trigger_data.key_exists("TriggerCalls", function_name)) {
		type = trigger_data.data("TriggerCalls", function_name, 3 + parameter);
	} else if (trigger_data.key_exists("TriggerEvents", function_name)) {
		type = trigger_data.data("TriggerEvents", function_name, 1 + parameter);
	} else if (trigger_data.key_exists("TriggerConditions", function_name)) {
		type = trigger_data.data("TriggerConditions", function_name, 1 + parameter);
	}
	return type;
}

std::string Triggers::generate_function_name(const std::string & trigger_name) const {
	auto time = std::chrono::high_resolution_clock::now().time_since_epoch().count();
	return "Trig_" + trigger_name + "_" + std::to_string(time & 0xFFFFFFFF);
}

std::string Triggers::convert_gui_to_jass(const Trigger& trigger, std::vector<std::string>& map_initializations) const {
	std::string trigger_name = trigger.name;
	// Replace spaces by underscores
	std::replace(trigger_name.begin(), trigger_name.end(), ' ', '_');

	std::string trigger_variable_name = "gg_trg_" + trigger_name;
	std::string trigger_action_name = "Trig_" + trigger_name + "_Actions";
	
	std::string events;
	std::string conditions;
	std::string pre_actions;
	std::string actions;

	events += "function InitTrig_" + trigger_name + " takes nothing returns nothing\n";
	events += "\tset " + trigger_variable_name + " = CreateTrigger()\n";

	actions += "function " + trigger_action_name + " takes nothing returns nothing\n";

	for (const auto& i : trigger.lines) {
		if (not i.enabled) {
			continue;
		}
		switch (i.type) {
			case ECA::Type::event:
				if (i.name == "MapInitializationEvent") {
					map_initializations.push_back(trigger_variable_name);
					continue;
				}
				events += "\tcall " + i.name + "(" + trigger_variable_name + ", ";
				for (int k = 0; k < i.parameters.size(); k++) {
					const auto& p = i.parameters[k];

					events += resolve_parameter(p, trigger_name, pre_actions, get_type(i.name, k));

					if (k < i.parameters.size() - 1) {
						events += ", ";
					}
				}
				events += ")\n";

				break;
			case ECA::Type::condition:
				conditions += "\tif (not (" + convert_eca_to_jass(i, pre_actions, trigger_name, true) + ")) then\n";
				conditions += "\treturn false\n";
				conditions += "\tendif\n";
				break;
			case ECA::Type::action:
				actions += "\t" + convert_eca_to_jass(i, pre_actions, trigger_name, false) + "\n";
				break;
		}
	}
	
	actions += "endfunction\n\n";

	if (!conditions.empty()) {
		conditions = "function Trig_" + trigger_name + "_Conditions takes nothing returns boolean\n" + conditions;
		conditions += "\treturn true\n";
		conditions += "endfunction\n\n";

		events += "\tcall TriggerAddCondition(" + trigger_variable_name + ", Condition(function Trig_" + trigger_name + "_Conditions))\n";
	}

	events += "\tcall TriggerAddAction(" + trigger_variable_name + ", function " + trigger_action_name + ")\n";
	events += "endfunction\n\n";

	return seperator + "// Trigger: " + trigger_name + "\n" + seperator + conditions + pre_actions + actions + seperator + events;
}