/**
 * Copyright ©2022. Brent Weichel. All Rights Reserved.
 * Permission to use, copy, modify, and/or distribute this software, in whole
 * or part by any means, without express prior written agreement is prohibited.
 */
#pragma once

#include <stdexcept>
#include <string>
#include <unistd.h>
#include <vector>

#include "Command.hpp"

/**
 * A class object for handling the construction
 * and execution of commands that consume the output of prior commands.
 */
class CommandPipeline
{
	std::vector< Command > mCommands;
	bool mHasExecuted;
	int mExitStatus;

public:
	/**
	 * Default constructor to an empty pipeline.
	 */
	CommandPipeline()
	{
		mExitStatus = 0;
		mHasExecuted = false;
	}

	/**
	 * Construct a CommandPipeline object with a list of commands.
	 * @param commands A vector of Command objects to initialize the pipeline with.
	 * @throw std::invalid_argument is thrown if any of the elements of {@param commands}
	 *        does not have a set application to be executed.
	 */
	CommandPipeline(
		const std::vector< Command >& commands )
	{
		// Sanity check
		for ( size_t index( -1 ); ++index < commands.size(); )
		{
			if ( commands[ index ].applicationName().empty() )
			{
				throw std::invalid_argument(
					"Command at index " + std::to_string( index )
					+ " does not have a set application" );
			}
		}

		// Set the commands vector as our commands
		mCommands = commands;
	}

	/**
	 * Append a Command to the pipeline.
	 * @param command Const reference to a Command to be appended to the pipeline.
	 * @return A reference to this CommandPipeline object is returned.
	 */
	CommandPipeline& appendCommand(
		const Command& command )
	{
		// Sanity check
		if ( command.applicationName().empty() )
		{
			throw std::invalid_argument( "Command does not have a set application" );
		}

		// Append the command
		mCommands.push_back( command );

		return *this;
	}

	/**
	 * Append a Command to the pipeline.
	 * @param command R-Value to a Command to be appended to the pipeline.
	 * @return A reference to this CommandPipeline object is returned.
	 */
	CommandPipeline& appendCommand(
		Command&& command )
	{
		// Sanity check
		if ( command.applicationName().empty() )
		{
			throw std::invalid_argument( "Command does not have a set application" );
		}

		// Append the command
		mCommands.push_back( std::move( command ) );

		return *this;
	}

	/**
	 * Append a list of Command objects to the pipeline.
	 * @param commands A vector of Command objects to append to the pipeline.
	 * @return A reference to this CommandPipeline object is returned.
	 */
	CommandPipeline& appendCommands(
		const std::vector< Command >& commands )
	{
		// Sanity check
		for ( size_t index( -1 ); ++index < commands.size(); )
		{
			if ( commands[ index ].applicationName().empty() )
			{
				throw std::invalid_argument(
					"Command at index " + std::to_string( index )
					+ " does not have a set application" );
			}
		}

		// Append commands to the end of the pipeline
		for ( size_t index( -1 ); ++index < commands.size(); )
		{
			mCommands.push_back( commands[ index ] );
		}

		return *this;
	}

	/**
	 * Return the running status of the pipeline. Zero is returned
	 * if nothing is running in the pipeline, one is returned if a
	 * contiguous region of the pipeline is in execution. Negative one
	 * is returned if the pipeline is broken.
	 * @return Zero is returned if the pipeline is not running.
	 *         One is returned if a contiguous region is in execution.
	 *         Negative one is returned if the pipeline is running but broken.
	 */
	int isRunning()
	{
		bool running = false;
		bool fallingEdge = false;

		if ( mHasExecuted )
		{
			for ( size_t index( mCommands.size() ); index--; )
			{
				if ( mCommands[ index ].isRunning() )
				{
					if ( fallingEdge )
					{
						return -1;
					}

					running = true;
				}
				else
				{
					fallingEdge = running;
				}
			}
		}

		return running;
	}

	/**
	 * Begin execution of the pipeline.
	 * @return Zero is returned upon successful initialization of the pipeline.
	 *         If an error occurs, then the pipeline is broken down, the resources
	 *         are released and an error code is returned.
	 */
	int execute()
	{
		mExitStatus = 0;

		int pipes[ 2 ][ 2 ];
		int inPipeIndex = 0, outPipeIndex = 1;
		size_t numberCommands = mCommands.size();

		for ( size_t index( -1 ); ++index < numberCommands; )
		{
			int* inPipe =  ( 0 < index ) ? pipes[ inPipeIndex ] : nullptr;
			int* outPipe = ( index < ( numberCommands - 1 ) ) ? pipes[ outPipeIndex ] : nullptr;

			if ( nullptr != outPipe )
			{
				pipe( pipes[ outPipeIndex ] );
			}

			mCommands[ index ]._forkRedirectToPipeAndExecute( inPipe, outPipe );

			if ( nullptr != inPipe )
			{
				close( inPipe[ 0 ] );
				close( inPipe[ 1 ] );
			}

			outPipeIndex = ( outPipeIndex + 1 ) & 0x1;
			inPipeIndex = ( inPipeIndex + 1 ) & 0x1;
		}

		mHasExecuted = true;

		return 0;
	}

	/**
	 * Begin execution of the pipeline and wait for
	 * everything to complete execution.
	 * @return A negative error code is returned upon
	 *         failure to initialize the pipeline, else the exit
	 *         status of the pipeline is returned.
	 */
	int executeAndWait()
	{
		int exitStatus = 0;

		if ( 0 == ( exitStatus = this->execute() ) )
		{
			exitStatus = this->wait();
		}

		return exitStatus;
	}

	/**
	 * Return the exit status of the pipeline.
	 * If the pipeline has yet to execute or the pipeline is
	 * currently executing, then zero is returned, else the
	 * exit status of the last process is returned.
	 * @return The exit status of the pipeline is returned.
	 */
	int exitStatus()
	{
		return mExitStatus;
	}

	/**
	 * Terminate the execution of the pipeline.
	 * @return Zero is returned upon success, else a non-zero exit code is returned.
	 */
	int terminate()
	{
		int terminateCode;
		int returnCode = 0;

		if ( mHasExecuted )
		{
			for ( size_t index( -1 ); ++index < mCommands.size(); )
			{
				terminateCode = mCommands[ index ].terminate();

				if ( ( 0 == returnCode ) and ( 0 != terminateCode ) )
				{
					returnCode = terminateCode;
				}
			}
		}

		return returnCode;
	}

	/**
	 * Wait for the pipeline to complete execution.
	 * The strategy executed is to wait on each process individually
	 * from the beginning of the pipeline toward the end until each
	 * process has completed. If the pipeline is found to be broken,
	 * then all remaining commands are terminated.
	 * @return The exit code of the last process is returned.
	 */
	int wait()
	{
		int waitCode = 0;
		bool terminatePipeline = false;

		if ( mHasExecuted )
		{
			for ( size_t index( -1 ); ++index < mCommands.size(); )
			{
				if ( terminatePipeline )
				{
					mCommands[ index ].terminate();
				}
				else	
				{
					waitCode = mCommands[ index ].wait();
					terminatePipeline = ( 0 != waitCode );
					mExitStatus = waitCode;
				}
			}
		}

		return waitCode;
	}
};
