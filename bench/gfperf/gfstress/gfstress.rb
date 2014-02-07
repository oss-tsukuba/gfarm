#!/usr/bin/env ruby

require "optparse"
require "pp"
require 'thread'

$debug = false

$running = true

def make_path()
  gpath = $config[:testdir]
  if (gpath[0..8] == "gfarm:///")
    gpath.slice!(0..7)
  end
  return $config[:gfarm2fs]+gpath
end

$hostname = `hostname`.chomp
$pid = Process.pid
$gfsds = `gfsched -w`.split("\n")
if ($gfsds.size == 0)
  STDERR.print "no gfsd!\n"
  exit(1)
end

$config = Hash.new
$config[:testdir] = "gfarm:///stress"
$config[:localdir] = "/tmp"
$config[:number] = 1
$config[:timeout] = 300     # 5 minutes
opts = OptionParser.new
opts.on("-t",
        "--testdir GFARM_URL",
        String,
        "test gfarm url (default: #{$config[:testdir]})") {|v|
  $config[:testdir] = v
}
opts.on("-l",
        "--localdir LOCAL_URL",
        String,
        "test local directory (default: #{$config[:localdir]})") {|v|
  $config[:localdir] = v
}
opts.on("-m",
        "--gfarm2fs GFARM_DIR",
        String,
        "gfarm2fs mountpoint") {|v|
  $config[:gfarm2fs] = v
}
opts.on("-n",
        "--number NUMBER",
        Integer,
        "number of multiplex") {|v|
  $config[:number] = v
}
opts.on("-T",
        "--timeout SECONDS",
        Integer,
        "timeout (default: #{$config[:timeout]} seconds)") {|v|
  $config[:timeout] = v
}
opts.parse!(ARGV)

if (!$config[:gfarm2fs].nil?)
  $config[:fullpath] = make_path()
  $full_path = "#{$config[:fullpath]}/#{$hostname}-#{$pid}"
end

$top_dir = "#{$config[:testdir]}/#{$hostname}-#{$pid}"
r = system("gfmkdir -p #{$top_dir}");
if (r == false)
  exit(1);
end
$local_dir = "#{$config[:localdir]}/gfstress-#{$pid}"
r = system("mkdir -p #{$local_dir}");
if (r == false)
  exit(1);
end
r = system("gfncopy -s 1 #{$top_dir}")
if (r == false)
  STDERR.print("gfncopy error!\n")
  exit(1);
end

$config[:number].times { |i|
  r = system("gfmkdir -p #{$top_dir}/gfpcopy/#{i}");
  if (r == false)
    exit(1);
  end
  r = system("mkdir -p #{$local_dir}/gfpcopy/#{i}");
  if (r == false)
    exit(1);
  end
  r = system("gfmkdir -p #{$top_dir}/metadata/#{i}");
  if (r == false)
    exit(1);
  end
  r = system("gfmkdir -p #{$top_dir}/tree/#{i}");
  if (r == false)
    exit(1);
  end
  r = system("gfmkdir -p #{$top_dir}/io/#{i}");
  if (r == false)
    exit(1);
  end
  if (!$config[:gfarm2fs].nil?)
    r = system("gfmkdir -p #{$top_dir}/metadata2/#{i}");
    if (r == false)
      exit(1);
    end
    r = system("gfmkdir -p #{$top_dir}/tree2/#{i}");
    if (r == false)
      exit(1);
    end
    r = system("gfmkdir -p #{$top_dir}/io2/#{i}");
    if (r == false)
      exit(1);
    end
  end
}
$commands = Array.new

$config[:number].times { |i|
  $commands.push("gfpcopy-stress -G #{$top_dir}/gfpcopy/#{i} -L #{$local_dir}/gfpcopy/#{i} -d 100 -f 10 -s 1K -p 1 -l 1000")
  $commands.push("gfperf-metadata -t #{$top_dir}/metadata/#{i} -n 500")
  $commands.push("gfperf-tree -t #{$top_dir}/tree/#{i} -w 3 -d 5")
  $gfsds.each {|g|
    $commands.push("gfperf-read -t #{$top_dir}/io/#{i} -l 1G -g #{g} -k -1")
    $commands.push("gfperf-write -t #{$top_dir}/io/#{i} -l 1G -g #{g} -k -1")
  }
  if ($gfsds.size > 1)
    tg = $gfsds.clone
    tg.push(tg.shift)
    $gfsds.each_index {|j|
      $commands.push("gfperf-replica -s #{$gfsds[j]} -d #{tg[j]} -l 1M -t #{$top_dir}/io/#{i}")
    }
  end

  if (!$config[:gfarm2fs].nil?)
    $commands.push("gfperf-metadata -t file://#{$full_path}/metadata2/#{i} -n 500")
    $commands.push("gfperf-tree -t file://#{$full_path}/tree2/#{i} -w 3 -d 5")
    $gfsds.each {|g|
      $commands.push("gfperf-read -t #{$top_dir}/io2/#{i} -m #{$config[:gfarm2fs]} -l 1G -g #{g} -k -1")
      $commands.push("gfperf-write -t #{$top_dir}/io2/#{i} -m #{$config[:gfarm2fs]} -l 1G -g #{g} -k -1")
    }
  end
}

$mutex_stdout = Mutex.new

class Runner
  def init(manager, command)
    @manager = manager
    @command = command
    return self
  end

  def start()
    @running = true
    @thread = Thread.new { self.run() }
    return self
  end

  def run()
    while (@running)
      @pipe = Array.new
      @pipe[0] = IO.pipe
      @pipe[1] = IO.pipe
      @pipe[2] = IO.pipe

      @stdin = @pipe[0][1]
      @stdout = @pipe[1][0]
      @stderr = @pipe[2][0]

      break unless (@running)
      $mutex_stdout.lock # avoid deadlock at STDOUT.reopen()
      @pid = fork {
        @pipe[0][1].close
        @pipe[1][0].close
        @pipe[2][0].close
        STDIN.reopen(@pipe[0][0])
        STDOUT.reopen(@pipe[1][1])
        STDERR.reopen(@pipe[2][1])
        exec("#{@command}");
      }
      $mutex_stdout.unlock

      @pipe[0][0].close
      @pipe[1][1].close
      @pipe[2][1].close
      outstr = ""
      errstr = ""
      t0 = Thread.new {
        while (tmp = @stdout.read)
          if (tmp == "")
            break
          end
          outstr += tmp
        end
      }
      t1 = Thread.new {
        @stderr.each { |line|
          now = Time.now
          msec = sprintf("%03d", (now.usec / 1000).round);
          errstr += now.strftime("%H:%M:%S.") + msec + " " + line
        }
      }
      if ($debug)
        $mutex_stdout.lock
        print "[COMMAND START] " + @command + "\n"
        STDOUT.flush
        $mutex_stdout.unlock
      end

      pid2, status = Process.waitpid2(@pid, 0)
      t0.join
      t1.join

      @stdin.close
      @stdout.close
      @stderr.close
      if (!@running)
        prefix = "INTERRUPTED: "
      elsif (status.exitstatus == 0)
        prefix = "IGNORED: "
      else
        prefix = "ERROR: "
      end
      tmp = errstr.split("\n").map{|l|
        if (!l.include?("[1000058] connecting to gfmd"))
          prefix + l
        end
      }.compact
      if (tmp.size > 0)
        errstr = tmp.join("\n")+"\n"
      else
        errstr = ""
      end

      $mutex_stdout.lock
      if (errstr.length > 0 ||
          (!status.exitstatus.nil? && status.exitstatus != 0))
        t = Time.now.strftime("%Y-%m-%d %H:%M:%S")
        print "[COMMAND] " + @command + "\n"
        print errstr
        print "EXIT: #{t}, STATUS=#{status.exitstatus}\n"
      end
      STDOUT.flush
      $mutex_stdout.unlock
    end
  end

  def wait()
    @thread.join
    return self
  end

  def stop()
    @running = false
    begin
      Process.kill(:TERM, @pid)
    rescue
    end
  end
end

class Manager
  def init()
    @interrupted = false
    @runners = Array.new
    $commands.each { |com|
      @runners.push(Runner.new.init(self, com))
    }
    return self
  end

  def run()
    @runners.each { |r|
      r.start
    }
    if ($config[:timeout] > 0)
      $mutex_stdout.lock
      print "timeout: #{$config[:timeout]} seconds\n"
      STDOUT.flush
      $mutex_stdout.unlock

      @timeout_thread = Thread.new {
        sleep $config[:timeout]
        @runners.each { |r|
          r.stop
        }
        $mutex_stdout.lock
        print "end of gfstress.rb\n"
        STDOUT.flush
        $mutex_stdout.unlock
      }
    end
    return self
  end

  def intr()
    @interrupted = true
    @stop_thread = Thread.new {
      @runners.each { |r|
        r.stop
      }
    }
    return self
  end

  def wait()
    @runners.each { |r|
      r.wait
    }
    if (@interrupted)
      print "interrupted\n"
    end
    @stop_thread.join unless (@stop_thread.nil?)
    @timeout_thread.kill unless (@timeout_thread.nil?)
    return self
  end

end

print "start at #{Time.now.to_s}\n"

$manager = Manager.new.init.run

Signal.trap(:INT) {
  $manager.intr
}

Signal.trap(:TERM) {
  $manager.intr
}

Signal.trap(:PIPE) {
  $manager.intr
}

$manager.wait

print "stop at #{Time.now.to_s}\n"

print "clean up..."
system("gfrm -rf #{$top_dir}");
system("rm -rf #{$local_dir}");
print "done.\n"
