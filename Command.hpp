/**
 * Copyright ©2022. Brent Weichel. All Rights Reserved.
 * Permission to use, copy, modify, and/or distribute this software, in whole
 * or part by any means, without express prior written agreement is prohibited.
 */
#pragma once

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

/**
 * A management class for executing other applications
 * without all the hassle of having to write the same code repeatedly.
 *
 * If you're looking to set up a pipe, I'd recommend creating a vector
 * of Command objects and then using the redirectStdoutToCommand() method to
 * set the STDOUT of the nth command to the STDIN of the nth + 1 command.
 * The commands will be daisy chained together and when the first
 * command in the chain is executed it will initialize all the commands
 * down the chain. If wait is called, then waiting will happen on all
 * processes down wind of the command that wait was called upon.
 *
 * At the moment a 1-to-many (for std(out|err) to many stdin) is not needed
 * any where, but should the occasion arise I can integrate that behaviour
 * at a later point in time.
 *
 * TODO:
 *   [ ] Complete redirectStderrToStdout()
 *   [ ] Add a terminate method
 *   [ ] Create a Pipe object to manage command pipelines.
 */
class Command
{
private:
	char* mApplication;
	char** mArguments;
	size_t mArgumentCount;
	size_t mArgumentsBufferSize;

	pid_t mChildProcessID;
	int mExitStatus;

	bool mRedirectStdoutToCommand;
	Command* mNextCommand;
	bool mHasInPipe;
	bool mHasOutPipe;

	bool mRedirectStdoutToLogFile;
	bool mRedirectStderrToLogFile;
	std::string mStdoutLogFilePrefix;
	std::string mStderrLogFilePrefix;

	// Append arguments to the end of the arguments list;
	// expanding the list if needed.
	void _appendArguments(
		const std::vector< std::string >& arguments )
	{
		// If needed, update the array size first
		if ( 0 == arguments.size() )
		{
			return;
		}

		if ( mArgumentsBufferSize < ( mArgumentCount + arguments.size() + 1 ) )
		{
			size_t additionalBlock = 128 * ( ( arguments.size() + 127 ) / 128 );
			void* newArray = realloc( mArguments, mArgumentsBufferSize + additionalBlock );

			if ( nullptr == newArray )
			{
				throw std::bad_alloc();
			}

			mArguments = static_cast< char** >( newArray );
			for ( size_t index( additionalBlock ); index--;
				mArguments[ mArgumentsBufferSize + index ] = nullptr );
			mArgumentsBufferSize += additionalBlock;
		}

		// Append arguments to the end of the array
		for ( const std::string& argument : arguments )
		{
			mArguments[ ++mArgumentCount ] = strdup( argument.c_str() );
		}
	}

	// Fork to create the child process,
	// establish any redirection as configured,
	// and then execute the set application.
	int _forkRedirectAndExecute(
		int* inPipe,
		int* outPipe )
	{
		FILE* stdoutLogFile = nullptr;
		FILE* stderrLogFile = nullptr;
		std::string stdoutLogFilePath;
		std::string stderrLogFilePath;

		_getStdLogFilePaths( stdoutLogFilePath, stderrLogFilePath );

		// If we're not in a pipe and a log file has been requested
		// for the stdout stream, then open the log file for stdout.
		if ( ( not mHasOutPipe ) and mRedirectStdoutToLogFile )
		{
			if ( nullptr == ( stdoutLogFile = fopen( stdoutLogFilePath.c_str(), "a" ) ) )
			{
				goto closeFileHandlesAndReturnError;
			}
		}

		// Open the log file for stderr.
		if ( mRedirectStderrToLogFile )
		{
			if ( nullptr == ( stderrLogFile = fopen( stderrLogFilePath.c_str(), "a" ) ) )
			{
				goto closeFileHandlesAndReturnError;
			}
		}

		mChildProcessID = vfork();
		if ( 0 > mChildProcessID )
		{
			goto closeFileHandlesAndReturnError;
		}

		// Child process
		if ( 0 == mChildProcessID )
		{
			if ( nullptr != inPipe )
			{
				// Capture STDIN if we have a pipe
				dup2( inPipe[ 0 ], STDIN_FILENO );
				close( inPipe[ 0 ] );
				close( inPipe[ 1 ] );
			}

			if ( nullptr != outPipe )
			{
				// Redirect STDOUT if we have a pipe
				dup2( outPipe[ 1 ], STDOUT_FILENO );
				close( outPipe[ 0 ] );
				close( outPipe[ 1 ] );
			}
			else if ( mRedirectStdoutToLogFile )
			{
				// Redirect STDOUT to a log file
				dup2( fileno( stdoutLogFile ), STDOUT_FILENO );
				fclose( stdoutLogFile );
			}

			if ( mRedirectStderrToLogFile )
			{
				// Redirect STDERR to a log file
				dup2( fileno( stderrLogFile ), STDERR_FILENO );
				fclose( stderrLogFile );
			}

			if ( '/' == mApplication[ 0 ] )
			{
				execv( mApplication, mArguments );
			}
			else
			{
				execvp( mApplication, mArguments );
			}

			_exit( EXIT_FAILURE );
			return -EPERM;
		}

		// Parent process
		if ( nullptr != inPipe )
		{
			close( inPipe[ 0 ] );
			close( inPipe[ 1 ] );
		}

		return 0;

	closeFileHandlesAndReturnError:

		if ( nullptr != stdoutLogFile )
		{
			fclose( stdoutLogFile );
		}

		if ( nullptr != stderrLogFile )
		{
			fclose( stderrLogFile );
		}

		return -errno;
	}

	// Generate the name of the log files for stdout and stderr
	void _getStdLogFilePaths(
		std::string& stdoutLogFilePath,
		std::string& stderrLogFilePath )
	{
		time_t currentTime;
		struct tm* currentTimeStruct;
		char formattedTimeBuffer[ 128 ];

		time( &currentTime );
		currentTimeStruct = localtime( &currentTime );
		strftime( formattedTimeBuffer, sizeof( formattedTimeBuffer ),
			"_%Y%m%d%H%M%S", currentTimeStruct );

		std::string dateTimeBasenameSuffix( formattedTimeBuffer );

		if ( mRedirectStdoutToLogFile )
		{
			if ( not mStdoutLogFilePrefix.empty() )
			{
				stdoutLogFilePath = mStdoutLogFilePrefix + "_";
			}

			stdoutLogFilePath.append( mArguments[ 0 ] );
			stdoutLogFilePath.append( dateTimeBasenameSuffix );
			stdoutLogFilePath.append( ".stdout.log" );
		}
		else
		{
			stdoutLogFilePath.clear();
		}

		if ( mRedirectStderrToLogFile )
		{
			if ( not mStderrLogFilePrefix.empty() )
			{
				stderrLogFilePath = mStderrLogFilePrefix + "_";
			}

			stderrLogFilePath.append( mArguments[ 0 ] );
			stderrLogFilePath.append( dateTimeBasenameSuffix );
			stderrLogFilePath.append( ".stderr.log" );
		}
		else
		{
			stderrLogFilePath.clear();
		}
	}

	// Initialize the Command object
	void _initialize()
	{
		mApplication = nullptr;
		mArgumentCount = 0;
		mArgumentsBufferSize = 128;
		mArguments = static_cast< char** >( calloc( mArgumentsBufferSize, sizeof( char* ) ) );
		mChildProcessID = -1;
		mExitStatus = 0;
		mHasInPipe = true;
		mHasOutPipe = true;
		mRedirectStdoutToCommand = false;
		mNextCommand = nullptr;
		mRedirectStdoutToLogFile = false;
		mRedirectStderrToLogFile = false;
	}

	// Set the name of the application in both mApplication and mArguments[ 0 ]
	void _setApplication(
		const char* application )
	{
		if ( nullptr != mApplication )
		{
			free( mApplication );
			mApplication = nullptr;
		}

		if ( nullptr != mArguments[ 0 ] )
		{
			free( mArguments[ 0 ] );
			mArguments[ 0 ] = nullptr;
		}

		if ( ( nullptr == application )
			or ( 0 == strlen( application ) ) )
		{
			return;
		}

		mApplication = strdup( application );
		const char* forwardSlash = strrchr( application, '/' );

		if ( nullptr != forwardSlash )
		{
			mArguments[ 0 ] = strdup( forwardSlash + 1 );
		}
		else
		{
			mArguments[ 0 ] = strdup( application );
		}
	}

public:
	/**
	 * Default constructor to empty command.
	 */
	Command()
	{
		_initialize();
	}

	/**
	 * Construct the command and set the application to be ran.
	 * @param application Name of the application to execute.
	 */
	Command(
		const char* application )
	{
		_initialize();
		_setApplication( application );
	}

	/**
	 * Construct the command and set the application to be ran.
	 * @param application Name of the application to execute.
	 */
	Command(
		const std::string& application )
	{
		_initialize();
		_setApplication( application.c_str() );
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
		_initialize();
		_setApplication( application );
		_appendArguments( arguments );
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
		_initialize();
		_setApplication( application.c_str() );
		_appendArguments( arguments );
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
		_appendArguments( std::vector< std::string >{ argument } );
		return *this;
	}

	/**
	 * Append an argument to the list of arguments.
	 * @param argument A string containing the argument to append for the application.
	 * @return A reference to this Command object is returned.
	 */
	Command& appendArgument(
		const std::string& argument )
	{
		_appendArguments( std::vector< std::string >{ argument } );
		return *this;
	}

	/**
	 * Append a list of arguments to the list of arguments.
	 * @param arguments A vector of strings containing the arguments to append for the application.
	 * @return A reference to this Command object is returned.
	 */
	Command& appendArguments(
		const std::vector< std::string >& arguments )
	{
		_appendArguments( arguments );
		return *this;
	}

	/**
	 * Get the name of the application being executed.
	 * @return The name of the application being ran is returned.
	 */
	std::string applicationName()
	{
		return std::string( ( nullptr == mApplication ) ? "" : mApplication );
	}

	/**
	 * Initialize the execution of the application.
	 * @return If the application was successfully initialized, then
	 *         zero is returned, else a non-zero error code is returned.
	 */
	int execute()
	{
		std::vector< Command* > commandStack;
		Command* command = this;

		// Get the full chain of commands to be executed.
		do
		{
			if ( ( nullptr == command->mApplication )
				or ( 0 == strlen( command->mApplication ) ) )
			{
				return -ENOENT;
			}

			command->mHasOutPipe = ( nullptr != command->mNextCommand );
			command->mHasInPipe = ( 0 < commandStack.size() );
			commandStack.push_back( command );
			command = command->mNextCommand;
		}
		while ( nullptr != command );

		// Starting from the beginning of the vector
		// execute all the commands in the pipe.
		int pipes[ 2 ][ 2 ];
		int inPipeIndex = 0, outPipeIndex = 1;

		for ( size_t index( -1 ); ++index < commandStack.size(); )
		{
			command = commandStack[ index ];
			int* inPipe = ( command->mHasInPipe ) ? pipes[ inPipeIndex ] : nullptr;
			int* outPipe = ( command->mHasOutPipe ) ? pipes[ outPipeIndex ] : nullptr;

			if ( command->mHasOutPipe )
			{
				pipe( pipes[ outPipeIndex ] );
			}

			// The method is responsible for closing
			// the inPipe if it does not equal nullptr.
			command->_forkRedirectAndExecute( inPipe, outPipe );

			outPipeIndex = ( outPipeIndex + 1 ) & 0x1;
			inPipeIndex = ( inPipeIndex + 1 ) & 0x1;
		}

		return 0;
	}

	/**
	 * Execute the command and wait for it to complete.
	 * @return The exit status of the command is returned.
	 */
	int executeAndWait()
	{
		int returnCode = 0;

		if ( 0 == ( returnCode = execute() ) )
		{
			returnCode = wait();
		}

		return returnCode;
	}

	/**
	 * Get the exit status of the application executed. If the application
	 * is currently running, or has yet to run, then this method returns zero.
	 * @return Exit status of the command is returned.
	 */
	int exitStatus()
	{
		return mExitStatus;
	}

	/**
	 * Check if this application is currently executing.
	 * @return True is returned if the application is currently executing, false otherwise.
	 */
	bool isRunning()
	{
		if ( 0 < mChildProcessID )
		{
			int status, returnValue;
			returnValue = waitpid( mChildProcessID, &status, WNOHANG );
			return 0 == returnValue;
		}

		return false;
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
		mRedirectStderrToLogFile = true;

		if ( ( nullptr == prefix )
			or ( 0 == strlen( prefix ) ) )
		{
			mStderrLogFilePrefix.clear();
		}
		else
		{
			mStderrLogFilePrefix = std::string( prefix );
		}

		return *this;
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
		return this->logStderrToFile( prefix.c_str() );
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
		mRedirectStdoutToLogFile = true;

		if ( ( nullptr == prefix )
			or ( 0 == strlen( prefix ) ) )
		{
			mStdoutLogFilePrefix.clear();
		}
		else
		{
			mStdoutLogFilePrefix = std::string( prefix );
		}

		return *this;
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
		return this->logStdoutToFile( prefix.c_str() );
	}

	/**
	 * Redirect the stderr stream to stdout.
	 * @return A reference to this Command object is returned.
	 */
	Command& redirectStderrToStdout()
	{
		// TODO: Complete this method.
		return *this;
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
		Command* command )
	{
		mRedirectStdoutToLogFile = false;
		mRedirectStdoutToCommand = true;
		mNextCommand = command;
	}

	/**
	 * Set the application to be executed by this mangement class.
	 * @param application Name of the application to be executed.
	 * @return A reference to this Command object is returned.
	 */
	Command& setApplication(
		const char* application )
	{
		_setApplication( application );
		return *this;
	}

	/**
	 * Set the application to be executed by this mangement class.
	 * @param application Name of the application to be executed.
	 * @return A reference to this Command object is returned.
	 */
	Command& setApplication(
		const std::string& application = std::string() )
	{
		_setApplication( application.c_str() );
		return *this;
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
		int exitStatus;

		if ( 0 < mChildProcessID )
		{
			waitpid( mChildProcessID, &exitStatus, 0 );

			mChildProcessID = -1;
			mExitStatus = WEXITSTATUS( exitStatus );
			return mExitStatus;
		}

		return 0;
	}
};
