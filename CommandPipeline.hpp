/**
 * Copyright ©2022. Brent Weichel. All Rights Reserved.
 * Permission to use, copy, modify, and/or distribute this software, in whole
 * or part by any means, without express prior written agreement is prohibited.
 */
#pragma once

#include <stdexcept>
#include <vector>

#include "Command.hpp"

/**
 * A class object for handling the construction
 * and execution of commands that consume the output of prior commands.
 */
class CommandPipeline
{
	std::vector< Command > mCommands;

public:
	/**
	 * Default constructor to an empty pipeline.
	 */
	CommandPipeline()
	{
	}

	CommandPipeline(
		const std::vector< Command >& commands )
	{
		// Sanity check
		for ( size_t index( -1 ); ++index < commands.size(); )
		{
			if ( command.applicationName.empty() )
			{
				throw std::invalid_argument();
			}
		}

		// Set the commands vector as our commands
		mCommands = commands;
	}

	CommandPipeline& appendCommand(
		const Command& command )
	{
		// Sanity check
		if ( command.applicationName.empty() )
		{
			throw std::invalid_argument();
		}

		// Append the command
		mCommands.push_back( command );
	}

	CommandPipeline& appendCommand(
		Command&& command )
	{
	}

	CommandPipeline& appendCommands(
		std::vector< Commnad >& commands )
	{
		// Sanity check
		for ( size_t index( -1 ); ++index < commands.size(); )
		{
			if ( command.applicationName().empty() )
			{
				throw std::invalid_argument();
			}
		}

		// Append commands to the end of the pipeline
	}

	/**
	 * Returns true if a contiguous region of the
	 * pipeline is in execution, false is returned if nothing
	 * is in execution or if the pipeline is broken.
	 */
	bool isRunning()
	{
	}

	int execute()
	{
	}

	int executeAndWait()
	{
	}

	int exitStatus()
	{
	}

	int wait()
	{
	}
};
