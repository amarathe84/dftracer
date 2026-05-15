#include <dftracer/service/service.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <fstream>
#include <iostream>
#include <string>

// Daemonize the process: detach from terminal and run in background
void daemonize() {
  pid_t pid = fork();
  if (pid < 0) exit(EXIT_FAILURE);  // Fork failed
  if (pid > 0) exit(EXIT_SUCCESS);  // Parent exits

  // Child continues as session leader
  if (setsid() < 0) exit(EXIT_FAILURE);

  pid = fork();
  if (pid < 0) exit(EXIT_FAILURE);  // Second fork failed
  if (pid > 0) exit(EXIT_SUCCESS);  // First child exits

  // Close standard file descriptors
  close(STDIN_FILENO);
  close(STDOUT_FILENO);
  close(STDERR_FILENO);
}

int main(int argc, char* argv[]) {
  // Check for correct usage
  if (argc < 2 || argc > 3) {
    std::cerr << "Usage: " << argv[0] << " <start|stop> [log_dir]" << std::endl;
    return 1;
  }

  std::string cmd = argv[1];
  std::string log_dir = (argc == 3) ? argv[2] : "/tmp";

  // Ensure log_dir ends without trailing slash
  if (!log_dir.empty() && log_dir.back() == '/') log_dir.pop_back();

  std::string pid_file_path = log_dir + "/dftracer_server.pid";
  std::string out_log_path = log_dir + "/dftracer_server.out";
  std::string err_log_path = log_dir + "/dftracer_server.err";

  if (cmd == "start") {
    auto conf =
        dftracer::Singleton<dftracer::ConfigurationManager>::get_instance();
    auto libuv_threads = std::to_string(conf->libuv_thread_count);
    setenv("UV_THREADPOOL_SIZE", libuv_threads.c_str(), 1);

    // Start the server as a daemon
    daemonize();

    // Redirect stdout and stderr to log files (truncate on running)
    freopen(out_log_path.c_str(), "w", stdout);
    freopen(err_log_path.c_str(), "w", stderr);

    // Write the server's PID to a file for later reference
    std::ofstream pid_file(pid_file_path);
    pid_file << getpid();
    pid_file.close();

    // Create and start the DFTracerService server. start() blocks until SIGINT.
    auto server = dftracer::DFTracerService();
    server.start();

    // Remove the PID file
    std::remove(pid_file_path.c_str());

    return 0;
  } else if (cmd == "stop") {
    // Stop the running server by sending SIGINT to its PID
    std::ifstream pid_file(pid_file_path);
    pid_t pid;
    if (!(pid_file >> pid)) {
      std::cerr << "No running server found." << std::endl;
      return 1;
    }
    pid_file.close();

    // Send SIGINT to the server process
    if (kill(pid, SIGINT) == 0) {
      std::cout << "Sent SIGINT to server (PID " << pid << ")." << std::endl;
      std::remove(pid_file_path.c_str());
    } else {
      std::cerr << "Failed to send SIGINT to server." << std::endl;
      return 1;
    }
    return 0;
  } else {
    // Unknown command
    std::cerr << "Unknown command: " << cmd << std::endl;
    return 1;
  }
}