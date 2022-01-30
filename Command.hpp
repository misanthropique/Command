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
#include <ctime>
#include <new>
#include <signal.h>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>
#include <vector>

/**
 * Required standard: C++14
 *
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
 */
class Command
{
private:
	friend class CommandPipeline;

	char* mApplication; // Path to the application to be called
	char** mArguments;  // Arguments to be passed to the application
	size_t mArgumentCount; // Number of arguments present
	size_t mArgumentsBufferSize; // Size of the arguments buffer

	pid_t mChildProcessID; // PID of the child process
	int mExitStatus; // Exit status of the child process

	bool mRedirectStdoutToLogFile; // The stdout stream should be redirected to a log file
	bool mRedirectStderrToLogFile; // The stderr stream should be redirected to a log file
	std::string mStdoutLogFilePrefix; // Prefix of the stdout log file
	std::string mStderrLogFilePrefix; // Prefix of the stderr log file

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

	// Free and zero the contents of this Command object.
	void _clear()
	{
		if ( nullptr != mApplication )
		{
			free( mApplication );
			mApplication = nullptr;
		}

		for ( size_t index( -1 ); ++index < mArgumentsBufferSize; )
		{
			if ( nullptr != mArguments[ index ] )
			{
				free( mArguments[ index ] );
				mArguments[ index ] = nullptr;
			}
		}

		free( mArguments );
		mArguments = nullptr;

		mArgumentCount = 0;
		mArgumentsBufferSize = 0;

		mExitStatus = 0;
		mChildProcessID = -1;
		mRedirectStdoutToLogFile = false;
		mRedirectStderrToLogFile = false;
	}

	// Copy the contents of other to this instance.
	void _copyAssignment(
		const Command& other )
	{
		this->_clear();

		if ( nullptr != other.mApplication )
		{
			mApplication = strdup( other.mApplication );
		}

		mArgumentCount = other.mArgumentCount;
		mArgumentsBufferSize = other.mArgumentsBufferSize;
		mArguments = static_cast< char** >( calloc( mArgumentsBufferSize, sizeof( char* ) ) );

		for ( size_t index( -1 ); ++index < mArgumentCount; )
		{
			mArguments[ index ] = strdup( other.mArguments[ index ] );
		}

		mRedirectStdoutToLogFile = other.mRedirectStdoutToLogFile;
		mRedirectStderrToLogFile = other.mRedirectStderrToLogFile;
		mStdoutLogFilePrefix = other.mStdoutLogFilePrefix;
		mStderrLogFilePrefix = other.mStderrLogFilePrefix;

		mChildProcessID = other.mChildProcessID;
		mExitStatus = other.mExitStatus;
	}

	// This method is intended to be called by
	// the CommandPipeline class when initializing the pipeline.
	int _forkRedirectToPipeAndExecute(
		int* inPipe,
		int* outPipe )
	{
		FILE* stderrLogFile = nullptr;
		std::string stdoutLogFilePath;
		std::string stderrLogFilePath;

		_getStdLogFilePaths( stdoutLogFilePath, stderrLogFilePath );

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
		mRedirectStdoutToLogFile = false;
		mRedirectStderrToLogFile = false;
		mStdoutLogFilePrefix.clear();
		mStderrLogFilePrefix.clear();
	}

	// Move the contents of other to this instance.
	void _moveAssignment(
		Command&& other )
	{
		mApplication = std::exchange( other.mApplication, nullptr );
		mArguments = std::exchange( other.mArguments, nullptr );
		mArgumentCount = std::exchange( other.mArgumentCount, 0 );
		mArgumentsBufferSize = std::exchange( other.mArgumentsBufferSize, 0 );

		mChildProcessID = std::exchange( other.mChildProcessID, -1 );
		mExitStatus = std::exchange( other.mExitStatus, 0 );

		mRedirectStdoutToLogFile = std::exchange( other.mRedirectStdoutToLogFile, false );
		mRedirectStderrToLogFile = std::exchange( other.mRedirectStderrToLogFile, false );
		mStdoutLogFilePrefix = std::move( other.mStdoutLogFilePrefix );
		mStderrLogFilePrefix = std::move( other.mStderrLogFilePrefix );
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
	 * Move constructor.
	 * @param other Command object to move to this instance.
	 */
	Command( Command&& other )
	{
		_moveAssignment( std::move( other ) );
	}

	/**
	 * Copy constructor.
	 * @param other Command object to copy to this instance.
	 */
	Command( const Command& other )
	{
		_copyAssignment( other );
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
		_clear();
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
	std::string applicationName() const
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
		mExitStatus = 0;

		FILE* stdoutLogFile = nullptr;
		FILE* stderrLogFile = nullptr;
		std::string stdoutLogFilePath;
		std::string stderrLogFilePath;

		_getStdLogFilePaths( stdoutLogFilePath, stderrLogFilePath );

		// Open the log file for stdout.
		if ( mRedirectStdoutToLogFile )
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
			if ( mRedirectStdoutToLogFile )
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

	/**
	 * Execute the command and wait for it to complete.
	 * @return The exit status of the command is returned.
	 */
	int executeAndWait()
	{
		int returnCode = 0;

		if ( 0 == ( returnCode = this->execute() ) )
		{
			returnCode = this->wait();
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
		int status, returnValue;

		if ( 0 < mChildProcessID )
		{
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
	 * Move assignment operator.
	 * @param other Command object to move to this instance.
	 * @return A reference to this Command object is returned.
	 */
	Command& operator=(
		Command&& other )
	{
		if ( this != &other )
		{
			_moveAssignment( std::move( other ) );
		}

		return *this;
	}

	/**
	 * Copy assignment operator.
	 * @param other Command object to copy to this instance.
	 * @return A reference to this Command object is returned.
	 */
	Command& operator=(
		const Command& other )
	{
		if ( this != &other )
		{
			_copyAssignment( other );
		}

		return *this;
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
	 * Send a terminate signal to the child process if one is running.
	 * @return Zero is returned on success, else an error code is returned.
	 */
	int terminate()
	{
		int errorCode = 0;

		if ( 0 < mChildProcessID )
		{
			errorCode = kill( mChildProcessID, SIGTERM );
		}

		return errorCode;
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
			mExitStatus = WEXITSTATUS( exitStatus );
			mChildProcessID = -1;

			return mExitStatus;
		}

		return 0;
	}
};
