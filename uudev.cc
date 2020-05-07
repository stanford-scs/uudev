#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <libudev.h>

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <map>
#include <regex>
#include <set>

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
list_foreach(udev_list_entry *e,
	     std::function<void(std::string, std::string)> cb)
{
  for (int i = 0; e; e = udev_list_entry_get_next(e), ++i) {
    const char *k = udev_list_entry_get_name(e),
      *v = udev_list_entry_get_value(e);
    if (v)
      cb(k, v);
    else
      cb(std::to_string(i), k);
  }
}

static void
dev_foreach(const unique_udev_device_t &dev,
	    std::function<void(std::string, std::string)> cb)
{
  list_foreach(udev_device_get_devlinks_list_entry(dev.get()),
	       [&cb](std::string, std::string v) {
		 cb("DEVLINK", v);
	       });
  list_foreach(udev_device_get_properties_list_entry(dev.get()), cb);
}

struct DevProps {
  std::set<std::string> links_;
  std::map<std::string, std::string> props_;
  DevProps(const unique_udev_device_t &dev);
  bool eq(const std::string &k, const std::string &v) const;
  bool neq(const std::string &k, const std::string &v) const {
    return !eq(k, v);
  }
};
using DevPred = std::function<bool(const DevProps &)>;

DevProps::DevProps(const unique_udev_device_t &dev)
{
  list_foreach(udev_device_get_devlinks_list_entry(dev.get()),
	       [this](std::string, std::string v) {
		 links_.emplace(std::move(v));
	       });
  list_foreach(udev_device_get_properties_list_entry(dev.get()),
	       [this](std::string k, std::string v) {
		 props_.emplace(std::move(k), std::move(v));
	       });
}

bool
DevProps::eq(const std::string &k, const std::string &v) const
{
  if (k == "DEVLINK")
    return links_.find(v) != links_.end() || (links_.empty() && v.empty());
  auto i = props_.find(k);
  return (i != props_.end() && i->second == v) ||
    (i == props_.end() && v.empty());
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
run(const unique_udev_device_t *dev, const std::string cmds)
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
    if (dev)
      dev_setenv(*dev);
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
  waitpid(pid, nullptr, 0);
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
	      << udev_device_get_devpath(dev.get())
	      << " (" << udev_device_get_subsystem(dev.get())
	      << ")" << std::endl;
    if (opt_verbose) {
      dump(std::cout, dev);
      std::cout << "----------------------------------------------"
		<< std::endl;
    }
  }
}

struct Uudev {
  unique_udev_t udev_;
  unique_udev_monitor_t mon_;

  struct Rule {
    DevPred pred_;
    std::string rule_;
    std::string commands_;
    bool immediate_ = false;
    bool preamble_ = false;
    bool parse(std::string line);
  };
  std::vector<Rule> config_;

  Uudev() : udev_(udev_new()),
	    mon_(udev_monitor_new_from_netlink(udev_.get(), "udev")) {}
  bool parse(std::string path);
  void dumpconf();
  void loop();
};

void
Uudev::loop()
{
  udev_monitor_enable_receiving(mon_.get());

  {
    std::string cmds, debug1;
    bool runit(false);
    for (auto r : config_) {
      if (r.immediate_) {
	cmds += r.commands_;
	if (!r.preamble_) {
	  runit = true;
	  if (opt_verbose >= 1) {
	    debug1 += r.rule_;
	    debug1 += '\n';
	  }
	}
      }
    }
    if (runit) {
      if (opt_verbose >= 1)
	std::cout << "** startup" << std::endl
		  << debug1;
      run(nullptr, cmds);
    }
  }

  for (;;) {
    unique_udev_device_t dev(udev_monitor_receive_device(mon_.get()));
    if (!dev) {
      waitfd(udev_monitor_get_fd(mon_.get()), POLLIN);
      continue;
    }
    DevProps props(dev);
    std::string cmds, debug1;
    bool runit(false);
    for (auto r : config_) {
      if (!r.pred_(props))
	continue;
      if (opt_verbose >= 1) {
	debug1 += r.rule_;
	debug1 += '\n';
      }
      cmds += r.commands_;
      if (!r.preamble_)
	runit = true;
    }
    if (runit) {
      if (opt_verbose >= 1)
	std::cout << "** ACTION==" << udev_device_get_action(dev.get())
		  << ", DEVPATH==\"" << udev_device_get_devpath(dev.get())
		  << "\"" << std::endl
		  << debug1;
      if (opt_verbose >= 2)
	dump(std::cout, dev);
      run(&dev, cmds);
    }
  }
}

void
Uudev::dumpconf()
{
  std::cout << "------------ DUMPING CONFIGURATION -----------" << std::endl;
  for (auto i : config_) {
    std::cout << i.rule_ << std::endl
	      << i.commands_
	      << "----------------------------------------------" << std::endl;
  }
  std::cout << "------------ CONFIGURATION DUMPED ------------" << std::endl;
}

static std::regex rulestart(R"/(^\*[?!]*)/");
static std::regex rulerx(R"/(^\s*(\w+)\s*(==|!=)\s*"([^"]*)"\s*)/");
bool
Uudev::Rule::parse(std::string line)
{
  auto p = line.cbegin(), e = line.cend();
  std::smatch m;
  if (!regex_search(p, e, m, rulestart))
    return false;
  p = m.suffix().first;
  rule_ = line;
  std::string flags(m.str(0));
  immediate_ = flags.find('!') != flags.npos;
  preamble_ = flags.find('?') != flags.npos;
  pred_ = nullptr;

  DevPred ret([](const DevProps &p) { return true; });
  while (regex_search(p, e, m, rulerx)) {
    const std::string k = m.str(1), v = m.str(3);
    if (m.str(2) == "==")
      ret = [=](const DevProps &p) { return p.eq(k, v) && ret(p); };
    else if (m.str(2) == "!=")
      ret = [=](const DevProps &p) { return p.neq(k, v) && ret(p); };
    else
      return false;

    p = m.suffix().first;
    if (p == e)
      break;
    else if (*p != ',')
      return false;
    p++;
  }
  while (p < e && isspace(*p))
    p++;
  if (p != e)
    return false;
  pred_ = ret;
  return true;
}

bool
Uudev::parse(std::string path)
{
  std::ifstream file(path);
  if (file.fail()) {
    perror(path.c_str());
    return false;
  }

  Rule r;
  bool valid(false);
  for (int lineno = 1;; ++lineno) {
    std::string line;
    std::getline(file, line);
    if (!file || (!line.empty() && line[0] == '*')) {
      if (valid && !r.commands_.empty())
	config_.emplace_back(std::move(r));
      r.commands_.clear();
      if (!file) {
	return file.eof();
      }
    }
    if (line.empty() || line[0] != '*') {
      r.commands_ += line;
      r.commands_ += '\n';
    }
    else if (!(valid = r.parse(line)))
      std::cerr << path << ":" << lineno << ": syntax error" << std::endl;
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
    Uudev uu;
    if (!uu.parse(get_confpath()))
      exit(1);
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_IGN;
    sigaction(SIGPIPE, &sa, nullptr);
    if (opt_verbose > 1)
      uu.dumpconf();
    uu.loop();
  }
}
