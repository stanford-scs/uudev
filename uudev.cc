#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <libudev.h>

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <map>

#include "cleanup.h"

using unique_udev_t = unique_destructor_t<udev_unref>;
using unique_udev_monitor_t = unique_destructor_t<udev_monitor_unref>;
using unique_udev_device_t = unique_destructor_t<udev_device_unref>;

const char confname[] = "uudev.conf";
const char *progname;

static const char *opt_confpath;
int opt_verbose;

static std::string
get_confpath()
{
  std::string ret;
  if (opt_confpath)
    ret = opt_confpath;
  else if (const char *path = getenv("UUDEV_CONF"))
    ret = path;
  else if (const char *conf = getenv("XDG_CONFIG_HOME"))
    ret = std::string(*conf ? conf : ".") + confname;
  else if (const char *home = getenv("HOME"))
    ret = std::string(*home ? home : ".") + "/.config/" + confname;
  else {
    std::cerr << progname << ": no HOME directory and no -c option"
	      << std::endl;
    exit(1);
  }
  if (ret.empty())
    ret = ".";
  return ret;
}

void
waitfd(int fd, short events)
{
  pollfd fds[1];
  memset(fds, 0, sizeof(fds));
  fds[0].fd = fd;
  fds[0].events = events;
  poll(fds, 1, -1);
}

static std::string
upcase(const char *s)
{
  std::string ret;
  while (*s)
    ret += char(toupper(*s++));
  return ret;
}

static void
dev_foreach(const unique_udev_device_t &dev,
	    std::function<void(std::string, std::string)> cb)
{
#define GET(x)							\
  do {								\
    if (const char *val = udev_device_get_##x(dev.get())) {	\
      cb(upcase(#x), val);					\
    }								\
  } while(0)
  GET(action);
  GET(devpath);
  GET(syspath);
  GET(sysname);
  GET(sysnum);
  GET(devnode);
  GET(devtype);
  GET(subsystem);
  GET(driver);
#undef GET
  dev_t rdev = udev_device_get_devnum(dev.get());
  if (rdev != 0) {
    cb("MAJOR", std::to_string(major(rdev)));
    cb("MINOR", std::to_string(minor(rdev)));
  }
}

static void
dump(std::ostream &out, const unique_udev_device_t &dev)
{
  if (!dev) {
    out << "NULL" << std::endl;
    return;
  }
  dev_foreach(dev, [&out](std::string k, std::string v) {
		     out << k << "=" << v << std::endl;
		   });
}

static void
dev_setenv(const unique_udev_device_t &dev)
{
  dev_foreach(dev, [](std::string k, std::string v) {
		     setenv(k.c_str(), v.c_str(), 1);
		   });
}

static void
run(const unique_udev_device_t &dev, const std::string cmds)
{
  int fds[2];
  if (pipe(fds) == -1) {
    perror("pipe");
    return;
  }

  pid_t pid = fork();
  if (pid == -1) {
    perror("fork");
    close(fds[0]);
    close(fds[1]);
    return;
  }
  else if (pid == 0) {
    close(fds[1]);
    dev_setenv(dev);
    if (fds[0] != 0) {
      dup2(fds[0], 0);
      close(fds[0]);
    }
    execl("/bin/sh", "/bin/sh", nullptr);
    exit(1);
  }
  close(fds[0]);
  if (opt_verbose >= 3) {
    write(0, cmds.data(), cmds.size());
  }
  write(fds[1], cmds.data(), cmds.size());
  close(fds[1]);
}

void
monitor()
{
  unique_udev_t ud (udev_new());
  unique_udev_monitor_t mon (udev_monitor_new_from_netlink(ud.get(), "udev"));
  udev_monitor_enable_receiving(mon.get());
  for (;;) {
    unique_udev_device_t dev(udev_monitor_receive_device(mon.get()));
    if (!dev) {
      waitfd(udev_monitor_get_fd(mon.get()), POLLIN);
      continue;
    }
    std::cout << "* " << udev_device_get_action(dev.get()) << " "
	      << udev_device_get_devpath(dev.get()) << std::endl;
    if (opt_verbose) {
      dump(std::cout, dev);
      std::cout << "----------------------------------------------"
		<< std::endl;
    }
  }
}

struct uudev {
  unique_udev_t udev_;
  unique_udev_monitor_t mon_;
  std::map<std::string, std::string> config_;

  uudev() : udev_(udev_new()),
	    mon_(udev_monitor_new_from_netlink(udev_.get(), "udev")) {}
  bool parse(std::string path);
  void dumpconf();
  void loop();
};

void
uudev::loop()
{
  udev_monitor_enable_receiving(mon_.get());
  for (;;) {
    unique_udev_device_t dev(udev_monitor_receive_device(mon_.get()));
    if (!dev) {
      waitfd(udev_monitor_get_fd(mon_.get()), POLLIN);
      continue;
    }
    const char *action = udev_device_get_action(dev.get()),
      *devpath = udev_device_get_devpath(dev.get());
    if (!action || !devpath)
      continue;
    std::string trigger = action + std::string(" ") + devpath;
    if (opt_verbose >= 1) {
      std::cout << "* " << trigger << std::endl;
      if (opt_verbose >= 2)
	dump(std::cout, dev);
    }

    auto i = config_.find(trigger);
    if (i == config_.end())
      continue;
    run(dev, i->second);
  }
}

void
uudev::dumpconf()
{
  for (auto i : config_) {
    std::cout << "* " << i.first << std::endl
	      << i.second
	      << "----------------------------------------------" << std::endl;
  }
}

bool
uudev::parse(std::string path)
{
  std::ifstream file(path);
  if (file.fail()) {
    perror(path.c_str());
    return false;
  }

  std::string trigger;
  std::ostringstream actions;
  for (int lineno = 0;; ++lineno) {
    std::string line;
    std::getline(file, line);
    if (!file || (!line.empty() && line[0] == '*')) {
      if (!trigger.empty() && actions.tellp()) {
	config_[trigger] += actions.str();
      }
      trigger.clear();
      actions.str("");
    }
    if (!file) {
      return file.eof();
    }
    if (line.empty() || line[0] != '*') {
      actions << line << std::endl;
      continue;
    }
    std::istringstream ss(line);
    std::string star, act, dev;
    if (!(ss >> star >> act >> dev) || star != "*")
      continue;
    trigger = act + " " + dev;
  }
  return true;
}

void
query_path(const char *path)
{
  struct stat sb;
  if (stat(path, &sb) == -1) {
    perror(path);
    exit(1);
  }
  char type;
  if (S_ISCHR(sb.st_mode))
    type = 'c';
  else if (S_ISBLK(sb.st_mode))
    type = 'b';
  else {
    std::cerr << path << ": not a character or block device" << std::endl;
    exit(1);
  }

  unique_udev_t ud (udev_new());
  unique_udev_device_t dev(udev_device_new_from_devnum(ud.get(), type,
						       sb.st_rdev));
  if (!dev) {
    std::cerr << path << ": could not find device path" << std::endl;
    exit(1);
  }
  if (opt_verbose)
    dump(std::cout, dev);
  else
    std::cout << udev_device_get_devpath(dev.get()) << std::endl;
}

[[noreturn]] static void usage(int exitval = 1);
static void
usage(int exitval)
{
  using namespace std;
  (exitval ? cerr : cout)
    << "usage: " << progname << " [-c conf]" << endl
    << "       " << progname << " [-v] -p /dev/path" << endl
    << "       " << progname << " [-v] -m" << endl;
  exit(exitval);
}

int
main(int argc, char **argv)
{
  if (argc < 1)
    progname = "uudev";
  else if ((progname = strrchr(argv[0], '/')))
    progname++;
  else
    progname = argv[0];

  const char *opt_path = nullptr;
  bool opt_monitor = 0;

  int opt;
  while ((opt = getopt(argc, argv, "hmc:vp:")) != -1) {
    switch (opt) {
    case 'c':
      opt_confpath = optarg;
      break;
    case 'm':
      opt_monitor = true;
      break;
    case 'p':
      opt_path = optarg;
      break;
    case 'v':
      opt_verbose++;
      break;
    case 'h':
      usage(0);
      break;
    default:
      usage(1);
      break;
    }
  }

  if (opt_path)
    query_path(opt_path);
  else if (opt_monitor)
    monitor();
  else {
    uudev uu;
    if (!uu.parse(get_confpath()))
      exit(1);
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_IGN;
    sigaction(SIGCHLD, &sa, nullptr);
    sigaction(SIGPIPE, &sa, nullptr);
    if (opt_verbose > 0)
      uu.dumpconf();
    uu.loop();
  }
}
