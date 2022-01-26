/**
 * Copyright ©2022. Brent Weichel. All Rights Reserved.
 * Permission to use, copy, modify, and/or distribute this software, in whole
 * or part by any means, without express prior written agreement is prohibited.
 */
#pragma once

#include <string>
#include <vector>

/**
 * A management class for executing other applications
 * without all the hassle of having to write the same code repeatedly.
 *
 * If you're looking to set up a pipe, I'd recommend creating a vector
 * of Command objects and then using the redirect*() methods to set the
 * STD(OUT|ERR) of the nth command to the STDIN of the nth + 1 command.
 *
 * At the moment a 1-to-many (for std(out|err) to many stdin) is not needed
 * any where, but should the occasion arise I can integrate that behaviour
 * at a later point in time.
 */
class Command
{
private:
public:
	/**
	 * Default constructor to empty command.
	 */
	Command()
	{
	}

	/**
	 * Construct the command and set the application to be ran.
	 * @param application Name of the application to execute.
	 */
	Command(
		const char* application )
	{
	}

	/**
	 * Construct the command and set the application to be ran.
	 * @param application Name of the application to execute.
	 */
	Command(
		const std::string& application )
	{
	}

	/**
	 * Construct the command and set the application and arguments.
	 * @param application Name of the application to execute.
	 * @param arguments A vector of arguments to set along with the application.
	 */
	Command(
		const char* application,
		const std::vector< std::string >& arguments )
	{
	}

	/**
	 * Construct the command and set the application and arguments.
	 * @param application Name of the application to execute.
	 * @param arguments A vector of arguments to set along with the application.
	 */
	Command(
		const std::string& application,
		const std::vector< std::string >& arguments )
	{
	}

	/**
	 * Destructor to release the resources.
	 */
	~Command()
	{
	}

	/**
	 * Append an argument to the list of arguments.
	 * @param argument A string containing the argument to append for the application.
	 * @return A reference to this Command object is returned.
	 */
	Command& appendArgument(
		const char* argument )
	{
	}

	/**
	 * Append an argument to the list of arguments.
	 * @param argument A string containing the argument to append for the application.
	 * @return A reference to this Command object is returned.
	 */
	Command& appendArgument(
		const std::string& argument )
	{
	}

	/**
	 * Append a list of arguments to the list of arguments.
	 * @param arguments A vector of strings containing the arguments to append for the application.
	 * @return A reference to this Command object is returned.
	 */
	Command& appendArguments(
		const std::vector< std::string >& arguments )
	{
	}

	/**
	 * Get the name of the application being executed.
	 * @return The name of the application being ran is returned.
	 */
	std::string applicationName()
	{
	}

	/**
	 * Initialize the execution of the application.
	 * @return If the application was successfully initialized, then
	 *         zero is returned, else a non-zero error code is returned.
	 */
	int execute()
	{
	}

	/**
	 * Execute the command and wait for it to complete.
	 * @return The exit status of the command is returned.
	 */
	int executeAndWait()
	{
	}

	/**
	 * Get the exit status of the application executed. If the application
	 * is currently running, or has yet to run, then this method returns zero.
	 * @return Exit status of the command is returned.
	 */
	int exitStatus()
	{
	}

	/**
	 * Check if the application is currently executing.
	 * @return True is returned if the application is currently executing, false otherwise.
	 */
	bool isRunning()
	{
	}

	/**
	 * Redirect the stderr stream of this command to a log file.
	 * If the prefix is empty, then the stderr is not redirected to a file.
	 * @param prefix The prefix of the log file. ${prefix}.stderr.log [default: ]
	 * @return A reference to this Command object is returned.
	 */
	Command& logStderrToFile(
		const char* prefix )
	{
	}

	/**
	 * Redirect the stderr stream of this command to a log file.
	 * If the prefix is empty, then the stderr is not redirected to a file.
	 * @param prefix The prefix of the log file. ${prefix}.stderr.log [default: ]
	 * @return A reference to this Command object is returned.
	 */
	Command& logStderrToFile(
		const std::string& prefix = std::string() )
	{
	}

	/**
	 * Redirect the stdout stream of this command to a log file.
	 * If the prefix is empty, then the stdout is not redirected to a file.
	 * @param prefix The prefix of the log file. ${prefix}.stdout.log [default: ]
	 * @return A reference to this Command object is returned.
	 */
	Command& logStdoutToFile(
		const char* prefix )
	{
	}

	/**
	 * Redirect the stdout stream of this command to a log file.
	 * If the prefix is empty, then the stdout is not redirected to a file.
	 * @param prefix The prefix of the log file. ${prefix}.stdout.log [default: ]
	 * @return A reference to this Command object is returned.
	 */
	Command& logStdoutToFile(
		const std::string& prefix = std::string() )
	{
	}

	/**
	 * Redirect the stderr output stream of this command
	 * to the stdin stream of the given command. Only one command
	 * can take the stderr stream of this command, so subsequent calls
	 * override the command that'll consume the stream.
	 * @param command Reference to the command to take the stderr from this command.
	 * @return A reference to this Command object is returned.
	 */
	Command& redirectStderrToCommand(
		Command& command )
	{
	}

	/**
	 * Redirect the stdout output stream of this command
	 * to the stdin stream of the given command. Only one command
	 * can take the stdout stream of this command, so subsequent calls
	 * override the command that'll consume the stream.
	 * @param command Reference to the command to take the stdout from this command.
	 * @return A reference to this Command object is returned.
	 */
	Command& redirectStdoutToCommand(
		Command& command )
	{
	}

	/**
	 * Set the application to be executed by this mangement class.
	 * @param application Name of the application to be executed.
	 * @return A reference to this Command object is returned.
	 */
	Command& setApplication(
		const char* application )
	{
	}

	/**
	 * Set the application to be executed by this mangement class.
	 * @param application Name of the application to be executed.
	 * @return A reference to this Command object is returned.
	 */
	Command& setApplication(
		const std::string& application = std::string() )
	{
	}

	/**
	 * Wait on the application to finish if it's currently running.
	 * This method will return immediately if the application has already
	 * finished or has yet to be executed.
	 * @return Zero is returned if no application is running, else the exit
	 *         code of the application is returned upon completion.
	 */
	int wait()
	{
	}
};
