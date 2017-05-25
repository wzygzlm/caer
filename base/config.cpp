#include "config.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <iostream>

#include <boost/program_options.hpp>
#include <boost/filesystem.hpp>

namespace po = boost::program_options;

static boost::filesystem::path configFile;

[[ noreturn ]] static inline void printHelpAndExit(po::options_description &desc) {
	std::cout << std::endl << desc << std::endl;
	exit(EXIT_FAILURE);
}

void caerConfigInit(int argc, char *argv[]) {
	// Allowed command-line options for configuration.
	po::options_description cliDescription("Command-line options");
	cliDescription.add_options()("help,h", "print help text")("config,c", po::value<std::string>(),
		"use the specified XML configuration file")("override,o", po::value<std::vector<std::string>>()->multitoken(),
		"override a configuration parameter from the XML configuration file with the supplied value.\n"
			"Format: <node> <attribute> <type> <value>\nExample: /caer/logger/ logLevel byte 7");

	po::variables_map cliVarMap;
	try {
		po::store(boost::program_options::parse_command_line(argc, argv, cliDescription), cliVarMap);
		po::notify(cliVarMap);
	}
	catch (...) {
		std::cout << "Failed to parse command-line options!" << std::endl;
		printHelpAndExit(cliDescription);
	}

	// Parse/check command-line options.
	if (cliVarMap.count("help")) {
		printHelpAndExit(cliDescription);
	}

	if (cliVarMap.count("override")) {
		// Always four components per override needed!
		if ((cliVarMap["override"].as<std::vector<std::string>>().size() % 4) != 0) {
			std::cout << "Configuration overrides must always have four components!" << std::endl;
			printHelpAndExit(cliDescription);
		}
	}

	if (cliVarMap.count("config")) {
		// User supplied config file.
		configFile = boost::filesystem::path(cliVarMap["config"].as<std::string>());
	}
	else {
		// Default config file.
		configFile = boost::filesystem::path(CAER_CONFIG_FILE_NAME);
	}

	// Ensure file path is absolute.
	configFile = boost::filesystem::absolute(configFile);

	// Check that config file ends in .xml, exists, and is a normal file.
	if (configFile.extension().string() != ".xml") {
		std::cout << "Supplied configuration file " << configFile << " has no XML extension." << std::endl;
		printHelpAndExit(cliDescription);
	}

	if (boost::filesystem::exists(configFile)) {
		if (!boost::filesystem::is_regular_file(configFile)) {
			std::cout << "Supplied configuration file " << configFile << " could not be accessed." << std::endl;
			printHelpAndExit(cliDescription);
		}
	}
	else {
		// File doesn't exist yet, let's make sure the parent directory
		// at least exists and is a directory.
		if (!boost::filesystem::is_directory(configFile.parent_path())) {
			std::cout << "Supplied configuration file directory " << configFile.parent_path()
				<< " could not be accessed." << std::endl;
			printHelpAndExit(cliDescription);
		}
	}

	// Let's try to open the file for reading, or create it.
	int configFileFd = open(configFile.c_str(), O_RDONLY | O_CREAT, S_IWUSR | S_IRUSR | S_IRGRP);

	if (configFileFd >= 0) {
		// File opened for reading (or created) successfully.
		// Load XML configuration from file if not empty.
		struct stat configFileStat;
		fstat(configFileFd, &configFileStat);

		if (configFileStat.st_size > 0) {
			sshsNodeImportSubTreeFromXML(sshsGetNode(sshsGetGlobal(), "/"), configFileFd, true);
		}

		close(configFileFd);

		// This means it exists and we can access it, so let's remember
		// it for writing the configuration later at shutdown.
		configFile = boost::filesystem::canonical(configFile);

		// Ensure configuration is written back at shutdown.
		atexit(&caerConfigWriteBack);
	}
	else {
		caerLog(CAER_LOG_EMERGENCY, "Config",
			"Could not create and/or read from the configuration file '%s'. Error: %d.", configFile.c_str(), errno);
		exit(EXIT_FAILURE);
	}

	// Override with command-line arguments if requested.
	if (cliVarMap.count("override")) {
		std::vector<std::string> configOverrides = cliVarMap["override"].as<std::vector<std::string>>();

		auto iter = configOverrides.begin();

		while (iter != configOverrides.end()) {
			sshsNode node = sshsGetNode(sshsGetGlobal(), iter[0].c_str());
			if (node == NULL) {
				caerLog(CAER_LOG_EMERGENCY, "Config", "SSHS: node '%s' doesn't exist on override.", iter[0].c_str());
			}
			else {
				if (!sshsNodeStringToAttributeConverter(node, iter[1].c_str(), iter[2].c_str(), iter[3].c_str())) {
					caerLog(CAER_LOG_EMERGENCY, "Config",
						"SSHS: failed to convert attribute '%s' of type '%s' with value '%s' on override.",
						iter[1].c_str(), iter[2].c_str(), iter[3].c_str());
				}
			}

			iter += 4;
		}
	}
}

void caerConfigWriteBack(void) {
	// configFile can only be correctly initialized, absolute and canonical
	// by the point this function may ever be called, so we use it directly.
	int configFileFd = open(configFile.c_str(), O_WRONLY | O_TRUNC);

	if (configFileFd >= 0) {
		const char *filterKeys[] = { "running", "connectedClients" };
		const char *filterNodes[] = { "sourceInfo" };

		sshsNodeExportSubTreeToXML(sshsGetNode(sshsGetGlobal(), "/"), configFileFd, filterKeys, 2, filterNodes, 1);

		close(configFileFd);
	}
	else {
		caerLog(CAER_LOG_EMERGENCY, "Config", "Could not write to the configuration file '%s'. Error: %d.",
			configFile.c_str(), errno);
	}
}
