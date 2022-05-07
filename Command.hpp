/**
 * Copyright ©2022. Brent Weichel. All Rights Reserved.
 * Permission to use, copy, modify, and/or distribute this software, in whole
 * or part by any means, without express prior written agreement is prohibited.
 */
#pragma once

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <new>
#include <signal.h>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>
#include <vector>

/**
 * Minimum required standard: C++14
 * Notes:
 *   - Not thread safe
 *   - clear(), terminate(), isRunning() are asynchronous.
 *   - terminate() can be made to be synchronous by supplying
 *     true as the parameter argument; which is false by default.
 *
 * TODO:
 *   [ ] Capture std{err,out} from Command as either a string or a vector of strings.
 *   [ ] Implement redirect of stderr to stdout.
 *   [ ] Identifiy the set of cases that need to be handled
 *       and enumerate those cases here. These are the cases
 *       that could cause fatal unwanted behaviour, such as
 *       clearing the Command object before the child process
 *       has completed and leaving a zombie process behind.
 *
 * A management class for executing other applications
 * without all the hassle of having to write the same code repeatedly.
 *
 * There is a CommandPipeline object that can be used to daisy chain
 * Command objects into a complete pipeline. Only stdout is piped to stdin
 * per the usual piping behaviour. Redirecting stderr to stdout is something
 * that will need to be implemented here in Command.hpp.
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

	// User set environment variables
	std::map< std::string, std::string > mEnvironmentVariables;

	// If set, clear the preset environment variables
	// before setting the user defined variables.
	bool mClearEnvironmentVariables;

	std::atmoic< uint32_t > mExecuteCalled;
	std::atomic< bool > mIsExecuting;
	std::atomic< bool > mSetForClear;
	std::atomic< bool > mTerminateCalled;

	// 0 < mChildProcessID :: T == mIsExecuting
	std::atomic< pid_t > mChildProcessID; // PID of the child process
	//pid_t mChildProcessID; // PID of the child process
	std::atomic< int > mExitStatus;
	//int mExitStatus; // Exit status of the child process

	bool mRedirectStdoutToLogFile; // The stdout stream should be redirected to a log file
	bool mRedirectStderrToLogFile; // The stderr stream should be redirected to a log file
	std::string mStdoutLogFilePrefix; // Prefix of the stdout log file
	std::string mStderrLogFilePrefix; // Prefix of the stderr log file

	// Append arguments to the end of the arguments list;
	// expanding the list if needed.
	void _appendArguments(
		const std::vector< std::string >& arguments )
	{
		// Ignore the request if we're currently executing
		if ( ( 0 == mExecuteCalled.load() ) and ( 0 > mChildProcessID.load() ) )
		{
			return;
		}

		// Return here, nothing to append
		if ( 0 == arguments.size() )
		{
			return;
		}

		// If needed, update the array size first
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
			mArguments[ mArgumentCount++ ] = strdup( argument.c_str() );
		}
	}

	// Free and zero the contents of this Command object.
	void _clear()
	{
		// Terminate the child process
		this->terminate( true );

		// Clear the application
		if ( nullptr != mApplication )
		{
			free( mApplication );
			mApplication = nullptr;
		}

		// Clear the arguments
		if ( nullptr != mArguments )
		{
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
		}

		// Clear environment variables
		mEnvironmentVariables.clear();
		mClearEnvironmentVariables = false;

		// Clear everything else
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
		mEnvironmentVariables = other.mEnvironmentVariables;
		mClearEnvironmentVariables = other.mClearEnvironmentVariables;

		for ( size_t index( -1 ); ++index < mArgumentCount; )
		{
			mArguments[ index ] = strdup( other.mArguments[ index ] );
		}

		mRedirectStdoutToLogFile = other.mRedirectStdoutToLogFile;
		mRedirectStderrToLogFile = other.mRedirectStderrToLogFile;
		mStdoutLogFilePrefix = other.mStdoutLogFilePrefix;
		mStderrLogFilePrefix = other.mStderrLogFilePrefix;
	}

	// This method is intended to be called by
	// the CommandPipeline class when initializing the pipeline.
	int _forkRedirectToPipeAndExecute(
		int* inPipe,
		int* outPipe )
	{
		pid_t childProcessID;
		std::string stdoutLogFilePath;
		std::string stderrLogFilePath;

		_getStdLogFilePaths( stdoutLogFilePath, stderrLogFilePath );

		childProcessID = vfork();
		if ( 0 > childProcessID )
		{
			return -errno;
		}

		// Child process
		if ( 0 == childProcessID )
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
				int stderrLogFileFD;
				if ( -1 == ( stderrLogFileFD = open( stderrLogFilePath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666 ) ) )
				{
					_exit( EXIT_FAILURE );
				}

				dup2( stderrLogFileFD, STDERR_FILENO );
				close( stderrLogFileFD );
			}

			if ( mClearEnvironmentVariables )
			{
				clearenv();
			}

			for ( const auto& [ variableName, value ] : mEnvironmentVariables )
			{
				setenv( variableName.c_str(), value.c_str(), true );
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
		mChildProcessID = childProcessID;

		return 0;
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
		mArgumentCount = 1;
		mArgumentsBufferSize = 128;
		mArguments = static_cast< char** >( calloc( mArgumentsBufferSize, sizeof( char* ) ) );
		mEnvironmentVariables.clear();
		mClearEnvironmentVariables = false;
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
		mEnvironmentVariables = std::move( other.mEnvironmentVariables );
		mClearEnvironmentVariables = std::exchange( other.mClearEnvironmentVariables, false );

		mChildProcessID = other.mChildProcessID.exchange( -1 );
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
		// If we're currently executing, do nothing.
		if ( ( 0 != mExecuteCalled.load() ) or ( 0 < mChildProcessID.load() ) )
		{
			return;
		}

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
	Command(
		Command&& other )
	{
		_initialize();
		_moveAssignment( std::move( other ) );
	}

	/**
	 * Copy constructor.
	 * @param other Command object to copy to this instance.
	 */
	Command(
		const Command& other )
	{
		_initialize();
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
		if ( nullptr != argument )
		{
			_appendArguments( std::vector< std::string >{ argument } );
		}

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
	 * Clear the Command object and set
	 * it back to an initialized state.
	 */
	void clear()
	{
		_clear();
		_initialize();
	}

	/**
	 * Clear all environment variables.
	 */
	void clearEnvironmentVariables()
	{
		mEnvironmentVariables.clear();
		mClearEnvironmentVariables = true;
	}

	/**
	 * Initialize the execution of the application.
	 * @return If the application was successfully initialized, then
	 *         zero is returned, else a non-zero error code is returned.
	 *         If another thread is alread present in this method, or
	 *         if the command is already running, then -ECANCELED is returned.
	 */
	int execute()
	{
		// Set that we're in the execute method
		uint32_t previous = mExecuteCalled.fetch_add( 1 );

		if ( 0 < previous )
		{
			mExecuteCalled.fetch_add( -1 );
			return -ECANCELED;
		}

		// executeCalled is non-zero at this point
		if ( 0 < mChildProcessID.load() )
		{
			mExecuteCalled.fetch_add( -1 );
			return -ECANCELED;
		}

		mExitStatus = 0;

		int childProcessID;
		std::string stdoutLogFilePath;
		std::string stderrLogFilePath;

		_getStdLogFilePaths( stdoutLogFilePath, stderrLogFilePath );

		childProcessID = vfork();
		if ( 0 > childProcessID )
		{
			return -errno;
		}

		// Child process
		if ( 0 == childProcessID )
		{
			int stdoutLogFileFD;
			int stderrLogFileFD;

			if ( mRedirectStdoutToLogFile )
			{
				// Redirect STDOUT to a log file
				if ( -1 == ( stdoutLogFileFD = open( stdoutLogFilePath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666 ) ) )
				{
					_exit( EXIT_FAILURE );
				}

				dup2( stdoutLogFileFD, STDOUT_FILENO );
			}

			if ( mRedirectStderrToLogFile )
			{
				// Redirect STDERR to a log file
				if ( -1 == ( stderrLogFileFD = open( stderrLogFilePath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666 ) ) )
				{
					_exit( EXIT_FAILURE );
				}

				dup2( stderrLogFileFD, STDERR_FILENO );
			}

			if ( mClearEnvironmentVariables )
			{
				clearenv();
			}

			for ( const auto& [ variableName, value ] : mEnvironmentVariables )
			{
				setenv( variableName.c_str(), value.c_str(), true );
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
		mChildProcessID = childProcessID;
		mExecuteCalled.fetch_add( -1 );

		return 0;
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
		else if ( -ECANCELED == returnCode )
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
	 * Get the user set environment variables.
	 * @return A const reference to the map of user set environment variables.
	 */
	const std::map< std::string, std::string >& getEnvironmentVariables() const
	{
		return mEnvironmentVariables;
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
	 * Cast the Command object to a string.
	 * Should no application be set, then the application
	 * part of the command line will be set as "(null)"
	 */
	operator std::string() const
	{
		std::string commandAndArgs(
			( nullptr == mApplication ) ? "(null)" : mApplication );

		for ( size_t index( 0 ); ++index < mArgumentCount; )
		{
			commandAndArgs.append( " " ).append( mArguments[ index ] );
		}

		return commandAndArgs;
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
	 * Set an environment variable to be set for the application.
	 * The environment variable will not be set for the calling application.
	 * If {@param variableName} is empty, then {@param value} is ignored and nothing is done.
	 * @param variableName Name of the environment variable.
	 * @param value Value to set the variable to.
	 * @return A reference to this Command object is returned.
	 */
	Command& setEnvironmentVariable(
		const std::string& variableName,
		const std::string& value )
	{
		if ( not variableName.empty() )
		{
			mEnvironmentVariables[ variableName ] = value;
		}

		return *this;
	}

	/**
	 * Set the environment variables to be set for the application.
	 * The environment variables will not be set for the calling application.
	 * If the variable name is empty, then the value will be ignored.
	 * @param environmentVariables A map of variableName to values.
	 * @return A reference to this Command object is returned.
	 */
	Command& setEnvironmentVariables(
		const std::map< std::string, std::string >& environmentVariables )
	{
		for ( const auto& [ variableName, value ] : environmentVariables )
		{
			if ( not variableName.empty() )
			{
				mEnvironmentVariables[ variableName ] = value;
			}
		}

		return *this;
	}

	/**
	 * Send a terminate signal to the child process if one is running.
	 * @param wait If set to true, wait on the child process after sending
	 *             the SIGTERM signal to collect the exit status. [default: false]
	 * @return Zero is returned on success, else an error code is returned.
	 */
	int terminate(
		bool wait = false )
	{
		int errorCode = 0;

		if ( 0 < mChildProcessID )
		{
			// TODO: We only need to send this signal once.
			errorCode = kill( mChildProcessID, SIGTERM );

			if ( wait and ( 0 == errorCode ) )
			{
				errorCode = this->wait();
			}
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
			// TODO: We only really need 1 thread waiting, the rest can wait
			//       for that thread to collect the exitStatus.
			waitpid( mChildProcessID, &exitStatus, 0 );
			mExitStatus = WEXITSTATUS( exitStatus );
			mChildProcessID = -1;

			return mExitStatus;
		}

		return 0;
	}
};
